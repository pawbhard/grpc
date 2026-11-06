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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_ENGINE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_ENGINE_H

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/port_platform.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/extensions/supports_win_sockets.h"
#include "src/core/lib/event_engine/handle_containers.h"
#include "src/core/lib/event_engine/posix.h"
#include "src/core/lib/event_engine/posix_engine/timer_manager.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_event_engine::experimental {

#ifndef GPR_WINDOWS
using UvEventEngineBase = PosixEventEngineWithFdSupport;
#elif defined(GRPC_WINSOCK_SOCKET)
using UvEventEngineBase =
    ExtendedType<EventEngine, EventEngineWindowsSocketSupport>;
#else
using UvEventEngineBase = EventEngine;
#endif

// Multi-loop libuv EventEngine implementation (`GRPC_EVENT_ENGINE=uv`).
// Shards socket I/O across `N` dedicated `UvLoopWorker` threads while executing
// all user closures on a `WorkStealingThreadPool` and timers via
// `TimerManager`, ensuring `uv_loop_t` threads never block on application code.
class UvEventEngine final : public UvEventEngineBase {
 public:
#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  enum class OnForkRole { kChild, kParent };
#endif

  static std::shared_ptr<UvEventEngine> MakeUvEventEngine();
  static std::shared_ptr<UvEventEngine> MakeUvEventEngine(size_t num_loops);

  UvEventEngine();
  explicit UvEventEngine(size_t num_loops);
  ~UvEventEngine() override;

  UvEventEngine(const UvEventEngine&) = delete;
  UvEventEngine& operator=(const UvEventEngine&) = delete;

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override;

  ConnectionHandle Connect(OnConnectCallback on_connect,
                           const ResolvedAddress& addr,
                           const EndpointConfig& args,
                           MemoryAllocator memory_allocator,
                           Duration timeout) override;

  bool CancelConnect(ConnectionHandle handle) override;
  bool IsWorkerThread() override;

  absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override;

  void Run(Closure* closure) override;
  void Run(absl::AnyInvocable<void()> closure) override;
  TaskHandle RunAfter(Duration when, Closure* closure) override
      ABSL_LOCKS_EXCLUDED(task_mu_);
  TaskHandle RunAfter(Duration when,
                      absl::AnyInvocable<void()> closure) override
      ABSL_LOCKS_EXCLUDED(task_mu_);
  bool Cancel(TaskHandle handle) override ABSL_LOCKS_EXCLUDED(task_mu_);

#ifndef GPR_WINDOWS
  absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
  CreatePosixEndpointFromFd(int fd, const EndpointConfig& config,
                            MemoryAllocator memory_allocator) override;
  absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> CreateEndpointFromFd(
      int fd, const EndpointConfig& config) override;
  ConnectionHandle CreateEndpointFromUnconnectedFd(
      int fd, EventEngine::OnConnectCallback on_connect,
      const EventEngine::ResolvedAddress& addr, const EndpointConfig& config,
      MemoryAllocator memory_allocator, EventEngine::Duration timeout) override;
  absl::StatusOr<std::unique_ptr<EventEngine::Listener>> CreatePosixListener(
      PosixEventEngineWithFdSupport::PosixAcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override;
#elif defined(GRPC_WINSOCK_SOCKET)
  std::unique_ptr<EventEngine::Endpoint> CreateEndpointFromWinSocket(
      SOCKET socket, const EndpointConfig& config) override;
#endif

#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  void BeforeFork();
  void AfterFork(OnForkRole on_fork_role);
#endif

  size_t num_loops() const { return workers_.size(); }

 private:
  static constexpr size_t kNumConnectShards = 64;

  struct TimerClosureData;
  struct ConnectState;

  struct PendingConnectEntry {
    grpc_core::RefCountedPtr<ConnectState> state;
    OnConnectCallback on_connect;
    std::shared_ptr<EventEngine> engine_ref;
    TaskHandle timeout_handle = TaskHandle::kInvalid;
  };

  struct ConnectShard {
    grpc_core::Mutex mu;
    absl::flat_hash_map<int64_t, PendingConnectEntry> pending
        ABSL_GUARDED_BY(mu);
  };

  ConnectionHandle ConnectInternal(int adopted_fd, OnConnectCallback on_connect,
                                   const ResolvedAddress& addr,
                                   const EndpointConfig& args,
                                   MemoryAllocator memory_allocator,
                                   Duration timeout);
  void OnConnectTimeout(int64_t conn_id);
  TaskHandle RunAfterInternal(Duration when, absl::AnyInvocable<void()> cb)
      ABSL_LOCKS_EXCLUDED(task_mu_);
  TimerClosureData* CancelTimerLocked(TaskHandle handle)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(task_mu_);
  void CancelAllPendingTimers() ABSL_LOCKS_EXCLUDED(task_mu_);
  UvLoopWorker* SelectWorkerRoundRobin();

#if GRPC_ARES == 1
  void RegisterAresResolverForFork(AresResolver* resolver);
#endif
#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  void AfterForkInChild() ABSL_LOCKS_EXCLUDED(resolver_handles_mu_);
#endif

  std::shared_ptr<ThreadPool> thread_pool_;
  std::shared_ptr<TimerManager> timer_manager_;
  std::vector<std::unique_ptr<UvLoopWorker>> workers_;
  std::atomic<size_t> next_worker_idx_{0};

  grpc_core::Mutex task_mu_;
  TaskHandleSet known_handles_ ABSL_GUARDED_BY(task_mu_);
  bool disallow_new_timers_ ABSL_GUARDED_BY(task_mu_) = false;
  std::atomic<intptr_t> aba_token_{0};

  std::atomic<int64_t> next_conn_id_{1};
  std::array<ConnectShard, kNumConnectShards> connect_shards_;

#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  grpc_core::Mutex resolver_handles_mu_;
  std::vector<std::weak_ptr<AresResolver::ReinitHandle>> resolver_handles_
      ABSL_GUARDED_BY(resolver_handles_mu_);
#endif
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_ENGINE_H
