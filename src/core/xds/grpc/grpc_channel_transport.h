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

#ifndef GRPC_SRC_CORE_XDS_GRPC_GRPC_CHANNEL_TRANSPORT_H
#define GRPC_SRC_CORE_XDS_GRPC_GRPC_CHANNEL_TRANSPORT_H

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/status.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/xds/xds_client/xds_transport_interface.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"

namespace grpc_core {

// A bidi-streaming transport over a gRPC channel, with no knowledge of xDS.
//
// The xDS client uses it through GrpcXdsTransport, which adds the factory
// cache. A non-xDS side channel can use it directly, for a channel built from a
// caller-supplied target, credentials and metadata. It caches nothing itself,
// so orphaning has nothing to undo.
class GrpcChannelTransport : public XdsTransportInterface {
 public:
  class GrpcStreamingCall;

  struct Params {
    RefCountedPtr<Channel> channel;
    RefCountedPtr<grpc_call_credentials> call_creds;
    std::vector<std::pair<std::string, std::string>> initial_metadata;
    Duration timeout = Duration::Infinity();
    // Polling entities for the calls. When null, the transport creates its own
    // and owns it. It must never end up null: a call created with neither a
    // completion queue nor a pollset set gets no polling entity at all, and the
    // client channel dereferences that entity as soon as a pick is queued.
    grpc_pollset_set* interested_parties = nullptr;
    // Name recorded by refcount tracing, when tracing is enabled.
    const char* trace = nullptr;
  };

  explicit GrpcChannelTransport(Params params);
  ~GrpcChannelTransport() override;

  // Nothing is cached here, so there is nothing to undo. A subclass that caches
  // itself elsewhere overrides this to deregister.
  void Orphaned() override {}

  void StartConnectivityFailureWatch(
      RefCountedPtr<ConnectivityFailureWatcher> watcher) override;
  void StopConnectivityFailureWatch(
      const RefCountedPtr<ConnectivityFailureWatcher>& watcher) override;

  OrphanablePtr<StreamingCall> CreateStreamingCall(
      const char* method,
      std::unique_ptr<StreamingCall::EventHandler> event_handler,
      CallOptions options) override;

  void ResetBackoff() override;

  Channel* channel() const { return channel_.get(); }

 private:
  class StateWatcher;

  RefCountedPtr<Channel> channel_;
  RefCountedPtr<grpc_call_credentials> call_creds_;
  std::vector<std::pair<std::string, std::string>> initial_metadata_;
  Duration timeout_;
  grpc_pollset_set* interested_parties_;
  // True when interested_parties_ was created here and must be destroyed here.
  bool owns_interested_parties_ = false;

  Mutex mu_;
  absl::flat_hash_map<RefCountedPtr<ConnectivityFailureWatcher>, StateWatcher*>
      watchers_ ABSL_GUARDED_BY(&mu_);
};

class GrpcChannelTransport::GrpcStreamingCall final
    : public XdsTransportInterface::StreamingCall {
 public:
  GrpcStreamingCall(
      grpc_pollset_set* interested_parties, Channel* channel,
      const char* method,
      std::unique_ptr<StreamingCall::EventHandler> event_handler,
      grpc_call_credentials* call_creds,
      const std::vector<std::pair<std::string, std::string>>& initial_metadata,
      Duration timeout, CallOptions options);
  ~GrpcStreamingCall() override;

  void Orphan() override;

  void SendMessage(std::string payload, bool send_half_close) override;

  void StartRecvMessage() override;

  void SendHalfClose() override;

 private:
  using OpList = absl::InlinedVector<grpc_op, 3>;

  void AddSendInitialMetadataOp(OpList& op_list);
  void AddRecvInitialMetadataOp(OpList& op_list);
  void AddRecvTrailingMetadataOp(OpList& op_list);
  void AddSendCloseFromClientOp(OpList& op_list);
  void AddSendMessageOp(std::string payload, OpList& op_list);
  void StartBatch(const OpList& op_list, const char* ref_reason,
                  grpc_closure* closure);

  static void OnRecvInitialMetadata(void* arg, grpc_error_handle /*error*/);
  static void OnRequestSent(void* arg, grpc_error_handle error);
  static void OnHalfClosed(void* arg, grpc_error_handle /*error*/);
  static void OnResponseReceived(void* arg, grpc_error_handle /*error*/);
  static void OnStatusReceived(void* arg, grpc_error_handle /*error*/);

  grpc_pollset_set* interested_parties_;

  std::unique_ptr<StreamingCall::EventHandler> event_handler_;

  // Always non-NULL.
  grpc_call* call_;

  // recv_initial_metadata
  grpc_metadata_array initial_metadata_recv_;
  grpc_closure on_recv_initial_metadata_;

  // send_initial_metadata
  std::vector<grpc_metadata> send_initial_metadata_;
  bool sent_initial_metadata_ = false;

  // send_message
  grpc_byte_buffer* send_message_payload_ = nullptr;
  grpc_closure on_request_sent_;

  // half_close
  grpc_closure on_half_closed_;

  // recv_message
  grpc_byte_buffer* recv_message_payload_ = nullptr;
  grpc_closure on_response_received_;

  // recv_trailing_metadata
  grpc_metadata_array trailing_metadata_recv_;
  grpc_status_code status_code_;
  grpc_slice status_details_ = grpc_empty_slice();
  grpc_closure on_status_received_;

  const CallOptions options_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_GRPC_CHANNEL_TRANSPORT_H
