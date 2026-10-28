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

#include "src/core/load_balancing/autosharding/autosharding_client.h"

#include <grpc/event_engine/event_engine.h>

#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/upb_utils.h"
#include "upb/mem/arena.hpp"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

constexpr char kWatchShardingAssignmentMethod[] =
    "/google.cloud.autosharding.v1.AutoshardingService/"
    "WatchShardingAssignment";

// Maximum length of AssignmentAck.error_message, in Unicode code points.
// Our messages are ASCII, so bytes and code points are the same.
constexpr size_t kMaxAckErrorMessageLength = 512;

std::string TruncateAckErrorMessage(std::string message) {
  if (message.size() > kMaxAckErrorMessageLength) {
    message.resize(kMaxAckErrorMessageLength);
  }
  return message;
}

BackOff::Options MakeBackoffOptions() {
  BackOff::Options options;
  options.set_initial_backoff(Duration::Seconds(1));
  options.set_multiplier(1.6);
  options.set_jitter(0.2);
  options.set_max_backoff(Duration::Minutes(2));
  return options;
}

}  // namespace

//
// AutoshardingClient::StreamEventHandler
//

class AutoshardingClient::StreamEventHandler final
    : public XdsTransport::StreamingCall::EventHandler {
 public:
  StreamEventHandler(RefCountedPtr<AutoshardingClient> client,
                     uint64_t stream_id)
      : client_(std::move(client)),
        work_serializer_(client_->work_serializer_),
        stream_id_(stream_id) {}

  void OnRequestSent(bool ok) override {
    work_serializer_->Run([client = client_, stream_id = stream_id_,
                           ok]() { client->OnRequestSent(stream_id, ok); },
                          DEBUG_LOCATION);
  }

  void OnRecvMessage(absl::string_view payload) override {
    std::string message(payload);
    work_serializer_->Run(
        [client = client_, stream_id = stream_id_,
         message = std::move(message)]() {
          client->OnResponse(stream_id, message);
        },
        DEBUG_LOCATION);
  }

  void OnStatusReceived(absl::Status status) override {
    work_serializer_->Run(
        [client = client_, stream_id = stream_id_,
         status = std::move(status)]() mutable {
          client->OnStatusReceived(stream_id, std::move(status));
        },
        DEBUG_LOCATION);
  }

 private:
  RefCountedPtr<AutoshardingClient> client_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  const uint64_t stream_id_;
};

//
// AutoshardingClient
//

AutoshardingClient::AutoshardingClient(Options options)
    : work_serializer_(std::move(options.work_serializer)),
      event_engine_(options.event_engine),
      transport_(std::move(options.transport)),
      target_(std::move(options.target)),
      uuid_(std::move(options.uuid)),
      initial_assignment_timeout_(options.initial_assignment_timeout),
      on_assignment_(std::move(options.on_assignment)),
      on_error_(std::move(options.on_error)),
      backoff_(MakeBackoffOptions()) {
  GRPC_CHECK(work_serializer_ != nullptr);
  GRPC_CHECK(event_engine_ != nullptr);
  GRPC_CHECK(transport_ != nullptr);
}

void AutoshardingClient::Start() {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding client " << this << "] starting for target \""
      << target_ << "\"";
  StartInitialAssignmentTimer();
  StartStreamLocked();
}

void AutoshardingClient::Shutdown() {
  if (shutdown_) return;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding client " << this << "] shutting down";
  shutdown_ = true;
  CancelTimer(&retry_timer_);
  CancelTimer(&initial_assignment_timer_);
  stream_.reset();
  pending_sends_.clear();
  send_in_flight_ = false;
  on_assignment_ = nullptr;
  on_error_ = nullptr;
  transport_.reset();
}

void AutoshardingClient::ResetBackoff() {
  backoff_.Reset();
  if (transport_ != nullptr) transport_->ResetBackoff();
}

void AutoshardingClient::StartInitialAssignmentTimer() {
  CancelTimer(&initial_assignment_timer_);
  initial_assignment_timer_ =
      event_engine_->RunAfter(initial_assignment_timeout_, [self = Ref()]() {
        self->work_serializer_->Run(
            [self = std::move(self)]() {
              self->initial_assignment_timer_.reset();
              if (self->shutdown_ || self->received_valid_assignment_) return;
              self->ReportError(absl::DeadlineExceededError(
                  "autosharding: initial_assignment_timeout fired before a "
                  "valid assignment was received"));
            },
            DEBUG_LOCATION);
      });
}

void AutoshardingClient::StartStreamLocked() {
  if (shutdown_) return;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding client " << this << "] starting stream";
  stream_.reset();
  // Any chunks received on a previous stream are part of an incomplete
  // assignment and must not be used for the new stream.
  builder_.ResetChunks();
  pending_sends_.clear();
  send_in_flight_ = false;
  received_valid_assignment_on_stream_ = false;
  ++stream_id_;
  stream_ = transport_->CreateStreamingCall(
      kWatchShardingAssignmentMethod,
      std::make_unique<StreamEventHandler>(Ref(), stream_id_),
      XdsTransport::CallOptions().set_wait_for_ready(true));
  SendInitialConfig();
  stream_->StartRecvMessage();
}

void AutoshardingClient::SendInitialConfig() {
  upb::Arena arena;
  auto* request =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_new(
          arena.ptr());
  auto* config =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_mutable_initial_client_config(
          request, arena.ptr());
  google_cloud_autosharding_v1_InitialClientConfig_set_target(
      config, StdStringToUpbString(target_));
  google_cloud_autosharding_v1_InitialClientConfig_set_client_uuid(
      config, StdStringToUpbString(uuid_));
  google_cloud_autosharding_v1_InitialClientConfig_set_latest_generation(
      config, builder_.last_accepted_generation());
  size_t length;
  char* serialized =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_serialize(
          request, arena.ptr(), &length);
  SendMessage(std::string(serialized, length));
}

void AutoshardingClient::SendAssignmentAck(int64_t generation, bool accepted,
                                           std::string error_message) {
  error_message = TruncateAckErrorMessage(std::move(error_message));
  upb::Arena arena;
  auto* request =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_new(
          arena.ptr());
  auto* ack =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_mutable_assignment_ack(
          request, arena.ptr());
  google_cloud_autosharding_v1_AssignmentAck_set_generation(ack, generation);
  google_cloud_autosharding_v1_AssignmentAck_set_accepted(ack, accepted);
  if (!error_message.empty()) {
    google_cloud_autosharding_v1_AssignmentAck_set_error_message(
        ack, StdStringToUpbString(error_message));
  }
  size_t length;
  char* serialized =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_serialize(
          request, arena.ptr(), &length);
  SendMessage(std::string(serialized, length));
}

void AutoshardingClient::SendMessage(std::string message) {
  pending_sends_.push_back(std::move(message));
  if (!send_in_flight_) SendNextMessage();
}

void AutoshardingClient::SendNextMessage() {
  if (shutdown_ || stream_ == nullptr || pending_sends_.empty()) {
    send_in_flight_ = false;
    return;
  }
  std::string message = std::move(pending_sends_.front());
  pending_sends_.pop_front();
  send_in_flight_ = true;
  stream_->SendMessage(std::move(message));
}

void AutoshardingClient::OnRequestSent(uint64_t stream_id, bool ok) {
  if (shutdown_ || stream_id != stream_id_) return;
  if (!ok) {
    OnStatusReceived(stream_id,
                     absl::UnavailableError("failed to send message"));
    return;
  }
  send_in_flight_ = false;
  SendNextMessage();
}

void AutoshardingClient::OnResponse(uint64_t stream_id,
                                    absl::string_view payload) {
  if (shutdown_ || stream_id != stream_id_ || stream_ == nullptr) return;
  upb::Arena arena;
  auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_parse(
          payload.data(), payload.size(), arena.ptr());
  if (response == nullptr) {
    OnStatusReceived(
        stream_id,
        absl::InternalError("failed to parse response from sharding service"));
    return;
  }
  if (google_cloud_autosharding_v1_WatchShardingAssignmentResponse_has_chunk(
          response)) {
    builder_.AddChunk(
        google_cloud_autosharding_v1_WatchShardingAssignmentResponse_chunk(
            response));
  } else if (
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_has_metadata(
          response)) {
    HandleMetadata(google_cloud_autosharding_v1_AssignmentMetadata_generation(
        google_cloud_autosharding_v1_WatchShardingAssignmentResponse_metadata(
            response)));
  }
  // LoadReportingConfig messages are ignored for now (gRFC A119).
  if (shutdown_ || stream_id != stream_id_ || stream_ == nullptr) return;
  stream_->StartRecvMessage();
}

void AutoshardingClient::HandleMetadata(int64_t generation) {
  AutoshardingAssignmentBuilder::Result result = builder_.Build(generation);
  switch (result.outcome) {
    case AutoshardingAssignmentBuilder::Outcome::kStale:
      GRPC_TRACE_LOG(autosharding_lb, INFO)
          << "[autosharding client " << this << "] " << result.error_message;
      SendAssignmentAck(generation, /*accepted=*/false,
                        std::move(result.error_message));
      return;
    case AutoshardingAssignmentBuilder::Outcome::kRejected:
      GRPC_TRACE_LOG(autosharding_lb, INFO)
          << "[autosharding client " << this << "] " << result.error_message;
      SendAssignmentAck(generation, /*accepted=*/false, result.error_message);
      ReportError(absl::UnavailableError(result.error_message));
      return;
    case AutoshardingAssignmentBuilder::Outcome::kAccepted:
      GRPC_TRACE_LOG(autosharding_lb, INFO)
          << "[autosharding client " << this << "] accepted assignment with "
          << result.assignment->slices.size() << " slices and "
          << result.assignment->endpoint_names.size() << " endpoints";
      SendAssignmentAck(generation, /*accepted=*/true,
                        std::move(result.error_message));
      ReportAssignment(std::move(*result.assignment));
      return;
  }
}

void AutoshardingClient::OnStatusReceived(uint64_t stream_id,
                                          absl::Status status) {
  if (shutdown_ || stream_id != stream_id_) return;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding client " << this << "] stream failed: " << status;
  stream_.reset();
  pending_sends_.clear();
  send_in_flight_ = false;
  // If the stream received a valid assignment, reset the backoff, so that the
  // next attempt starts with the initial backoff delay.
  if (received_valid_assignment_on_stream_) {
    backoff_.Reset();
    received_valid_assignment_on_stream_ = false;
  }
  if (status.ok()) {
    status = absl::UnavailableError("autosharding stream closed by server");
  }
  ReportError(std::move(status));
  ScheduleStreamRetry();
}

void AutoshardingClient::ReportAssignment(AutoshardingAssignment assignment) {
  received_valid_assignment_ = true;
  received_valid_assignment_on_stream_ = true;
  CancelTimer(&initial_assignment_timer_);
  if (on_assignment_ != nullptr) on_assignment_(std::move(assignment));
}

void AutoshardingClient::ReportError(absl::Status status) {
  if (shutdown_) return;
  // gRFC A119: cancel the initial assignment timer when reporting an error.
  CancelTimer(&initial_assignment_timer_);
  if (received_valid_assignment_) return;
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding client " << this << "] reporting error: " << status;
  if (on_error_ != nullptr) on_error_(std::move(status));
}

void AutoshardingClient::ScheduleStreamRetry() {
  CancelTimer(&retry_timer_);
  const Duration delay = backoff_.NextAttemptDelay();
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[autosharding client " << this << "] retrying stream in " << delay;
  retry_timer_ = event_engine_->RunAfter(delay, [self = Ref()]() {
    self->work_serializer_->Run(
        [self = std::move(self)]() {
          self->retry_timer_.reset();
          if (!self->shutdown_) self->StartStreamLocked();
        },
        DEBUG_LOCATION);
  });
}

void AutoshardingClient::CancelTimer(
    std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>*
        timer) {
  if (!timer->has_value()) return;
  event_engine_->Cancel(**timer);
  timer->reset();
}

}  // namespace grpc_core
