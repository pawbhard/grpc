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

#include "src/core/xds/grpc/xds_access_log.h"

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/wrappers.pb.h>
#include <grpc/grpc.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "src/core/util/crash.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc_builder.h"
#include "src/core/xds/grpc/xds_listener.h"
#include "src/core/xds/grpc/xds_listener_parser.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

using envoy::config::accesslog::v3::AccessLog;
using envoy::config::listener::v3::Listener;
using envoy::extensions::access_loggers::open_telemetry::v3::
    OpenTelemetryAccessLogConfig;
using envoy::extensions::filters::http::router::v3::Router;
using envoy::extensions::filters::network::http_connection_manager::v3::
    HttpConnectionManager;

namespace grpc_core {
namespace testing {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

constexpr char kCollectorTarget[] = "dns:collector.example.com";

struct Side {
  bool is_client;
  static std::string Name(const ::testing::TestParamInfo<Side>& info) {
    return info.param.is_client ? "Client" : "Server";
  }
};

class XdsAccessLogParseTest : public ::testing::TestWithParam<Side> {
 protected:
  XdsAccessLogParseTest()
      : env_("GRPC_EXPERIMENTAL_XDS_OTEL_ACCESS_LOG"),
        xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(),
                        *xds_client_->bootstrap().servers().front(),
                        upb_def_pool_.ptr(), upb_arena_.ptr()} {}

  static RefCountedPtr<XdsClient> MakeXdsClient() {
    auto bootstrap = GrpcXdsBootstrapBuilder::Build(
        absl::StrCat("{\n"
                     "  \"xds_servers\": [{\n"
                     "    \"server_uri\": \"xds.example.com\",\n"
                     "    \"channel_creds\": [{\"type\": \"google_default\"}]\n"
                     "  }],\n"
                     "  \"node\": {\n"
                     "    \"id\": \"node-1\",\n"
                     "    \"cluster\": \"cluster-1\",\n"
                     "    \"locality\": {\"zone\": \"zone-1\"}\n"
                     "  },\n"
                     "  \"allowed_grpc_services\": {\n"
                     "    \"",
                     kCollectorTarget,
                     "\": {\n"
                     "      \"channel_creds\": [{\"type\": \"insecure\"}]\n"
                     "    }\n"
                     "  }\n"
                     "}"));
    if (!bootstrap.ok()) {
      Crash(absl::StrFormat("Error parsing bootstrap: %s",
                            bootstrap.status().ToString().c_str()));
    }
    return MakeRefCounted<XdsClient>(std::move(*bootstrap),
                                     /*transport_factory=*/nullptr,
                                     /*event_engine=*/nullptr,
                                     /*metrics_reporter=*/nullptr, "foo agent",
                                     "foo version");
  }

  static OpenTelemetryAccessLogConfig MakeValidOtelConfig() {
    OpenTelemetryAccessLogConfig config;
    config.mutable_grpc_service()->mutable_google_grpc()->set_target_uri(
        kCollectorTarget);
    config.set_log_name("my-log");
    config.mutable_body()->set_string_value("%REQ(:path)% %GRPC_STATUS%");
    auto* attr = config.mutable_attributes()->add_values();
    attr->set_key("ua");
    attr->mutable_value()->set_string_value("%REQ(user-agent)%");
    attr = config.mutable_attributes()->add_values();
    attr->set_key("static");
    attr->mutable_value()->set_int_value(5);
    auto* resource_attr = config.mutable_resource_attributes()->add_values();
    resource_attr->set_key("region");
    resource_attr->mutable_value()->set_string_value("us-east1");
    resource_attr = config.mutable_resource_attributes()->add_values();
    resource_attr->set_key("node_name");
    resource_attr->mutable_value()->set_string_value("override");
    config.mutable_buffer_flush_interval()->set_seconds(2);
    config.mutable_buffer_size_bytes()->set_value(1024);
    return config;
  }

  static HttpConnectionManager MakeHcm() {
    HttpConnectionManager hcm;
    auto* filter = hcm.add_http_filters();
    filter->set_name("router");
    filter->mutable_typed_config()->PackFrom(Router());
    auto* rds = hcm.mutable_rds();
    rds->set_route_config_name("rds_name");
    rds->mutable_config_source()->mutable_self();
    return hcm;
  }

  static void AddOtelAccessLog(HttpConnectionManager* hcm,
                               const OpenTelemetryAccessLogConfig& config) {
    AccessLog* access_log = hcm->add_access_log();
    access_log->set_name("envoy.access_loggers.open_telemetry");
    access_log->mutable_typed_config()->PackFrom(config);
  }

  absl::StatusOr<XdsListenerResource::HttpConnectionManager> Decode(
      const HttpConnectionManager& hcm) {
    Listener listener;
    listener.set_name("foo");
    if (GetParam().is_client) {
      listener.mutable_api_listener()->mutable_api_listener()->PackFrom(hcm);
    } else {
      listener.mutable_default_filter_chain()
          ->add_filters()
          ->mutable_typed_config()
          ->PackFrom(hcm);
      auto* address = listener.mutable_address()->mutable_socket_address();
      address->set_address("127.0.0.1");
      address->set_port_value(443);
    }
    std::string serialized;
    EXPECT_TRUE(listener.SerializeToString(&serialized));
    auto result =
        XdsListenerResourceType::Get()->Decode(decode_context_, serialized);
    if (!result.resource.ok()) return result.resource.status();
    const auto& resource =
        static_cast<const XdsListenerResource&>(**result.resource);
    if (GetParam().is_client) {
      return std::get<XdsListenerResource::HttpConnectionManager>(
          resource.listener);
    }
    return std::get<XdsListenerResource::TcpListener>(resource.listener)
        .default_filter_chain->http_connection_manager;
  }

  std::string ErrorPrefix() const {
    if (GetParam().is_client) {
      return "errors validating ApiListener: [field:api_listener.api_listener"
             ".value[envoy.extensions.filters.network.http_connection_manager"
             ".v3.HttpConnectionManager].access_log[0].typed_config.value"
             "[envoy.extensions.access_loggers.open_telemetry.v3"
             ".OpenTelemetryAccessLogConfig]";
    }
    return "errors validating server Listener: [field:default_filter_chain"
           ".filters[0].typed_config.value[envoy.extensions.filters.network"
           ".http_connection_manager.v3.HttpConnectionManager].access_log[0]"
           ".typed_config.value[envoy.extensions.access_loggers.open_telemetry"
           ".v3.OpenTelemetryAccessLogConfig]";
  }

  // The env var must be set before the bootstrap is parsed, because it
  // also enables "allowed_grpc_services".
  ScopedExperimentalEnvVar env_;
  RefCountedPtr<XdsClient> xds_client_;
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
  XdsResourceType::DecodeContext decode_context_;
};

INSTANTIATE_TEST_SUITE_P(XdsAccessLog, XdsAccessLogParseTest,
                         ::testing::Values(Side{true}, Side{false}),
                         &Side::Name);

TEST_P(XdsAccessLogParseTest, DisabledByEnvVar) {
  ScopedEnvVar env("GRPC_EXPERIMENTAL_XDS_OTEL_ACCESS_LOG", "false");
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, MakeValidOtelConfig());
  auto result = Decode(hcm);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->access_log_config, nullptr);
}

TEST_P(XdsAccessLogParseTest, ValidConfig) {
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, MakeValidOtelConfig());
  auto result = Decode(hcm);
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_NE(result->access_log_config, nullptr);
  const auto& config =
      DownCast<const XdsAccessLogConfig&>(*result->access_log_config);
  EXPECT_EQ(config.is_client, GetParam().is_client);
  ASSERT_EQ(config.loggers.size(), 1u);
  const XdsOtelAccessLoggerConfig& logger = config.loggers[0];
  ASSERT_NE(logger.collector, nullptr);
  EXPECT_EQ(logger.collector->server_uri(), kCollectorTarget);
  ASSERT_NE(logger.collector->channel_creds_config(), nullptr);
  EXPECT_EQ(logger.collector->channel_creds_config()->type(), "insecure");
  EXPECT_EQ(logger.log_name, "my-log");
  EXPECT_EQ(logger.buffer_flush_interval, Duration::Seconds(2));
  EXPECT_EQ(logger.buffer_size_bytes, 1024u);
  ASSERT_TRUE(logger.body.has_value());
  EXPECT_EQ(std::get<AccessLogFormatter>(*logger.body).format(),
            "%REQ(:path)% %GRPC_STATUS%");
  ASSERT_EQ(logger.attributes.size(), 2u);
  EXPECT_EQ(logger.attributes[0].first, "ua");
  EXPECT_EQ(std::get<AccessLogFormatter>(logger.attributes[0].second).format(),
            "%REQ(user-agent)%");
  EXPECT_EQ(logger.attributes[1].first, "static");
  EXPECT_EQ(std::get<int64_t>(logger.attributes[1].second), 5);
  // Built-in labels first; configured attributes replace same-key labels.
  EXPECT_THAT(logger.resource_attributes,
              ElementsAre(Pair("log_name", AccessLogValue("my-log")),
                          Pair("zone_name", AccessLogValue("zone-1")),
                          Pair("cluster_name", AccessLogValue("cluster-1")),
                          Pair("node_name", AccessLogValue("override")),
                          Pair("region", AccessLogValue("us-east1"))));
  // Equality and ToString.
  auto result2 = Decode(hcm);
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(*result, *result2);
  EXPECT_THAT(result->ToString(), ::testing::HasSubstr("access_log="));
}

TEST_P(XdsAccessLogParseTest, DefaultsAndBuiltinLabelsDisabled) {
  OpenTelemetryAccessLogConfig otel_config;
  otel_config.mutable_grpc_service()->mutable_google_grpc()->set_target_uri(
      kCollectorTarget);
  otel_config.set_disable_builtin_labels(true);
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, otel_config);
  auto result = Decode(hcm);
  ASSERT_TRUE(result.ok()) << result.status();
  ASSERT_NE(result->access_log_config, nullptr);
  const auto& logger =
      DownCast<const XdsAccessLogConfig&>(*result->access_log_config)
          .loggers[0];
  EXPECT_TRUE(logger.resource_attributes.empty());
  EXPECT_FALSE(logger.body.has_value());
  EXPECT_EQ(logger.buffer_flush_interval, Duration::Seconds(1));
  EXPECT_EQ(logger.buffer_size_bytes, 16384u);
}

TEST_P(XdsAccessLogParseTest, UnsupportedLoggerTypeIgnored) {
  HttpConnectionManager hcm = MakeHcm();
  AccessLog* access_log = hcm.add_access_log();
  access_log->set_name("other");
  access_log->mutable_typed_config()->PackFrom(Router());
  auto result = Decode(hcm);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->access_log_config, nullptr);
}

TEST_P(XdsAccessLogParseTest, AccessLogFilterIgnored) {
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, MakeValidOtelConfig());
  hcm.mutable_access_log(0)
      ->mutable_filter()
      ->mutable_not_health_check_filter();
  auto result = Decode(hcm);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->access_log_config, nullptr);
}

TEST_P(XdsAccessLogParseTest, HttpServiceOnlyIgnored) {
  OpenTelemetryAccessLogConfig otel_config;
  otel_config.mutable_http_service();
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, otel_config);
  auto result = Decode(hcm);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->access_log_config, nullptr);
}

TEST_P(XdsAccessLogParseTest, MultipleLoggersKeepOrder) {
  HttpConnectionManager hcm = MakeHcm();
  OpenTelemetryAccessLogConfig first = MakeValidOtelConfig();
  first.set_log_name("first");
  OpenTelemetryAccessLogConfig second = MakeValidOtelConfig();
  second.set_log_name("second");
  AddOtelAccessLog(&hcm, first);
  AddOtelAccessLog(&hcm, second);
  auto result = Decode(hcm);
  ASSERT_TRUE(result.ok()) << result.status();
  const auto& config =
      DownCast<const XdsAccessLogConfig&>(*result->access_log_config);
  ASSERT_EQ(config.loggers.size(), 2u);
  EXPECT_EQ(config.loggers[0].log_name, "first");
  EXPECT_EQ(config.loggers[1].log_name, "second");
  EXPECT_NE(config.loggers[0].SinkKey(), config.loggers[1].SinkKey());
}

TEST_P(XdsAccessLogParseTest, MissingGrpcService) {
  OpenTelemetryAccessLogConfig otel_config = MakeValidOtelConfig();
  otel_config.clear_grpc_service();
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, otel_config);
  auto result = Decode(hcm);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      result.status().message(),
      absl::StrCat(ErrorPrefix(), ".grpc_service error:field not present]"))
      << result.status();
}

TEST_P(XdsAccessLogParseTest, InvalidTemplate) {
  OpenTelemetryAccessLogConfig otel_config = MakeValidOtelConfig();
  otel_config.mutable_body()->set_string_value("%NOT_AN_OPERATOR%");
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, otel_config);
  auto result = Decode(hcm);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(),
            absl::StrCat(ErrorPrefix(),
                         ".body.string_value error:unsupported access log "
                         "operator: NOT_AN_OPERATOR]"))
      << result.status();
}

TEST_P(XdsAccessLogParseTest, NestedValueRejected) {
  OpenTelemetryAccessLogConfig otel_config = MakeValidOtelConfig();
  otel_config.mutable_attributes()
      ->mutable_values(0)
      ->mutable_value()
      ->mutable_kvlist_value();
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, otel_config);
  auto result = Decode(hcm);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(),
            absl::StrCat(ErrorPrefix(),
                         ".attributes.values[0].value error:unsupported value "
                         "type: only string_value, int_value, bool_value, and "
                         "double_value are supported]"))
      << result.status();
}

TEST_P(XdsAccessLogParseTest, InvalidBufferFlushInterval) {
  OpenTelemetryAccessLogConfig otel_config = MakeValidOtelConfig();
  otel_config.mutable_buffer_flush_interval()->set_seconds(0);
  HttpConnectionManager hcm = MakeHcm();
  AddOtelAccessLog(&hcm, otel_config);
  auto result = Decode(hcm);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(),
            absl::StrCat(ErrorPrefix(),
                         ".buffer_flush_interval error:must be greater than "
                         "zero]"))
      << result.status();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
