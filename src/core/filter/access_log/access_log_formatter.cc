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

#include "src/core/filter/access_log/access_log_formatter.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kDefaultStartTimeFormat = "%Y-%m-%dT%H:%M:%E3SZ";
constexpr absl::string_view kUnavailableValue = "-";

bool IsOperatorNameChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Converts the Envoy fractional-second specifiers ("%f" and "%<N>f") to
// the absl::FormatTime() equivalents ("%E9f" and "%E<N>f").
std::string TranslateTimeFormat(absl::string_view format) {
  std::string result;
  result.reserve(format.size());
  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%' || i + 1 >= format.size()) {
      result.push_back(format[i]);
      continue;
    }
    const char next = format[i + 1];
    if (next == 'f') {
      absl::StrAppend(&result, "%E9f");
      ++i;
    } else if (next >= '1' && next <= '9' && i + 2 < format.size() &&
               format[i + 2] == 'f') {
      absl::StrAppend(&result, "%E", absl::string_view(&next, 1), "f");
      i += 2;
    } else {
      // Copy "%x" as-is, including "%%".
      result.push_back('%');
      result.push_back(next);
      ++i;
    }
  }
  return result;
}

// Same names as Envoy's Grpc::Utility::grpcStatusToString().
absl::string_view GrpcStatusCamelString(grpc_status_code status) {
  switch (status) {
    case GRPC_STATUS_OK:
      return "OK";
    case GRPC_STATUS_CANCELLED:
      return "Canceled";
    case GRPC_STATUS_UNKNOWN:
      return "Unknown";
    case GRPC_STATUS_INVALID_ARGUMENT:
      return "InvalidArgument";
    case GRPC_STATUS_DEADLINE_EXCEEDED:
      return "DeadlineExceeded";
    case GRPC_STATUS_NOT_FOUND:
      return "NotFound";
    case GRPC_STATUS_ALREADY_EXISTS:
      return "AlreadyExists";
    case GRPC_STATUS_PERMISSION_DENIED:
      return "PermissionDenied";
    case GRPC_STATUS_RESOURCE_EXHAUSTED:
      return "ResourceExhausted";
    case GRPC_STATUS_FAILED_PRECONDITION:
      return "FailedPrecondition";
    case GRPC_STATUS_ABORTED:
      return "Aborted";
    case GRPC_STATUS_OUT_OF_RANGE:
      return "OutOfRange";
    case GRPC_STATUS_UNIMPLEMENTED:
      return "Unimplemented";
    case GRPC_STATUS_INTERNAL:
      return "Internal";
    case GRPC_STATUS_UNAVAILABLE:
      return "Unavailable";
    case GRPC_STATUS_DATA_LOSS:
      return "DataLoss";
    case GRPC_STATUS_UNAUTHENTICATED:
      return "Unauthenticated";
    default:
      return "";
  }
}

std::optional<std::string> LookupHeader(
    const AccessLogCallData::HeaderMap& headers, const std::string& name,
    const std::string& alternative) {
  auto it = headers.find(name);
  if (it != headers.end()) return it->second;
  if (!alternative.empty()) {
    it = headers.find(alternative);
    if (it != headers.end()) return it->second;
  }
  return std::nullopt;
}

}  // namespace

absl::StatusOr<AccessLogFormatter> AccessLogFormatter::Parse(
    absl::string_view format) {
  AccessLogFormatter formatter{std::string(format)};
  std::string literal;
  auto flush_literal = [&]() {
    if (literal.empty()) return;
    Segment segment;
    segment.op = Operator::kLiteral;
    segment.text = std::move(literal);
    formatter.segments_.push_back(std::move(segment));
    literal.clear();
  };
  size_t pos = 0;
  while (pos < format.size()) {
    const char c = format[pos];
    if (c != '%') {
      literal.push_back(c);
      ++pos;
      continue;
    }
    // "%%" is an escaped "%".
    if (pos + 1 < format.size() && format[pos + 1] == '%') {
      literal.push_back('%');
      pos += 2;
      continue;
    }
    const size_t op_start = pos;
    ++pos;
    // Operator name.
    const size_t name_start = pos;
    while (pos < format.size() && IsOperatorNameChar(format[pos])) ++pos;
    absl::string_view name = format.substr(name_start, pos - name_start);
    if (name.empty()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "invalid access log format: expected operator name at position ",
          op_start));
    }
    // Optional "(arg)".
    std::optional<std::string> arg;
    if (pos < format.size() && format[pos] == '(') {
      const size_t close = format.find(')', pos + 1);
      if (close == absl::string_view::npos) {
        return absl::InvalidArgumentError(absl::StrCat(
            "invalid access log format: missing ')' for operator ", name));
      }
      arg = std::string(format.substr(pos + 1, close - pos - 1));
      pos = close + 1;
    }
    // Optional ":N".
    std::optional<size_t> max_length;
    if (pos < format.size() && format[pos] == ':') {
      ++pos;
      const size_t digits_start = pos;
      while (pos < format.size() && absl::ascii_isdigit(format[pos])) ++pos;
      size_t value;
      if (pos == digits_start ||
          !absl::SimpleAtoi(format.substr(digits_start, pos - digits_start),
                            &value)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "invalid access log format: invalid length for operator ", name));
      }
      max_length = value;
    }
    // Closing "%".
    if (pos >= format.size() || format[pos] != '%') {
      return absl::InvalidArgumentError(absl::StrCat(
          "invalid access log format: missing closing '%' for operator ",
          name));
    }
    ++pos;
    auto segment = ParseOperator(name, std::move(arg), max_length);
    if (!segment.ok()) return segment.status();
    flush_literal();
    formatter.segments_.push_back(std::move(*segment));
  }
  flush_literal();
  return formatter;
}

absl::StatusOr<AccessLogFormatter::Segment> AccessLogFormatter::ParseOperator(
    absl::string_view name, std::optional<std::string> arg,
    std::optional<size_t> max_length) {
  Segment segment;
  segment.max_length = max_length;
  auto no_arg = [&]() -> absl::Status {
    if (arg.has_value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("access log operator ", name, " takes no argument"));
    }
    return absl::OkStatus();
  };
  if (name == "START_TIME") {
    segment.op = Operator::kStartTime;
    segment.text = (arg.has_value() && !arg->empty())
                       ? TranslateTimeFormat(*arg)
                       : std::string(kDefaultStartTimeFormat);
  } else if (name == "REQ" || name == "RESP" || name == "TRAILER") {
    segment.op = name == "REQ"    ? Operator::kRequestHeader
                 : name == "RESP" ? Operator::kResponseHeader
                                  : Operator::kResponseTrailer;
    if (!arg.has_value() || arg->empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("access log operator ", name, " requires a header"));
    }
    std::vector<std::string> parts = absl::StrSplit(*arg, '?');
    if (parts.size() > 2 || parts[0].empty() ||
        (parts.size() == 2 && parts[1].empty())) {
      return absl::InvalidArgumentError(absl::StrCat(
          "access log operator ", name, " has invalid header: ", *arg));
    }
    segment.text = absl::AsciiStrToLower(parts[0]);
    if (parts.size() == 2)
      segment.alternative = absl::AsciiStrToLower(parts[1]);
  } else if (name == "GRPC_STATUS") {
    segment.op = Operator::kGrpcStatus;
    if (!arg.has_value() || arg->empty() || *arg == "CAMEL_STRING") {
      segment.status_format = StatusFormat::kCamelString;
    } else if (*arg == "SNAKE_STRING") {
      segment.status_format = StatusFormat::kSnakeString;
    } else if (*arg == "NUMBER") {
      segment.status_format = StatusFormat::kNumber;
    } else {
      return absl::InvalidArgumentError(absl::StrCat(
          "access log operator GRPC_STATUS supports only CAMEL_STRING, "
          "SNAKE_STRING or NUMBER; got: ",
          *arg));
    }
  } else {
    if (name == "DURATION") {
      segment.op = Operator::kDuration;
    } else if (name == "GRPC_STATUS_NUMBER") {
      segment.op = Operator::kGrpcStatus;
      segment.status_format = StatusFormat::kNumber;
    } else if (name == "BYTES_RECEIVED") {
      segment.op = Operator::kBytesReceived;
    } else if (name == "BYTES_SENT") {
      segment.op = Operator::kBytesSent;
    } else if (name == "DOWNSTREAM_REMOTE_ADDRESS") {
      segment.op = Operator::kDownstreamRemoteAddress;
    } else if (name == "PROTOCOL") {
      segment.op = Operator::kProtocol;
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("unsupported access log operator: ", name));
    }
    absl::Status status = no_arg();
    if (!status.ok()) return status;
  }
  return segment;
}

std::optional<std::string> AccessLogFormatter::Evaluate(
    const Segment& segment, const AccessLogCallData& data) {
  switch (segment.op) {
    case Operator::kLiteral:
      return segment.text;
    case Operator::kStartTime:
      if (data.start_time == absl::InfinitePast()) return std::nullopt;
      return absl::FormatTime(segment.text, data.start_time,
                              absl::UTCTimeZone());
    case Operator::kDuration:
      if (!data.duration.has_value()) return std::nullopt;
      return absl::StrCat(data.duration->millis());
    case Operator::kRequestHeader:
      return LookupHeader(data.request_headers, segment.text,
                          segment.alternative);
    case Operator::kResponseHeader:
      return LookupHeader(data.response_headers, segment.text,
                          segment.alternative);
    case Operator::kResponseTrailer:
      return LookupHeader(data.response_trailers, segment.text,
                          segment.alternative);
    case Operator::kGrpcStatus: {
      if (!data.grpc_status.has_value()) return std::nullopt;
      const grpc_status_code status = *data.grpc_status;
      switch (segment.status_format) {
        case StatusFormat::kCamelString: {
          absl::string_view name = GrpcStatusCamelString(status);
          if (name.empty()) return absl::StrCat(static_cast<int>(status));
          return std::string(name);
        }
        case StatusFormat::kSnakeString: {
          std::string name =
              absl::StatusCodeToString(static_cast<absl::StatusCode>(status));
          if (name.empty()) return absl::StrCat(static_cast<int>(status));
          return name;
        }
        case StatusFormat::kNumber:
          return absl::StrCat(static_cast<int>(status));
      }
      return std::nullopt;
    }
    case Operator::kBytesReceived:
      if (!data.bytes_received.has_value()) return std::nullopt;
      return absl::StrCat(*data.bytes_received);
    case Operator::kBytesSent:
      if (!data.bytes_sent.has_value()) return std::nullopt;
      return absl::StrCat(*data.bytes_sent);
    case Operator::kDownstreamRemoteAddress:
      return data.downstream_remote_address;
    case Operator::kProtocol:
      return "HTTP/2";
  }
  return std::nullopt;
}

std::string AccessLogFormatter::Format(const AccessLogCallData& data) const {
  std::string result;
  for (const Segment& segment : segments_) {
    if (segment.op == Operator::kLiteral) {
      absl::StrAppend(&result, segment.text);
      continue;
    }
    std::optional<std::string> value = Evaluate(segment, data);
    if (!value.has_value()) {
      absl::StrAppend(&result, kUnavailableValue);
      continue;
    }
    absl::string_view out = *value;
    if (segment.max_length.has_value() && out.size() > *segment.max_length) {
      out = out.substr(0, *segment.max_length);
    }
    absl::StrAppend(&result, out);
  }
  return result;
}

void AccessLogFormatter::AddReferencedHeaders(
    AccessLogHeaderNames* names) const {
  for (const Segment& segment : segments_) {
    std::set<std::string>* target = nullptr;
    switch (segment.op) {
      case Operator::kRequestHeader:
        target = &names->request;
        break;
      case Operator::kResponseHeader:
        target = &names->response;
        break;
      case Operator::kResponseTrailer:
        target = &names->trailer;
        break;
      default:
        continue;
    }
    target->insert(segment.text);
    if (!segment.alternative.empty()) target->insert(segment.alternative);
  }
}

}  // namespace grpc_core
