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

#ifndef GRPC_SRC_CPP_EXT_OTEL_OTEL_ACCESS_LOG_H
#define GRPC_SRC_CPP_EXT_OTEL_OTEL_ACCESS_LOG_H

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

#include "opentelemetry/sdk/logs/batch_log_record_processor_options.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "src/core/filter/access_log/access_log_sink.h"
#include "src/core/filter/access_log/otel_access_log_sink.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/time/time.h"

namespace grpc {
namespace internal {

// Creates the LogRecordExporter for one sink.
using OtelLogRecordExporterFactory =
    std::function<std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter>(
        grpc_core::RefCountedPtr<grpc_core::OtelLogsExportTransport>)>;

// Exports OTLP logs over an OtelLogsExportTransport.  The transport uses the
// channel that the xDS GrpcService config describes, so xDS channel
// credentials and call credentials apply.
//
// Records are serialized with the OTLP serializer of opentelemetry-cpp
// (OtlpLogRecordable and OtlpRecordableUtils).
class OtlpGrpcLogRecordExporter final
    : public opentelemetry::sdk::logs::LogRecordExporter {
 public:
  OtlpGrpcLogRecordExporter(
      grpc_core::RefCountedPtr<grpc_core::OtelLogsExportTransport> transport,
      absl::Duration export_timeout);

  std::unique_ptr<opentelemetry::sdk::logs::Recordable>
  MakeRecordable() noexcept override;

  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<
          std::unique_ptr<opentelemetry::sdk::logs::Recordable>>&
          records) noexcept override;

  bool ForceFlush(std::chrono::microseconds timeout) noexcept override;

  bool Shutdown(std::chrono::microseconds timeout) noexcept override;

 private:
  grpc_core::RefCountedPtr<grpc_core::OtelLogsExportTransport> transport_;
  const absl::Duration export_timeout_;
  std::atomic<bool> shutdown_{false};
};

// Maps the xDS buffer settings to BatchLogRecordProcessor options.
// The SDK limits are record counts, not bytes, so buffer_size_bytes is
// divided by an estimated record size.
opentelemetry::sdk::logs::BatchLogRecordProcessorOptions
MakeBatchProcessorOptions(const grpc_core::OtelAccessLogSinkArgs& args);

// Creates sinks that own one OpenTelemetry LoggerProvider each.
class OtelAccessLogSinkFactoryImpl final
    : public grpc_core::OtelAccessLogSinkFactory {
 public:
  explicit OtelAccessLogSinkFactoryImpl(
      OtelLogRecordExporterFactory exporter_factory);

  std::unique_ptr<grpc_core::AccessLogSink> CreateSink(
      grpc_core::OtelAccessLogSinkArgs args) override;

 private:
  OtelLogRecordExporterFactory exporter_factory_;
};

// Registers OtelAccessLogSinkFactoryImpl globally with the given exporter
// factory.  Replaces any earlier registration.  For tests.
void EnableOpenTelemetryXdsAccessLoggingForTesting(
    OtelLogRecordExporterFactory exporter_factory);

// Removes the global registration.  For tests.
void DisableOpenTelemetryXdsAccessLoggingForTesting();

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_ACCESS_LOG_H
