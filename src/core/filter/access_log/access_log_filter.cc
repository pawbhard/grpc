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

#include "src/core/filter/access_log/access_log_filter.h"

#include <grpc/event_engine/event_engine.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/match.h"
#include "src/core/util/uri.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"

namespace grpc_core {

//
// AccessLogValueTemplate
//

AccessLogValue EvaluateAccessLogValueTemplate(
    const AccessLogValueTemplate& value_template,
    const AccessLogCallData& data) {
  return Match(
      value_template,
      [&](const AccessLogFormatter& formatter) -> AccessLogValue {
        return formatter.Format(data);
      },
      [](int64_t value) -> AccessLogValue { return value; },
      [](bool value) -> AccessLogValue { return value; },
      [](double value) -> AccessLogValue { return value; });
}

std::string AccessLogValueTemplateToString(
    const AccessLogValueTemplate& value_template) {
  return Match(
      value_template,
      [](const AccessLogFormatter& formatter) {
        return absl::StrCat("string=\"", formatter.format(), "\"");
      },
      [](int64_t value) { return absl::StrCat("int=", value); },
      [](bool value) {
        return absl::StrCat("bool=", value ? "true" : "false");
      },
      [](double value) { return absl::StrCat("double=", value); });
}

//
// AccessLogFilter::SinkHolder
//

UniqueTypeName AccessLogFilter::SinkHolder::Type() {
  return GRPC_UNIQUE_TYPE_NAME_HERE("AccessLogSinkHolder");
}

AccessLogFilter::SinkHolder::~SinkHolder() {
  if (sink_ == nullptr) return;
  grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
      [sink = std::move(sink_)]() mutable {
        ExecCtx exec_ctx;
        sink.reset();
      });
}

//
// AccessLogFilter::Config
//

bool AccessLogFilter::Config::Equals(const FilterConfig& other) const {
  const auto& o = DownCast<const Config&>(other);
  return is_client == o.is_client && loggers == o.loggers;
}

std::string AccessLogFilter::Config::ToString() const {
  std::vector<std::string> logger_strings;
  logger_strings.reserve(loggers.size());
  for (const Logger& logger : loggers) {
    std::vector<std::string> parts;
    if (logger.body.has_value()) {
      parts.push_back(absl::StrCat(
          "body={", AccessLogValueTemplateToString(*logger.body), "}"));
    }
    if (!logger.attributes.empty()) {
      std::vector<std::string> attrs;
      for (const auto& [key, value] : logger.attributes) {
        attrs.push_back(absl::StrCat(
            key, "={", AccessLogValueTemplateToString(value), "}"));
      }
      parts.push_back(
          absl::StrCat("attributes={", absl::StrJoin(attrs, ", "), "}"));
    }
    logger_strings.push_back(
        absl::StrCat("{", absl::StrJoin(parts, ", "), "}"));
  }
  return absl::StrCat("{is_client=", is_client ? "true" : "false",
                      ", loggers=[", absl::StrJoin(logger_strings, ", "), "]}");
}

void AccessLogFilter::Config::ComputeHeaderNames() {
  header_names = AccessLogHeaderNames();
  auto add = [&](const AccessLogValueTemplate& value_template) {
    if (const auto* formatter =
            std::get_if<AccessLogFormatter>(&value_template)) {
      formatter->AddReferencedHeaders(&header_names);
    }
  };
  for (const Logger& logger : loggers) {
    if (logger.body.has_value()) add(*logger.body);
    for (const auto& [key, value] : logger.attributes) add(value);
  }
}

//
// AccessLogFilter
//

const grpc_channel_filter AccessLogFilter::kFilterVtable =
    MakePromiseBasedFilter<AccessLogFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>();

absl::StatusOr<std::unique_ptr<AccessLogFilter>> AccessLogFilter::Create(
    const ChannelArgs& /*args*/, ChannelFilter::Args filter_args) {
  if (filter_args.config() == nullptr) {
    return absl::InternalError("access_log: filter config not set");
  }
  if (filter_args.config()->type() != Config::Type()) {
    return absl::InternalError(
        absl::StrCat("wrong config type passed to access log filter: ",
                     filter_args.config()->type().name()));
  }
  return std::make_unique<AccessLogFilter>(
      filter_args.config().TakeAsSubclass<const Config>());
}

namespace {

void CaptureHeaders(const grpc_metadata_batch& md,
                    const std::set<std::string>& names,
                    AccessLogCallData::HeaderMap* out) {
  for (const std::string& name : names) {
    std::string buffer;
    std::optional<absl::string_view> value = md.GetStringValue(name, &buffer);
    if (value.has_value()) (*out)[name] = std::string(*value);
  }
}

// Converts a peer URI ("ipv4:1.2.3.4:5") to "ip:port".
std::string PeerStringToAddress(absl::string_view peer) {
  absl::StatusOr<URI> uri = URI::Parse(peer);
  if (uri.ok() && (uri->scheme() == "ipv4" || uri->scheme() == "ipv6")) {
    return uri->path();
  }
  return std::string(peer);
}

}  // namespace

void AccessLogFilter::Call::OnClientInitialMetadata(ClientMetadata& md,
                                                    AccessLogFilter* filter) {
  start_ = Timestamp::Now();
  data_.start_time = absl::Now();
  const Config& config = filter->config();
  CaptureHeaders(md, config.header_names.request, &data_.request_headers);
  if (!config.is_client) {
    const Slice* peer = md.get_pointer(PeerString());
    if (peer != nullptr) {
      data_.downstream_remote_address =
          PeerStringToAddress(peer->as_string_view());
    }
  }
}

void AccessLogFilter::Call::OnServerInitialMetadata(ServerMetadata& md,
                                                    AccessLogFilter* filter) {
  CaptureHeaders(md, filter->config().header_names.response,
                 &data_.response_headers);
}

void AccessLogFilter::Call::OnServerTrailingMetadata(ServerMetadata& md,
                                                     AccessLogFilter* filter) {
  CaptureHeaders(md, filter->config().header_names.trailer,
                 &data_.response_trailers);
  std::optional<grpc_status_code> status = md.get(GrpcStatusMetadata());
  if (status.has_value()) data_.grpc_status = *status;
  // The v3 call stack does not run OnFinalize(), so log here.
  MaybeLog(filter->config(), /*final_info=*/nullptr);
}

void AccessLogFilter::Call::OnClientToServerMessage(const Message& message) {
  client_to_server_bytes_ += message.payload()->Length();
}

void AccessLogFilter::Call::OnServerToClientMessage(const Message& message) {
  server_to_client_bytes_ += message.payload()->Length();
}

void AccessLogFilter::Call::OnFinalize(const grpc_call_final_info* final_info,
                                       AccessLogFilter* filter) {
  // Covers calls that end without server trailing metadata passing
  // through this filter.
  MaybeLog(filter->config(), final_info);
}

void AccessLogFilter::Call::MaybeLog(const Config& config,
                                     const grpc_call_final_info* final_info) {
  if (logged_) return;
  logged_ = true;
  if (config.loggers.empty()) return;
  if (start_ != Timestamp::InfPast()) {
    data_.duration = Timestamp::Now() - start_;
  }
  if (!data_.grpc_status.has_value() && final_info != nullptr) {
    data_.grpc_status = final_info->final_status;
  }
  if (config.is_client) {
    data_.bytes_sent = client_to_server_bytes_;
    data_.bytes_received = server_to_client_bytes_;
  } else {
    data_.bytes_received = client_to_server_bytes_;
    data_.bytes_sent = server_to_client_bytes_;
  }
  const absl::Time timestamp =
      data_.start_time == absl::InfinitePast() ? absl::Now() : data_.start_time;
  for (const Logger& logger : config.loggers) {
    if (logger.sink == nullptr || logger.sink->sink() == nullptr) continue;
    AccessLogRecord record;
    record.timestamp = timestamp;
    if (logger.body.has_value()) {
      record.body = EvaluateAccessLogValueTemplate(*logger.body, data_);
    }
    record.attributes.reserve(logger.attributes.size());
    for (const auto& [key, value] : logger.attributes) {
      record.attributes.emplace_back(
          key, EvaluateAccessLogValueTemplate(value, data_));
    }
    logger.sink->sink()->Log(std::move(record));
  }
}

}  // namespace grpc_core
