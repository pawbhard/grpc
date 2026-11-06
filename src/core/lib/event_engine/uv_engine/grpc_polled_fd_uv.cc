// Copyright 2026 The gRPC Authors
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

#include "src/core/lib/event_engine/uv_engine/grpc_polled_fd_uv.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#if GRPC_ARES == 1

#include <grpc/event_engine/event_engine.h>
#include <uv.h>

#ifdef GPR_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

// Pull in ares.h after event_engine.h / platform socket headers because ares.h
// is not self-contained on Windows.
#include <ares.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/uv_engine/uv_error_utils.h"
#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#ifdef GPR_WINDOWS
struct iovec {
  void* iov_base;
  size_t iov_len;
};
#endif

namespace grpc_event_engine::experimental {
namespace {

// Persistent per-`ares_socket_t` poll state owned by
// `GrpcPolledFdFactoryLibuv`.
//
// `AresResolver` initializes c-ares with `ARES_FLAG_STAYOPEN`, so
// `CheckSocketsLocked()` calls `ShutdownLocked(OkStatus())` and destroys the
// `GrpcPolledFd` wrapper between sequential queries while c-ares keeps the
// underlying `ares_socket_t` open for subsequent queries. Keeping one
// `UvPolledSocket` (`uv_poll_t`) per live `ares_socket_t` avoids `UV_EEXIST`
// from double `uv_poll_init_socket` calls and defers closing the OS socket
// until `OnPollCloseCb` runs on the `uv_loop_t` thread.
class UvPolledSocket final : public grpc_core::RefCounted<UvPolledSocket> {
 public:
  UvPolledSocket(ares_socket_t as, UvLoopWorker* loop_worker,
                 std::shared_ptr<ThreadPool> thread_pool)
      : as_(as),
        name_(absl::StrCat("c-ares fd: ", static_cast<int64_t>(as))),
        loop_worker_(loop_worker),
        thread_pool_(std::move(thread_pool)),
        fork_generation_(loop_worker->fork_generation()) {}

  ~UvPolledSocket() override { CloseOsSocketOnce(); }

  ares_socket_t ares_socket() const { return as_; }
  const char* name() const { return name_.c_str(); }

  bool IsCurrent() const {
    return fork_generation_ == loop_worker_->fork_generation();
  }

  void PrepareForReuse() ABSL_LOCKS_EXCLUDED(mu_) {
    grpc_core::MutexLock lock(mu_);
    GRPC_CHECK(!read_pending_);
    GRPC_CHECK(!write_pending_);
    wrapper_shutdown_ = false;
    shutdown_error_ = absl::OkStatus();
  }

  void RegisterForOnReadable(
      absl::AnyInvocable<void(absl::Status)> read_closure)
      ABSL_LOCKS_EXCLUDED(mu_) {
    bool enqueue_sync = false;
    absl::AnyInvocable<void(absl::Status)> immediate_cb;
    absl::Status immediate_status;
    {
      grpc_core::MutexLock lock(mu_);
      GRPC_CHECK(!read_pending_);
      if (wrapper_shutdown_ || closing_) {
        immediate_cb = std::move(read_closure);
        immediate_status =
            shutdown_error_.ok()
                ? absl::UnavailableError("GrpcPolledFd shut down")
                : shutdown_error_;
      } else {
        read_closure_ = std::move(read_closure);
        read_pending_ = true;
        enqueue_sync = ArmSyncLocked();
      }
    }
    if (immediate_cb != nullptr) {
      ScheduleClosure(std::move(immediate_cb), std::move(immediate_status));
      return;
    }
    if (enqueue_sync) {
      loop_worker_->Enqueue(new SyncPollCmd(Ref()));
    }
  }

  void RegisterForOnWriteable(
      absl::AnyInvocable<void(absl::Status)> write_closure)
      ABSL_LOCKS_EXCLUDED(mu_) {
    bool enqueue_sync = false;
    absl::AnyInvocable<void(absl::Status)> immediate_cb;
    absl::Status immediate_status;
    {
      grpc_core::MutexLock lock(mu_);
      GRPC_CHECK(!write_pending_);
      if (wrapper_shutdown_ || closing_) {
        immediate_cb = std::move(write_closure);
        immediate_status =
            shutdown_error_.ok()
                ? absl::UnavailableError("GrpcPolledFd shut down")
                : shutdown_error_;
      } else {
        write_closure_ = std::move(write_closure);
        write_pending_ = true;
        enqueue_sync = ArmSyncLocked();
      }
    }
    if (immediate_cb != nullptr) {
      ScheduleClosure(std::move(immediate_cb), std::move(immediate_status));
      return;
    }
    if (enqueue_sync) {
      loop_worker_->Enqueue(new SyncPollCmd(Ref()));
    }
  }

  bool IsFdStillReadable() const {
#ifdef GPR_WINDOWS
    u_long bytes_available = 0;
    return ioctlsocket(as_, FIONREAD, &bytes_available) == 0 &&
           bytes_available > 0;
#else
    int bytes_available = 0;
    return ioctl(as_, FIONREAD, &bytes_available) == 0 && bytes_available > 0;
#endif
  }

  bool ShutdownWrapper(absl::Status error) ABSL_LOCKS_EXCLUDED(mu_) {
    // Match `Epoll1EventHandle::HandleShutdownInternal`: non-Cancelled shutdown
    // (such as `CheckSocketsLocked()` retiring an idle/timed-out socket with
    // `OkStatus()`) must deliver a non-OK status so `AresResolver::OnReadable`
    // neither calls `ares_process_fd` on the retired socket nor cancels active
    // queries on other sockets.
    if (!absl::IsCancelled(error)) {
      error = absl::UnavailableError(error.message());
    }
    absl::AnyInvocable<void(absl::Status)> cancel_read;
    absl::AnyInvocable<void(absl::Status)> cancel_write;
    bool enqueue_sync = false;
    {
      grpc_core::MutexLock lock(mu_);
      if (!wrapper_shutdown_) {
        wrapper_shutdown_ = true;
        shutdown_error_ = error;
      }
      if (read_pending_) {
        read_pending_ = false;
        cancel_read = std::move(read_closure_);
      }
      if (write_pending_) {
        write_pending_ = false;
        cancel_write = std::move(write_closure_);
      }
      if (!closing_ && ever_synced_) {
        enqueue_sync = ArmSyncLocked();
      }
    }
    if (enqueue_sync) {
      loop_worker_->Enqueue(new SyncPollCmd(Ref()));
    }
    if (cancel_read != nullptr) {
      ScheduleClosure(std::move(cancel_read), error);
    }
    if (cancel_write != nullptr) {
      ScheduleClosure(std::move(cancel_write), error);
    }
    return true;
  }

  void CloseAndDestroy() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::AnyInvocable<void(absl::Status)> cancel_read;
    absl::AnyInvocable<void(absl::Status)> cancel_write;
    bool need_loop_close = false;
    {
      grpc_core::MutexLock lock(mu_);
      if (closing_) {
        return;
      }
      closing_ = true;
      if (shutdown_error_.ok()) {
        shutdown_error_ = absl::UnavailableError("c-ares socket closed");
      }
      if (read_pending_) {
        read_pending_ = false;
        cancel_read = std::move(read_closure_);
      }
      if (write_pending_) {
        write_pending_ = false;
        cancel_write = std::move(write_closure_);
      }
      need_loop_close = ever_synced_;
    }
    if (cancel_read != nullptr) {
      ScheduleClosure(std::move(cancel_read),
                      absl::UnavailableError("c-ares socket closed"));
    }
    if (cancel_write != nullptr) {
      ScheduleClosure(std::move(cancel_write),
                      absl::UnavailableError("c-ares socket closed"));
    }
    if (!need_loop_close) {
      CloseOsSocketOnce();
      ReleasePollRefIfHeld();
      return;
    }
    loop_worker_->Enqueue(new ClosePollCmd(Ref()));
  }

 private:
  class SyncPollCmd final : public LoopCommand {
   public:
    explicit SyncPollCmd(grpc_core::RefCountedPtr<UvPolledSocket> socket)
        : socket_(std::move(socket)) {}

    void Execute(uv_loop_t* loop) override {
      grpc_core::RefCountedPtr<UvPolledSocket> self = std::move(socket_);
      delete this;
      self->ExecuteSyncOnLoop(loop);
    }

    void OnRejected() override {
      grpc_core::RefCountedPtr<UvPolledSocket> self = std::move(socket_);
      delete this;
      self->OnSyncRejected();
    }

   private:
    grpc_core::RefCountedPtr<UvPolledSocket> socket_;
  };

  class ClosePollCmd final : public LoopCommand {
   public:
    explicit ClosePollCmd(grpc_core::RefCountedPtr<UvPolledSocket> socket)
        : socket_(std::move(socket)) {}

    void Execute(uv_loop_t* /*loop*/) override {
      grpc_core::RefCountedPtr<UvPolledSocket> self = std::move(socket_);
      delete this;
      self->ExecuteCloseOnLoop();
    }

    void OnRejected() override {
      grpc_core::RefCountedPtr<UvPolledSocket> self = std::move(socket_);
      delete this;
      self->CloseOsSocketOnce();
      self->ReleasePollRefIfHeld();
    }

   private:
    grpc_core::RefCountedPtr<UvPolledSocket> socket_;
  };

  bool ArmSyncLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    ever_synced_ = true;
    if (sync_cmd_queued_) {
      return false;
    }
    sync_cmd_queued_ = true;
    return true;
  }

  void ScheduleClosure(absl::AnyInvocable<void(absl::Status)> closure,
                       absl::Status status) const {
    thread_pool_->Run(SelfDeletingClosure::Create(
        [closure = std::move(closure), status = std::move(status)]() mutable {
          closure(std::move(status));
        }));
  }

  void FailPendingClosuresFromLoop(const absl::Status& status)
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::AnyInvocable<void(absl::Status)> fail_read;
    absl::AnyInvocable<void(absl::Status)> fail_write;
    {
      grpc_core::MutexLock lock(mu_);
      if (read_pending_) {
        read_pending_ = false;
        fail_read = std::move(read_closure_);
      }
      if (write_pending_) {
        write_pending_ = false;
        fail_write = std::move(write_closure_);
      }
    }
    if (fail_read != nullptr) {
      ScheduleClosure(std::move(fail_read), status);
    }
    if (fail_write != nullptr) {
      ScheduleClosure(std::move(fail_write), status);
    }
  }

  void OnSyncRejected() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::AnyInvocable<void(absl::Status)> fail_read;
    absl::AnyInvocable<void(absl::Status)> fail_write;
    {
      grpc_core::MutexLock lock(mu_);
      sync_cmd_queued_ = false;
      if (read_pending_) {
        read_pending_ = false;
        fail_read = std::move(read_closure_);
      }
      if (write_pending_) {
        write_pending_ = false;
        fail_write = std::move(write_closure_);
      }
    }
    const absl::Status err = absl::CancelledError("UvLoopWorker shut down");
    if (fail_read != nullptr) {
      ScheduleClosure(std::move(fail_read), err);
    }
    if (fail_write != nullptr) {
      ScheduleClosure(std::move(fail_write), err);
    }
  }

  void ExecuteSyncOnLoop(uv_loop_t* loop) ABSL_LOCKS_EXCLUDED(mu_) {
    if (!IsCurrent()) {
      {
        grpc_core::MutexLock lock(mu_);
        sync_cmd_queued_ = false;
      }
      if (poll_initialized_ && poll_active_ &&
          !uv_is_closing(reinterpret_cast<uv_handle_t*>(&poll_))) {
        uv_poll_stop(&poll_);
        poll_active_ = false;
        active_events_ = 0;
      }
      FailPendingClosuresFromLoop(
          absl::UnavailableError("GrpcPolledFd stale across fork"));
      return;
    }
    int desired_events = 0;
    bool is_closing = false;
    {
      grpc_core::MutexLock lock(mu_);
      sync_cmd_queued_ = false;
      is_closing = closing_;
      if (!closing_ && !wrapper_shutdown_) {
        if (read_pending_) desired_events |= UV_READABLE;
        if (write_pending_) desired_events |= UV_WRITABLE;
      }
    }
    if (is_closing) {
      return;
    }
    if (desired_events == 0) {
      if (poll_initialized_ && poll_active_ &&
          !uv_is_closing(reinterpret_cast<uv_handle_t*>(&poll_))) {
        uv_poll_stop(&poll_);
        poll_active_ = false;
        active_events_ = 0;
      }
      return;
    }
    if (!poll_initialized_) {
      poll_.data = this;
      const int init_rc =
          uv_poll_init_socket(loop, &poll_, static_cast<uv_os_sock_t>(as_));
      if (init_rc != 0) {
        char err_buf[128];
        LOG(ERROR) << "uv_poll_init_socket failed for " << name_ << ": "
                   << uv_strerror_r(init_rc, err_buf, sizeof(err_buf));
        FailPendingClosuresFromLoop(
            UvErrorToStatus(init_rc, "uv_poll_init_socket"));
        return;
      }
      // Hold +1 ref owned by `poll_` until `OnPollCloseCb` (or rejected
      // cleanup) releases it via `ReleasePollRefIfHeld()`.
      Ref().release();
      poll_ref_held_.store(true, std::memory_order_release);
      poll_initialized_ = true;
    }
    if (uv_is_closing(reinterpret_cast<uv_handle_t*>(&poll_))) {
      return;
    }
    if (!poll_active_ || active_events_ != desired_events) {
      const int start_rc =
          uv_poll_start(&poll_, desired_events, &UvPolledSocket::OnUvPollCb);
      if (start_rc != 0) {
        poll_active_ = false;
        active_events_ = 0;
        char err_buf[128];
        LOG(ERROR) << "uv_poll_start failed for " << name_ << ": "
                   << uv_strerror_r(start_rc, err_buf, sizeof(err_buf));
        FailPendingClosuresFromLoop(UvErrorToStatus(start_rc, "uv_poll_start"));
        return;
      }
      poll_active_ = true;
      active_events_ = desired_events;
    }
  }

  void ExecuteCloseOnLoop() ABSL_LOCKS_EXCLUDED(mu_) {
    if (!poll_initialized_) {
      CloseOsSocketOnce();
      ReleasePollRefIfHeld();
      return;
    }
    uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&poll_);
    if (!uv_is_closing(handle)) {
      if (poll_active_) {
        uv_poll_stop(&poll_);
        poll_active_ = false;
        active_events_ = 0;
      }
      uv_close(handle, &UvPolledSocket::OnPollCloseCb);
    }
  }

  static void OnPollCloseCb(uv_handle_t* handle) {
    auto* self = static_cast<UvPolledSocket*>(handle->data);
    self->CloseOsSocketOnce();
    self->ReleasePollRefIfHeld();
  }

  static void OnUvPollCb(uv_poll_t* handle, int status, int events) {
    auto* self = static_cast<UvPolledSocket*>(handle->data);
    if (status < 0) {
      // In libuv (`uv__poll_io`), `status < 0` occurs when `POLLERR` fires on
      // the socket and libuv automatically stops the watcher internally.
      self->poll_active_ = false;
      self->active_events_ = 0;
    }
    absl::AnyInvocable<void(absl::Status)> fire_read;
    absl::AnyInvocable<void(absl::Status)> fire_write;
    int remaining_events = 0;
    {
      grpc_core::MutexLock lock(self->mu_);
      if (!self->closing_ && !self->wrapper_shutdown_) {
        const bool wake_read =
            status < 0 || (events & (UV_READABLE | UV_DISCONNECT)) != 0;
        const bool wake_write =
            status < 0 || (events & (UV_WRITABLE | UV_DISCONNECT)) != 0;
        if (wake_read && self->read_pending_) {
          self->read_pending_ = false;
          fire_read = std::move(self->read_closure_);
        }
        if (wake_write && self->write_pending_) {
          self->write_pending_ = false;
          fire_write = std::move(self->write_closure_);
        }
        if (self->read_pending_) remaining_events |= UV_READABLE;
        if (self->write_pending_) remaining_events |= UV_WRITABLE;
      }
    }
    if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&self->poll_))) {
      if (remaining_events == 0) {
        if (self->poll_active_) {
          uv_poll_stop(&self->poll_);
          self->poll_active_ = false;
          self->active_events_ = 0;
        }
      } else if (!self->poll_active_ ||
                 self->active_events_ != remaining_events) {
        const int rc = uv_poll_start(&self->poll_, remaining_events,
                                     &UvPolledSocket::OnUvPollCb);
        if (rc == 0) {
          self->poll_active_ = true;
          self->active_events_ = remaining_events;
        } else {
          self->poll_active_ = false;
          self->active_events_ = 0;
          self->FailPendingClosuresFromLoop(
              UvErrorToStatus(rc, "uv_poll_start"));
        }
      }
    }
    // Deliver readiness with `OkStatus()` on `ThreadPool` (never inline on
    // `uv_loop_t`!). Even on `POLLERR` (`status < 0`), passing `OkStatus()`
    // matches `PosixEventEngine` so `AresResolver` invokes `ares_process_fd`
    // and lets c-ares inspect the socket error and fail over across servers.
    if (fire_read != nullptr) {
      self->ScheduleClosure(std::move(fire_read), absl::OkStatus());
    }
    if (fire_write != nullptr) {
      self->ScheduleClosure(std::move(fire_write), absl::OkStatus());
    }
  }

  void ReleasePollRefIfHeld() {
    if (poll_ref_held_.exchange(false, std::memory_order_acq_rel)) {
      Unref();
    }
  }

  void CloseOsSocketOnce() ABSL_LOCKS_EXCLUDED(mu_) {
    bool should_close = false;
    {
      grpc_core::MutexLock lock(mu_);
      if (!os_socket_closed_) {
        os_socket_closed_ = true;
        should_close = true;
      }
    }
    if (should_close && as_ != ARES_SOCKET_BAD) {
#ifdef GPR_WINDOWS
      closesocket(as_);
#else
      ::close(as_);
#endif
    }
  }

  const ares_socket_t as_;
  const std::string name_;
  UvLoopWorker* const loop_worker_;
  const std::shared_ptr<ThreadPool> thread_pool_;
  const uint64_t fork_generation_;
  std::atomic<bool> poll_ref_held_{false};

  // Accessed exclusively on `loop_worker_`'s `uv_loop_t` thread:
  uv_poll_t poll_{};
  bool poll_initialized_ = false;
  bool poll_active_ = false;
  int active_events_ = 0;

  mutable grpc_core::Mutex mu_;
  absl::AnyInvocable<void(absl::Status)> read_closure_ ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<void(absl::Status)> write_closure_ ABSL_GUARDED_BY(mu_);
  bool read_pending_ ABSL_GUARDED_BY(mu_) = false;
  bool write_pending_ ABSL_GUARDED_BY(mu_) = false;
  bool wrapper_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  absl::Status shutdown_error_ ABSL_GUARDED_BY(mu_);
  bool closing_ ABSL_GUARDED_BY(mu_) = false;
  bool ever_synced_ ABSL_GUARDED_BY(mu_) = false;
  bool sync_cmd_queued_ ABSL_GUARDED_BY(mu_) = false;
  bool os_socket_closed_ ABSL_GUARDED_BY(mu_) = false;
};

class GrpcPolledFdLibuv final : public GrpcPolledFd {
 public:
  explicit GrpcPolledFdLibuv(grpc_core::RefCountedPtr<UvPolledSocket> socket)
      : socket_(std::move(socket)) {}

  ~GrpcPolledFdLibuv() override = default;

  void RegisterForOnReadableLocked(
      absl::AnyInvocable<void(absl::Status)> read_closure) override {
    socket_->RegisterForOnReadable(std::move(read_closure));
  }

  void RegisterForOnWriteableLocked(
      absl::AnyInvocable<void(absl::Status)> write_closure) override {
    socket_->RegisterForOnWriteable(std::move(write_closure));
  }

  bool IsFdStillReadableLocked() override {
    return socket_->IsFdStillReadable();
  }

  GRPC_MUST_USE_RESULT bool ShutdownLocked(absl::Status error) override {
    return socket_->ShutdownWrapper(std::move(error));
  }

  ares_socket_t GetWrappedAresSocketLocked() override {
    return socket_->ares_socket();
  }

  const char* GetName() const override { return socket_->name(); }

  bool IsCurrent() const override { return socket_->IsCurrent(); }

 private:
  grpc_core::RefCountedPtr<UvPolledSocket> socket_;
};

class GrpcPolledFdFactoryLibuv final : public GrpcPolledFdFactory {
 public:
  GrpcPolledFdFactoryLibuv(UvLoopWorker* loop_worker,
                           std::shared_ptr<ThreadPool> thread_pool)
      : loop_worker_(loop_worker), thread_pool_(std::move(thread_pool)) {
    GRPC_CHECK_NE(loop_worker_, nullptr);
    GRPC_CHECK_NE(thread_pool_, nullptr);
  }

  ~GrpcPolledFdFactoryLibuv() override {
    std::vector<grpc_core::RefCountedPtr<UvPolledSocket>> remaining;
    {
      grpc_core::MutexLock lock(mu_);
      remaining.reserve(sockets_.size());
      for (auto& [as, socket] : sockets_) {
        (void)as;
        remaining.push_back(std::move(socket));
      }
      sockets_.clear();
    }
    for (auto& socket : remaining) {
      socket->CloseAndDestroy();
    }
  }

  void Initialize(grpc_core::Mutex* /*mutex*/,
                  EventEngine* /*event_engine*/) override {}

  std::unique_ptr<GrpcPolledFd> NewGrpcPolledFdLocked(ares_socket_t as) override
      ABSL_LOCKS_EXCLUDED(mu_) {
    grpc_core::RefCountedPtr<UvPolledSocket> socket;
    {
      grpc_core::MutexLock lock(mu_);
      auto it = sockets_.find(as);
      if (it != sockets_.end() && it->second->IsCurrent()) {
        socket = it->second;
      } else {
        socket = grpc_core::MakeRefCounted<UvPolledSocket>(as, loop_worker_,
                                                           thread_pool_);
        sockets_[as] = socket;
      }
    }
    socket->PrepareForReuse();
    return std::make_unique<GrpcPolledFdLibuv>(std::move(socket));
  }

  void ConfigureAresChannelLocked(ares_channel channel) override {
    static const struct ares_socket_functions kCustomSockFuncs = {
        /*asocket=*/&CustomSockFuncs::Socket,
        /*aclose=*/&CustomSockFuncs::Close,
        /*aconnect=*/&CustomSockFuncs::Connect,
        /*arecvfrom=*/&CustomSockFuncs::RecvFrom,
        /*asendv=*/&CustomSockFuncs::WriteV,
    };
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    ares_set_socket_functions(channel, &kCustomSockFuncs, this);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    ares_set_socket_configure_callback(channel,
                                       &CustomSockFuncs::ConfigureSocket, this);
  }

  std::unique_ptr<GrpcPolledFdFactory> NewEmptyInstance() const override {
    return std::make_unique<GrpcPolledFdFactoryLibuv>(loop_worker_,
                                                      thread_pool_);
  }

 private:
  struct CustomSockFuncs {
    static ares_socket_t Socket(int af, int type, int protocol,
                                void* user_data) {
      auto* self = static_cast<GrpcPolledFdFactoryLibuv*>(user_data);
#ifdef GPR_WINDOWS
      ares_socket_t s =
          WSASocketW(af, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
#else
      int sock_type = type;
#ifdef SOCK_CLOEXEC
      sock_type |= SOCK_CLOEXEC;
#endif
      ares_socket_t s = socket(af, sock_type, protocol);
#if defined(GRPC_HAVE_SO_NOSIGPIPE) || defined(SO_NOSIGPIPE)
      if (s != ARES_SOCKET_BAD) {
        int enable = 1;
        (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
      }
#endif
#endif
      if (s == ARES_SOCKET_BAD) {
        return ARES_SOCKET_BAD;
      }
      auto polled_socket = grpc_core::MakeRefCounted<UvPolledSocket>(
          s, self->loop_worker_, self->thread_pool_);
      {
        grpc_core::MutexLock lock(self->mu_);
        self->sockets_[s] = std::move(polled_socket);
      }
      return s;
    }

    static int ConfigureSocket(ares_socket_t as, int type,
                               void* /*user_data*/) {
#ifdef GPR_WINDOWS
      u_long nonblock = 1;
      if (ioctlsocket(as, FIONBIO, &nonblock) != 0) {
        return -1;
      }
      if (type == SOCK_STREAM) {
        const char enable = 1;
        if (setsockopt(as, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) !=
            0) {
          return -1;
        }
      }
      return 0;
#else
      const int status_flags = fcntl(as, F_GETFL, 0);
      if (status_flags < 0 ||
          fcntl(as, F_SETFL, status_flags | O_NONBLOCK) != 0) {
        return -1;
      }
      const int fd_flags = fcntl(as, F_GETFD, 0);
      if (fd_flags < 0 || fcntl(as, F_SETFD, fd_flags | FD_CLOEXEC) != 0) {
        return -1;
      }
      if (type == SOCK_STREAM) {
        int enable = 1;
        if (setsockopt(as, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) !=
            0) {
          return -1;
        }
      }
      return 0;
#endif
    }

    static int Connect(ares_socket_t as, const struct sockaddr* target,
                       ares_socklen_t target_len, void* /*user_data*/) {
      return connect(as, target, target_len);
    }

    static ares_ssize_t RecvFrom(ares_socket_t as, void* data, size_t data_len,
                                 int flags, struct sockaddr* from,
                                 ares_socklen_t* from_len,
                                 void* /*user_data*/) {
#ifdef GPR_WINDOWS
      constexpr size_t kMaxWinRecvLen =
          static_cast<size_t>(std::numeric_limits<int>::max());
      const int recv_len = data_len > kMaxWinRecvLen
                               ? std::numeric_limits<int>::max()
                               : static_cast<int>(data_len);
      return recvfrom(as, static_cast<char*>(data), recv_len, flags, from,
                      from_len);
#else
      return recvfrom(as, data, data_len, flags, from, from_len);
#endif
    }

    static ares_ssize_t WriteV(ares_socket_t as, const struct iovec* iov,
                               int iovec_count, void* /*user_data*/) {
#ifdef GPR_WINDOWS
      std::vector<WSABUF> buffers;
      buffers.reserve(static_cast<size_t>(iovec_count));
      for (int i = 0; i < iovec_count; ++i) {
        WSABUF buf;
        buf.buf = static_cast<CHAR*>(iov[i].iov_base);
        buf.len = static_cast<ULONG>(iov[i].iov_len);
        buffers.push_back(buf);
      }
      DWORD bytes_sent = 0;
      if (WSASend(as, buffers.data(), static_cast<DWORD>(buffers.size()),
                  &bytes_sent, 0, nullptr, nullptr) != 0) {
        return -1;
      }
      return static_cast<ares_ssize_t>(bytes_sent);
#elif defined(GRPC_HAVE_MSG_NOSIGNAL)
      struct msghdr msg = {};
      msg.msg_iov = const_cast<struct iovec*>(iov);
      msg.msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(iovec_count);
      return sendmsg(as, &msg, MSG_NOSIGNAL);
#else
      return writev(as, iov, iovec_count);
#endif
    }

    static int Close(ares_socket_t as, void* user_data) {
      auto* self = static_cast<GrpcPolledFdFactoryLibuv*>(user_data);
      grpc_core::RefCountedPtr<UvPolledSocket> socket;
      {
        grpc_core::MutexLock lock(self->mu_);
        auto nh = self->sockets_.extract(as);
        if (!nh.empty()) {
          socket = std::move(nh.mapped());
        }
      }
      if (socket != nullptr) {
        GRPC_TRACE_LOG(cares_resolver, INFO)
            << "(EventEngine c-ares uv) deferred close on " << socket->name();
        socket->CloseAndDestroy();
        return 0;
      }
      if (as != ARES_SOCKET_BAD) {
#ifdef GPR_WINDOWS
        return closesocket(as);
#else
        return ::close(as);
#endif
      }
      return 0;
    }
  };

  UvLoopWorker* const loop_worker_;
  const std::shared_ptr<ThreadPool> thread_pool_;
  grpc_core::Mutex mu_;
  absl::flat_hash_map<ares_socket_t, grpc_core::RefCountedPtr<UvPolledSocket>>
      sockets_ ABSL_GUARDED_BY(mu_);
};

}  // namespace

std::unique_ptr<GrpcPolledFdFactory> MakeGrpcPolledFdFactoryLibuv(
    UvLoopWorker* loop_worker, std::shared_ptr<ThreadPool> thread_pool) {
  return std::make_unique<GrpcPolledFdFactoryLibuv>(loop_worker,
                                                    std::move(thread_pool));
}

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_ARES == 1
