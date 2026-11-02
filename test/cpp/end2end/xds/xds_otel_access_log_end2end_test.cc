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

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/sync.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_messages.pb.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/end2end_test_utils.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::rbac::v3::Policy;
using ::envoy::config::rbac::v3::RBAC_Action_DENY;
using ::envoy::extensions::access_loggers::open_telemetry::v3::
    OpenTelemetryAccessLogConfig;
using ::envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor;
using ::envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::service::ext_proc::v3::ProcessingRequest;
using ::envoy::service::ext_proc::v3::ProcessingResponse;
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

// A fake external processor.  For each request headers message, it
// overwrites x-user with "bob", adds x-added: yes, and removes x-remove.
class HeaderChangingExtProcService final
    : public ::envoy::service::ext_proc::v3::ExternalProcessor::Service {
 public:
  Status Process(ServerContext* /*context*/,
                 ServerReaderWriter<ProcessingResponse, ProcessingRequest>*
                     stream) override {
    ProcessingRequest request;
    while (stream->Read(&request)) {
      if (!request.has_request_headers()) continue;
      ProcessingResponse response;
      auto* mutation = response.mutable_request_headers()
                           ->mutable_response()
                           ->mutable_header_mutation();
      auto add_header = [mutation](absl::string_view key,
                                   absl::string_view value) {
        auto* option = mutation->add_set_headers();
        // The default action appends to an existing value.
        option->set_append_action(::envoy::config::core::v3::HeaderValueOption::
                                      OVERWRITE_IF_EXISTS_OR_ADD);
        option->mutable_header()->set_key(key);
        option->mutable_header()->set_raw_value(value);
      };
      add_header("x-user", "bob");
      add_header("x-added", "yes");
      mutation->add_remove_headers("x-remove");
      if (!stream->Write(response)) break;
    }
    return Status::OK;
  }
};

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

  class ExtProcServerThread final : public ServerThread {
   public:
    explicit ExtProcServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       InsecureServerCredentials()) {}

   private:
    const char* Type() override { return "ExtProc"; }
    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(&service_);
    }
    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    HeaderChangingExtProcService service_;
  };

  void SetUp() override {
    env_var_.emplace("GRPC_EXPERIMENTAL_XDS_OTEL_ACCESS_LOG");
    // The xDS HTTP filter registry is built with the bootstrap, so the
    // ext_proc filter must be enabled before InitClient().
    ext_proc_env_var_.emplace(GetParam().filter_on_server()
                                  ? "GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_SERVER"
                                  : "GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT");
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
    if (ext_proc_server_ != nullptr) ext_proc_server_->Shutdown();
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

  // Starts the fake external processor and returns an ext_proc filter
  // config that sends only the request headers to it.
  ExternalProcessor StartExtProcServer() {
    ext_proc_server_ = std::make_unique<ExtProcServerThread>(this);
    ext_proc_server_->Start();
    ExternalProcessor ext_proc;
    auto* google_grpc = ext_proc.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->set_target_uri(ext_proc_server_->target());
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::insecure::v3::
            InsecureCredentials());
    auto* mode = ext_proc.mutable_processing_mode();
    mode->set_request_header_mode(ProcessingMode::SEND);
    mode->set_response_header_mode(ProcessingMode::SKIP);
    mode->set_response_trailer_mode(ProcessingMode::SKIP);
    mode->set_request_body_mode(ProcessingMode::NONE);
    mode->set_response_body_mode(ProcessingMode::NONE);
    return ext_proc;
  }

  // If ext_proc is not null, an ext_proc filter is added before the router.
  // If route_config is not null, it replaces the default route config.
  // If extra_filter is not null, it is added before the router.
  void SetAccessLogConfig(const OpenTelemetryAccessLogConfig& config,
                          const ExternalProcessor* ext_proc = nullptr,
                          const RouteConfiguration* route_config = nullptr,
                          const HttpFilter* extra_filter = nullptr) {
    Listener listener = GetParam().filter_on_server() ? default_server_listener_
                                                      : default_listener_;
    std::unique_ptr<HcmAccessor> hcm_accessor;
    if (GetParam().filter_on_server()) {
      hcm_accessor = std::make_unique<ServerHcmAccessor>();
    } else {
      hcm_accessor = std::make_unique<ClientHcmAccessor>();
    }
    HttpConnectionManager hcm = hcm_accessor->Unpack(listener);
    if (ext_proc != nullptr) {
      HttpFilter* filter0 = hcm.mutable_http_filters(0);
      *hcm.add_http_filters() = *filter0;
      filter0->set_name("ext_proc");
      filter0->mutable_typed_config()->PackFrom(*ext_proc);
    }
    if (extra_filter != nullptr) {
      // The router is the last filter.  Put extra_filter in its place, then
      // add the router again after it.
      HttpFilter router = hcm.http_filters(hcm.http_filters_size() - 1);
      *hcm.mutable_http_filters(hcm.http_filters_size() - 1) = *extra_filter;
      *hcm.add_http_filters() = std::move(router);
    }
    auto* access_log = hcm.add_access_log();
    access_log->set_name("envoy.access_loggers.open_telemetry");
    access_log->mutable_typed_config()->PackFrom(config);
    hcm_accessor->Pack(hcm, &listener);
    if (GetParam().filter_on_server()) {
      SetServerListenerNameAndRouteConfiguration(
          balancer_.get(), listener, backends_[0]->port(),
          route_config != nullptr ? *route_config
                                  : default_server_route_config_);
    } else {
      SetListenerAndRouteConfiguration(
          balancer_.get(), listener,
          route_config != nullptr ? *route_config : default_route_config_);
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
  std::optional<grpc_core::testing::ScopedExperimentalEnvVar> ext_proc_env_var_;
  std::unique_ptr<CollectorServerThread> collector_;
  std::unique_ptr<ExtProcServerThread> ext_proc_server_;
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

// An RPC that matches no route fails before a route chain is selected.
// The route failure chain still writes one record.  The RPC gets the same
// status as without an access log.
TEST_P(XdsOtelAccessLogEnd2endTest, UnroutedRpcIsLogged) {
  RouteConfiguration route_config = GetParam().filter_on_server()
                                        ? default_server_route_config_
                                        : default_route_config_;
  route_config.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_match()
      ->set_path("/grpc.testing.EchoTestService/Echo");
  SetAccessLogConfig(MakeAccessLogConfig(), /*ext_proc=*/nullptr,
                     &route_config);
  StartBackendServer();
  CheckRpcSendOk(DEBUG_LOCATION);
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      GetParam().filter_on_server()
                          ? "UNAVAILABLE:no route matched"
                          : "No matching route found in xDS route config",
                      RpcOptions().set_rpc_method(METHOD_ECHO1));
  std::vector<LogRecord> log_records =
      collector_->service()->WaitForLogRecords(2, absl::Seconds(10));
  ASSERT_EQ(log_records.size(), 2u);
  EXPECT_THAT(log_records,
              ::testing::Contains(::testing::Truly([](const LogRecord& r) {
                return r.body().string_value() ==
                           "/grpc.testing.EchoTestService/Echo1 Unavailable" &&
                       GetStringAttribute(r, "status") == "14";
              })));
  // No extra records arrive.
  absl::SleepFor(absl::Milliseconds(300) * grpc_test_slowdown_factor());
  EXPECT_EQ(
      collector_->service()->WaitForLogRecords(2, absl::ZeroDuration()).size(),
      2u);
}

// ext_proc changes the request headers.  The access log filter is first in
// the chain, so it sees the headers before ext_proc runs.  The request
// header filter before the router captures them again, so the record shows
// the changed headers, as Envoy does.
TEST_P(XdsOtelAccessLogEnd2endTest, RecordShowsRequestHeadersAfterExtProc) {
  if (GetParam().filter_on_server()) {
    SKIP_TEST_FOR_PH2_SERVER("ext_proc is not yet supported with PH2");
  }
  OpenTelemetryAccessLogConfig config = MakeAccessLogConfig();
  config.mutable_body()->set_string_value(
      "%REQ(x-user)% %REQ(x-added)% %REQ(x-remove)%");
  ExternalProcessor ext_proc = StartExtProcServer();
  SetAccessLogConfig(config, &ext_proc);
  StartBackendServer();
  CheckRpcSendOk(
      DEBUG_LOCATION, 1,
      RpcOptions().set_metadata({{"x-user", "alice"}, {"x-remove", "gone"}}));
  std::vector<LogRecord> log_records =
      collector_->service()->WaitForLogRecords(1, absl::Seconds(10));
  ASSERT_EQ(log_records.size(), 1u);
  EXPECT_EQ(log_records[0].body().string_value(), "bob yes -");
}

// The default route matches every path, so an RPC for a service that the
// backend does not implement passes routing and the xDS filters.  The
// backend fails it with UNIMPLEMENTED.  The access log writes one record
// with that status.
TEST_P(XdsOtelAccessLogEnd2endTest, UnimplementedMethodIsLogged) {
  SetAccessLogConfig(MakeAccessLogConfig());
  StartBackendServer();
  CheckRpcSendOk(DEBUG_LOCATION);
  std::unique_ptr<UnimplementedEchoService::Stub> stub =
      UnimplementedEchoService::NewStub(channel_);
  ClientContext context;
  context.set_deadline(grpc_timeout_milliseconds_to_deadline(5000));
  EchoRequest request;
  request.set_message("hello");
  EchoResponse response;
  Status status = stub->Unimplemented(&context, request, &response);
  EXPECT_EQ(status.error_code(), StatusCode::UNIMPLEMENTED)
      << status.error_message();
  std::vector<LogRecord> log_records =
      collector_->service()->WaitForLogRecords(2, absl::Seconds(10));
  ASSERT_EQ(log_records.size(), 2u);
  EXPECT_THAT(log_records,
              ::testing::Contains(::testing::Truly([](const LogRecord& r) {
                return r.body().string_value() ==
                           "/grpc.testing.UnimplementedEchoService/"
                           "Unimplemented Unimplemented" &&
                       GetStringAttribute(r, "status") == "12";
              })));
  // No extra records arrive.
  absl::SleepFor(absl::Milliseconds(300) * grpc_test_slowdown_factor());
  EXPECT_EQ(
      collector_->service()->WaitForLogRecords(2, absl::ZeroDuration()).size(),
      2u);
}

// RBAC is a later filter than the access log filter.  When RBAC denies the
// RPC, the access log filter still sees the final status and writes one
// record.  RBAC runs only on the server.
TEST_P(XdsOtelAccessLogEnd2endTest, RbacDenialIsLogged) {
  if (!GetParam().filter_on_server()) {
    GTEST_SKIP() << "RBAC is a server-side filter";
  }
  ::envoy::extensions::filters::http::rbac::v3::RBAC rbac;
  auto* rules = rbac.mutable_rules();
  rules->set_action(RBAC_Action_DENY);
  Policy policy;
  policy.add_permissions()->set_any(true);
  policy.add_principals()->set_any(true);
  (*rules->mutable_policies())["deny-all"] = policy;
  HttpFilter rbac_filter;
  rbac_filter.set_name("envoy.filters.http.rbac");
  rbac_filter.mutable_typed_config()->PackFrom(rbac);
  SetAccessLogConfig(MakeAccessLogConfig(), /*ext_proc=*/nullptr,
                     /*route_config=*/nullptr, &rbac_filter);
  StartBackendServer();
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::PERMISSION_DENIED, ".*",
                      RpcOptions().set_wait_for_ready(true));
  std::vector<LogRecord> log_records =
      collector_->service()->WaitForLogRecords(1, absl::Seconds(10));
  ASSERT_EQ(log_records.size(), 1u);
  EXPECT_EQ(log_records[0].body().string_value(),
            "/grpc.testing.EchoTestService/Echo PermissionDenied");
  EXPECT_EQ(GetStringAttribute(log_records[0], "status"), "7");
  // No extra records arrive.
  absl::SleepFor(absl::Milliseconds(300) * grpc_test_slowdown_factor());
  EXPECT_EQ(
      collector_->service()->WaitForLogRecords(1, absl::ZeroDuration()).size(),
      1u);
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
