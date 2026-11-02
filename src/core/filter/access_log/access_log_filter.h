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

#ifndef GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_FILTER_H
#define GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_FILTER_H

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/filter/access_log/access_log_formatter.h"
#include "src/core/filter/access_log/access_log_sink.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/blackboard.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// A configured value: a string template, or a static scalar.
using AccessLogValueTemplate =
    std::variant<AccessLogFormatter, int64_t, bool, double>;

// Returns the value of the template for one call.
AccessLogValue EvaluateAccessLogValueTemplate(
    const AccessLogValueTemplate& value_template,
    const AccessLogCallData& data);

std::string AccessLogValueTemplateToString(
    const AccessLogValueTemplate& value_template);

// Writes access log records.  Writes exactly one record per RPC for each
// configured logger.  The records are written when server trailing
// metadata passes through the filter, or when the call is finalized,
// whichever comes first.  (The v3 call stack does not run OnFinalize().)
//
// The filter is placed first in each xDS filter chain, so that it also
// sees the status that later filters (such as RBAC) produce.
class AccessLogFilter final : public ImplementChannelFilter<AccessLogFilter> {
 public:
  // Shares one sink between filter chains and across config updates.
  // Stored in the Blackboard.
  class SinkHolder final : public Blackboard::Entry {
   public:
    explicit SinkHolder(std::unique_ptr<AccessLogSink> sink)
        : sink_(std::move(sink)) {}

    // Destroys the sink on an EventEngine thread, because the sink may
    // block while it flushes buffered records.
    ~SinkHolder() override;

    static UniqueTypeName Type();

    AccessLogSink* sink() const { return sink_.get(); }

   private:
    std::unique_ptr<AccessLogSink> sink_;
  };

  // One access logger.
  struct Logger {
    std::optional<AccessLogValueTemplate> body;
    std::vector<std::pair<std::string, AccessLogValueTemplate>> attributes;
    RefCountedPtr<SinkHolder> sink;

    bool operator==(const Logger& other) const {
      return body == other.body && attributes == other.attributes &&
             sink == other.sink;
    }
  };

  struct Config final : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("access_log_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override;
    std::string ToString() const override;

    // Adds header names referenced by all loggers to header_names.
    // Must be called after loggers is populated.
    void ComputeHeaderNames();

    // True on clients, false on servers.  Selects which direction counts
    // as "received" and "sent".
    bool is_client = true;
    std::vector<Logger> loggers;
    AccessLogHeaderNames header_names;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "access_log"; }

  static absl::StatusOr<std::unique_ptr<AccessLogFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  explicit AccessLogFilter(RefCountedPtr<const Config> config)
      : config_(std::move(config)) {}

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md, AccessLogFilter* filter);
    void OnServerInitialMetadata(ServerMetadata& md, AccessLogFilter* filter);
    void OnServerTrailingMetadata(ServerMetadata& md, AccessLogFilter* filter);
    void OnClientToServerMessage(const Message& message);
    void OnServerToClientMessage(const Message& message);
    static inline const NoInterceptor OnClientToServerHalfClose;
    void OnFinalize(const grpc_call_final_info* final_info,
                    AccessLogFilter* filter);

    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }

   private:
    // Writes the records, unless they were already written.
    void MaybeLog(const Config& config, const grpc_call_final_info* final_info);

    bool logged_ = false;
    Timestamp start_ = Timestamp::InfPast();
    uint64_t client_to_server_bytes_ = 0;
    uint64_t server_to_client_bytes_ = 0;
    AccessLogCallData data_;
  };

  const Config& config() const { return *config_; }

 private:
  RefCountedPtr<const Config> config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_FILTER_H
