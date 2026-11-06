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

#include "src/core/lib/event_engine/uv_engine/uv_listener.h"

#include <grpc/support/port_platform.h>
#include <uv.h>

#ifndef GPR_WINDOWS
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/uv_engine/uv_endpoint.h"
#include "src/core/lib/event_engine/uv_engine/uv_error_utils.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/strerror.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_event_engine::experimental {

namespace {

constexpr int kDefaultListenBacklog = 4096;
constexpr int kMaxEphemeralPortRetries = 3;
constexpr uint32_t kIpv4LinkLocalMask = 0xFFFF0000u;
constexpr uint32_t kIpv4LinkLocalPrefix = 0xA9FE0000u;
constexpr uint8_t kIpv6LinkLocalByte0 = 0xFE;
constexpr uint8_t kIpv6LinkLocalByte1Mask = 0xC0;
constexpr uint8_t kIpv6LinkLocalByte1Prefix = 0x80;

bool IsLinkLocalAddress(const sockaddr* addr) {
  if (addr->sa_family == AF_INET) {
    const auto* a4 = reinterpret_cast<const sockaddr_in*>(addr);
    return (ntohl(a4->sin_addr.s_addr) & kIpv4LinkLocalMask) ==
           kIpv4LinkLocalPrefix;
  }
  if (addr->sa_family == AF_INET6) {
    const auto* a6 = reinterpret_cast<const sockaddr_in6*>(addr);
    return a6->sin6_addr.s6_addr[0] == kIpv6LinkLocalByte0 &&
           (a6->sin6_addr.s6_addr[1] & kIpv6LinkLocalByte1Mask) ==
               kIpv6LinkLocalByte1Prefix;
  }
  return false;
}

absl::Status ApplyListenerFdOptions(uv_handle_t* handle,
                                    const PosixTcpOptions& options,
                                    int* out_fd) {
#ifndef GPR_WINDOWS
  uv_os_fd_t raw_fd = -1;
  if (uv_fileno(handle, &raw_fd) == 0 && raw_fd >= 0) {
    *out_fd = static_cast<int>(raw_fd);
    return ApplyPosixSocketMutatorAndDscp(raw_fd, options,
                                          GRPC_FD_SERVER_LISTENER_USAGE);
  }
#else
  (void)handle;
  (void)options;
  *out_fd = -1;
#endif
  return absl::OkStatus();
}

struct TempAcceptHandle {
  explicit TempAcceptHandle(UvSocketState::HandleKind handle_kind)
      : kind(handle_kind) {}

  static void OnCloseCb(uv_handle_t* handle) {
    auto* self = static_cast<TempAcceptHandle*>(handle->data);
#ifndef GPR_WINDOWS
    if (self->raw_fd >= 0 && self->raw_fd <= STDERR_FILENO) {
      ::close(self->raw_fd);
    }
#endif
    delete self;
  }

  UvSocketState::HandleKind kind;
  int raw_fd = -1;
  union HandleUnion {
    HandleUnion() : tcp{} {}
    ~HandleUnion() {}
    uv_tcp_t tcp;
    uv_pipe_t pipe;
    uv_stream_t stream;
    uv_handle_t handle;
  } h;
};

// Drains `server->accepted_fd` into a short-lived handle and immediately closes
// it so libuv (`uv__server_io`) never disables `POLLIN` or leaks an unaccepted
// FD when `OnUvConnectionCb` exits early.
void DrainAcceptedConnection(uv_stream_t* server, uv_loop_t* loop,
                             UvSocketState::HandleKind kind) {
  auto* temp = new TempAcceptHandle(kind);
  temp->h.handle.data = temp;
  int rc = (kind == UvSocketState::HandleKind::kTcp)
               ? uv_tcp_init(loop, &temp->h.tcp)
               : uv_pipe_init(loop, &temp->h.pipe, 0);
  if (rc != 0) {
    delete temp;
    return;
  }
  if (uv_accept(server, &temp->h.stream) == 0) {
#ifndef GPR_WINDOWS
    uv_os_fd_t fd = -1;
    if (uv_fileno(&temp->h.handle, &fd) == 0) {
      temp->raw_fd = static_cast<int>(fd);
    }
#endif
  }
  uv_close(&temp->h.handle, &TempAcceptHandle::OnCloseCb);
}

}  // namespace

struct UvListener::ServerSocket final : public LoopForkable {
  struct CloseCmd final : public LoopCommand {
    explicit CloseCmd(ServerSocket* s) : sock(s) {}
    void Execute(uv_loop_t* /*loop*/) override;
    void OnRejected() override;

    ServerSocket* sock;
  };

  ServerSocket(UvLoopWorker* worker, UvSocketState::HandleKind handle_kind,
               grpc_core::RefCountedPtr<ListenerState> listener)
      : owner_worker(worker),
        kind(handle_kind),
        listener_ref(std::move(listener)) {}

  void OnForkInChild(uv_loop_t* loop) override;
  static void OnUvConnectionCb(uv_stream_t* server, int status);
  static void OnServerCloseCb(uv_handle_t* handle);

  UvLoopWorker* const owner_worker;
  const UvSocketState::HandleKind kind;
  grpc_core::RefCountedPtr<ListenerState> listener_ref;

  union HandleUnion {
    HandleUnion() : tcp{} {}
    ~HandleUnion() {}
    uv_tcp_t tcp;
    uv_pipe_t pipe;
    uv_stream_t stream;
    uv_handle_t handle;
  } server_handle;

  EventEngine::ResolvedAddress bound_addr;
  int port = 0;
  int listen_fd = -1;
  bool is_v6only = false;
  bool closed_on_loop = false;
  CloseCmd close_cmd{this};
};

struct UvListener::ListenerState final
    : public grpc_core::RefCounted<ListenerState> {
#ifndef GPR_WINDOWS
  class AttachAcceptedFdCmd final : public LoopCommand {
   public:
    AttachAcceptedFdCmd(grpc_core::RefCountedPtr<ListenerState> listener,
                        UvLoopWorker* worker, int listen_fd, int accepted_fd,
                        UvSocketState::HandleKind kind, bool is_external,
                        SliceBuffer pending_data)
        : listener_(std::move(listener)),
          worker_(worker),
          enqueue_fork_gen_(worker->fork_generation()),
          listen_fd_(listen_fd),
          fd_(accepted_fd),
          kind_(kind),
          is_external_(is_external),
          pending_data_(std::move(pending_data)) {}

    void Execute(uv_loop_t* loop) override {
      std::unique_ptr<AttachAcceptedFdCmd> self(this);
      auto engine = listener_->weak_engine.lock();
      absl::Cleanup drop_engine_off_loop = [&]() {
        SafeDropEngineRefFromLoop(std::move(engine), listener_->thread_pool);
      };
      if (engine == nullptr || listener_->IsShuttingDown() ||
          worker_->fork_generation() != enqueue_fork_gen_) {
        if (fd_ >= 0) {
          close(fd_);
          fd_ = -1;
        }
        return;
      }

      auto state = grpc_core::MakeRefCounted<UvSocketState>(
          worker_, listener_->thread_pool, engine, MemoryAllocator(),
          listener_->options, kind_, listener_->write_mode);
      state->handle_ptr()->data = state.get();

      int rc = 0;
      if (kind_ == UvSocketState::HandleKind::kTcp) {
        rc = uv_tcp_init(loop, &state->stream_handle.tcp);
        if (rc == 0) {
          state->MarkHandleInitializedOnLoop();
          rc = uv_tcp_open(&state->stream_handle.tcp, fd_);
          if (rc == 0) {
            state->raw_fd = fd_;
            fd_ = -1;  // Owned by uv_tcp_t now.
          }
        }
      } else {
        rc = uv_pipe_init(loop, &state->stream_handle.pipe, 0);
        if (rc == 0) {
          state->MarkHandleInitializedOnLoop();
          rc = uv_pipe_open(&state->stream_handle.pipe, fd_);
          if (rc == 0) {
            state->raw_fd = fd_;
            fd_ = -1;  // Owned by uv_pipe_t now.
          }
        }
      }

      if (rc != 0) {
        if (fd_ >= 0) {
          close(fd_);
          fd_ = -1;
        }
        state->CloseOnLoop();
        return;
      }

      if (kind_ == UvSocketState::HandleKind::kTcp) {
        if (!is_external_) {
          absl::Status opt_status =
              ApplyUvTcpOptions(&state->stream_handle.tcp, listener_->options,
                                GRPC_FD_SERVER_CONNECTION_USAGE);
          if (!opt_status.ok()) {
            LOG(ERROR) << "Failed to apply TCP server connection options: "
                       << opt_status;
            state->CloseOnLoop();
            return;
          }
        } else {
          (void)uv_tcp_nodelay(&state->stream_handle.tcp, 1);
        }
      } else if (!is_external_ &&
                 listener_->options.socket_mutator != nullptr) {
        uv_os_fd_t raw_fd = -1;
        if (uv_fileno(state->handle_ptr(), &raw_fd) == 0 && raw_fd >= 0) {
          PosixTcpOptions mutator_only = listener_->options;
          mutator_only.dscp = PosixTcpOptions::kDscpNotSet;
          if (!ApplyPosixSocketMutatorAndDscp(raw_fd, mutator_only,
                                              GRPC_FD_SERVER_CONNECTION_USAGE)
                   .ok()) {
            state->CloseOnLoop();
            return;
          }
        }
      }

      PopulateUvSocketAddresses(state.get());
      auto peer_uri = ResolvedAddressToURI(state->peer_address);
      std::string peer_str = peer_uri.ok() ? *peer_uri : "<unknown>";
      const absl::string_view prefix = is_external_ ? "external:" : "";
      {
        grpc_core::MutexLock lock(state->alloc_mu);
        state->memory_allocator =
            listener_->memory_allocator_factory->CreateMemoryAllocator(
                absl::StrCat(prefix,
                             "endpoint-tcp-server-connection: ", peer_str));
      }
      MemoryAllocator accept_allocator =
          listener_->memory_allocator_factory->CreateMemoryAllocator(
              absl::StrCat(prefix,
                           "on-accept-tcp-server-connection: ", peer_str));
      auto endpoint =
          std::make_unique<UvEndpoint>(std::move(state), std::move(engine));

      grpc_core::RefCountedPtr<ListenerState> listener_keepalive = listener_;
      int listen_fd = listen_fd_;
      bool is_external = is_external_;
      listener_->thread_pool->Run(
          [listener_keepalive = std::move(listener_keepalive), listen_fd,
           endpoint = std::move(endpoint), is_external,
           accept_allocator = std::move(accept_allocator),
           pending = std::move(pending_data_)]() mutable {
            SliceBuffer* pending_ptr =
                pending.Length() > 0 ? &pending : nullptr;
            listener_keepalive->on_accept(
                listen_fd, std::move(endpoint), is_external,
                std::move(accept_allocator), pending_ptr);
          });
    }

    void OnRejected() override {
      if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
      }
      delete this;
    }

   private:
    grpc_core::RefCountedPtr<ListenerState> listener_;
    UvLoopWorker* const worker_;
    const uint64_t enqueue_fork_gen_;
    const int listen_fd_;
    int fd_;
    const UvSocketState::HandleKind kind_;
    const bool is_external_;
    SliceBuffer pending_data_;
  };
#endif  // !GPR_WINDOWS

  ListenerState(UvAcceptCallback accept_cb,
                absl::AnyInvocable<void(absl::Status)> shutdown_cb,
                const EndpointConfig& config,
                std::unique_ptr<MemoryAllocatorFactory> allocator_factory,
                std::vector<UvLoopWorker*> loop_workers,
                std::shared_ptr<ThreadPool> pool,
                std::weak_ptr<EventEngine> engine_weak)
      : workers(std::move(loop_workers)),
        thread_pool(std::move(pool)),
        weak_engine(std::move(engine_weak)),
        options(TcpOptionsFromEndpointConfig(config)),
        write_mode(ResolveUvWriteMode(config)),
        on_accept(std::move(accept_cb)),
        on_shutdown(std::move(shutdown_cb)),
        memory_allocator_factory(std::move(allocator_factory)) {}

  ~ListenerState() override {
    if (on_shutdown != nullptr) {
      thread_pool->Run(
          [cb = std::move(on_shutdown)]() mutable { cb(absl::OkStatus()); });
    }
  }

  bool IsShuttingDown() const {
    return shutting_down.load(std::memory_order_acquire);
  }

  UvLoopWorker* PickAcceptWorker() {
    size_t idx = next_accept_worker.fetch_add(1, std::memory_order_relaxed) %
                 workers.size();
    return workers[idx];
  }

  void CloseSocketAsync(ServerSocket* sock) {
    sock->owner_worker->Enqueue(&sock->close_cmd);
  }

#ifndef GPR_WINDOWS
  using BindFdCallback = PosixListenerWithFdSupport::OnPosixBindNewFdCallback;
#else
  using BindFdCallback = absl::AnyInvocable<void(absl::StatusOr<int>)>;
#endif

  absl::StatusOr<int> BindInternal(const EventEngine::ResolvedAddress& addr,
                                   BindFdCallback on_bind_new_fd)
      ABSL_LOCKS_EXCLUDED(mu);
  absl::StatusOr<int> BindUnixSocketLocked(
      const EventEngine::ResolvedAddress& addr, BindFdCallback* on_bind_new_fd)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu);
  absl::StatusOr<int> BindSingleTcpAddressLocked(
      const EventEngine::ResolvedAddress& addr, BindFdCallback* on_bind_new_fd,
      bool* out_is_v6only) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu);
  absl::Status BindOneTcpShardOnWorker(UvLoopWorker* worker,
                                       const EventEngine::ResolvedAddress& addr,
                                       unsigned int bind_flags,
                                       ServerSocket** out_sock,
                                       int* out_uv_err);

  const std::vector<UvLoopWorker*> workers;
  const std::shared_ptr<ThreadPool> thread_pool;
  const std::weak_ptr<EventEngine> weak_engine;
  const PosixTcpOptions options;
  const UvWriteMode write_mode;
  UvAcceptCallback on_accept;
  absl::AnyInvocable<void(absl::Status)> on_shutdown;
  std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory;
  std::atomic<size_t> next_accept_worker{0};
  std::atomic<bool> shutting_down{false};

  mutable grpc_core::Mutex mu;
  bool started ABSL_GUARDED_BY(mu) = false;
  int bound_port ABSL_GUARDED_BY(mu) = 0;
  std::vector<ServerSocket*> sockets ABSL_GUARDED_BY(mu);
};

void UvListener::ServerSocket::CloseCmd::Execute(uv_loop_t* /*loop*/) {
  sock->closed_on_loop = true;
  if (!uv_is_closing(&sock->server_handle.handle)) {
#ifndef GPR_WINDOWS
    if (sock->listen_fd < 0) {
      uv_os_fd_t fd = -1;
      if (uv_fileno(&sock->server_handle.handle, &fd) == 0) {
        sock->listen_fd = static_cast<int>(fd);
      }
    }
#endif
    uv_close(&sock->server_handle.handle, &ServerSocket::OnServerCloseCb);
  }
}

void UvListener::ServerSocket::CloseCmd::OnRejected() { delete sock; }

void UvListener::ServerSocket::OnForkInChild(uv_loop_t* /*loop*/) {
  listener_ref->shutting_down.store(true, std::memory_order_release);
  {
    grpc_core::MutexLock lock(listener_ref->mu);
    listener_ref->sockets.erase(std::remove(listener_ref->sockets.begin(),
                                            listener_ref->sockets.end(), this),
                                listener_ref->sockets.end());
  }
  closed_on_loop = true;
  if (!uv_is_closing(&server_handle.handle)) {
#ifndef GPR_WINDOWS
    if (kind == UvSocketState::HandleKind::kPipe &&
        server_handle.pipe.pipe_fname != nullptr) {
      // Do NOT unlink the parent process's live Unix domain socket file when
      // closing the inherited listener handle in the child process.
      std::free(const_cast<char*>(server_handle.pipe.pipe_fname));
      server_handle.pipe.pipe_fname = nullptr;
    }
    if (listen_fd < 0) {
      uv_os_fd_t fd = -1;
      if (uv_fileno(&server_handle.handle, &fd) == 0) {
        listen_fd = static_cast<int>(fd);
      }
    }
#endif
    uv_close(&server_handle.handle, &ServerSocket::OnServerCloseCb);
  }
}

void UvListener::ServerSocket::OnServerCloseCb(uv_handle_t* handle) {
  auto* sock = static_cast<ServerSocket*>(handle->data);
  sock->owner_worker->UnregisterForkable(sock);
#ifndef GPR_WINDOWS
  // Note: for AF_UNIX (`uv_pipe_t`), `libuv`'s `uv__pipe_close` already unlinks
  // `pipe_fname` synchronously inside `uv_close` before closing the FD. We only
  // close FDs <= STDERR_FILENO here because `uv__stream_close` skips them.
  if (sock->listen_fd >= 0 && sock->listen_fd <= STDERR_FILENO) {
    ::close(sock->listen_fd);
    sock->listen_fd = -1;
  }
#endif
  delete sock;
}

void UvListener::ServerSocket::OnUvConnectionCb(uv_stream_t* server,
                                                int status) {
  auto* sock = static_cast<ServerSocket*>(server->data);
  if (status < 0) {
    char err_buf[128];
    VLOG(2) << "uv_listen connection error: "
            << uv_strerror_r(status, err_buf, sizeof(err_buf));
    return;
  }
  uv_loop_t* loop = sock->owner_worker->loop();
  if (sock->closed_on_loop || sock->listener_ref->IsShuttingDown()) {
    DrainAcceptedConnection(server, loop, sock->kind);
    return;
  }

  ListenerState* listener = sock->listener_ref.get();
  UvLoopWorker* target_worker = sock->owner_worker;
#ifndef GPR_WINDOWS
  // Cold fallback when `SO_REUSEPORT` is disabled or unsupported (`Mode B`):
  // a single listening socket accepts on `owner_worker`, duplicates the FD
  // above `STDERR_FILENO`, closes the temporary handle, and hands `dup_fd` to
  // `target_worker` round-robin via `AttachAcceptedFdCmd`. When `SO_REUSEPORT`
  // is enabled (`Mode A`), each worker owns its own shard and `target_worker ==
  // sock->owner_worker` takes the zero-hop fast path below.
  if (sock->kind == UvSocketState::HandleKind::kTcp &&
      listener->workers.size() > 1) {
    target_worker = listener->PickAcceptWorker();
  }
  if (target_worker != sock->owner_worker) {
    auto* temp_tcp = new TempAcceptHandle(UvSocketState::HandleKind::kTcp);
    temp_tcp->h.handle.data = temp_tcp;
    if (uv_tcp_init(loop, &temp_tcp->h.tcp) != 0) {
      delete temp_tcp;
      DrainAcceptedConnection(server, loop, sock->kind);
      return;
    }
    if (uv_accept(server, &temp_tcp->h.stream) != 0) {
      uv_close(&temp_tcp->h.handle, &TempAcceptHandle::OnCloseCb);
      return;
    }
    uv_os_fd_t raw_fd = -1;
    int dup_fd = -1;
    if (uv_fileno(&temp_tcp->h.handle, &raw_fd) == 0 && raw_fd >= 0) {
      temp_tcp->raw_fd = static_cast<int>(raw_fd);
      dup_fd = fcntl(raw_fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    }
    uv_close(&temp_tcp->h.handle, &TempAcceptHandle::OnCloseCb);
    if (dup_fd < 0) {
      VLOG(2) << "fcntl(F_DUPFD_CLOEXEC) failed on accepted socket: "
              << grpc_core::StrError(errno);
      return;
    }
    target_worker->Enqueue(new ListenerState::AttachAcceptedFdCmd(
        sock->listener_ref, target_worker, sock->listen_fd, dup_fd,
        UvSocketState::HandleKind::kTcp, /*is_external=*/false, SliceBuffer()));
    return;
  }
#endif

  auto engine = listener->weak_engine.lock();
  if (engine == nullptr) {
    DrainAcceptedConnection(server, loop, sock->kind);
    return;
  }
  absl::Cleanup drop_engine_off_loop = [&]() {
    SafeDropEngineRefFromLoop(std::move(engine), listener->thread_pool);
  };

  auto state = grpc_core::MakeRefCounted<UvSocketState>(
      sock->owner_worker, listener->thread_pool, engine, MemoryAllocator(),
      listener->options, sock->kind, listener->write_mode);
  state->handle_ptr()->data = state.get();

  int rc = (sock->kind == UvSocketState::HandleKind::kTcp)
               ? uv_tcp_init(loop, &state->stream_handle.tcp)
               : uv_pipe_init(loop, &state->stream_handle.pipe, 0);
  if (rc != 0) {
    DrainAcceptedConnection(server, loop, sock->kind);
    return;
  }
  state->MarkHandleInitializedOnLoop();

  if (uv_accept(server, state->stream_ptr()) != 0) {
    state->CloseOnLoop();
    return;
  }

  if (sock->kind == UvSocketState::HandleKind::kTcp) {
    absl::Status opt_status =
        ApplyUvTcpOptions(&state->stream_handle.tcp, listener->options,
                          GRPC_FD_SERVER_CONNECTION_USAGE);
    if (!opt_status.ok()) {
      LOG(ERROR) << "Failed to apply TCP server connection options: "
                 << opt_status;
      state->CloseOnLoop();
      return;
    }
  } else {
#ifndef GPR_WINDOWS
    if (listener->options.socket_mutator != nullptr) {
      uv_os_fd_t raw_fd = -1;
      if (uv_fileno(state->handle_ptr(), &raw_fd) == 0 && raw_fd >= 0) {
        PosixTcpOptions mutator_only = listener->options;
        mutator_only.dscp = PosixTcpOptions::kDscpNotSet;
        if (!ApplyPosixSocketMutatorAndDscp(raw_fd, mutator_only,
                                            GRPC_FD_SERVER_CONNECTION_USAGE)
                 .ok()) {
          state->CloseOnLoop();
          return;
        }
      }
    }
#endif
  }

  PopulateUvSocketAddresses(state.get());
  auto peer_uri = ResolvedAddressToURI(state->peer_address);
  std::string peer_str = peer_uri.ok() ? *peer_uri : "<unknown>";
  {
    grpc_core::MutexLock lock(state->alloc_mu);
    state->memory_allocator =
        listener->memory_allocator_factory->CreateMemoryAllocator(
            absl::StrCat("endpoint-tcp-server-connection: ", peer_str));
  }
  MemoryAllocator accept_allocator =
      listener->memory_allocator_factory->CreateMemoryAllocator(
          absl::StrCat("on-accept-tcp-server-connection: ", peer_str));
  auto endpoint =
      std::make_unique<UvEndpoint>(std::move(state), std::move(engine));

  grpc_core::RefCountedPtr<ListenerState> listener_keepalive =
      sock->listener_ref;
  int listen_fd = sock->listen_fd;
  listener->thread_pool->Run(
      [listener_keepalive = std::move(listener_keepalive), listen_fd,
       endpoint = std::move(endpoint),
       accept_allocator = std::move(accept_allocator)]() mutable {
#ifndef GPR_WINDOWS
        listener_keepalive->on_accept(listen_fd, std::move(endpoint),
                                      /*is_external=*/false,
                                      std::move(accept_allocator),
                                      /*pending_data=*/nullptr);
#else
        (void)listen_fd;
        listener_keepalive->on_accept(std::move(endpoint),
                                      std::move(accept_allocator));
#endif
      });
}

absl::Status UvListener::ListenerState::BindOneTcpShardOnWorker(
    UvLoopWorker* worker, const EventEngine::ResolvedAddress& addr,
    unsigned int bind_flags, ServerSocket** out_sock, int* out_uv_err) {
  *out_sock = nullptr;
  *out_uv_err = 0;

  auto* candidate =
      new ServerSocket(worker, UvSocketState::HandleKind::kTcp, Ref());
  absl::Status status = absl::OkStatus();
  int uv_err = 0;

  bool ran = worker->RunSync([&](uv_loop_t* loop) {
    candidate->server_handle.handle.data = candidate;
    const unsigned int domain =
        static_cast<unsigned int>(addr.address()->sa_family);
    int rc = uv_tcp_init_ex(loop, &candidate->server_handle.tcp, domain);
    if (rc != 0) {
      uv_err = rc;
      status = UvErrorToStatus(rc, "uv_tcp_init_ex");
      delete candidate;
      candidate = nullptr;
      return;
    }
    worker->RegisterForkable(candidate);

    absl::Status opt_status = ApplyListenerFdOptions(
        &candidate->server_handle.handle, options, &candidate->listen_fd);
    if (!opt_status.ok()) {
      status = opt_status;
      uv_close(&candidate->server_handle.handle,
               &ServerSocket::OnServerCloseCb);
      candidate = nullptr;
      return;
    }

    rc = uv_tcp_bind(&candidate->server_handle.tcp, addr.address(), bind_flags);
    if (rc != 0) {
      uv_err = rc;
      status = UvErrorToStatus(rc, "uv_tcp_bind");
      uv_close(&candidate->server_handle.handle,
               &ServerSocket::OnServerCloseCb);
      candidate = nullptr;
      return;
    }

    sockaddr_storage ss{};
    int ss_len = sizeof(ss);
    rc = uv_tcp_getsockname(&candidate->server_handle.tcp,
                            reinterpret_cast<sockaddr*>(&ss), &ss_len);
    if (rc != 0 || ss_len < static_cast<int>(sizeof(sa_family_t))) {
      uv_err = (rc != 0) ? rc : UV_EINVAL;
      status = UvErrorToStatus(uv_err, "uv_tcp_getsockname");
      uv_close(&candidate->server_handle.handle,
               &ServerSocket::OnServerCloseCb);
      candidate = nullptr;
      return;
    }

    const socklen_t clamped_len =
        std::min<socklen_t>(static_cast<socklen_t>(ss_len),
                            EventEngine::ResolvedAddress::MAX_SIZE_BYTES);
    candidate->bound_addr = EventEngine::ResolvedAddress(
        reinterpret_cast<const sockaddr*>(&ss), clamped_len);
    candidate->port = ResolvedAddressGetPort(candidate->bound_addr);
#if !defined(GPR_WINDOWS) && defined(IPV6_V6ONLY)
    if (addr.address()->sa_family == AF_INET6 && candidate->listen_fd >= 0) {
      int v6only = 0;
      socklen_t optlen = sizeof(v6only);
      if (getsockopt(candidate->listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only,
                     &optlen) == 0) {
        candidate->is_v6only = (v6only != 0);
      }
    }
#endif
  });

  if (!ran) {
    delete candidate;
    return absl::UnavailableError("UvLoopWorker shut down during Bind");
  }
  *out_uv_err = uv_err;
  if (!status.ok()) {
    return status;
  }
  *out_sock = candidate;
  return absl::OkStatus();
}

absl::StatusOr<int> UvListener::ListenerState::BindSingleTcpAddressLocked(
    const EventEngine::ResolvedAddress& addr, BindFdCallback* on_bind_new_fd,
    bool* out_is_v6only) {
  if (out_is_v6only != nullptr) {
    *out_is_v6only = false;
  }

#if !defined(GPR_WINDOWS) && (UV_VERSION_HEX >= ((1 << 16) | (49 << 8)))
  const int requested_port = ResolvedAddressGetPort(addr);
  const bool want_reuseport =
      options.allow_reuse_port && IsSocketReusePortSupported();

  if (want_reuseport) {
    const int max_attempts = (requested_port == 0 && workers.size() > 1)
                                 ? kMaxEphemeralPortRetries
                                 : 1;
    bool fallback_to_mode_b = false;
    absl::Status last_err = absl::OkStatus();

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
      std::vector<ServerSocket*> shard_socks;
      shard_socks.reserve(workers.size());

      ServerSocket* s0 = nullptr;
      int uv_err0 = 0;
      absl::Status st0 = BindOneTcpShardOnWorker(
          workers[0], addr, UV_TCP_REUSEPORT, &s0, &uv_err0);
      if (!st0.ok()) {
        if (uv_err0 == UV_ENOTSUP || uv_err0 == UV_EINVAL) {
          fallback_to_mode_b = true;
          break;
        }
        return st0;
      }
      shard_socks.push_back(s0);

      EventEngine::ResolvedAddress shard_addr = addr;
      ResolvedAddressSetPort(shard_addr, s0->port);

      bool collision_on_ephemeral = false;
      last_err = absl::OkStatus();
      for (size_t i = 1; i < workers.size(); ++i) {
        ServerSocket* si = nullptr;
        int uv_erri = 0;
        absl::Status sti = BindOneTcpShardOnWorker(
            workers[i], shard_addr, UV_TCP_REUSEPORT, &si, &uv_erri);
        if (!sti.ok()) {
          last_err = sti;
          if (requested_port == 0 && uv_erri == UV_EADDRINUSE) {
            collision_on_ephemeral = true;
          } else if (uv_erri == UV_ENOTSUP || uv_erri == UV_EINVAL) {
            fallback_to_mode_b = true;
          }
          break;
        }
        shard_socks.push_back(si);
      }

      if (shard_socks.size() == workers.size()) {
        const int assigned_port = shard_socks[0]->port;
        if (out_is_v6only != nullptr) {
          *out_is_v6only = shard_socks[0]->is_v6only;
        }
        for (ServerSocket* s : shard_socks) {
          sockets.push_back(s);
          if (on_bind_new_fd != nullptr && *on_bind_new_fd != nullptr) {
            (*on_bind_new_fd)(s->listen_fd);
          }
        }
        return assigned_port;
      }

      for (ServerSocket* s : shard_socks) {
        CloseSocketAsync(s);
      }
      if (fallback_to_mode_b) {
        break;
      }
      if (!collision_on_ephemeral) {
        return last_err;
      }
    }

    if (!fallback_to_mode_b) {
      return last_err.ok()
                 ? absl::UnavailableError("Failed to bind SO_REUSEPORT shards")
                 : last_err;
    }
  }
#endif

  // Mode B: single listening socket on a designated worker, dispatching
  // accepted connections round-robin across workers[0..N-1].
  UvLoopWorker* owner = workers[sockets.size() % workers.size()];
  ServerSocket* sock = nullptr;
  int uv_err = 0;
  absl::Status st =
      BindOneTcpShardOnWorker(owner, addr, /*bind_flags=*/0, &sock, &uv_err);
  if (!st.ok()) {
    return st;
  }
  if (out_is_v6only != nullptr) {
    *out_is_v6only = sock->is_v6only;
  }
  const int assigned_port = sock->port;
  sockets.push_back(sock);
  if (on_bind_new_fd != nullptr && *on_bind_new_fd != nullptr) {
    (*on_bind_new_fd)(sock->listen_fd);
  }
  return assigned_port;
}

absl::StatusOr<int> UvListener::ListenerState::BindUnixSocketLocked(
    const EventEngine::ResolvedAddress& addr, BindFdCallback* on_bind_new_fd) {
#ifndef GPR_WINDOWS
  absl::StatusOr<UnixSocketPath> uds_path = ExtractUnixSocketPath(addr);
  if (!uds_path.ok()) {
    return uds_path.status();
  }
  if (uds_path->path[0] != '\0') {
    UnlinkIfUnixDomainSocket(addr);
  }

  UvLoopWorker* owner = workers[sockets.size() % workers.size()];
  auto* candidate =
      new ServerSocket(owner, UvSocketState::HandleKind::kPipe, Ref());
  absl::Status status = absl::OkStatus();

  bool ran = owner->RunSync([&](uv_loop_t* loop) {
    candidate->server_handle.handle.data = candidate;
    int rc = uv_pipe_init(loop, &candidate->server_handle.pipe, 0);
    if (rc != 0) {
      status = UvErrorToStatus(rc, "uv_pipe_init");
      delete candidate;
      candidate = nullptr;
      return;
    }
    owner->RegisterForkable(candidate);

    rc = uv_pipe_bind2(&candidate->server_handle.pipe, uds_path->path,
                       uds_path->length, UV_PIPE_NO_TRUNCATE);
    if (rc != 0) {
      status = UvErrorToStatus(rc, "uv_pipe_bind2");
      uv_os_fd_t fd = -1;
      if (uv_fileno(&candidate->server_handle.handle, &fd) == 0) {
        candidate->listen_fd = static_cast<int>(fd);
      }
      uv_close(&candidate->server_handle.handle,
               &ServerSocket::OnServerCloseCb);
      candidate = nullptr;
      return;
    }

    absl::Status opt_status = ApplyListenerFdOptions(
        &candidate->server_handle.handle, options, &candidate->listen_fd);
    if (!opt_status.ok()) {
      status = opt_status;
      uv_close(&candidate->server_handle.handle,
               &ServerSocket::OnServerCloseCb);
      candidate = nullptr;
      return;
    }

    candidate->bound_addr = addr;
    candidate->port = 1;
  });

  if (!ran) {
    delete candidate;
    return absl::UnavailableError("UvLoopWorker shut down during UDS Bind");
  }
  if (!status.ok()) {
    return status;
  }
  sockets.push_back(candidate);
  if (on_bind_new_fd != nullptr && *on_bind_new_fd != nullptr) {
    (*on_bind_new_fd)(candidate->listen_fd);
  }
  return 1;
#else
  (void)addr;
  (void)on_bind_new_fd;
  return absl::UnimplementedError("Unix domain sockets unsupported on Windows");
#endif
}

absl::StatusOr<int> UvListener::ListenerState::BindInternal(
    const EventEngine::ResolvedAddress& addr, BindFdCallback on_bind_new_fd) {
  absl::Status addr_status = ValidateResolvedAddress(addr);
  if (!addr_status.ok()) {
    return addr_status;
  }

  grpc_core::MutexLock lock(mu);
  if (started || IsShuttingDown()) {
    return absl::FailedPreconditionError(
        "Listener is already started or shutting down");
  }

#ifdef GRPC_HAVE_UNIX_SOCKET
  if (addr.address()->sa_family == AF_UNIX) {
    return BindUnixSocketLocked(addr, &on_bind_new_fd);
  }
#endif

  EventEngine::ResolvedAddress res_addr = addr;
  int requested_port = ResolvedAddressGetPort(res_addr);
  if (requested_port == 0 && bound_port > 0) {
    requested_port = bound_port;
    ResolvedAddressSetPort(res_addr, requested_port);
  }

  std::optional<int> wildcard_port = MaybeGetWildcardPortFromAddress(res_addr);
  if (wildcard_port.has_value()) {
    int port_to_bind = *wildcard_port;
    if (options.expand_wildcard_addrs) {
      uv_interface_address_t* ifaddrs = nullptr;
      int ifcount = 0;
      if (uv_interface_addresses(&ifaddrs, &ifcount) == 0 && ifcount > 0) {
        int assigned_port = port_to_bind;
        int bound_count = 0;
        for (int i = 0; i < ifcount; ++i) {
          const sockaddr* if_sa =
              reinterpret_cast<const sockaddr*>(&ifaddrs[i].address);
          if (if_sa->sa_family != AF_INET && if_sa->sa_family != AF_INET6) {
            continue;
          }
          if (IsLinkLocalAddress(if_sa)) {
            continue;
          }
          socklen_t sa_len = (if_sa->sa_family == AF_INET)
                                 ? sizeof(sockaddr_in)
                                 : sizeof(sockaddr_in6);
          EventEngine::ResolvedAddress if_addr(if_sa, sa_len);
          ResolvedAddressSetPort(if_addr, assigned_port);
          auto res =
              BindSingleTcpAddressLocked(if_addr, &on_bind_new_fd, nullptr);
          if (res.ok()) {
            assigned_port = *res;
            ++bound_count;
          }
        }
        uv_free_interface_addresses(ifaddrs, ifcount);
        if (bound_count > 0) {
          if (bound_port == 0 && assigned_port > 0) {
            bound_port = assigned_port;
          }
          return assigned_port;
        }
      } else if (ifaddrs != nullptr) {
        uv_free_interface_addresses(ifaddrs, ifcount);
      }
    }

    int assigned_port = 0;
    absl::Status v6_err = absl::UnavailableError("IPv6 unavailable");
    bool try_v4 = true;
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
    const bool ipv6_ok = IsIpv6LoopbackAvailable();
#else
    const bool ipv6_ok = true;
#endif
    if (ipv6_ok) {
      EventEngine::ResolvedAddress wild6 =
          ResolvedAddressMakeWild6(port_to_bind);
      bool is_v6only = false;
      auto v6_res =
          BindSingleTcpAddressLocked(wild6, &on_bind_new_fd, &is_v6only);
      if (v6_res.ok()) {
        assigned_port = *v6_res;
        port_to_bind = assigned_port;
        try_v4 = is_v6only;
      } else {
        v6_err = v6_res.status();
      }
    }
    absl::Status v4_err = absl::OkStatus();
    if (try_v4) {
      EventEngine::ResolvedAddress wild4 =
          ResolvedAddressMakeWild4(port_to_bind);
      auto v4_res = BindSingleTcpAddressLocked(wild4, &on_bind_new_fd, nullptr);
      if (v4_res.ok()) {
        assigned_port = *v4_res;
      } else {
        v4_err = v4_res.status();
      }
    }
    if (assigned_port > 0) {
      if (bound_port == 0) {
        bound_port = assigned_port;
      }
      return assigned_port;
    }
    return absl::FailedPreconditionError(
        absl::StrCat("Failed to bind wildcard listener: v6=", v6_err.message(),
                     " v4=", v4_err.message()));
  }

  // Non-wildcard TCP bind: try v4-mapped IPv6 first when dual-stack IPv6 is
  // available, falling back to native AF_INET on failure.
#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
  const bool ipv6_available = IsIpv6LoopbackAvailable();
#else
  const bool ipv6_available = true;
#endif
  EventEngine::ResolvedAddress v4mapped;
  if (ipv6_available && ResolvedAddressToV4Mapped(res_addr, &v4mapped)) {
    auto res6 = BindSingleTcpAddressLocked(v4mapped, &on_bind_new_fd, nullptr);
    if (res6.ok()) {
      if (bound_port == 0 && *res6 > 0) {
        bound_port = *res6;
      }
      return *res6;
    }
  }

  auto res = BindSingleTcpAddressLocked(res_addr, &on_bind_new_fd, nullptr);
  if (res.ok() && bound_port == 0 && *res > 0) {
    bound_port = *res;
  }
  return res;
}

UvListener::UvListener(
    UvAcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory,
    std::vector<UvLoopWorker*> workers, std::shared_ptr<ThreadPool> thread_pool,
    std::shared_ptr<EventEngine> engine)
    : engine_(std::move(engine)),
      state_(grpc_core::MakeRefCounted<ListenerState>(
          std::move(on_accept), std::move(on_shutdown), config,
          std::move(memory_allocator_factory), std::move(workers),
          std::move(thread_pool), engine_)) {}

UvListener::~UvListener() { TriggerShutdown(); }

void UvListener::TriggerShutdown() {
  if (shutdown_called_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  state_->shutting_down.store(true, std::memory_order_release);
  std::vector<ServerSocket*> to_close;
  {
    grpc_core::MutexLock lock(state_->mu);
    to_close.swap(state_->sockets);
  }
  for (ServerSocket* s : to_close) {
    state_->CloseSocketAsync(s);
  }
}

absl::StatusOr<int> UvListener::Bind(const EventEngine::ResolvedAddress& addr) {
  return state_->BindInternal(addr, nullptr);
}

#ifndef GPR_WINDOWS
absl::StatusOr<int> UvListener::BindWithFd(
    const EventEngine::ResolvedAddress& addr,
    PosixListenerWithFdSupport::OnPosixBindNewFdCallback on_bind_new_fd) {
  return state_->BindInternal(addr, std::move(on_bind_new_fd));
}

absl::Status UvListener::HandleExternalConnection(int listener_fd, int fd,
                                                  SliceBuffer* pending_data) {
  if (listener_fd < 0) {
    return absl::UnknownError(absl::StrCat(
        "HandleExternalConnection: Invalid listener socket: ", listener_fd));
  }
  if (fd < 0) {
    return absl::UnknownError(
        absl::StrCat("HandleExternalConnection: Invalid peer socket: ", fd));
  }
  sockaddr_storage ss_peer{};
  socklen_t peer_len = sizeof(ss_peer);
  if (getpeername(fd, reinterpret_cast<sockaddr*>(&ss_peer), &peer_len) < 0) {
    int err = errno;
    close(fd);
    return absl::UnknownError(
        absl::StrCat("HandleExternalConnection: peer not connected: ",
                     grpc_core::StrError(err)));
  }

  UvSocketState::HandleKind kind = UvSocketState::HandleKind::kTcp;
  sockaddr_storage ss_local{};
  socklen_t local_len = sizeof(ss_local);
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&ss_local), &local_len) ==
          0 &&
      ss_local.ss_family == AF_UNIX) {
    kind = UvSocketState::HandleKind::kPipe;
  }

  // Synchronously swap caller's stack-allocated SliceBuffer before returning!
  SliceBuffer owned_pending;
  if (pending_data != nullptr && pending_data->Length() > 0) {
    owned_pending.Swap(*pending_data);
  }

  UvLoopWorker* target_worker = state_->PickAcceptWorker();
  target_worker->Enqueue(new ListenerState::AttachAcceptedFdCmd(
      state_, target_worker, listener_fd, fd, kind, /*is_external=*/true,
      std::move(owned_pending)));
  return absl::OkStatus();
}

void UvListener::ShutdownListeningFds() { TriggerShutdown(); }
#endif

absl::Status UvListener::Start() {
  std::vector<ServerSocket*> failed_rollback;
  absl::Status start_err = absl::OkStatus();
  {
    grpc_core::MutexLock lock(state_->mu);
    if (state_->started || state_->IsShuttingDown()) {
      return absl::FailedPreconditionError(
          "Listener is already started or shutting down");
    }
    state_->started = true;

    for (ServerSocket* s : state_->sockets) {
      absl::Status listen_status = absl::OkStatus();
      bool ran = s->owner_worker->RunSync([&](uv_loop_t* /*loop*/) {
        int rc = uv_listen(&s->server_handle.stream, kDefaultListenBacklog,
                           &ServerSocket::OnUvConnectionCb);
        if (rc != 0) {
          listen_status = UvErrorToStatus(rc, "uv_listen");
        }
      });
      if (!ran) {
        start_err =
            absl::UnavailableError("UvLoopWorker shut down during Start");
        break;
      }
      if (!listen_status.ok()) {
        start_err = listen_status;
        break;
      }
    }

    if (!start_err.ok()) {
      state_->shutting_down.store(true, std::memory_order_release);
      failed_rollback.swap(state_->sockets);
    }
  }
  for (ServerSocket* s : failed_rollback) {
    state_->CloseSocketAsync(s);
  }
  return start_err;
}

}  // namespace grpc_event_engine::experimental
