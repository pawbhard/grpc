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

#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"

#include <grpc/support/port_platform.h>

#ifndef GPR_WINDOWS
#include <pthread.h>
#include <signal.h>
#endif

#include <cstring>
#include <string>

#include "src/core/lib/event_engine/thread_local.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace grpc_event_engine::experimental {

namespace {

thread_local const UvLoopWorker* g_current_uv_worker = nullptr;

#if !defined(GPR_WINDOWS) && defined(SIGPIPE)
// libuv uses writev() without MSG_NOSIGNAL on Linux; ensure SIGPIPE cannot
// terminate the process when writing to a peer-closed socket or pipe.
void EnsureSigpipeIgnored() {
  static const bool kInitialized = []() {
    struct sigaction old_sa;
    std::memset(&old_sa, 0, sizeof(old_sa));
    if (sigaction(SIGPIPE, nullptr, &old_sa) == 0 &&
        old_sa.sa_handler == SIG_DFL && (old_sa.sa_flags & SA_SIGINFO) == 0) {
      struct sigaction ign_sa;
      std::memset(&ign_sa, 0, sizeof(ign_sa));
      ign_sa.sa_handler = SIG_IGN;
      sigemptyset(&ign_sa.sa_mask);
      (void)sigaction(SIGPIPE, &ign_sa, nullptr);
    }
    return true;
  }();
  (void)kInitialized;
}

void BlockSigpipeOnCurrentThread() {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGPIPE);
  (void)pthread_sigmask(SIG_BLOCK, &mask, nullptr);
}
#endif

class SyncLoopCommand final : public LoopCommand {
 public:
  explicit SyncLoopCommand(absl::FunctionRef<void(uv_loop_t*)> fn) : fn_(fn) {}

  void Execute(uv_loop_t* loop) override {
    fn_(loop);
    done_.Notify();
  }

  void OnRejected() override {}

  void Wait() { done_.WaitForNotification(); }

 private:
  absl::FunctionRef<void(uv_loop_t*)> fn_;
  grpc_core::Notification done_;
};

}  // namespace

UvLoopWorker::UvLoopWorker(size_t shard_index)
    : shard_index_(shard_index),
      thd_name_(absl::StrCat("grpc_uv_loop_", shard_index_)) {
#if !defined(GPR_WINDOWS) && defined(SIGPIPE)
  EnsureSigpipeIgnored();
#endif
  GRPC_CHECK_EQ(uv_loop_init(&loop_), 0);
#if !defined(GPR_WINDOWS) && defined(SIGPROF)
  (void)uv_loop_configure(&loop_, UV_LOOP_BLOCK_SIGNAL, SIGPROF);
#endif
  wakeup_.data = this;
  GRPC_CHECK_EQ(uv_async_init(&loop_, &wakeup_, &UvLoopWorker::OnAsyncWakeup),
                0);
  StartLoopThread();
}

UvLoopWorker::~UvLoopWorker() { Shutdown(); }

void UvLoopWorker::StartLoopThread() {
  bool ok = false;
  thread_ = grpc_core::Thread(
      thd_name_.c_str(),
      [](void* arg) { static_cast<UvLoopWorker*>(arg)->ThreadMain(); }, this,
      &ok);
  GRPC_CHECK(ok);
  thread_.Start();
  thread_running_ = true;
}

bool UvLoopWorker::IsCurrentThread() const {
  return g_current_uv_worker == this;
}

bool UvLoopWorker::IsAnyLoopThread() { return g_current_uv_worker != nullptr; }

void UvLoopWorker::RegisterForkable(LoopForkable* item) {
  GRPC_DCHECK(IsCurrentThread());
  if (item->fork_registered) {
    return;
  }
  item->fork_prev = nullptr;
  item->fork_next = fork_head_;
  if (fork_head_ != nullptr) {
    fork_head_->fork_prev = item;
  }
  fork_head_ = item;
  item->fork_registered = true;
}

void UvLoopWorker::UnregisterForkable(LoopForkable* item) {
  GRPC_DCHECK(IsCurrentThread());
  if (!item->fork_registered) {
    return;
  }
  if (item->fork_prev != nullptr) {
    item->fork_prev->fork_next = item->fork_next;
  } else {
    fork_head_ = item->fork_next;
  }
  if (item->fork_next != nullptr) {
    item->fork_next->fork_prev = item->fork_prev;
  }
  item->fork_prev = nullptr;
  item->fork_next = nullptr;
  item->fork_registered = false;
}

void UvLoopWorker::ClearForkables() {
  LoopForkable* cur = fork_head_;
  fork_head_ = nullptr;
  while (cur != nullptr) {
    LoopForkable* next = cur->fork_next;
    cur->fork_prev = nullptr;
    cur->fork_next = nullptr;
    cur->fork_registered = false;
    cur = next;
  }
}

bool UvLoopWorker::Enqueue(LoopCommand* cmd) {
  cmd->next = nullptr;
  {
    grpc_core::MutexLock lock(queue_mu_);
    if (!shutting_down_) {
      if (tail_ == nullptr) {
        head_ = tail_ = cmd;
      } else {
        tail_->next = cmd;
        tail_ = cmd;
      }
      if (!doorbell_armed_) {
        doorbell_armed_ = true;
        GRPC_CHECK_EQ(uv_async_send(&wakeup_), 0);
      }
      return true;
    }
  }
  cmd->OnRejected();
  return false;
}

bool UvLoopWorker::RunSync(absl::FunctionRef<void(uv_loop_t*)> fn) {
  if (IsCurrentThread()) {
    fn(&loop_);
    return true;
  }
  SyncLoopCommand cmd(fn);
  {
    grpc_core::MutexLock lock(queue_mu_);
    if (shutting_down_ || pausing_for_fork_) {
      return false;
    }
    if (tail_ == nullptr) {
      head_ = tail_ = &cmd;
    } else {
      tail_->next = &cmd;
      tail_ = &cmd;
    }
    if (!doorbell_armed_) {
      doorbell_armed_ = true;
      GRPC_CHECK_EQ(uv_async_send(&wakeup_), 0);
    }
  }
  cmd.Wait();
  return true;
}

void UvLoopWorker::PrepareFork() {
  if (shutdown_joined_ || !thread_running_) {
    return;
  }
  GRPC_CHECK(!IsCurrentThread());
  {
    grpc_core::MutexLock lock(queue_mu_);
    pausing_for_fork_ = true;
    if (!doorbell_armed_) {
      doorbell_armed_ = true;
      GRPC_CHECK_EQ(uv_async_send(&wakeup_), 0);
    }
  }
  thread_.Join();
  thread_running_ = false;
}

void UvLoopWorker::PostforkParent() {
  if (shutdown_joined_ || thread_running_) {
    return;
  }
  {
    grpc_core::MutexLock lock(queue_mu_);
    pausing_for_fork_ = false;
    if (head_ != nullptr && !doorbell_armed_) {
      doorbell_armed_ = true;
      GRPC_CHECK_EQ(uv_async_send(&wakeup_), 0);
    }
  }
  StartLoopThread();
}

void UvLoopWorker::PostforkChild() {
  if (shutdown_joined_ || thread_running_) {
    return;
  }
  fork_generation_.fetch_add(1, std::memory_order_acq_rel);
  const UvLoopWorker* prev_worker = g_current_uv_worker;
  g_current_uv_worker = this;
  ThreadLocal::SetIsEventEngineThread(true);
  absl::Cleanup reset_tls = [prev_worker]() {
    g_current_uv_worker = prev_worker;
    if (prev_worker == nullptr) {
      ThreadLocal::SetIsEventEngineThread(false);
    }
  };
#ifndef GPR_WINDOWS
  char err_buf[128];
  const int rc = uv_loop_fork(&loop_);
  if (rc != 0) {
    LOG(ERROR) << "uv_loop_fork failed on shard " << shard_index_ << ": "
               << uv_strerror_r(rc, err_buf, sizeof(err_buf));
  }
#endif
  {
    grpc_core::MutexLock lock(queue_mu_);
    pausing_for_fork_ = false;
    doorbell_armed_ = false;
  }
  // 1. Shut down and close every pre-fork socket and listener handle BEFORE
  //    draining commands so queued pre-fork writes cannot transmit onto
  //    inherited parent sockets.
  while (fork_head_ != nullptr) {
    LoopForkable* item = fork_head_;
    UnregisterForkable(item);
    item->OnForkInChild(&loop_);
  }
  // 2. Drain pre-fork commands (which now observe shut-down handles /
  //    incremented `fork_generation_`) plus c-ares / ConnectState reset cmds.
  DrainCommands();
  while (fork_head_ != nullptr) {
    LoopForkable* item = fork_head_;
    UnregisterForkable(item);
    item->OnForkInChild(&loop_);
  }
  // 3. Flush `closing_handles` (`OnCloseCb`, `OnServerCloseCb`, canceled
  //    write/connect callbacks) synchronously before restarting the loop
  //    thread.
  (void)uv_run(&loop_, UV_RUN_NOWAIT);
  {
    grpc_core::MutexLock lock(queue_mu_);
    if (head_ != nullptr && !doorbell_armed_) {
      doorbell_armed_ = true;
      (void)uv_async_send(&wakeup_);
    }
  }
  std::move(reset_tls).Invoke();
  StartLoopThread();
}

void UvLoopWorker::Shutdown() {
  if (shutdown_joined_) {
    return;
  }
  GRPC_CHECK(!IsCurrentThread());
  if (thread_running_) {
    {
      grpc_core::MutexLock lock(queue_mu_);
      pausing_for_fork_ = false;
      stop_requested_ = true;
      if (!shutting_down_ && !doorbell_armed_) {
        doorbell_armed_ = true;
        GRPC_CHECK_EQ(uv_async_send(&wakeup_), 0);
      }
    }
    thread_.Join();
    thread_running_ = false;
  } else {
    const UvLoopWorker* prev_worker = g_current_uv_worker;
    g_current_uv_worker = this;
    ThreadLocal::SetIsEventEngineThread(true);
    absl::Cleanup reset_tls = [prev_worker]() {
      g_current_uv_worker = prev_worker;
      if (prev_worker == nullptr) {
        ThreadLocal::SetIsEventEngineThread(false);
      }
    };
    {
      grpc_core::MutexLock lock(queue_mu_);
      pausing_for_fork_ = false;
      stop_requested_ = true;
      shutting_down_ = true;
    }
    DrainCommands();
    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&wakeup_))) {
      uv_close(reinterpret_cast<uv_handle_t*>(&wakeup_), nullptr);
    }
    while (uv_loop_close(&loop_) == UV_EBUSY) {
      uv_walk(
          &loop_,
          [](uv_handle_t* h, void* /*arg*/) {
            if (!uv_is_closing(h)) {
              uv_close(h, nullptr);
            }
          },
          nullptr);
      (void)uv_run(&loop_, UV_RUN_ONCE);
    }
    ClearForkables();
  }
  shutdown_joined_ = true;
}

void UvLoopWorker::OnAsyncWakeup(uv_async_t* handle) {
  auto* self = static_cast<UvLoopWorker*>(handle->data);
  self->DrainCommands();
}

void UvLoopWorker::DrainCommands() {
  for (;;) {
    LoopCommand* batch = nullptr;
    bool begin_shutdown = false;
    bool pause_for_fork = false;
    {
      grpc_core::MutexLock lock(queue_mu_);
      batch = head_;
      head_ = tail_ = nullptr;
      doorbell_armed_ = false;
      if (batch == nullptr) {
        if (stop_requested_ && !shutting_down_) {
          shutting_down_ = true;
          begin_shutdown = true;
        } else if (pausing_for_fork_) {
          pause_for_fork = true;
        }
      }
    }
    if (batch != nullptr) {
      while (batch != nullptr) {
        LoopCommand* cmd = batch;
        batch = batch->next;
        cmd->next = nullptr;
        cmd->Execute(&loop_);
      }
      continue;
    }
    if (begin_shutdown) {
      if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&wakeup_))) {
        uv_close(reinterpret_cast<uv_handle_t*>(&wakeup_), nullptr);
      }
    } else if (pause_for_fork) {
      uv_stop(&loop_);
    }
    break;
  }
}

void UvLoopWorker::ThreadMain() {
#if !defined(GPR_WINDOWS) && defined(SIGPIPE)
  BlockSigpipeOnCurrentThread();
#endif
  ThreadLocal::SetIsEventEngineThread(true);
  g_current_uv_worker = this;
  (void)uv_run(&loop_, UV_RUN_DEFAULT);

  bool paused = false;
  {
    grpc_core::MutexLock lock(queue_mu_);
    paused = pausing_for_fork_ && !shutting_down_;
  }
  if (paused) {
    g_current_uv_worker = nullptr;
    ThreadLocal::SetIsEventEngineThread(false);
    return;
  }

  // Drain any final close callbacks and straggler handles until uv_loop_close
  // succeeds cleanly.
  DrainCommands();
  (void)uv_run(&loop_, UV_RUN_NOWAIT);
  while (uv_loop_close(&loop_) == UV_EBUSY) {
    DrainCommands();
    uv_walk(
        &loop_,
        [](uv_handle_t* h, void* /*arg*/) {
          if (!uv_is_closing(h)) {
            uv_close(h, nullptr);
          }
        },
        nullptr);
    (void)uv_run(&loop_, UV_RUN_ONCE);
  }
  ClearForkables();
  g_current_uv_worker = nullptr;
  ThreadLocal::SetIsEventEngineThread(false);
}

}  // namespace grpc_event_engine::experimental
