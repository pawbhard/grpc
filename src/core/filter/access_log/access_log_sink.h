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

#ifndef GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_SINK_H
#define GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_SINK_H

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/time/time.h"

namespace grpc_core {

// A scalar log value.  Matches the scalar kinds of the OpenTelemetry
// AnyValue type.
using AccessLogValue = std::variant<std::string, int64_t, bool, double>;

// One access log entry.  The access log filter makes exactly one record
// per RPC for each configured access logger.
struct AccessLogRecord {
  // Call start time.
  absl::Time timestamp;
  std::optional<AccessLogValue> body;
  std::vector<std::pair<std::string, AccessLogValue>> attributes;
};

// Receives access log records.  Implementations live outside of gRPC
// core (for example, in the OpenTelemetry plugin).
//
// Log() is called on the RPC path and must not block.
//
// The sink is destroyed on an EventEngine thread, so the destructor may
// block (for example, to flush buffered records).
class AccessLogSink {
 public:
  virtual ~AccessLogSink() = default;

  virtual void Log(AccessLogRecord record) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_SINK_H
