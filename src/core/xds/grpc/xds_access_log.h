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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_ACCESS_LOG_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_ACCESS_LOG_H

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/accesslog/v3/accesslog.upb.h"
#include "src/core/filter/access_log/access_log_filter.h"
#include "src/core/filter/access_log/access_log_sink.h"
#include "src/core/filter/filter_args.h"
#include "src/core/filter/filter_chain.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "src/core/xds/xds_client/xds_transport_factory.h"
#include "upb/reflection/def.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// The parsed form of one OpenTelemetryAccessLogConfig.
struct XdsOtelAccessLoggerConfig {
  // The OTLP collector.  Never null after a successful parse.
  std::shared_ptr<const GrpcXdsServerTarget> collector;
  std::string log_name;
  // Resource attributes, with the built-in labels first (unless disabled).
  // Configured resource attributes replace built-in labels with the same
  // key.
  std::vector<std::pair<std::string, AccessLogValue>> resource_attributes;
  Duration buffer_flush_interval = Duration::Seconds(1);
  uint32_t buffer_size_bytes = 16384;
  std::optional<AccessLogValueTemplate> body;
  std::vector<std::pair<std::string, AccessLogValueTemplate>> attributes;

  bool operator==(const XdsOtelAccessLoggerConfig& other) const;
  std::string ToString() const;

  // Key that identifies the sink.  Loggers with the same key share one
  // sink (and so one OpenTelemetry LoggerProvider).
  std::string SinkKey() const;
};

// The access log config of one HttpConnectionManager.
// XdsAccessLogFilterFactory::MergeConfigs() converts it into an
// AccessLogFilter::Config.
struct XdsAccessLogConfig final : public FilterConfig {
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("xds_access_log_config");
  }
  UniqueTypeName type() const override { return Type(); }

  bool Equals(const FilterConfig& other) const override;
  std::string ToString() const override;

  bool is_client = true;
  std::vector<XdsOtelAccessLoggerConfig> loggers;
};

// Parses the HttpConnectionManager access_log list.
// - Returns nullptr if the feature is disabled or no supported logger is
//   configured.
// - Ignores entries with an unsupported type, an AccessLogFilter, or only
//   an http_service.
// - Adds errors for invalid OpenTelemetryAccessLogConfig entries.
RefCountedPtr<const FilterConfig> ParseXdsAccessLogs(
    bool is_client, const XdsResourceType::DecodeContext& context,
    const envoy_config_accesslog_v3_AccessLog* const* access_logs,
    size_t num_access_logs, ValidationErrors* errors);

// Adds the AccessLogFilter to filter chains.
//
// This factory is not registered in the XdsHttpFilterRegistry, so it
// cannot be configured as an HTTP filter.  XdsRouting adds it as the
// first filter of each chain when the HttpConnectionManager has an access
// log config.
class XdsAccessLogFilterFactory final : public XdsHttpFilterFactory {
 public:
  // Name of the internal filter entry in each chain.
  static constexpr absl::string_view kFilterName = "grpc.internal.access_log";

  static const XdsAccessLogFilterFactory* Get();

  absl::string_view ConfigProtoName() const override;
  absl::string_view OverrideConfigProtoName() const override { return ""; }
  void PopulateSymtab(upb_DefPool* symtab) const override;
  void AddFilter(FilterChainBuilder& builder,
                 RefCountedPtr<const FilterConfig> config) const override;
  RefCountedPtr<const FilterConfig> ParseTopLevelConfig(
      absl::string_view instance_name,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const override;
  RefCountedPtr<const FilterConfig> ParseOverrideConfig(
      absl::string_view instance_name,
      const XdsResourceType::DecodeContext& context,
      const XdsExtension& extension, ValidationErrors* errors) const override;
  RefCountedPtr<const FilterConfig> MergeConfigs(
      RefCountedPtr<const FilterConfig> top_level_config,
      RefCountedPtr<const FilterConfig> virtual_host_override_config,
      RefCountedPtr<const FilterConfig> route_override_config,
      RefCountedPtr<const FilterConfig> cluster_weight_override_config,
      XdsTransportFactory& transport_factory,
      Blackboard& blackboard) const override;
  bool IsSupportedOnClients() const override { return true; }
  bool IsSupportedOnServers() const override { return true; }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_ACCESS_LOG_H
