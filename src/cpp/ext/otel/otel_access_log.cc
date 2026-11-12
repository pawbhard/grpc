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

#include "src/cpp/ext/otel/otel_access_log.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/ext/otel_access_log.h>
#include <grpcpp/version_info.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/otlp_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/logs/log_record.h"
#include "opentelemetry/logs/logger.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/logs/batch_log_record_processor.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/resource/resource.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h"
// clang-format on

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace internal {

namespace {

namespace otel_common = opentelemetry::common;
namespace otel_logs = opentelemetry::logs;
namespace otel_sdk_logs = opentelemetry::sdk::logs;

// Estimated size of one exported record, used to convert
// buffer_size_bytes to a record count.
constexpr uint32_t kEstimatedRecordSizeBytes = 256;
// Upper bound on the time that one export waits for the collector.
constexpr absl::Duration kDefaultExportTimeout = absl::Seconds(30);
// Logger name used when log_name is empty.
constexpr absl::string_view kDefaultLoggerName = "grpc.access_log";

otel_common::AttributeValue ToAttributeValue(
    const grpc_core::AccessLogValue& value) {
  if (const auto* s = std::get_if<std::string>(&value)) {
    return opentelemetry::nostd::string_view(s->data(), s->size());
  }
  if (const auto* i = std::get_if<int64_t>(&value)) return *i;
  if (const auto* b = std::get_if<bool>(&value)) return *b;
  return std::get<double>(value);
}

opentelemetry::nostd::string_view ToOtelStringView(absl::string_view s) {
  return opentelemetry::nostd::string_view(s.data(), s.size());
}

// Owns one LoggerProvider with a BatchLogRecordProcessor.
class OtelAccessLogSink final : public grpc_core::AccessLogSink {
 public:
  OtelAccessLogSink(std::unique_ptr<otel_sdk_logs::LogRecordExporter> exporter,
                    const grpc_core::OtelAccessLogSinkArgs& args) {
    opentelemetry::sdk::resource::ResourceAttributes resource_attributes;
    for (const auto& [key, value] : args.resource_attributes) {
      resource_attributes.SetAttribute(ToOtelStringView(key),
                                       ToAttributeValue(value));
    }
    auto processor = std::make_unique<otel_sdk_logs::BatchLogRecordProcessor>(
        std::move(exporter), MakeBatchProcessorOptions(args));
    provider_ = std::make_shared<otel_sdk_logs::LoggerProvider>(
        std::move(processor),
        opentelemetry::sdk::resource::Resource::Create(resource_attributes));
    const absl::string_view logger_name =
        args.log_name.empty() ? kDefaultLoggerName : args.log_name;
    // The SDK uses library_name as the instrumentation scope name, so the
    // scope name is the log name.
    logger_ =
        provider_->GetLogger(ToOtelStringView(logger_name),
                             /*library_name=*/ToOtelStringView(logger_name),
                             /*library_version=*/GRPC_CPP_VERSION_STRING);
  }

  // Runs on an EventEngine thread (see AccessLogFilter::SinkHolder), so it
  // is safe to block while the buffered records are flushed.
  ~OtelAccessLogSink() override { provider_->Shutdown(); }

  void Log(grpc_core::AccessLogRecord record) override {
    opentelemetry::nostd::unique_ptr<otel_logs::LogRecord> log_record =
        logger_->CreateLogRecord();
    if (log_record == nullptr) return;
    log_record->SetTimestamp(
        otel_common::SystemTimestamp(absl::ToChronoTime(record.timestamp)));
    if (record.body.has_value()) {
      log_record->SetBody(ToAttributeValue(*record.body));
    }
    for (const auto& [key, value] : record.attributes) {
      log_record->SetAttribute(ToOtelStringView(key), ToAttributeValue(value));
    }
    logger_->EmitLogRecord(std::move(log_record));
  }

 private:
  std::shared_ptr<otel_sdk_logs::LoggerProvider> provider_;
  opentelemetry::nostd::shared_ptr<otel_logs::Logger> logger_;
};

}  // namespace

//
// OtlpGrpcLogRecordExporter
//

OtlpGrpcLogRecordExporter::OtlpGrpcLogRecordExporter(
    grpc_core::RefCountedPtr<grpc_core::OtelLogsExportTransport> transport,
    absl::Duration export_timeout)
    : transport_(std::move(transport)), export_timeout_(export_timeout) {}

std::unique_ptr<otel_sdk_logs::Recordable>
OtlpGrpcLogRecordExporter::MakeRecordable() noexcept {
  return std::make_unique<opentelemetry::exporter::otlp::OtlpLogRecordable>();
}

opentelemetry::sdk::common::ExportResult OtlpGrpcLogRecordExporter::Export(
    const opentelemetry::nostd::span<
        std::unique_ptr<otel_sdk_logs::Recordable>>& records) noexcept {
  if (shutdown_.load(std::memory_order_acquire)) {
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }
  if (records.empty())
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest request;
  opentelemetry::exporter::otlp::OtlpRecordableUtils::PopulateRequest(records,
                                                                      &request);
  std::string serialized_request;
  if (!request.SerializeToString(&serialized_request)) {
    LOG_EVERY_N_SEC(ERROR, 10) << "failed to serialize OTLP logs request";
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }
  // Wait for the RPC to finish.  The BatchLogRecordProcessor calls Export()
  // on its own thread, so blocking here does not block RPCs.
  struct ExportState {
    absl::Notification done;
    absl::Status status;
  };
  auto state = std::make_shared<ExportState>();
  transport_->Export(std::move(serialized_request),
                     [state](absl::Status status) {
                       state->status = std::move(status);
                       state->done.Notify();
                     });
  if (!state->done.WaitForNotificationWithTimeout(export_timeout_)) {
    LOG_EVERY_N_SEC(ERROR, 10) << "OTLP logs export timed out";
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }
  if (!state->status.ok()) {
    LOG_EVERY_N_SEC(ERROR, 10) << "OTLP logs export failed: " << state->status;
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }
  return opentelemetry::sdk::common::ExportResult::kSuccess;
}

bool OtlpGrpcLogRecordExporter::ForceFlush(
    std::chrono::microseconds /*timeout*/) noexcept {
  // Export() is synchronous, so there is nothing to flush.
  return true;
}

bool OtlpGrpcLogRecordExporter::Shutdown(
    std::chrono::microseconds /*timeout*/) noexcept {
  shutdown_.store(true, std::memory_order_release);
  return true;
}

//
// MakeBatchProcessorOptions()
//

otel_sdk_logs::BatchLogRecordProcessorOptions MakeBatchProcessorOptions(
    const grpc_core::OtelAccessLogSinkArgs& args) {
  otel_sdk_logs::BatchLogRecordProcessorOptions options;
  options.schedule_delay_millis = std::chrono::milliseconds(
      std::max<int64_t>(1, args.buffer_flush_interval.millis()));
  const size_t batch_size =
      std::max<size_t>(1, args.buffer_size_bytes / kEstimatedRecordSizeBytes);
  options.max_export_batch_size = std::min(batch_size, options.max_queue_size);
  return options;
}

//
// OtelAccessLogSinkFactoryImpl
//

OtelAccessLogSinkFactoryImpl::OtelAccessLogSinkFactoryImpl(
    OtelLogRecordExporterFactory exporter_factory)
    : exporter_factory_(std::move(exporter_factory)) {}

std::unique_ptr<grpc_core::AccessLogSink>
OtelAccessLogSinkFactoryImpl::CreateSink(
    grpc_core::OtelAccessLogSinkArgs args) {
  auto exporter = exporter_factory_(args.transport);
  if (exporter == nullptr) return nullptr;
  return std::make_unique<OtelAccessLogSink>(std::move(exporter), args);
}

//
// Registration
//

void EnableOpenTelemetryXdsAccessLoggingForTesting(
    OtelLogRecordExporterFactory exporter_factory) {
  grpc_core::SetOtelAccessLogSinkFactory(
      std::make_shared<OtelAccessLogSinkFactoryImpl>(
          std::move(exporter_factory)));
}

void DisableOpenTelemetryXdsAccessLoggingForTesting() {
  grpc_core::SetOtelAccessLogSinkFactory(nullptr);
}

}  // namespace internal

namespace experimental {

void EnableOpenTelemetryXdsAccessLogging() {
  static const bool kRegistered = []() {
    grpc_core::SetOtelAccessLogSinkFactory(
        std::make_shared<internal::OtelAccessLogSinkFactoryImpl>(
            [](grpc_core::RefCountedPtr<grpc_core::OtelLogsExportTransport>
                   transport) {
              return std::make_unique<internal::OtlpGrpcLogRecordExporter>(
                  std::move(transport), internal::kDefaultExportTimeout);
            }));
    return true;
  }();
  (void)kRegistered;
}

}  // namespace experimental
}  // namespace grpc
