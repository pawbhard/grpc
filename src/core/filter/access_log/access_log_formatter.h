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

#ifndef GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_FORMATTER_H
#define GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_FORMATTER_H

#include <grpc/status.h>
#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/core/util/time.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace grpc_core {

// Per-RPC data that access log substitution operators can read.
// All header names are lower case.
struct AccessLogCallData {
  using HeaderMap = absl::flat_hash_map<std::string, std::string>;

  // Time at which the call started.  absl::InfinitePast() means unknown.
  absl::Time start_time = absl::InfinitePast();
  // Total call duration.
  std::optional<Duration> duration;
  // Only the headers that some template references are captured.
  HeaderMap request_headers;
  HeaderMap response_headers;
  HeaderMap response_trailers;
  std::optional<grpc_status_code> grpc_status;
  std::optional<uint64_t> bytes_received;
  std::optional<uint64_t> bytes_sent;
  std::optional<std::string> downstream_remote_address;
};

// The set of header names referenced by one or more formatters.
// Lets the access log filter copy only the headers that it needs.
struct AccessLogHeaderNames {
  std::set<std::string> request;
  std::set<std::string> response;
  std::set<std::string> trailer;

  bool empty() const {
    return request.empty() && response.empty() && trailer.empty();
  }
};

// A pre-compiled Envoy-style substitution format string, such as
// "%REQ(:PATH)% %GRPC_STATUS% %DURATION%".
//
// Supported syntax:
// - "%%" is a literal "%".
// - "%OP%", "%OP(arg)%", and "%OP(arg):N%", where N truncates the value
//   to at most N bytes.
//
// Supported operators:
// - START_TIME[(fmt)]: call start time (UTC).  The default format is
//   "%Y-%m-%dT%H:%M:%E3SZ".  "%f" and "%<N>f" give fractional seconds.
// - DURATION: call duration in milliseconds.
// - REQ(h[?alt]): request header value.
// - RESP(h[?alt]): response header value.
// - TRAILER(h[?alt]): response trailer value.
// - GRPC_STATUS[(CAMEL_STRING|SNAKE_STRING|NUMBER)]: final gRPC status.
// - GRPC_STATUS_NUMBER: final gRPC status as a number.
// - BYTES_RECEIVED: received message bytes.
// - BYTES_SENT: sent message bytes.
// - DOWNSTREAM_REMOTE_ADDRESS: the peer address (server side only).
// - PROTOCOL: always "HTTP/2".
//
// A value that is not available is written as "-".
class AccessLogFormatter final {
 public:
  // Returns an error if the format string is invalid or uses an
  // unsupported operator.
  static absl::StatusOr<AccessLogFormatter> Parse(absl::string_view format);

  // Evaluates the template against the call data.
  std::string Format(const AccessLogCallData& data) const;

  // Adds all header names that this template reads to *names.
  void AddReferencedHeaders(AccessLogHeaderNames* names) const;

  // Returns the original format string.
  const std::string& format() const { return format_; }

  bool operator==(const AccessLogFormatter& other) const {
    return format_ == other.format_;
  }

 private:
  enum class Operator {
    kLiteral,
    kStartTime,
    kDuration,
    kRequestHeader,
    kResponseHeader,
    kResponseTrailer,
    kGrpcStatus,
    kBytesReceived,
    kBytesSent,
    kDownstreamRemoteAddress,
    kProtocol,
  };

  enum class StatusFormat { kCamelString, kSnakeString, kNumber };

  struct Segment {
    Operator op = Operator::kLiteral;
    // Literal text, primary header name, or time format.
    std::string text;
    // Alternative header name.  Empty if not set.
    std::string alternative;
    StatusFormat status_format = StatusFormat::kCamelString;
    std::optional<size_t> max_length;
  };

  explicit AccessLogFormatter(std::string format)
      : format_(std::move(format)) {}

  static absl::StatusOr<Segment> ParseOperator(
      absl::string_view name, std::optional<std::string> arg,
      std::optional<size_t> max_length);

  static std::optional<std::string> Evaluate(const Segment& segment,
                                             const AccessLogCallData& data);

  std::string format_;
  std::vector<Segment> segments_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_FORMATTER_H
