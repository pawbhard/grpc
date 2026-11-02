//
// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <grpcpp/ext/otel_access_log.h>
#include <grpcpp/generic/callback_generic_service.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::access_loggers::open_telemetry::v3::
    OpenTelemetryAccessLogConfig;
using ::opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest;
using ::opentelemetry::proto::logs::v1::LogRecord;

constexpr char kExportMethod[] =
    "/opentelemetry.proto.collector.logs.v1.LogsService/Export";

// A fake OTLP collector.  Implements LogsService/Export with the generic
// API, so that the test does not need generated gRPC code for the OTLP
// protos.
class FakeOtlpCollectorService final : public CallbackGenericService {
 public:
  ServerGenericBidiReactor* CreateReactor(
      GenericCallbackServerContext* context) override {
    return new ExportReactor(this, context->method() == kExportMethod);
  }

  // Waits until at least num_records log records have arrived.
  std::vector<LogRecord> WaitForLogRecords(size_t num_records,
                                           absl::Duration timeout) {
    grpc_core::MutexLock lock(&mu_);
    const absl::Time deadline =
        absl::Now() + timeout * grpc_test_slowdown_factor();
    while (log_records_.size() < num_records) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) break;
    }
    return log_records_;
  }

  // Returns the resource attributes of the last request.
  std::vector<std::pair<std::string, std::string>> resource_attributes() {
    grpc_core::MutexLock lock(&mu_);
    return resource_attributes_;
  }

 private:
  class ExportReactor final : public ServerGenericBidiReactor {
   public:
    ExportReactor(FakeOtlpCollectorService* service, bool known_method)
        : service_(service) {
      if (!known_method) {
        Finish(Status(StatusCode::UNIMPLEMENTED, "unknown method"));
        return;
      }
      StartRead(&request_);
    }

    void OnReadDone(bool ok) override {
      if (!ok) {
        Finish(Status(StatusCode::INTERNAL, "no request"));
        return;
      }
      std::vector<Slice> slices;
      EXPECT_TRUE(request_.Dump(&slices).ok());
      std::string serialized;
      for (const Slice& slice : slices) {
        serialized.append(reinterpret_cast<const char*>(slice.begin()),
                          slice.size());
      }
      ExportLogsServiceRequest request;
      EXPECT_TRUE(request.ParseFromString(serialized));
      service_->AddRequest(request);
      // ExportLogsServiceResponse with no fields set is empty.
      Slice empty("");
      response_ = ByteBuffer(&empty, 1);
      StartWriteAndFinish(&response_, WriteOptions(), Status::OK);
    }

    void OnDone() override { delete this; }

   private:
    FakeOtlpCollectorService* service_;
    ByteBuffer request_;
    ByteBuffer response_;
  };

  void AddRequest(const ExportLogsServiceRequest& request) {
    grpc_core::MutexLock lock(&mu_);
    for (const auto& resource_logs : request.resource_logs()) {
      resource_attributes_.clear();
      for (const auto& kv : resource_logs.resource().attributes()) {
        resource_attributes_.emplace_back(kv.key(), kv.value().string_value());
      }
      for (const auto& scope_logs : resource_logs.scope_logs()) {
        for (const auto& log_record : scope_logs.log_records()) {
          log_records_.push_back(log_record);
        }
      }
    }
    cv_.SignalAll();
  }

  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  std::vector<LogRecord> log_records_ ABSL_GUARDED_BY(mu_);
  std::vector<std::pair<std::string, std::string>> resource_attributes_
      ABSL_GUARDED_BY(mu_);
};

std::string GetStringAttribute(const LogRecord& log_record,
                               absl::string_view key) {
  for (const auto& kv : log_record.attributes()) {
    if (kv.key() == key) return kv.value().string_value();
  }
  return "<missing>";
}

class XdsOtelAccessLogEnd2endTest : public XdsEnd2endTest {
 protected:
  class CollectorServerThread final : public ServerThread {
   public:
    explicit CollectorServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       InsecureServerCredentials()),
          service_(std::make_unique<FakeOtlpCollectorService>()) {}

    FakeOtlpCollectorService* service() { return service_.get(); }

   private:
    const char* Type() override { return "OtlpCollector"; }
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterCallbackGenericService(service_.get());
    }
    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    std::unique_ptr<FakeOtlpCollectorService> service_;
  };

  void SetUp() override {
    env_var_.emplace("GRPC_EXPERIMENTAL_XDS_OTEL_ACCESS_LOG");
    experimental::EnableOpenTelemetryXdsAccessLogging();
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
    // For server-side tests, the backend starts after it gets the listener
    // with the access log config.
    CreateBackends(1, /*xds_enabled=*/GetParam().filter_on_server());
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
        {"locality0", CreateEndpointsForBackends(0, 1)},
    })));
    collector_ = std::make_unique<CollectorServerThread>(this);
    collector_->Start();
  }

  void TearDown() override {
    if (collector_ != nullptr) collector_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  OpenTelemetryAccessLogConfig MakeAccessLogConfig() {
    OpenTelemetryAccessLogConfig config;
    auto* google_grpc = config.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->set_target_uri(collector_->target());
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::insecure::v3::
            InsecureCredentials());
    config.set_log_name("e2e-access-log");
    config.mutable_body()->set_string_value("%REQ(:path)% %GRPC_STATUS%");
    auto* attr = config.mutable_attributes()->add_values();
    attr->set_key("status");
    attr->mutable_value()->set_string_value("%GRPC_STATUS_NUMBER%");
    attr = config.mutable_attributes()->add_values();
    attr->set_key("peer");
    attr->mutable_value()->set_string_value("%DOWNSTREAM_REMOTE_ADDRESS%");
    config.mutable_buffer_flush_interval()->set_nanos(100000000);  // 100ms
    return config;
  }

  void SetAccessLogConfig(const OpenTelemetryAccessLogConfig& config) {
    Listener listener = GetParam().filter_on_server() ? default_server_listener_
                                                      : default_listener_;
    std::unique_ptr<HcmAccessor> hcm_accessor;
    if (GetParam().filter_on_server()) {
      hcm_accessor = std::make_unique<ServerHcmAccessor>();
    } else {
      hcm_accessor = std::make_unique<ClientHcmAccessor>();
    }
    HttpConnectionManager hcm = hcm_accessor->Unpack(listener);
    auto* access_log = hcm.add_access_log();
    access_log->set_name("envoy.access_loggers.open_telemetry");
    access_log->mutable_typed_config()->PackFrom(config);
    hcm_accessor->Pack(hcm, &listener);
    if (GetParam().filter_on_server()) {
      SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                                 backends_[0]->port(),
                                                 default_server_route_config_);
    } else {
      SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                       default_route_config_);
    }
  }

  void StartBackendServer() {
    StartBackend(0);
    if (GetParam().filter_on_server()) {
      EXPECT_THAT(backends_[0]->GetNextStatus(),
                  ::testing::Optional(absl::OkStatus()));
    }
  }

  std::optional<grpc_core::testing::ScopedExperimentalEnvVar> env_var_;
  std::unique_ptr<CollectorServerThread> collector_;
};

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsOtelAccessLogEnd2endTest,
    ::testing::Values(XdsTestType(), XdsTestType().set_filter_on_server()),
    &XdsTestType::Name);

TEST_P(XdsOtelAccessLogEnd2endTest, OneLogRecordPerRpc) {
  SetAccessLogConfig(MakeAccessLogConfig());
  StartBackendServer();
  constexpr size_t kNumRpcs = 3;
  CheckRpcSendOk(DEBUG_LOCATION, kNumRpcs);
  std::vector<LogRecord> log_records =
      collector_->service()->WaitForLogRecords(kNumRpcs, absl::Seconds(10));
  ASSERT_EQ(log_records.size(), kNumRpcs);
  for (const LogRecord& log_record : log_records) {
    EXPECT_EQ(log_record.body().string_value(),
              "/grpc.testing.EchoTestService/Echo OK");
    EXPECT_EQ(GetStringAttribute(log_record, "status"), "0");
    EXPECT_GT(log_record.time_unix_nano(), 0u);
    if (GetParam().filter_on_server()) {
      EXPECT_NE(GetStringAttribute(log_record, "peer"), "-");
    } else {
      EXPECT_EQ(GetStringAttribute(log_record, "peer"), "-");
    }
  }
  EXPECT_THAT(
      collector_->service()->resource_attributes(),
      ::testing::Contains(::testing::Pair("log_name", "e2e-access-log")));
  // No extra records arrive.
  absl::SleepFor(absl::Milliseconds(300) * grpc_test_slowdown_factor());
  EXPECT_EQ(collector_->service()
                ->WaitForLogRecords(kNumRpcs, absl::ZeroDuration())
                .size(),
            kNumRpcs);
}

TEST_P(XdsOtelAccessLogEnd2endTest, FailedRpcIsLogged) {
  SetAccessLogConfig(MakeAccessLogConfig());
  StartBackendServer();
  // Wait for the channel to be ready, then send an RPC that the backend
  // fails.
  CheckRpcSendOk(DEBUG_LOCATION);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::FAILED_PRECONDITION, "",
                      RpcOptions().set_server_fail(true));
  std::vector<LogRecord> log_records =
      collector_->service()->WaitForLogRecords(2, absl::Seconds(10));
  ASSERT_EQ(log_records.size(), 2u);
  EXPECT_THAT(log_records,
              ::testing::Contains(::testing::Truly([](const LogRecord& r) {
                return r.body().string_value() ==
                       "/grpc.testing.EchoTestService/Echo FailedPrecondition";
              })));
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
