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

#include "src/core/xds/grpc/grpc_channel_transport.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <string.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

GrpcChannelTransport::GrpcChannelTransport(Params params)
    : XdsTransportInterface(params.trace),
      channel_(std::move(params.channel)),
      call_creds_(std::move(params.call_creds)),
      initial_metadata_(std::move(params.initial_metadata)),
      timeout_(params.timeout),
      interested_parties_(params.interested_parties != nullptr
                              ? params.interested_parties
                              : grpc_pollset_set_create()),
      owns_interested_parties_(params.interested_parties == nullptr) {}

GrpcChannelTransport::~GrpcChannelTransport() {
  if (owns_interested_parties_) grpc_pollset_set_destroy(interested_parties_);
}

//
// GrpcChannelTransport::GrpcStreamingCall
//

GrpcChannelTransport::GrpcStreamingCall::GrpcStreamingCall(
    grpc_pollset_set* interested_parties, Channel* channel, const char* method,
    std::unique_ptr<StreamingCall::EventHandler> event_handler,
    grpc_call_credentials* call_creds,
    const std::vector<std::pair<std::string, std::string>>& initial_metadata,
    Duration timeout, CallOptions options)
    : interested_parties_(interested_parties),
      event_handler_(std::move(event_handler)),
      options_(options) {
  Timestamp deadline = (timeout == Duration::Infinity())
                           ? Timestamp::InfFuture()
                           : Timestamp::Now() + timeout;
  // Create call.
  call_ = channel->CreateCall(
      /*parent_call=*/nullptr, GRPC_PROPAGATE_DEFAULTS, /*cq=*/nullptr,
      interested_parties_, Slice::FromStaticString(method),
      /*authority=*/std::nullopt, deadline,
      /*registered_method=*/true, /*arena_init_function=*/std::nullopt);
  GRPC_CHECK_NE(call_, nullptr);
  // Set call creds, if any.
  if (call_creds != nullptr) grpc_call_set_credentials(call_, call_creds);
  // Init data associated with the call.
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  send_initial_metadata_.resize(initial_metadata.size());
  for (size_t i = 0; i < initial_metadata.size(); ++i) {
    send_initial_metadata_[i].key =
        grpc_slice_from_cpp_string(initial_metadata[i].first);
    send_initial_metadata_[i].value =
        grpc_slice_from_cpp_string(initial_metadata[i].second);
  }
  // Initialize closures.
  GRPC_CLOSURE_INIT(&on_recv_initial_metadata_, OnRecvInitialMetadata, this,
                    nullptr);
  GRPC_CLOSURE_INIT(&on_request_sent_, OnRequestSent, this, nullptr);
  GRPC_CLOSURE_INIT(&on_half_closed_, OnHalfClosed, this, nullptr);
  GRPC_CLOSURE_INIT(&on_response_received_, OnResponseReceived, this, nullptr);
  GRPC_CLOSURE_INIT(&on_status_received_, OnStatusReceived, this, nullptr);
  // Start batch for recv_initial_metadata (and send_initial_metadata, unless
  // the caller asked us to wait until the first message is sent).
  OpList op_list;
  if (!options_.start_upon_send_message) {
    sent_initial_metadata_ = true;
    AddSendInitialMetadataOp(op_list);
  }
  AddRecvInitialMetadataOp(op_list);
  StartBatch(op_list, "OnRecvInitialMetadata", &on_recv_initial_metadata_);
  // Start batch for recv_trailing_metadata.
  op_list.clear();
  AddRecvTrailingMetadataOp(op_list);
  StartBatch(op_list, "OnStatusReceived", &on_status_received_);
}

void GrpcChannelTransport::GrpcStreamingCall::
    AddSendInitialMetadataOp(OpList& op_list) {
  grpc_op& op = op_list.emplace_back();
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_INITIAL_METADATA;
  op.data.send_initial_metadata.count = send_initial_metadata_.size();
  op.data.send_initial_metadata.metadata =
      send_initial_metadata_.empty() ? nullptr : send_initial_metadata_.data();
  op.flags = options_.wait_for_ready
                 ? GRPC_INITIAL_METADATA_WAIT_FOR_READY |
                       GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET
                 : 0;
  op.reserved = nullptr;
}

void GrpcChannelTransport::GrpcStreamingCall::
    AddRecvInitialMetadataOp(OpList& op_list) {
  grpc_op& op = op_list.emplace_back();
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_INITIAL_METADATA;
  op.data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv_;
  op.flags = 0;
  op.reserved = nullptr;
}

void GrpcChannelTransport::GrpcStreamingCall::
    AddRecvTrailingMetadataOp(OpList& op_list) {
  grpc_op& op = op_list.emplace_back();
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op.data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
  op.data.recv_status_on_client.status = &status_code_;
  op.data.recv_status_on_client.status_details = &status_details_;
  op.flags = 0;
  op.reserved = nullptr;
}

void GrpcChannelTransport::GrpcStreamingCall::
    AddSendCloseFromClientOp(OpList& op_list) {
  grpc_op& op = op_list.emplace_back();
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op.flags = 0;
  op.reserved = nullptr;
}

void GrpcChannelTransport::GrpcStreamingCall::
    AddSendMessageOp(std::string payload, OpList& op_list) {
  grpc_slice slice = grpc_slice_from_cpp_string(std::move(payload));
  send_message_payload_ = grpc_raw_byte_buffer_create(&slice, 1);
  CSliceUnref(slice);
  grpc_op& op = op_list.emplace_back();
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
}

void GrpcChannelTransport::GrpcStreamingCall::StartBatch(
    const OpList& op_list, const char* ref_reason, grpc_closure* closure) {
  Ref(DEBUG_LOCATION, ref_reason).release();
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      call_, op_list.data(), op_list.size(), closure);
  GRPC_CHECK_EQ(call_error, GRPC_CALL_OK);
}

GrpcChannelTransport::GrpcStreamingCall::
    ~GrpcStreamingCall() {
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  CSliceUnref(status_details_);
  for (auto& md : send_initial_metadata_) {
    CSliceUnref(md.key);
    CSliceUnref(md.value);
  }
  GRPC_CHECK_NE(call_, nullptr);
  grpc_call_unref(call_);
}

void GrpcChannelTransport::GrpcStreamingCall::Orphan() {
  GRPC_CHECK_NE(call_, nullptr);
  // If we are here because xds_client wants to cancel the call,
  // OnStatusReceived() will complete the cancellation and clean up.
  // Otherwise, we are here because xds_client has to orphan a failed call,
  // in which case the following cancellation will be a no-op.
  grpc_call_cancel_internal(call_);
  Unref(DEBUG_LOCATION, "Orphan");
}

void GrpcChannelTransport::GrpcStreamingCall::SendMessage(
    std::string payload, bool send_half_close) {
  OpList op_list;
  if (!sent_initial_metadata_) {
    sent_initial_metadata_ = true;
    AddSendInitialMetadataOp(op_list);
  }
  AddSendMessageOp(std::move(payload), op_list);
  if (send_half_close) {
    AddSendCloseFromClientOp(op_list);
  }
  StartBatch(op_list, "OnRequestSent", &on_request_sent_);
}

void GrpcChannelTransport::GrpcStreamingCall::
    StartRecvMessage() {
  OpList op_list;
  grpc_op& op = op_list.emplace_back();
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &recv_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
  StartBatch(op_list, "StartRecvMessage", &on_response_received_);
}

void GrpcChannelTransport::GrpcStreamingCall::
    SendHalfClose() {
  OpList op_list;
  AddSendCloseFromClientOp(op_list);
  StartBatch(op_list, "SendHalfClose", &on_half_closed_);
}

void GrpcChannelTransport::GrpcStreamingCall::
    OnRecvInitialMetadata(void* arg, grpc_error_handle /*error*/) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  grpc_metadata_array_destroy(&self->initial_metadata_recv_);
}

void GrpcChannelTransport::GrpcStreamingCall::
    OnRequestSent(void* arg, grpc_error_handle error) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  // Clean up the sent message.
  grpc_byte_buffer_destroy(self->send_message_payload_);
  self->send_message_payload_ = nullptr;
  // Invoke request handler.
  self->event_handler_->OnRequestSent(error.ok());
}

void GrpcChannelTransport::GrpcStreamingCall::OnHalfClosed(
    void* arg, grpc_error_handle /*error*/) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
}

void GrpcChannelTransport::GrpcStreamingCall::
    OnResponseReceived(void* arg, grpc_error_handle /*error*/) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  // If there was no payload, then we received status before we received
  // another message, so we stop reading.
  if (self->recv_message_payload_ != nullptr) {
    // Process the response.
    grpc_byte_buffer_reader bbr;
    grpc_byte_buffer_reader_init(&bbr, self->recv_message_payload_);
    grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
    grpc_byte_buffer_reader_destroy(&bbr);
    grpc_byte_buffer_destroy(self->recv_message_payload_);
    self->recv_message_payload_ = nullptr;
    self->event_handler_->OnRecvMessage(StringViewFromSlice(response_slice));
    CSliceUnref(response_slice);
  }
}

void GrpcChannelTransport::GrpcStreamingCall::
    OnStatusReceived(void* arg, grpc_error_handle /*error*/) {
  RefCountedPtr<GrpcStreamingCall> self(static_cast<GrpcStreamingCall*>(arg));
  self->event_handler_->OnStatusReceived(
      absl::Status(static_cast<absl::StatusCode>(self->status_code_),
                   StringViewFromSlice(self->status_details_)));
}

//
// GrpcChannelTransport::StateWatcher
//

class GrpcChannelTransport::StateWatcher final
    : public AsyncConnectivityStateWatcherInterface {
 public:
  explicit StateWatcher(RefCountedPtr<ConnectivityFailureWatcher> watcher)
      : watcher_(std::move(watcher)) {}

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& status) override {
    if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      watcher_->OnConnectivityFailure(absl::Status(
          status.code(),
          absl::StrCat("channel in TRANSIENT_FAILURE: ", status.message())));
    }
  }

  RefCountedPtr<ConnectivityFailureWatcher> watcher_;
};

//
// GrpcChannelTransport
//

void GrpcChannelTransport::StartConnectivityFailureWatch(
    RefCountedPtr<ConnectivityFailureWatcher> watcher) {
  if (channel_->IsLame()) return;
  auto* state_watcher = new StateWatcher(watcher);
  {
    MutexLock lock(mu_);
    watchers_.emplace(watcher, state_watcher);
  }
  channel_->AddConnectivityWatcher(
      GRPC_CHANNEL_IDLE,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface>(state_watcher));
}

void GrpcChannelTransport::StopConnectivityFailureWatch(
    const RefCountedPtr<ConnectivityFailureWatcher>& watcher) {
  if (channel_->IsLame()) return;
  StateWatcher* state_watcher = nullptr;
  {
    MutexLock lock(mu_);
    auto it = watchers_.find(watcher);
    if (it == watchers_.end()) return;
    state_watcher = it->second;
    watchers_.erase(it);
  }
  channel_->RemoveConnectivityWatcher(state_watcher);
}

OrphanablePtr<XdsTransportInterface::StreamingCall>
GrpcChannelTransport::CreateStreamingCall(
    const char* method,
    std::unique_ptr<StreamingCall::EventHandler> event_handler,
    CallOptions options) {
  return MakeOrphanable<GrpcStreamingCall>(
      interested_parties_, channel_.get(), method, std::move(event_handler),
      call_creds_.get(), initial_metadata_, timeout_, options);
}

void GrpcChannelTransport::ResetBackoff() {
  channel_->ResetConnectionBackoff();
}

}  // namespace grpc_core
