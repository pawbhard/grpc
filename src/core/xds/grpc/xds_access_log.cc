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

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "envoy/config/core/v3/grpc_service.upb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.upb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.upb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.upbdefs.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "opentelemetry/proto/common/v1/common.upb.h"
#include "src/core/filter/access_log/otel_access_log_sink.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/match.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/sync.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_grpc_service_parser.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kOtelAccessLogConfigType =
    "envoy.extensions.access_loggers.open_telemetry.v3."
    "OpenTelemetryAccessLogConfig";

constexpr char kOtlpLogsExportMethod[] =
    "/opentelemetry.proto.collector.logs.v1.LogsService/Export";

std::string AccessLogValueToString(const AccessLogValue& value) {
  return Match(
      value,
      [](const std::string& s) { return absl::StrCat("string=\"", s, "\""); },
      [](int64_t v) { return absl::StrCat("int=", v); },
      [](bool v) { return absl::StrCat("bool=", v ? "true" : "false"); },
      [](double v) { return absl::StrCat("double=", v); });
}

//
// OTLP export transport over an XdsTransport
//

// Sends each export request as a unary RPC on an XdsTransport.  The
// XdsTransport applies the channel credentials, call credentials,
// initial metadata, and timeout from the xDS GrpcService.
class XdsOtelLogsExportTransport final : public OtelLogsExportTransport {
 public:
  explicit XdsOtelLogsExportTransport(RefCountedPtr<XdsTransport> transport)
      : transport_(std::move(transport)) {}

  void Export(std::string serialized_request,
              absl::AnyInvocable<void(absl::Status)> on_done) override {
    // Export() is normally called on an OpenTelemetry SDK thread.
    ExecCtx exec_ctx;
    auto state = std::make_shared<CallState>();
    state->on_done = std::move(on_done);
    OrphanablePtr<XdsTransport::StreamingCall> call =
        transport_->CreateStreamingCall(
            kOtlpLogsExportMethod, std::make_unique<EventHandler>(state),
            XdsTransport::CallOptions().set_start_upon_send_message(true));
    call->SendMessage(std::move(serialized_request), /*send_half_close=*/true);
    call->StartRecvMessage();
    // Keep the call alive until the status arrives.  If the status already
    // arrived, `call` is orphaned when it goes out of scope.
    MutexLock lock(&state->mu);
    if (!state->done) state->call = std::move(call);
  }

 private:
  struct CallState {
    Mutex mu;
    bool done ABSL_GUARDED_BY(mu) = false;
    OrphanablePtr<XdsTransport::StreamingCall> call ABSL_GUARDED_BY(mu);
    absl::AnyInvocable<void(absl::Status)> on_done;
  };

  class EventHandler final : public XdsTransport::StreamingCall::EventHandler {
   public:
    explicit EventHandler(std::shared_ptr<CallState> state)
        : state_(std::move(state)) {}

    void OnRequestSent(bool /*ok*/) override {}
    void OnRecvMessage(absl::string_view /*payload*/) override {}

    void OnStatusReceived(absl::Status status) override {
      // The transport holds a ref to the call while this runs, so it is
      // safe to orphan the call here.
      OrphanablePtr<XdsTransport::StreamingCall> call;
      {
        MutexLock lock(&state_->mu);
        state_->done = true;
        call = std::move(state_->call);
      }
      auto on_done = std::move(state_->on_done);
      if (on_done != nullptr) on_done(std::move(status));
    }

   private:
    std::shared_ptr<CallState> state_;
  };

  RefCountedPtr<XdsTransport> transport_;
};

//
// Parsing
//

// Parses a value whose string form is a substitution template.
std::optional<AccessLogValueTemplate> ParseValueTemplate(
    const opentelemetry_proto_common_v1_AnyValue* value,
    ValidationErrors* errors) {
  if (value == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_string_value(value)) {
    auto formatter = AccessLogFormatter::Parse(UpbStringToAbsl(
        opentelemetry_proto_common_v1_AnyValue_string_value(value)));
    if (!formatter.ok()) {
      ValidationErrors::ScopedField field(errors, ".string_value");
      errors->AddError(formatter.status().message());
      return std::nullopt;
    }
    return std::move(*formatter);
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_int_value(value)) {
    return opentelemetry_proto_common_v1_AnyValue_int_value(value);
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_bool_value(value)) {
    return opentelemetry_proto_common_v1_AnyValue_bool_value(value);
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_double_value(value)) {
    return opentelemetry_proto_common_v1_AnyValue_double_value(value);
  }
  errors->AddError(
      "unsupported value type: only string_value, int_value, bool_value, and "
      "double_value are supported");
  return std::nullopt;
}

// Parses a value with no template support.
std::optional<AccessLogValue> ParseStaticValue(
    const opentelemetry_proto_common_v1_AnyValue* value,
    ValidationErrors* errors) {
  if (value == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_string_value(value)) {
    return UpbStringToStdString(
        opentelemetry_proto_common_v1_AnyValue_string_value(value));
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_int_value(value)) {
    return opentelemetry_proto_common_v1_AnyValue_int_value(value);
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_bool_value(value)) {
    return opentelemetry_proto_common_v1_AnyValue_bool_value(value);
  }
  if (opentelemetry_proto_common_v1_AnyValue_has_double_value(value)) {
    return opentelemetry_proto_common_v1_AnyValue_double_value(value);
  }
  errors->AddError(
      "unsupported value type: only string_value, int_value, bool_value, and "
      "double_value are supported");
  return std::nullopt;
}

// Sets key to value, replacing an existing entry with the same key.
void SetResourceAttribute(
    std::vector<std::pair<std::string, AccessLogValue>>* attributes,
    std::string key, AccessLogValue value) {
  for (auto& entry : *attributes) {
    if (entry.first == key) {
      entry.second = std::move(value);
      return;
    }
  }
  attributes->emplace_back(std::move(key), std::move(value));
}

// Returns nullopt if the entry must be ignored or is invalid.
std::optional<XdsOtelAccessLoggerConfig> ParseOtelAccessLoggerConfig(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_config, ValidationErrors* errors) {
  const auto* otel_config =
      envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_parse(
          serialized_config.data(), serialized_config.size(), context.arena);
  if (otel_config == nullptr) {
    errors->AddError("could not parse OpenTelemetry access log config");
    return std::nullopt;
  }
  const size_t original_error_size = errors->size();
  XdsOtelAccessLoggerConfig logger;
  // grpc_service, with fallback to the deprecated common_config.
  const auto* common_config =
      envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_common_config(
          otel_config);
  const envoy_config_core_v3_GrpcService* grpc_service =
      envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_grpc_service(
          otel_config);
  absl::string_view grpc_service_field = ".grpc_service";
  if (grpc_service == nullptr && common_config != nullptr) {
    grpc_service =
        envoy_extensions_access_loggers_grpc_v3_CommonGrpcAccessLogConfig_grpc_service(
            common_config);
    grpc_service_field = ".common_config.grpc_service";
  }
  if (grpc_service == nullptr) {
    if (envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_has_http_service(
            otel_config)) {
      // OTLP/HTTP is not supported.  Ignore the entry.
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[xds_client " << context.client
          << "] ignoring OpenTelemetry access log with http_service";
      return std::nullopt;
    }
    ValidationErrors::ScopedField field(errors, ".grpc_service");
    errors->AddError("field not present");
    return std::nullopt;
  }
  {
    ValidationErrors::ScopedField field(errors, grpc_service_field);
    logger.collector = std::make_shared<const GrpcXdsServerTarget>(
        ParseXdsGrpcService(context, grpc_service, errors));
  }
  // log_name, with fallback to the deprecated common_config.
  logger.log_name = UpbStringToStdString(
      envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_log_name(
          otel_config));
  if (logger.log_name.empty() && common_config != nullptr) {
    logger.log_name = UpbStringToStdString(
        envoy_extensions_access_loggers_grpc_v3_CommonGrpcAccessLogConfig_log_name(
            common_config));
  }
  // buffer_flush_interval
  if (const auto* interval =
          envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_buffer_flush_interval(
              otel_config);
      interval != nullptr) {
    ValidationErrors::ScopedField field(errors, ".buffer_flush_interval");
    logger.buffer_flush_interval = ParseDuration(interval, errors);
    if (logger.buffer_flush_interval <= Duration::Zero()) {
      errors->AddError("must be greater than zero");
    }
  }
  // buffer_size_bytes
  logger.buffer_size_bytes =
      ParseUInt32Value(
          envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_buffer_size_bytes(
              otel_config))
          .value_or(logger.buffer_size_bytes);
  // body
  if (envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_has_body(
          otel_config)) {
    ValidationErrors::ScopedField field(errors, ".body");
    logger.body = ParseValueTemplate(
        envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_body(
            otel_config),
        errors);
  }
  // attributes
  if (const auto* attributes =
          envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_attributes(
              otel_config);
      attributes != nullptr) {
    size_t num_values;
    const auto* const* values =
        opentelemetry_proto_common_v1_KeyValueList_values(attributes,
                                                          &num_values);
    for (size_t i = 0; i < num_values; ++i) {
      ValidationErrors::ScopedField field(
          errors, absl::StrCat(".attributes.values[", i, "]"));
      std::string key = UpbStringToStdString(
          opentelemetry_proto_common_v1_KeyValue_key(values[i]));
      if (key.empty()) {
        ValidationErrors::ScopedField field(errors, ".key");
        errors->AddError("must be non-empty");
        continue;
      }
      ValidationErrors::ScopedField value_field(errors, ".value");
      auto value = ParseValueTemplate(
          opentelemetry_proto_common_v1_KeyValue_value(values[i]), errors);
      if (value.has_value()) {
        logger.attributes.emplace_back(std::move(key), std::move(*value));
      }
    }
  }
  // Built-in labels, as in Envoy.
  if (!envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_disable_builtin_labels(
          otel_config)) {
    const XdsBootstrap::Node* node =
        DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap()).node();
    logger.resource_attributes.emplace_back("log_name", logger.log_name);
    logger.resource_attributes.emplace_back(
        "zone_name", node == nullptr ? "" : node->locality_zone());
    logger.resource_attributes.emplace_back(
        "cluster_name", node == nullptr ? "" : node->cluster());
    logger.resource_attributes.emplace_back("node_name",
                                            node == nullptr ? "" : node->id());
  }
  // resource_attributes
  if (const auto* resource_attributes =
          envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_resource_attributes(
              otel_config);
      resource_attributes != nullptr) {
    size_t num_values;
    const auto* const* values =
        opentelemetry_proto_common_v1_KeyValueList_values(resource_attributes,
                                                          &num_values);
    for (size_t i = 0; i < num_values; ++i) {
      ValidationErrors::ScopedField field(
          errors, absl::StrCat(".resource_attributes.values[", i, "]"));
      std::string key = UpbStringToStdString(
          opentelemetry_proto_common_v1_KeyValue_key(values[i]));
      if (key.empty()) {
        ValidationErrors::ScopedField field(errors, ".key");
        errors->AddError("must be non-empty");
        continue;
      }
      ValidationErrors::ScopedField value_field(errors, ".value");
      auto value = ParseStaticValue(
          opentelemetry_proto_common_v1_KeyValue_value(values[i]), errors);
      if (value.has_value()) {
        SetResourceAttribute(&logger.resource_attributes, std::move(key),
                             std::move(*value));
      }
    }
  }
  if (errors->size() != original_error_size) return std::nullopt;
  return logger;
}

}  // namespace

//
// XdsOtelAccessLoggerConfig
//

bool XdsOtelAccessLoggerConfig::operator==(
    const XdsOtelAccessLoggerConfig& other) const {
  if ((collector == nullptr) != (other.collector == nullptr)) return false;
  if (collector != nullptr && !collector->Equals(*other.collector)) {
    return false;
  }
  return log_name == other.log_name &&
         resource_attributes == other.resource_attributes &&
         buffer_flush_interval == other.buffer_flush_interval &&
         buffer_size_bytes == other.buffer_size_bytes && body == other.body &&
         attributes == other.attributes;
}

std::string XdsOtelAccessLoggerConfig::ToString() const {
  std::vector<std::string> parts;
  parts.push_back(absl::StrCat(
      "collector=", collector == nullptr ? "<null>" : collector->Key()));
  parts.push_back(absl::StrCat("log_name=\"", log_name, "\""));
  if (!resource_attributes.empty()) {
    std::vector<std::string> attrs;
    for (const auto& [key, value] : resource_attributes) {
      attrs.push_back(
          absl::StrCat(key, "={", AccessLogValueToString(value), "}"));
    }
    parts.push_back(
        absl::StrCat("resource_attributes={", absl::StrJoin(attrs, ", "), "}"));
  }
  parts.push_back(
      absl::StrCat("buffer_flush_interval=", buffer_flush_interval.ToString()));
  parts.push_back(absl::StrCat("buffer_size_bytes=", buffer_size_bytes));
  if (body.has_value()) {
    parts.push_back(
        absl::StrCat("body={", AccessLogValueTemplateToString(*body), "}"));
  }
  if (!attributes.empty()) {
    std::vector<std::string> attrs;
    for (const auto& [key, value] : attributes) {
      attrs.push_back(
          absl::StrCat(key, "={", AccessLogValueTemplateToString(value), "}"));
    }
    parts.push_back(
        absl::StrCat("attributes={", absl::StrJoin(attrs, ", "), "}"));
  }
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

std::string XdsOtelAccessLoggerConfig::SinkKey() const {
  // The templates are evaluated by the filter, not by the sink, so they
  // are not part of the key.
  std::vector<std::string> attrs;
  for (const auto& [key, value] : resource_attributes) {
    attrs.push_back(
        absl::StrCat(key, "={", AccessLogValueToString(value), "}"));
  }
  return absl::StrCat(
      "{collector=", collector == nullptr ? "<null>" : collector->Key(),
      ", log_name=\"", log_name, "\", resource_attributes={",
      absl::StrJoin(attrs, ", "),
      "}, buffer_flush_interval=", buffer_flush_interval.ToString(),
      ", buffer_size_bytes=", buffer_size_bytes, "}");
}

//
// XdsAccessLogConfig
//

bool XdsAccessLogConfig::Equals(const FilterConfig& other) const {
  const auto& o = DownCast<const XdsAccessLogConfig&>(other);
  return is_client == o.is_client && loggers == o.loggers;
}

std::string XdsAccessLogConfig::ToString() const {
  std::vector<std::string> logger_strings;
  logger_strings.reserve(loggers.size());
  for (const auto& logger : loggers)
    logger_strings.push_back(logger.ToString());
  return absl::StrCat("{is_client=", is_client ? "true" : "false",
                      ", loggers=[", absl::StrJoin(logger_strings, ", "), "]}");
}

//
// ParseXdsAccessLogs()
//

RefCountedPtr<const FilterConfig> ParseXdsAccessLogs(
    bool is_client, const XdsResourceType::DecodeContext& context,
    const envoy_config_accesslog_v3_AccessLog* const* access_logs,
    size_t num_access_logs, ValidationErrors* errors) {
  if (num_access_logs == 0 || !XdsOtelAccessLogEnabled()) return nullptr;
  auto config = MakeRefCounted<XdsAccessLogConfig>();
  config->is_client = is_client;
  for (size_t i = 0; i < num_access_logs; ++i) {
    ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
    const auto* access_log = access_logs[i];
    // AccessLogFilter is not supported.  Ignore the entry rather than log
    // every RPC.
    if (envoy_config_accesslog_v3_AccessLog_has_filter(access_log)) {
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[xds_client " << context.client
          << "] ignoring access log with unsupported AccessLogFilter";
      continue;
    }
    const google_protobuf_Any* typed_config =
        envoy_config_accesslog_v3_AccessLog_typed_config(access_log);
    if (typed_config == nullptr) continue;
    ValidationErrors::ScopedField typed_config_field(errors, ".typed_config");
    auto extension = ExtractXdsExtension(context, typed_config, errors);
    if (!extension.has_value()) continue;
    // Unsupported access logger types are ignored.
    if (extension->type != kOtelAccessLogConfigType) continue;
    const absl::string_view* serialized_config =
        std::get_if<absl::string_view>(&extension->value);
    if (serialized_config == nullptr) {
      errors->AddError("could not parse OpenTelemetry access log config");
      continue;
    }
    auto logger =
        ParseOtelAccessLoggerConfig(context, *serialized_config, errors);
    if (logger.has_value()) config->loggers.push_back(std::move(*logger));
  }
  if (config->loggers.empty()) return nullptr;
  return config;
}

//
// XdsAccessLogFilterFactory
//

const XdsAccessLogFilterFactory* XdsAccessLogFilterFactory::Get() {
  static const XdsAccessLogFilterFactory* factory =
      new XdsAccessLogFilterFactory();
  return factory;
}

absl::string_view XdsAccessLogFilterFactory::ConfigProtoName() const {
  return kOtelAccessLogConfigType;
}

void XdsAccessLogFilterFactory::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_access_loggers_open_telemetry_v3_OpenTelemetryAccessLogConfig_getmsgdef(
      symtab);
}

void XdsAccessLogFilterFactory::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  // Skip the filter if no logger has a sink.
  if (config == nullptr ||
      DownCast<const AccessLogFilter::Config&>(*config).loggers.empty()) {
    return;
  }
  builder.AddFilter<AccessLogFilter>(std::move(config));
}

RefCountedPtr<const FilterConfig>
XdsAccessLogFilterFactory::ParseTopLevelConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& /*context*/,
    const XdsExtension& /*extension*/, ValidationErrors* errors) const {
  errors->AddError("access loggers are not HTTP filters");
  return nullptr;
}

RefCountedPtr<const FilterConfig>
XdsAccessLogFilterFactory::ParseOverrideConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& /*context*/,
    const XdsExtension& /*extension*/, ValidationErrors* errors) const {
  errors->AddError("access loggers do not support config overrides");
  return nullptr;
}

RefCountedPtr<const FilterConfig> XdsAccessLogFilterFactory::MergeConfigs(
    RefCountedPtr<const FilterConfig> top_level_config,
    RefCountedPtr<const FilterConfig> /*virtual_host_override_config*/,
    RefCountedPtr<const FilterConfig> /*route_override_config*/,
    RefCountedPtr<const FilterConfig> /*cluster_weight_override_config*/,
    XdsTransportFactory& transport_factory, Blackboard& blackboard) const {
  const auto& parsed_config =
      DownCast<const XdsAccessLogConfig&>(*top_level_config);
  auto config = MakeRefCounted<AccessLogFilter::Config>();
  config->is_client = parsed_config.is_client;
  std::shared_ptr<OtelAccessLogSinkFactory> sink_factory =
      GetOtelAccessLogSinkFactory();
  if (sink_factory == nullptr) {
    LOG_EVERY_N_SEC(WARNING, 60)
        << "OpenTelemetry access log is configured, but no sink factory is "
           "registered; access logs are dropped";
    return config;
  }
  for (const auto& logger_config : parsed_config.loggers) {
    AccessLogFilter::Logger logger;
    logger.body = logger_config.body;
    logger.attributes = logger_config.attributes;
    logger.sink = blackboard.GetOrSet<AccessLogFilter::SinkHolder>(
        logger_config.SinkKey(), [&]() {
          absl::Status status;
          auto transport =
              transport_factory.GetTransport(*logger_config.collector, &status);
          if (!status.ok()) {
            LOG(ERROR) << "error creating OTLP collector channel for access "
                          "log: "
                       << status;
          }
          if (transport == nullptr) {
            // No way to export.  The logger is skipped below.
            return MakeRefCounted<AccessLogFilter::SinkHolder>(nullptr);
          }
          OtelAccessLogSinkArgs args;
          args.transport =
              MakeRefCounted<XdsOtelLogsExportTransport>(std::move(transport));
          args.log_name = logger_config.log_name;
          args.resource_attributes = logger_config.resource_attributes;
          args.buffer_flush_interval = logger_config.buffer_flush_interval;
          args.buffer_size_bytes = logger_config.buffer_size_bytes;
          return MakeRefCounted<AccessLogFilter::SinkHolder>(
              sink_factory->CreateSink(std::move(args)));
        });
    if (logger.sink == nullptr || logger.sink->sink() == nullptr) continue;
    config->loggers.push_back(std::move(logger));
  }
  config->ComputeHeaderNames();
  return config;
}

}  // namespace grpc_core
