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

#include "src/core/filter/access_log/access_log_call_tracer.h"

#include <optional>
#include <utility>

#include "src/core/call/message.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/call_final_info.h"
#include "absl/time/clock.h"

namespace grpc_core {

namespace {

// Returns the start time of the call.  Each call sets itself as the Call
// arena context when it is created.
Timestamp CallStartTime(Arena* arena) {
  Call* call = arena->GetContext<Call>();
  if (call == nullptr) return Timestamp::Now();
  return Timestamp::FromCycleCounterRoundDown(call->start_time());
}

}  // namespace

//
// AccessLogCallState
//

AccessLogCallState::AccessLogCallState(
    RefCountedPtr<const AccessLogFilter::Config> config,
    const grpc_metadata_batch& initial_metadata, Timestamp start)
    : config_(std::move(config)), start_(start) {
  // Convert the monotonic start time to wall clock time.
  data_.start_time =
      absl::Now() - absl::Milliseconds((Timestamp::Now() - start_).millis());
  CaptureAccessLogHeaders(initial_metadata, config_->header_names.request,
                          &data_.request_headers);
  if (!config_->is_client) {
    const Slice* peer = initial_metadata.get_pointer(PeerString());
    if (peer != nullptr) {
      data_.downstream_remote_address =
          AccessLogPeerToAddress(peer->as_string_view());
    }
  }
}

void AccessLogCallState::SetRequestHeaders(const grpc_metadata_batch& md) {
  if (config_->header_names.request.empty()) return;
  MutexLock lock(&mu_);
  data_.request_headers.clear();
  CaptureAccessLogHeaders(md, config_->header_names.request,
                          &data_.request_headers);
}

void AccessLogCallState::SetResponseHeaders(const grpc_metadata_batch& md) {
  if (config_->header_names.response.empty()) return;
  MutexLock lock(&mu_);
  data_.response_headers.clear();
  CaptureAccessLogHeaders(md, config_->header_names.response,
                          &data_.response_headers);
}

void AccessLogCallState::SetTrailers(const grpc_metadata_batch& md) {
  MutexLock lock(&mu_);
  data_.response_trailers.clear();
  CaptureAccessLogHeaders(md, config_->header_names.trailer,
                          &data_.response_trailers);
  // The first status wins.  On the server, the config selector
  // interceptor reports the status of a call that it or the xDS filter
  // chain failed.  Later, ServerCallTracerFilter sees only a CANCELLED
  // status for that call.  On the client, RecordEnd() sets the status.
  std::optional<grpc_status_code> status = md.get(GrpcStatusMetadata());
  if (status.has_value() && !data_.grpc_status.has_value()) {
    data_.grpc_status = *status;
  }
}

void AccessLogCallState::AddSentBytes(uint64_t bytes) {
  MutexLock lock(&mu_);
  sent_bytes_ += bytes;
}

void AccessLogCallState::AddReceivedBytes(uint64_t bytes) {
  MutexLock lock(&mu_);
  received_bytes_ += bytes;
}

void AccessLogCallState::Finish(grpc_status_code status, bool status_wins) {
  MutexLock lock(&mu_);
  if (finished_) return;
  finished_ = true;
  data_.duration = Timestamp::Now() - start_;
  if (status_wins || !data_.grpc_status.has_value()) {
    data_.grpc_status = status;
  }
  data_.bytes_sent = sent_bytes_;
  data_.bytes_received = received_bytes_;
  WriteAccessLogRecords(*config_, data_);
}

//
// AccessLogClientCallTracer
//

// Collects the data of one attempt.  The attempt runs below the xDS HTTP
// filters, so it sees the request headers after those filters ran.
class AccessLogClientCallTracer::AttemptTracer final
    : public ClientCallTracerInterface::CallAttemptTracer {
 public:
  explicit AttemptTracer(AccessLogCallState* state) : state_(state) {}

  void RecordSendInitialMetadata(grpc_metadata_batch* md) override {
    state_->SetRequestHeaders(*md);
  }
  // Used instead of RecordSendInitialMetadata() if the
  // call_tracer_send_initial_metadata_is_an_annotation experiment is on.
  void MutateSendInitialMetadata(grpc_metadata_batch* md) override {
    state_->SetRequestHeaders(*md);
  }
  void RecordSendTrailingMetadata(grpc_metadata_batch* /*md*/) override {}
  void MutateSendTrailingMetadata(grpc_metadata_batch* /*md*/) override {}
  void RecordSendMessage(const Message& message) override {
    state_->AddSentBytes(message.payload()->Length());
  }
  void RecordSendCompressedMessage(const Message& /*message*/) override {}
  void RecordReceivedInitialMetadata(grpc_metadata_batch* md) override {
    state_->SetResponseHeaders(*md);
  }
  void RecordReceivedMessage(const Message& message) override {
    state_->AddReceivedBytes(message.payload()->Length());
  }
  void RecordReceivedDecompressedMessage(const Message& /*message*/) override {}
  void RecordCancel(grpc_error_handle /*cancel_error*/) override {}
  void RecordReceivedTrailingMetadata(
      absl::Status /*status*/, grpc_metadata_batch* md,
      const grpc_transport_stream_stats* /*stats*/) override {
    if (md != nullptr) state_->SetTrailers(*md);
  }
  void RecordEnd() override {}
  void SetOptionalLabel(OptionalLabelKey /*key*/,
                        RefCountedStringValue /*value*/) override {}
  void RecordIncomingBytes(const TransportByteSize& /*size*/) override {}
  void RecordOutgoingBytes(const TransportByteSize& /*size*/) override {}
  std::shared_ptr<TcpCallTracer> StartNewTcpTrace() override { return nullptr; }
  void RecordAnnotation(absl::string_view /*annotation*/) override {}
  void RecordAnnotation(const Annotation& /*annotation*/) override {}
  std::string TraceId() override { return ""; }
  std::string SpanId() override { return ""; }
  bool IsSampled() override { return false; }

 private:
  AccessLogCallState* const state_;
};

void AccessLogClientCallTracer::AddToCall(
    Arena* arena, RefCountedPtr<const AccessLogFilter::Config> config,
    const grpc_metadata_batch& initial_metadata) {
  auto* tracer = arena->ManagedNew<AccessLogClientCallTracer>(
      arena, std::move(config), initial_metadata, CallStartTime(arena));
  AddClientCallTracerToContext(arena, tracer);
}

ClientCallTracerInterface::CallAttemptTracer*
AccessLogClientCallTracer::StartNewAttempt(bool /*is_transparent_retry*/) {
  return arena_->ManagedNew<AttemptTracer>(&state_);
}

void AccessLogClientCallTracer::RecordEnd(grpc_status_code status_code,
                                          absl::string_view /*error_string*/) {
  // The status at call end wins.  It includes statuses that the client
  // made locally, for example a route selection failure or a deadline.
  state_.Finish(status_code, /*status_wins=*/true);
}

//
// AccessLogServerCallTracer
//

void AccessLogServerCallTracer::AddToCall(
    Arena* arena, RefCountedPtr<const AccessLogFilter::Config> config,
    const grpc_metadata_batch& initial_metadata) {
  auto* tracer = arena->ManagedNew<AccessLogServerCallTracer>(
      std::move(config), initial_metadata, CallStartTime(arena));
  // AccessLogRequestHeaderFilter (if the chain has one) replaces the
  // request headers after the xDS HTTP filters ran.
  auto* context = arena->ManagedNew<AccessLogCallContext>();
  context->request_headers = tracer->state_.request_headers();
  arena->SetContext<AccessLogCallContext>(context);
  AddServerCallTracerToContext(arena, tracer);
}

void AccessLogServerCallTracer::RecordSendMessage(const Message& message) {
  state_.AddSentBytes(message.payload()->Length());
}

void AccessLogServerCallTracer::RecordReceivedMessage(const Message& message) {
  state_.AddReceivedBytes(message.payload()->Length());
}

void AccessLogServerCallTracer::RecordEnd(
    const grpc_call_final_info* final_info) {
  // final_info has only the status that the application sent.  A status
  // that a filter or the config selector made is only in the trailers.
  state_.Finish(
      final_info != nullptr ? final_info->final_status : GRPC_STATUS_UNKNOWN,
      /*status_wins=*/false);
}

}  // namespace grpc_core
