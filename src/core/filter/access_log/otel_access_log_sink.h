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

#ifndef GRPC_SRC_CORE_FILTER_ACCESS_LOG_OTEL_ACCESS_LOG_SINK_H
#define GRPC_SRC_CORE_FILTER_ACCESS_LOG_OTEL_ACCESS_LOG_SINK_H

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/filter/access_log/access_log_sink.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace grpc_core {

// Sends OTLP logs export requests to a collector.  gRPC core implements
// this on top of the channel that the xDS GrpcService config describes,
// so that the xDS channel and call credentials apply.
class OtelLogsExportTransport : public RefCounted<OtelLogsExportTransport> {
 public:
  // Sends one serialized
  // opentelemetry.proto.collector.logs.v1.ExportLogsServiceRequest as a
  // unary RPC to LogsService/Export.  Calls on_done exactly once, on a
  // gRPC thread.  Can be called from any thread.
  virtual void Export(std::string serialized_request,
                      absl::AnyInvocable<void(absl::Status)> on_done) = 0;
};

// Parameters to create one OpenTelemetry access log sink.
struct OtelAccessLogSinkArgs {
  RefCountedPtr<OtelLogsExportTransport> transport;
  // OpenTelemetry instrumentation scope name for the logger.
  std::string log_name;
  // Resource attributes, including the built-in labels if enabled.
  std::vector<std::pair<std::string, AccessLogValue>> resource_attributes;
  // Maximum time that a record waits in the buffer before export.
  Duration buffer_flush_interval = Duration::Seconds(1);
  // Soft limit for the buffer size in bytes.
  uint32_t buffer_size_bytes = 16384;
};

// Creates OpenTelemetry access log sinks.  The OpenTelemetry plugin
// registers an implementation.  When no factory is registered, gRPC
// ignores OpenTelemetry access log configs.
class OtelAccessLogSinkFactory {
 public:
  virtual ~OtelAccessLogSinkFactory() = default;

  virtual std::unique_ptr<AccessLogSink> CreateSink(
      OtelAccessLogSinkArgs args) = 0;
};

// Sets the global factory.  Pass nullptr to clear it.
void SetOtelAccessLogSinkFactory(
    std::shared_ptr<OtelAccessLogSinkFactory> factory);

// Returns the global factory, or nullptr if none is set.
std::shared_ptr<OtelAccessLogSinkFactory> GetOtelAccessLogSinkFactory();

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_ACCESS_LOG_OTEL_ACCESS_LOG_SINK_H
