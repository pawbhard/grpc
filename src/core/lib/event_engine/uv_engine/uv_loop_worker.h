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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_LOOP_WORKER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_LOOP_WORKER_H

#include <uv.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "src/core/util/sync.h"
#include "src/core/util/thd.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/function_ref.h"

namespace grpc_event_engine::experimental {

struct LoopCommand {
  virtual ~LoopCommand() = default;
  // Runs on the owner uv_loop_t thread AFTER DrainCommands() unlinks `this`.
  virtual void Execute(uv_loop_t* loop) = 0;
  // Runs on the caller thread AFTER releasing queue_mu_ if Enqueue() rejects.
  virtual void OnRejected() = 0;

  LoopCommand* next = nullptr;
};

struct HeapLoopCommand : public LoopCommand {
  void Execute(uv_loop_t* loop) final {
    Run(loop);
    delete this;
  }
  void OnRejected() final { delete this; }
  virtual void Run(uv_loop_t* loop) = 0;
};

// Intrusive doubly-linked list node for loop-owned socket and listener handles
// so `PostforkChild()` can synchronously close inherited parent FDs on `loop_`
// with zero hot-path overhead and zero heap allocations.
struct LoopForkable {
  virtual ~LoopForkable() = default;
  virtual void OnForkInChild(uv_loop_t* loop) = 0;

  LoopForkable* fork_prev = nullptr;
  LoopForkable* fork_next = nullptr;
  bool fork_registered = false;
};

// Owns a dedicated background thread driving a single `uv_loop_t` shard.
// Cross-thread commands are submitted through an intrusive FIFO queue and
// coalesced via a single `uv_async_t` wakeup handle; all `uv_handle_t` state
// bound to `loop_` is mutated exclusively on this worker's thread.
class UvLoopWorker {
 public:
  explicit UvLoopWorker(size_t shard_index);
  ~UvLoopWorker();

  UvLoopWorker(const UvLoopWorker&) = delete;
  UvLoopWorker& operator=(const UvLoopWorker&) = delete;

  // Enqueues an intrusive command and coalesces uv_async_send wakeups.
  // Returns false (after calling cmd->OnRejected() outside queue_mu_) if this
  // worker has already shut down.
  bool Enqueue(LoopCommand* cmd) ABSL_LOCKS_EXCLUDED(queue_mu_);

  // Executes `fn` synchronously on the loop thread (or inline if already on
  // this loop thread). Used on cold control paths such as Listener::Bind().
  // Returns false if the worker is shutting down or pausing for fork.
  bool RunSync(absl::FunctionRef<void(uv_loop_t*)> fn)
      ABSL_LOCKS_EXCLUDED(queue_mu_);

  bool IsCurrentThread() const;
  static bool IsAnyLoopThread();

  // Loop-thread-only registration of live socket/listener handles for child
  // post-fork cleanup.
  void RegisterForkable(LoopForkable* item);
  void UnregisterForkable(LoopForkable* item);

  size_t shard_index() const { return shard_index_; }
  uv_loop_t* loop() { return &loop_; }
  uint64_t fork_generation() const {
    return fork_generation_.load(std::memory_order_acquire);
  }

  // Fork hooks (invoked by UvEventEngine under pthread_atfork): pauses and
  // joins the loop thread without closing active handles before fork(), then
  // restarts the thread (after uv_loop_fork in child) post-fork.
  void PrepareFork() ABSL_LOCKS_EXCLUDED(queue_mu_);
  void PostforkParent() ABSL_LOCKS_EXCLUDED(queue_mu_);
  void PostforkChild() ABSL_LOCKS_EXCLUDED(queue_mu_);

  // Initiates clean loop drain and joins the worker thread. Idempotent.
  void Shutdown() ABSL_LOCKS_EXCLUDED(queue_mu_);

 private:
  static void OnAsyncWakeup(uv_async_t* handle);
  void StartLoopThread();
  void ThreadMain();
  void DrainCommands() ABSL_LOCKS_EXCLUDED(queue_mu_);
  void ClearForkables();

  const size_t shard_index_;
  uv_loop_t loop_{};
  uv_async_t wakeup_{};
  LoopForkable* fork_head_ = nullptr;

  grpc_core::Mutex queue_mu_;
  LoopCommand* head_ ABSL_GUARDED_BY(queue_mu_) = nullptr;
  LoopCommand* tail_ ABSL_GUARDED_BY(queue_mu_) = nullptr;
  bool doorbell_armed_ ABSL_GUARDED_BY(queue_mu_) = false;
  bool pausing_for_fork_ ABSL_GUARDED_BY(queue_mu_) = false;
  bool stop_requested_ ABSL_GUARDED_BY(queue_mu_) = false;
  bool shutting_down_ ABSL_GUARDED_BY(queue_mu_) = false;

  bool thread_running_ = false;
  bool shutdown_joined_ = false;
  std::atomic<uint64_t> fork_generation_{0};
  std::string thd_name_;
  grpc_core::Thread thread_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_LOOP_WORKER_H
