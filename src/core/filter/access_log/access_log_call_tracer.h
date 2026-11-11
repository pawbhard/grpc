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

#ifndef GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_CALL_TRACER_H
#define GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_CALL_TRACER_H

#include <grpc/status.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "src/core/call/metadata_batch.h"
#include "src/core/filter/access_log/access_log_filter.h"
#include "src/core/filter/access_log/access_log_formatter.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// The access log call tracers.  An xDS config selector adds one tracer to
// each call in GetCallConfig(), if the HTTP connection manager (HCM) of
// the Listener has an access log.  The tracer collects the call data and
// writes one record for each logger when the call ends.
//
// The tracers work with or without a stats plugin.  If the call already
// has stats tracers, the tracer goes after them in the delegating tracer.
// So the trace and span IDs still come from the stats tracer.
//
// The tracers do not change the call: they do not change metadata,
// status, or messages.

// Per-call data, shared by the client call tracer and its attempt tracers.
class AccessLogCallState final {
 public:
  // start is the call start time.  The tracer is created in
  // GetCallConfig(), after the call started, so it uses the start time of
  // the call, not its own creation time.
  AccessLogCallState(RefCountedPtr<const AccessLogFilter::Config> config,
                     const grpc_metadata_batch& initial_metadata,
                     Timestamp start);

  const AccessLogFilter::Config& config() const { return *config_; }

  // Replaces the request headers.  The client calls it for each attempt,
  // after the xDS HTTP filters ran.
  void SetRequestHeaders(const grpc_metadata_batch& md);
  void SetResponseHeaders(const grpc_metadata_batch& md);
  void SetTrailers(const grpc_metadata_batch& md);
  void AddSentBytes(uint64_t bytes);
  void AddReceivedBytes(uint64_t bytes);
  // Pointer to the request headers.  Lets AccessLogRequestHeaderFilter
  // replace them after the xDS HTTP filters ran.
  AccessLogCallData::HeaderMap* request_headers() {
    return &data_.request_headers;
  }

  // Writes the records.  Only the first call writes records.  If
  // status_wins is false, status is used only if the trailers had no
  // status.
  void Finish(grpc_status_code status, bool status_wins);

 private:
  RefCountedPtr<const AccessLogFilter::Config> config_;
  const Timestamp start_;
  Mutex mu_;
  bool finished_ ABSL_GUARDED_BY(mu_) = false;
  uint64_t sent_bytes_ ABSL_GUARDED_BY(mu_) = 0;
  uint64_t received_bytes_ ABSL_GUARDED_BY(mu_) = 0;
  AccessLogCallData data_;
};

// Client side.  The call tracer writes the records in RecordEnd(), which
// the surface calls once for each call (grpc/grpc#43838).  RecordEnd()
// also runs when route selection fails and no attempt starts.  The
// attempt tracers only collect data.
class AccessLogClientCallTracer final : public ClientCallTracerInterface {
 public:
  // Creates the tracer on the call arena and adds it to the call.
  static void AddToCall(Arena* arena,
                        RefCountedPtr<const AccessLogFilter::Config> config,
                        const grpc_metadata_batch& initial_metadata);

  AccessLogClientCallTracer(Arena* arena,
                            RefCountedPtr<const AccessLogFilter::Config> config,
                            const grpc_metadata_batch& initial_metadata,
                            Timestamp start)
      : arena_(arena), state_(std::move(config), initial_metadata, start) {}

  CallAttemptTracer* StartNewAttempt(bool is_transparent_retry) override;
  void RecordEnd(grpc_status_code status_code,
                 absl::string_view error_string) override;
  void RecordAnnotation(absl::string_view /*annotation*/) override {}
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  class AttemptTracer;

  Arena* const arena_;
  AccessLogCallState state_;
};

// Server side.  ServerCallTracerFilter calls RecordEnd(final_info) when
// the call is finalized.  The request headers come from GetCallConfig(),
// because ServerCallTracerFilter reads the client initial metadata before
// the config selector runs.
class AccessLogServerCallTracer final : public ServerCallTracerInterface {
 public:
  // Creates the tracer on the call arena and adds it to the call.
  static void AddToCall(Arena* arena,
                        RefCountedPtr<const AccessLogFilter::Config> config,
                        const grpc_metadata_batch& initial_metadata);

  AccessLogServerCallTracer(RefCountedPtr<const AccessLogFilter::Config> config,
                            const grpc_metadata_batch& initial_metadata,
                            Timestamp start)
      : state_(std::move(config), initial_metadata, start) {}

  void RecordSendInitialMetadata(grpc_metadata_batch* md) override {
    state_.SetResponseHeaders(*md);
  }
  void MutateSendInitialMetadata(grpc_metadata_batch* md) override {
    state_.SetResponseHeaders(*md);
  }
  void RecordSendTrailingMetadata(grpc_metadata_batch* md) override {
    state_.SetTrailers(*md);
  }
  void MutateSendTrailingMetadata(grpc_metadata_batch* md) override {
    state_.SetTrailers(*md);
  }
  void RecordSendMessage(const Message& message) override;
  void RecordSendCompressedMessage(const Message& /*message*/) override {}
  void RecordReceivedInitialMetadata(grpc_metadata_batch* /*md*/) override {}
  void RecordReceivedMessage(const Message& message) override;
  void RecordReceivedDecompressedMessage(const Message& /*message*/) override {}
  void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
  void RecordReceivedTrailingMetadata(grpc_metadata_batch* /*md*/) override {}
  void RecordEnd(const grpc_call_final_info* final_info) override;
  void RecordIncomingBytes(const TransportByteSize& /*size*/) override {}
  void RecordOutgoingBytes(const TransportByteSize& /*size*/) override {}
  std::shared_ptr<TcpCallTracer> StartNewTcpTrace() override { return nullptr; }
  void RecordAnnotation(absl::string_view /*annotation*/) override {}
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  AccessLogCallState state_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_ACCESS_LOG_ACCESS_LOG_CALL_TRACER_H
