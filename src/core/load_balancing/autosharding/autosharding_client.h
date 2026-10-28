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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H
#define GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H

#include <grpc/event_engine/event_engine.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "src/core/load_balancing/autosharding/autosharding_assignment.h"
#include "src/core/util/backoff.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Client for the autosharding service (gRFC A119).
//
// The client maintains a WatchShardingAssignment stream, assembles the
// received chunks into a validated assignment using
// AutoshardingAssignmentBuilder, and reports assignments and errors to the LB
// policy.  It retries the stream with exponential backoff, and it reports an
// error if the initial assignment timer fires before a valid assignment has
// been received.
//
// A client instance is scoped to one (channel factory key, autosharding
// target) pair.  The LB policy creates a new client whenever either of those
// changes, which resets the assignment generation: the new client may be
// talking to a different sharding service or a different target, so a
// generation from the previous client is not meaningful (gRFC A119).  The
// client reports results only until it has reported a valid assignment; after
// that, errors are not reported, because the LB policy keeps using the last
// valid assignment.
//
// All methods, and all callbacks, run on the LB policy's WorkSerializer.
// Events from the stream and from timers are posted to the WorkSerializer
// before they are handled.
class AutoshardingClient final : public RefCounted<AutoshardingClient> {
 public:
  using AssignmentCallback = std::function<void(AutoshardingAssignment)>;
  using ErrorCallback = std::function<void(absl::Status)>;

  struct Options {
    // The LB policy's WorkSerializer.
    std::shared_ptr<WorkSerializer> work_serializer;
    // Used for the stream retry timer and the initial assignment timer.
    grpc_event_engine::experimental::EventEngine* event_engine = nullptr;
    // Transport for the sharding service.  The LB policy creates this from the
    // channel factory (gRFC A119).
    RefCountedPtr<XdsTransport> transport;
    // Effective autosharding target, i.e., after substituting the Locality
    // attribute for the "%s" token.
    std::string target;
    // UUID created by the LB policy at build time.  It is reused across stream
    // restarts and client instances (gRFC A119).
    std::string uuid;
    // How long to wait for the first valid assignment or error.
    Duration initial_assignment_timeout;
    // Called when a valid assignment is received.
    AssignmentCallback on_assignment;
    // Called on errors, but only if no valid assignment has been reported yet.
    ErrorCallback on_error;
  };

  explicit AutoshardingClient(Options options);

  // Starts the client: starts the initial assignment timer and creates the
  // first stream.
  void Start();

  // Shuts the client down.  Must be called before the client is dropped, and
  // no callbacks will be invoked after this returns.
  void Shutdown();

  // Resets the stream retry backoff.
  void ResetBackoff();

 private:
  class StreamEventHandler;

  void StartStreamLocked();
  void OnRequestSent(uint64_t stream_id, bool ok);
  void OnResponse(uint64_t stream_id, absl::string_view payload);
  void OnStatusReceived(uint64_t stream_id, absl::Status status);
  void HandleMetadata(int64_t generation);
  void SendInitialConfig();
  void SendAssignmentAck(int64_t generation, bool accepted,
                         std::string error_message);
  void SendMessage(std::string message);
  void SendNextMessage();
  void ReportAssignment(AutoshardingAssignment assignment);
  void ReportError(absl::Status status);
  void ScheduleStreamRetry();
  void StartInitialAssignmentTimer();
  void CancelTimer(
      std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>*
          timer);

  std::shared_ptr<WorkSerializer> work_serializer_;
  grpc_event_engine::experimental::EventEngine* event_engine_;
  RefCountedPtr<XdsTransport> transport_;
  const std::string target_;
  const std::string uuid_;
  const Duration initial_assignment_timeout_;
  AssignmentCallback on_assignment_;
  ErrorCallback on_error_;
  AutoshardingAssignmentBuilder builder_;
  BackOff backoff_;
  OrphanablePtr<XdsTransport::StreamingCall> stream_;
  // Incremented for each stream.  Used to ignore events from old streams.
  uint64_t stream_id_ = 0;
  // Messages waiting to be sent.  Only one message may be in flight at a time.
  std::deque<std::string> pending_sends_;
  bool send_in_flight_ = false;
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      retry_timer_;
  std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      initial_assignment_timer_;
  // True once a valid assignment has been reported to the LB policy.
  bool received_valid_assignment_ = false;
  // True if the current stream received a valid assignment.  Used to decide
  // whether to reset the backoff when the stream fails.
  bool received_valid_assignment_on_stream_ = false;
  bool shutdown_ = false;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARDING_CLIENT_H
