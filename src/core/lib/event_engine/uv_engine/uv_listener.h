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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_LISTENER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_LISTENER_H

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include <atomic>
#include <memory>
#include <vector>

#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_event_engine::experimental {

#ifndef GPR_WINDOWS
using UvListenerBase = PosixListenerWithFdSupport;
using UvAcceptCallback = PosixEventEngineWithFdSupport::PosixAcceptCallback;
#else
using UvListenerBase = EventEngine::Listener;
using UvAcceptCallback = EventEngine::Listener::AcceptCallback;
#endif

// Multi-loop EventEngine::Listener backed by `uv_tcp_t` / `uv_pipe_t` handles.
// Shards TCP listeners across `UvLoopWorker`s via `SO_REUSEPORT` when supported
// (falling back to single-socket accept + round-robin cross-worker dispatch),
// and guarantees `on_shutdown` runs only after all listening handles close and
// all in-flight `on_accept` callbacks complete.
class UvListener final : public UvListenerBase {
 public:
  UvListener(UvAcceptCallback on_accept,
             absl::AnyInvocable<void(absl::Status)> on_shutdown,
             const EndpointConfig& config,
             std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory,
             std::vector<UvLoopWorker*> workers,
             std::shared_ptr<ThreadPool> thread_pool,
             std::shared_ptr<EventEngine> engine);
  ~UvListener() override;

  UvListener(const UvListener&) = delete;
  UvListener& operator=(const UvListener&) = delete;

  absl::StatusOr<int> Bind(const EventEngine::ResolvedAddress& addr) override;
  absl::Status Start() override;

#ifndef GPR_WINDOWS
  absl::StatusOr<int> BindWithFd(
      const EventEngine::ResolvedAddress& addr,
      PosixListenerWithFdSupport::OnPosixBindNewFdCallback on_bind_new_fd)
      override;
  absl::Status HandleExternalConnection(int listener_fd, int fd,
                                        SliceBuffer* pending_data) override;
  void ShutdownListeningFds() override;
#endif

 private:
  struct ListenerState;
  struct ServerSocket;

  void TriggerShutdown();

  std::shared_ptr<EventEngine> engine_;
  grpc_core::RefCountedPtr<ListenerState> state_;
  std::atomic<bool> shutdown_called_{false};
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_LISTENER_H
