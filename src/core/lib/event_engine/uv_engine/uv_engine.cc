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

#include "src/core/lib/event_engine/uv_engine/uv_engine.h"

#include <grpc/support/cpu.h>
#include <grpc/support/port_platform.h>
#include <uv.h>

#ifdef __linux__
#include <sched.h>
#endif

#ifndef GPR_WINDOWS
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <optional>
#include <string>
#include <utility>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/posix_engine/timer.h"
#include "src/core/lib/event_engine/ref_counted_dns_resolver_interface.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/thread_local.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/lib/event_engine/uv_engine/grpc_polled_fd_uv.h"
#include "src/core/lib/event_engine/uv_engine/native_uv_dns_resolver.h"
#include "src/core/lib/event_engine/uv_engine/uv_endpoint.h"
#include "src/core/lib/event_engine/uv_engine/uv_error_utils.h"
#include "src/core/lib/event_engine/uv_engine/uv_listener.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/env.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/useful.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
#include <pthread.h>

#include <algorithm>

#include "src/core/util/fork.h"
#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/inlined_vector.h"
#endif

namespace grpc_event_engine::experimental {

namespace {

constexpr int kMinExplicitUvLoops = 1;
constexpr int kMaxExplicitUvLoops = 64;
constexpr size_t kMinAutoUvLoops = 4;
constexpr size_t kMaxAutoUvLoops = 8;
constexpr size_t kMinCallbackThreadPoolSize = 4;
constexpr size_t kMaxCallbackThreadPoolSize = 16;

#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

template <template <typename> typename PtrType>
struct ForkHandlerPointers {
  ForkHandlerPointers(PtrType<UvEventEngine> engine, PtrType<ThreadPool> pool,
                      PtrType<TimerManager> timers)
      : event_engine(std::move(engine)),
        executor(std::move(pool)),
        timer_manager(std::move(timers)) {}

  PtrType<UvEventEngine> event_engine;
  PtrType<ThreadPool> executor;
  PtrType<TimerManager> timer_manager;
};

absl::NoDestructor<grpc_core::Mutex> g_uv_fork_mu;
absl::NoDestructor<absl::InlinedVector<ForkHandlerPointers<std::weak_ptr>, 16>>
    g_uv_fork_handlers ABSL_GUARDED_BY(g_uv_fork_mu.get());

std::vector<ForkHandlerPointers<std::shared_ptr>> LockForkHandlers() {
  grpc_core::MutexLock lock(*g_uv_fork_mu);
  std::vector<ForkHandlerPointers<std::shared_ptr>> locked;
  locked.reserve(g_uv_fork_handlers->size());
  for (const auto& [event_engine, executor, timer_manager] :
       *g_uv_fork_handlers) {
    locked.emplace_back(event_engine.lock(), executor.lock(),
                        timer_manager.lock());
  }
  return locked;
}

void PrepareFork() {
  for (const auto& [event_engine, executor, timer_manager] :
       LockForkHandlers()) {
    if (event_engine != nullptr) {
      event_engine->BeforeFork();
    }
    if (timer_manager != nullptr) {
      timer_manager->PrepareFork();
    }
    if (executor != nullptr) {
      executor->PrepareFork();
    }
  }
}

void PostForkInParent() {
  for (const auto& [event_engine, executor, timer_manager] :
       LockForkHandlers()) {
    if (executor != nullptr) {
      executor->PostFork();
    }
    if (timer_manager != nullptr) {
      timer_manager->PostFork();
    }
    if (event_engine != nullptr) {
      event_engine->AfterFork(UvEventEngine::OnForkRole::kParent);
    }
  }
}

void PostForkInChild() {
  for (const auto& [event_engine, executor, timer_manager] :
       LockForkHandlers()) {
    if (executor != nullptr) {
      executor->PostFork();
    }
    if (timer_manager != nullptr) {
      timer_manager->PostFork();
    }
    if (event_engine != nullptr) {
      event_engine->AfterFork(UvEventEngine::OnForkRole::kChild);
    }
  }
}

void RegisterEventEngineForFork(
    const std::shared_ptr<UvEventEngine>& uv_engine,
    const std::shared_ptr<ThreadPool>& executor,
    const std::shared_ptr<TimerManager>& timer_manager) {
  if (!grpc_core::Fork::Enabled()) {
    return;
  }
  grpc_core::MutexLock lock(*g_uv_fork_mu);
  g_uv_fork_handlers->erase(
      std::remove_if(g_uv_fork_handlers->begin(), g_uv_fork_handlers->end(),
                     [](const auto& ptr) {
                       return ptr.event_engine.expired() &&
                              ptr.executor.expired() &&
                              ptr.timer_manager.expired();
                     }),
      g_uv_fork_handlers->end());
  g_uv_fork_handlers->emplace_back(uv_engine, executor, timer_manager);
  static bool handlers_installed = false;
  if (!handlers_installed) {
    pthread_atfork(PrepareFork, PostForkInParent, PostForkInChild);
    handlers_installed = true;
  }
}

#else  // GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

void RegisterEventEngineForFork(
    const std::shared_ptr<UvEventEngine>& /*uv_engine*/,
    const std::shared_ptr<ThreadPool>& /*executor*/,
    const std::shared_ptr<TimerManager>& /*timer_manager*/) {}

#endif  // GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

size_t ResolveEffectiveCpuCores() {
#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  if (sched_getaffinity(0, sizeof(cpuset), &cpuset) == 0) {
    const int count = CPU_COUNT(&cpuset);
    if (count > 0) {
      return static_cast<size_t>(count);
    }
  }
#endif
  const unsigned cores = gpr_cpu_num_cores();
  return cores > 0 ? static_cast<size_t>(cores) : 1u;
}

size_t ResolveDefaultNumUvLoops() {
  std::optional<std::string> env_loops = grpc_core::GetEnv("GRPC_UV_LOOPS");
  if (env_loops.has_value() && !env_loops->empty()) {
    int parsed = 0;
    if (absl::SimpleAtoi(*env_loops, &parsed) && parsed > 0) {
      return static_cast<size_t>(
          grpc_core::Clamp(parsed, kMinExplicitUvLoops, kMaxExplicitUvLoops));
    }
  }
  const size_t cores = ResolveEffectiveCpuCores();
  if (cores <= 1) {
    return 1;
  }
  if (cores < 6) {
    return 2;
  }
  return grpc_core::Clamp<size_t>(cores / 2, kMinAutoUvLoops, kMaxAutoUvLoops);
}

class UvAresDNSResolver final : public EventEngine::DNSResolver {
 public:
  explicit UvAresDNSResolver(
      grpc_core::OrphanablePtr<RefCountedDNSResolverInterface> resolver)
      : resolver_(std::move(resolver)) {}

  void LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name,
                      absl::string_view default_port) override {
    resolver_->LookupHostname(std::move(on_resolve), name, default_port);
  }

  void LookupSRV(LookupSRVCallback on_resolve,
                 absl::string_view name) override {
    resolver_->LookupSRV(std::move(on_resolve), name);
  }

  void LookupTXT(LookupTXTCallback on_resolve,
                 absl::string_view name) override {
    resolver_->LookupTXT(std::move(on_resolve), name);
  }

 private:
  grpc_core::OrphanablePtr<RefCountedDNSResolverInterface> resolver_;
};

}  // namespace

struct UvEventEngine::TimerClosureData final : public EventEngine::Closure {
  void Run() override {
    GRPC_TRACE_LOG(event_engine, INFO)
        << "UvEventEngine:" << engine << " executing timer:" << handle;
    {
      grpc_core::MutexLock lock(engine->task_mu_);
      engine->known_handles_.erase(handle);
    }
    cb();
    delete this;
  }

  absl::AnyInvocable<void()> cb;
  Timer timer;
  UvEventEngine* engine = nullptr;
  EventEngine::TaskHandle handle = EventEngine::TaskHandle::kInvalid;
};

struct UvEventEngine::ConnectState final
    : public grpc_core::RefCounted<ConnectState> {
  struct StartCmd final : public LoopCommand {
    explicit StartCmd(ConnectState* s) : state(s) {}
    void Execute(uv_loop_t* loop) override;
    void OnRejected() override;

    ConnectState* state;
  };

  struct AbortCmd final : public LoopCommand {
    explicit AbortCmd(ConnectState* s) : state(s) {}
    void Execute(uv_loop_t* loop) override;
    void OnRejected() override;

    ConnectState* state;
  };

  ConnectState(int64_t id, UvEventEngine* eng, ConnectShard* connect_shard,
               UvLoopWorker* loop_worker, std::shared_ptr<ThreadPool> pool,
               const EventEngine::ResolvedAddress& addr,
               const PosixTcpOptions& opts, int fd,
               grpc_core::RefCountedPtr<UvSocketState> socket_state)
      : conn_id(id),
        engine_ptr(eng),
        shard(connect_shard),
        worker(loop_worker),
        thread_pool(std::move(pool)),
        target_addr(addr),
        tcp_options(opts),
        adopted_fd(fd),
        sock_state(std::move(socket_state)) {}

  ~ConnectState() override {
#ifndef GPR_WINDOWS
    if (adopted_fd >= 0) {
      close(adopted_fd);
      adopted_fd = -1;
    }
#endif
  }

  static void OnUvConnectCb(uv_connect_t* req, int status);
  void CloseSocketOnLoopIfNeeded();
  void FailWithStatusFromLoop(absl::Status err) ABSL_LOCKS_EXCLUDED(shard->mu);
  void FailFromLoop(int uv_err, absl::string_view context)
      ABSL_LOCKS_EXCLUDED(shard->mu);

  const int64_t conn_id;
  UvEventEngine* const engine_ptr;
  ConnectShard* const shard;
  UvLoopWorker* const worker;
  const std::shared_ptr<ThreadPool> thread_pool;
  const EventEngine::ResolvedAddress target_addr;
  const PosixTcpOptions tcp_options;
  int adopted_fd;
  grpc_core::RefCountedPtr<UvSocketState> sock_state;

  std::atomic<bool> aborted{false};

  // Accessed exclusively on `worker`'s uv_loop_t thread:
  uv_connect_t connect_req{};
  bool handle_initialized_on_loop = false;
  bool close_started_on_loop = false;

  StartCmd start_cmd{this};
  AbortCmd abort_cmd{this};
  grpc_core::RefCountedPtr<ConnectState> start_cmd_ref;
  grpc_core::RefCountedPtr<ConnectState> abort_cmd_ref;
};

void UvEventEngine::ConnectState::CloseSocketOnLoopIfNeeded() {
  if (!handle_initialized_on_loop || close_started_on_loop ||
      sock_state == nullptr) {
    return;
  }
  close_started_on_loop = true;
  sock_state->CloseOnLoop();
}

void UvEventEngine::ConnectState::FailWithStatusFromLoop(absl::Status err) {
  CloseSocketOnLoopIfNeeded();
  std::optional<PendingConnectEntry> claimed;
  {
    grpc_core::MutexLock lock(shard->mu);
    auto it = shard->pending.find(conn_id);
    if (it != shard->pending.end()) {
      claimed.emplace(std::move(it->second));
      shard->pending.erase(it);
    }
  }
  if (!claimed.has_value()) {
    return;
  }
  if (claimed->timeout_handle != TaskHandle::kInvalid) {
    (void)engine_ptr->Cancel(claimed->timeout_handle);
  }
  thread_pool->Run(
      [cb = std::move(claimed->on_connect), err = std::move(err),
       engine_keepalive = std::move(claimed->engine_ref)]() mutable {
        (void)engine_keepalive;
        cb(std::move(err));
      });
}

void UvEventEngine::ConnectState::FailFromLoop(int uv_err,
                                               absl::string_view context) {
  FailWithStatusFromLoop(UvErrorToStatus(uv_err, context));
}

void UvEventEngine::ConnectState::StartCmd::Execute(uv_loop_t* loop) {
  ConnectState* s = state;
  grpc_core::RefCountedPtr<ConnectState> self_ref = std::move(s->start_cmd_ref);

  if (s->aborted.load(std::memory_order_acquire)) {
#ifndef GPR_WINDOWS
    if (s->adopted_fd >= 0) {
      close(s->adopted_fd);
      s->adopted_fd = -1;
    }
#endif
    return;
  }

  s->sock_state->handle_ptr()->data = s->sock_state.get();
  if (s->sock_state->kind == UvSocketState::HandleKind::kTcp) {
    int rc = 0;
#ifndef GPR_WINDOWS
    if (s->adopted_fd >= 0) {
      rc = uv_tcp_init(loop, &s->sock_state->stream_handle.tcp);
      if (rc != 0) {
        s->FailFromLoop(rc, "uv_tcp_init");
        return;
      }
      s->sock_state->MarkHandleInitializedOnLoop();
      s->handle_initialized_on_loop = true;
      rc = uv_tcp_open(&s->sock_state->stream_handle.tcp, s->adopted_fd);
      if (rc == 0) {
        s->sock_state->raw_fd = s->adopted_fd;
        s->adopted_fd = -1;
      } else {
        s->FailFromLoop(rc, "uv_tcp_open");
        return;
      }
    } else
#endif
    {
      const unsigned int domain =
          static_cast<unsigned int>(s->target_addr.address()->sa_family);
      rc = uv_tcp_init_ex(loop, &s->sock_state->stream_handle.tcp, domain);
      if (rc != 0) {
        s->FailFromLoop(rc, "uv_tcp_init_ex");
        return;
      }
      s->sock_state->MarkHandleInitializedOnLoop();
      s->handle_initialized_on_loop = true;
    }

    absl::Status opt_status =
        ApplyUvTcpOptions(&s->sock_state->stream_handle.tcp, s->tcp_options,
                          GRPC_FD_CLIENT_CONNECTION_USAGE);
    if (!opt_status.ok()) {
      s->FailWithStatusFromLoop(std::move(opt_status));
      return;
    }

    s->connect_req.data = s->Ref().release();
    rc = uv_tcp_connect(&s->connect_req, &s->sock_state->stream_handle.tcp,
                        s->target_addr.address(), &ConnectState::OnUvConnectCb);
    if (rc != 0) {
      grpc_core::RefCountedPtr<ConnectState> reclaim(
          static_cast<ConnectState*>(s->connect_req.data));
      s->connect_req.data = nullptr;
      s->FailFromLoop(rc, "uv_tcp_connect");
    }
    return;
  }

  // Unix domain socket (AF_UNIX via uv_pipe_t)
#ifndef GPR_WINDOWS
  absl::StatusOr<UnixSocketPath> uds_path =
      ExtractUnixSocketPath(s->target_addr);
  if (!uds_path.ok()) {
    s->FailWithStatusFromLoop(uds_path.status());
    return;
  }

  int rc = uv_pipe_init(loop, &s->sock_state->stream_handle.pipe, 0);
  if (rc != 0) {
    s->FailFromLoop(rc, "uv_pipe_init");
    return;
  }
  s->sock_state->MarkHandleInitializedOnLoop();
  s->handle_initialized_on_loop = true;
  if (s->adopted_fd >= 0) {
    rc = uv_pipe_open(&s->sock_state->stream_handle.pipe, s->adopted_fd);
    if (rc == 0) {
      s->sock_state->raw_fd = s->adopted_fd;
      s->adopted_fd = -1;
    } else {
      s->FailFromLoop(rc, "uv_pipe_open");
      return;
    }
  }

  s->connect_req.data = s->Ref().release();
  rc = uv_pipe_connect2(&s->connect_req, &s->sock_state->stream_handle.pipe,
                        uds_path->path, uds_path->length, UV_PIPE_NO_TRUNCATE,
                        &ConnectState::OnUvConnectCb);
  if (rc != 0) {
    grpc_core::RefCountedPtr<ConnectState> reclaim(
        static_cast<ConnectState*>(s->connect_req.data));
    s->connect_req.data = nullptr;
    s->FailFromLoop(rc, "uv_pipe_connect2");
  }
#else
  s->FailFromLoop(UV_ENOTSUP, "AF_UNIX unsupported on Windows");
#endif
}

void UvEventEngine::ConnectState::StartCmd::OnRejected() {
  ConnectState* s = state;
  grpc_core::RefCountedPtr<ConnectState> self_ref = std::move(s->start_cmd_ref);
  std::optional<PendingConnectEntry> claimed;
  {
    grpc_core::MutexLock lock(s->shard->mu);
    auto it = s->shard->pending.find(s->conn_id);
    if (it != s->shard->pending.end()) {
      claimed.emplace(std::move(it->second));
      s->shard->pending.erase(it);
    }
  }
  if (claimed.has_value()) {
    s->thread_pool->Run(
        [cb = std::move(claimed->on_connect),
         engine_keepalive = std::move(claimed->engine_ref)]() mutable {
          (void)engine_keepalive;
          cb(absl::CancelledError("EventEngine loop shut down"));
        });
  }
}

void UvEventEngine::ConnectState::AbortCmd::Execute(uv_loop_t* /*loop*/) {
  ConnectState* s = state;
  grpc_core::RefCountedPtr<ConnectState> self_ref = std::move(s->abort_cmd_ref);
  s->CloseSocketOnLoopIfNeeded();
}

void UvEventEngine::ConnectState::AbortCmd::OnRejected() {
  ConnectState* s = state;
  grpc_core::RefCountedPtr<ConnectState> self_ref = std::move(s->abort_cmd_ref);
}

void UvEventEngine::ConnectState::OnUvConnectCb(uv_connect_t* req, int status) {
  grpc_core::RefCountedPtr<ConnectState> s(
      static_cast<ConnectState*>(req->data));
  req->data = nullptr;

  std::optional<PendingConnectEntry> claimed;
  {
    grpc_core::MutexLock lock(s->shard->mu);
    auto it = s->shard->pending.find(s->conn_id);
    if (it != s->shard->pending.end()) {
      claimed.emplace(std::move(it->second));
      s->shard->pending.erase(it);
    }
  }

  if (!claimed.has_value()) {
    s->CloseSocketOnLoopIfNeeded();
    return;
  }

  if (claimed->timeout_handle != TaskHandle::kInvalid) {
    (void)s->engine_ptr->Cancel(claimed->timeout_handle);
  }

  if (status != 0) {
    s->CloseSocketOnLoopIfNeeded();
    absl::Status err = UvErrorToStatus(status, "Connect failed");
    s->thread_pool->Run(
        [cb = std::move(claimed->on_connect), err = std::move(err),
         engine_keepalive = std::move(claimed->engine_ref)]() mutable {
          (void)engine_keepalive;
          cb(std::move(err));
        });
    return;
  }

  PopulateUvSocketAddresses(s->sock_state.get());
  auto endpoint = std::make_unique<UvEndpoint>(std::move(s->sock_state),
                                               std::move(claimed->engine_ref));
  s->thread_pool->Run(
      [cb = std::move(claimed->on_connect),
       endpoint = std::move(endpoint)]() mutable { cb(std::move(endpoint)); });
}

std::shared_ptr<UvEventEngine> UvEventEngine::MakeUvEventEngine() {
  return MakeUvEventEngine(ResolveDefaultNumUvLoops());
}

std::shared_ptr<UvEventEngine> UvEventEngine::MakeUvEventEngine(
    size_t num_loops) {
  auto engine = std::make_shared<UvEventEngine>(num_loops);
  RegisterEventEngineForFork(engine, engine->thread_pool_,
                             engine->timer_manager_);
  return engine;
}

UvEventEngine::UvEventEngine() : UvEventEngine(ResolveDefaultNumUvLoops()) {}

UvEventEngine::UvEventEngine(size_t num_loops) {
  const size_t clamped_loops = grpc_core::Clamp<size_t>(
      num_loops, static_cast<size_t>(kMinExplicitUvLoops),
      static_cast<size_t>(kMaxExplicitUvLoops));
  const size_t effective_cores = ResolveEffectiveCpuCores();
  const size_t pool_threads =
      grpc_core::Clamp<size_t>(effective_cores + 2, kMinCallbackThreadPoolSize,
                               kMaxCallbackThreadPoolSize);
  thread_pool_ = MakeThreadPool(pool_threads);
  timer_manager_ = std::make_shared<TimerManager>(thread_pool_);
  workers_.reserve(clamped_loops);
  for (size_t i = 0; i < clamped_loops; ++i) {
    workers_.push_back(std::make_unique<UvLoopWorker>(i));
  }
  VLOG(2) << "UvEventEngine initialized with " << clamped_loops
          << " libuv loops and " << pool_threads << " ThreadPool threads";
}

UvEventEngine::~UvEventEngine() {
  CancelAllPendingTimers();

  for (auto& shard : connect_shards_) {
    absl::flat_hash_map<int64_t, PendingConnectEntry> remaining;
    {
      grpc_core::MutexLock lock(shard.mu);
      remaining.swap(shard.pending);
    }
    for (auto& [conn_id, entry] : remaining) {
      (void)conn_id;
      if (entry.timeout_handle != TaskHandle::kInvalid) {
        (void)Cancel(entry.timeout_handle);
      }
      entry.state->aborted.store(true, std::memory_order_release);
      entry.state->abort_cmd_ref = entry.state;
      entry.state->worker->Enqueue(&entry.state->abort_cmd);
      thread_pool_->Run([cb = std::move(entry.on_connect)]() mutable {
        cb(absl::CancelledError("EventEngine shutting down"));
      });
    }
  }

  timer_manager_->Shutdown();
  for (auto& worker : workers_) {
    worker->Shutdown();
  }
  thread_pool_->Quiesce();
}

UvLoopWorker* UvEventEngine::SelectWorkerRoundRobin() {
  size_t idx = next_worker_idx_.fetch_add(1, std::memory_order_relaxed) %
               workers_.size();
  return workers_[idx].get();
}

absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
UvEventEngine::CreateListener(
    Listener::AcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  std::vector<UvLoopWorker*> raw_workers;
  raw_workers.reserve(workers_.size());
  for (const auto& w : workers_) {
    raw_workers.push_back(w.get());
  }
#ifndef GPR_WINDOWS
  PosixEventEngineWithFdSupport::PosixAcceptCallback posix_on_accept =
      [accept_cb = std::move(on_accept)](
          int /*listener_fd*/, std::unique_ptr<EventEngine::Endpoint> ep,
          bool /*is_external*/, MemoryAllocator allocator,
          SliceBuffer* /*pending_data*/) mutable {
        accept_cb(std::move(ep), std::move(allocator));
      };
  return std::make_unique<UvListener>(
      std::move(posix_on_accept), std::move(on_shutdown), config,
      std::move(memory_allocator_factory), std::move(raw_workers), thread_pool_,
      shared_from_this());
#else
  return std::make_unique<UvListener>(
      std::move(on_accept), std::move(on_shutdown), config,
      std::move(memory_allocator_factory), std::move(raw_workers), thread_pool_,
      shared_from_this());
#endif
}

#ifndef GPR_WINDOWS
absl::StatusOr<std::unique_ptr<EventEngine::Listener>>
UvEventEngine::CreatePosixListener(
    PosixEventEngineWithFdSupport::PosixAcceptCallback on_accept,
    absl::AnyInvocable<void(absl::Status)> on_shutdown,
    const EndpointConfig& config,
    std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory) {
  std::vector<UvLoopWorker*> raw_workers;
  raw_workers.reserve(workers_.size());
  for (const auto& w : workers_) {
    raw_workers.push_back(w.get());
  }
  return std::make_unique<UvListener>(
      std::move(on_accept), std::move(on_shutdown), config,
      std::move(memory_allocator_factory), std::move(raw_workers), thread_pool_,
      shared_from_this());
}

absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
UvEventEngine::CreatePosixEndpointFromFd(int fd, const EndpointConfig& config,
                                         MemoryAllocator memory_allocator) {
  if (fd < 0) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid fd: ", fd));
  }
  UvSocketState::HandleKind kind = UvSocketState::HandleKind::kTcp;
  sockaddr_storage ss_local{};
  socklen_t local_len = sizeof(ss_local);
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&ss_local), &local_len) ==
          0 &&
      ss_local.ss_family == AF_UNIX) {
    kind = UvSocketState::HandleKind::kPipe;
  }

  UvLoopWorker* target_worker = nullptr;
  for (const auto& w : workers_) {
    if (w->IsCurrentThread()) {
      target_worker = w.get();
      break;
    }
  }
  if (target_worker == nullptr) {
    target_worker = SelectWorkerRoundRobin();
  }

  PosixTcpOptions options = TcpOptionsFromEndpointConfig(config);
  if (!memory_allocator.IsValid()) {
    if (options.memory_allocator_factory != nullptr) {
      memory_allocator =
          options.memory_allocator_factory->CreateMemoryAllocator(
              absl::StrCat("allocator:", fd));
    } else {
      auto quota = options.resource_quota != nullptr
                       ? options.resource_quota
                       : grpc_core::ResourceQuota::Default();
      memory_allocator = quota->memory_quota()->CreateMemoryAllocator(
          absl::StrCat("allocator:", fd));
    }
  }

  std::shared_ptr<EventEngine> self_engine = shared_from_this();
  auto sock_state = grpc_core::MakeRefCounted<UvSocketState>(
      target_worker, thread_pool_, self_engine, std::move(memory_allocator),
      options, kind, ResolveUvWriteMode(config));
  absl::Status status = absl::OkStatus();
  bool fd_owned_by_uv = false;

  bool ran = target_worker->RunSync([&](uv_loop_t* loop) {
    sock_state->handle_ptr()->data = sock_state.get();
    int rc = 0;
    if (kind == UvSocketState::HandleKind::kTcp) {
      rc = uv_tcp_init(loop, &sock_state->stream_handle.tcp);
      if (rc == 0) {
        sock_state->MarkHandleInitializedOnLoop();
        rc = uv_tcp_open(&sock_state->stream_handle.tcp, fd);
      }
      if (rc == 0) {
        fd_owned_by_uv = true;
        sock_state->raw_fd = fd;
        status = ApplyUvTcpOptions(&sock_state->stream_handle.tcp, options,
                                   GRPC_FD_CLIENT_CONNECTION_USAGE);
        if (!status.ok()) {
          sock_state->CloseOnLoop();
          return;
        }
      }
    } else {
      rc = uv_pipe_init(loop, &sock_state->stream_handle.pipe, 0);
      if (rc == 0) {
        sock_state->MarkHandleInitializedOnLoop();
        rc = uv_pipe_open(&sock_state->stream_handle.pipe, fd);
      }
      if (rc == 0) {
        fd_owned_by_uv = true;
        sock_state->raw_fd = fd;
      }
    }

    if (rc != 0) {
      status = UvErrorToStatus(rc, "CreatePosixEndpointFromFd");
      sock_state->CloseOnLoop();
      return;
    }
    PopulateUvSocketAddresses(sock_state.get());
  });

  if (!ran) {
    if (!fd_owned_by_uv) {
      close(fd);
    }
    return absl::UnavailableError("UvLoopWorker shut down");
  }
  if (!status.ok()) {
    if (!fd_owned_by_uv) {
      close(fd);
    }
    return status;
  }
  return std::make_unique<UvEndpoint>(std::move(sock_state),
                                      std::move(self_engine));
}

absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>>
UvEventEngine::CreateEndpointFromFd(int fd, const EndpointConfig& config) {
  PosixTcpOptions options = TcpOptionsFromEndpointConfig(config);
  if (options.memory_allocator_factory != nullptr) {
    return CreatePosixEndpointFromFd(
        fd, config,
        options.memory_allocator_factory->CreateMemoryAllocator(
            absl::StrCat("allocator:", fd)));
  }
  auto quota = options.resource_quota != nullptr
                   ? options.resource_quota
                   : grpc_core::ResourceQuota::Default();
  return CreatePosixEndpointFromFd(fd, config,
                                   quota->memory_quota()->CreateMemoryAllocator(
                                       absl::StrCat("allocator:", fd)));
}

EventEngine::ConnectionHandle UvEventEngine::CreateEndpointFromUnconnectedFd(
    int fd, EventEngine::OnConnectCallback on_connect,
    const EventEngine::ResolvedAddress& addr, const EndpointConfig& config,
    MemoryAllocator memory_allocator, EventEngine::Duration timeout) {
  if (fd < 0) {
    Run([on_connect = std::move(on_connect)]() mutable {
      on_connect(absl::InvalidArgumentError("Invalid unconnected fd"));
    });
    return ConnectionHandle::kInvalid;
  }
  absl::Status addr_status = ValidateResolvedAddress(addr);
  if (!addr_status.ok()) {
    close(fd);
    Run([on_connect = std::move(on_connect),
         err = std::move(addr_status)]() mutable {
      on_connect(std::move(err));
    });
    return ConnectionHandle::kInvalid;
  }
  EventEngine::ResolvedAddress connect_addr = addr;
  sockaddr_storage ss_local{};
  socklen_t local_len = sizeof(ss_local);
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&ss_local), &local_len) ==
      0) {
    if (ss_local.ss_family == AF_INET6 &&
        addr.address()->sa_family == AF_INET) {
      EventEngine::ResolvedAddress v4mapped;
      if (ResolvedAddressToV4Mapped(addr, &v4mapped)) {
        connect_addr = v4mapped;
      }
    }
  }
  return ConnectInternal(fd, std::move(on_connect), connect_addr, config,
                         std::move(memory_allocator), timeout);
}
#elif defined(GRPC_WINSOCK_SOCKET)
std::unique_ptr<EventEngine::Endpoint>
UvEventEngine::CreateEndpointFromWinSocket(SOCKET socket,
                                           const EndpointConfig& config) {
  PosixTcpOptions options = TcpOptionsFromEndpointConfig(config);
  auto quota = options.resource_quota != nullptr
                   ? options.resource_quota
                   : grpc_core::ResourceQuota::Default();
  MemoryAllocator allocator =
      options.memory_allocator_factory != nullptr
          ? options.memory_allocator_factory->CreateMemoryAllocator(
                "winsock_endpoint")
          : quota->memory_quota()->CreateMemoryAllocator("winsock_endpoint");
  UvLoopWorker* target_worker = SelectWorkerRoundRobin();
  std::shared_ptr<EventEngine> self_engine = shared_from_this();
  auto sock_state = grpc_core::MakeRefCounted<UvSocketState>(
      target_worker, thread_pool_, self_engine, std::move(allocator), options,
      UvSocketState::HandleKind::kTcp, ResolveUvWriteMode(config));
  bool ok = false;
  target_worker->RunSync([&](uv_loop_t* loop) {
    sock_state->handle_ptr()->data = sock_state.get();
    if (uv_tcp_init(loop, &sock_state->stream_handle.tcp) == 0) {
      sock_state->MarkHandleInitializedOnLoop();
      if (uv_tcp_open(&sock_state->stream_handle.tcp, socket) == 0) {
        (void)ApplyUvTcpOptions(&sock_state->stream_handle.tcp, options,
                                GRPC_FD_CLIENT_CONNECTION_USAGE);
        PopulateUvSocketAddresses(sock_state.get());
        ok = true;
        return;
      }
      sock_state->CloseOnLoop();
    }
  });
  if (!ok) {
    return nullptr;
  }
  return std::make_unique<UvEndpoint>(std::move(sock_state),
                                      std::move(self_engine));
}
#endif

EventEngine::ConnectionHandle UvEventEngine::Connect(
    OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
  return ConnectInternal(/*adopted_fd=*/-1, std::move(on_connect), addr, args,
                         std::move(memory_allocator), timeout);
}

EventEngine::ConnectionHandle UvEventEngine::ConnectInternal(
    int adopted_fd, OnConnectCallback on_connect, const ResolvedAddress& addr,
    const EndpointConfig& args, MemoryAllocator memory_allocator,
    Duration timeout) {
  absl::Status addr_status = ValidateResolvedAddress(addr);
  if (!addr_status.ok()) {
#ifndef GPR_WINDOWS
    if (adopted_fd >= 0) {
      close(adopted_fd);
    }
#endif
    Run([on_connect = std::move(on_connect),
         err = std::move(addr_status)]() mutable {
      on_connect(std::move(err));
    });
    return ConnectionHandle::kInvalid;
  }

  const sa_family_t family = addr.address()->sa_family;
  UvSocketState::HandleKind kind = UvSocketState::HandleKind::kTcp;
#ifdef GRPC_HAVE_UNIX_SOCKET
  if (family == AF_UNIX) {
    kind = UvSocketState::HandleKind::kPipe;
  }
#else
  (void)family;
#endif

  PosixTcpOptions options = TcpOptionsFromEndpointConfig(args);
  UvLoopWorker* worker = SelectWorkerRoundRobin();
  std::shared_ptr<EventEngine> self_engine = shared_from_this();
  auto sock_state = grpc_core::MakeRefCounted<UvSocketState>(
      worker, thread_pool_, self_engine, std::move(memory_allocator), options,
      kind, ResolveUvWriteMode(args));

  const int64_t conn_id = next_conn_id_.fetch_add(1, std::memory_order_relaxed);
  ConnectShard* shard =
      &connect_shards_[static_cast<size_t>(conn_id) % connect_shards_.size()];

  auto state = grpc_core::MakeRefCounted<ConnectState>(
      conn_id, this, shard, worker, thread_pool_, addr, options, adopted_fd,
      std::move(sock_state));

  {
    grpc_core::MutexLock lock(shard->mu);
    PendingConnectEntry entry;
    entry.state = state;
    entry.on_connect = std::move(on_connect);
    entry.engine_ref = std::move(self_engine);
    shard->pending.emplace(conn_id, std::move(entry));
  }

  // Enqueue StartCmd BEFORE scheduling timeout or returning ConnectionHandle so
  // StartCmd strictly precedes any AbortCmd on `worker`'s FIFO queue.
  state->start_cmd_ref = state;
  if (!worker->Enqueue(&state->start_cmd)) {
    return ConnectionHandle::kInvalid;
  }

  std::weak_ptr<UvEventEngine> weak_self =
      std::static_pointer_cast<UvEventEngine>(shared_from_this());
  TaskHandle th =
      RunAfter(timeout, [weak_self = std::move(weak_self), conn_id]() {
        if (auto self = weak_self.lock()) {
          self->OnConnectTimeout(conn_id);
        }
      });
  if (th != TaskHandle::kInvalid) {
    bool cancel_immediately = false;
    {
      grpc_core::MutexLock lock(shard->mu);
      auto it = shard->pending.find(conn_id);
      if (it != shard->pending.end()) {
        it->second.timeout_handle = th;
      } else {
        cancel_immediately = true;
      }
    }
    if (cancel_immediately) {
      (void)Cancel(th);
    }
  }

  return ConnectionHandle{static_cast<intptr_t>(conn_id), 0};
}

void UvEventEngine::OnConnectTimeout(int64_t conn_id) {
  ConnectShard* shard =
      &connect_shards_[static_cast<size_t>(conn_id) % connect_shards_.size()];
  std::optional<PendingConnectEntry> claimed;
  {
    grpc_core::MutexLock lock(shard->mu);
    auto it = shard->pending.find(conn_id);
    if (it != shard->pending.end()) {
      claimed.emplace(std::move(it->second));
      shard->pending.erase(it);
    }
  }
  if (!claimed.has_value()) {
    return;
  }
  claimed->state->aborted.store(true, std::memory_order_release);
  claimed->state->abort_cmd_ref = claimed->state;
  claimed->state->worker->Enqueue(&claimed->state->abort_cmd);

  thread_pool_->Run(
      [cb = std::move(claimed->on_connect),
       engine_keepalive = std::move(claimed->engine_ref)]() mutable {
        (void)engine_keepalive;
        cb(absl::DeadlineExceededError("Connect timed out"));
      });
}

bool UvEventEngine::CancelConnect(ConnectionHandle handle) {
  const int64_t conn_id = static_cast<int64_t>(handle.keys[0]);
  if (conn_id <= 0 || handle.keys[1] != 0) {
    return false;
  }
  ConnectShard* shard =
      &connect_shards_[static_cast<size_t>(conn_id) % connect_shards_.size()];
  std::optional<PendingConnectEntry> claimed;
  {
    grpc_core::MutexLock lock(shard->mu);
    auto it = shard->pending.find(conn_id);
    if (it != shard->pending.end()) {
      claimed.emplace(std::move(it->second));
      shard->pending.erase(it);
    }
  }
  if (!claimed.has_value()) {
    return false;
  }

  if (claimed->timeout_handle != TaskHandle::kInvalid) {
    (void)Cancel(claimed->timeout_handle);
  }
  claimed->state->aborted.store(true, std::memory_order_release);
  claimed->state->abort_cmd_ref = claimed->state;
  claimed->state->worker->Enqueue(&claimed->state->abort_cmd);

  // Destroy `on_connect` synchronously outside `shard->mu` BEFORE returning
  // true (`NotifyOnDelete` contract).
  claimed->on_connect = nullptr;
  claimed->engine_ref.reset();
  return true;
}

bool UvEventEngine::IsWorkerThread() {
  if (ThreadLocal::IsEventEngineThread() ||
      TimerManager::IsTimerManagerThread()) {
    return true;
  }
  for (const auto& w : workers_) {
    if (w->IsCurrentThread()) {
      return true;
    }
  }
  return false;
}

#if GRPC_ARES == 1
void UvEventEngine::RegisterAresResolverForFork(
    GRPC_UNUSED AresResolver* resolver) {
#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
  grpc_core::MutexLock lock(resolver_handles_mu_);
  resolver_handles_.emplace_back(resolver->GetReinitHandle());
  auto new_end = std::remove_if(
      resolver_handles_.begin(), resolver_handles_.end(),
      +[](const std::weak_ptr<AresResolver::ReinitHandle>& callback) {
        return callback.expired();
      });
  resolver_handles_.erase(new_end, resolver_handles_.end());
#endif
}
#endif

absl::StatusOr<std::unique_ptr<EventEngine::DNSResolver>>
UvEventEngine::GetDNSResolver(const DNSResolver::ResolverOptions& options) {
#if GRPC_ARES == 1
  if (ShouldUseAresDnsResolver()) {
    GRPC_TRACE_LOG(event_engine_dns, INFO)
        << "UvEventEngine:" << this << " creating AresResolver";
    auto ares_resolver = AresResolver::CreateAresResolver(
        options.dns_server,
        MakeGrpcPolledFdFactoryLibuv(SelectWorkerRoundRobin(), thread_pool_),
        shared_from_this());
    if (!ares_resolver.ok()) {
      return ares_resolver.status();
    }
    RegisterAresResolverForFork(ares_resolver->get());
    return std::make_unique<UvAresDNSResolver>(std::move(*ares_resolver));
  }
#endif
  // Note: `PosixEventEngine::GetDNSResolver` (`posix_engine.cc:592-596`) also
  // falls back to the OS `getaddrinfo` resolver without failing when
  // `!options.dns_server.empty()`, because shared conformance test
  // `EventEngineDNSTest::StressTestQueryARecordWithNameDeletion`
  // (`dns_test.cc:419`) omits `SKIP_TEST_FOR_NATIVE_DNS_RESOLVER()` while
  // passing a non-empty `options.dns_server`. Log at VLOG(2) for diagnostics.
  if (!options.dns_server.empty()) {
    VLOG(2) << "UvEventEngine::GetDNSResolver ignoring custom dns_server='"
            << options.dns_server
            << "' because NativeUvDNSResolver uses OS getaddrinfo";
  }
  GRPC_TRACE_LOG(event_engine_dns, INFO)
      << "UvEventEngine:" << this << " creating NativeUvDNSResolver";
  return std::make_unique<NativeUvDNSResolver>(
      SelectWorkerRoundRobin(), thread_pool_, shared_from_this());
}

void UvEventEngine::Run(Closure* closure) { thread_pool_->Run(closure); }

void UvEventEngine::Run(absl::AnyInvocable<void()> closure) {
  thread_pool_->Run(std::move(closure));
}

EventEngine::TaskHandle UvEventEngine::RunAfter(Duration when,
                                                Closure* closure) {
  return RunAfterInternal(when, [closure]() { closure->Run(); });
}

EventEngine::TaskHandle UvEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> closure) {
  return RunAfterInternal(when, std::move(closure));
}

EventEngine::TaskHandle UvEventEngine::RunAfterInternal(
    Duration when, absl::AnyInvocable<void()> cb) {
  if (when <= Duration::zero()) {
    Run(std::move(cb));
    return TaskHandle::kInvalid;
  }
  auto when_ts = ToTimestamp(timer_manager_->Now(), when);
  auto* cd = new TimerClosureData();
  cd->cb = std::move(cb);
  cd->engine = this;
  EventEngine::TaskHandle handle{
      reinterpret_cast<intptr_t>(cd),
      aba_token_.fetch_add(1, std::memory_order_relaxed)};
  {
    grpc_core::MutexLock lock(task_mu_);
    if (!disallow_new_timers_) {
      known_handles_.insert(handle);
      cd->handle = handle;
      timer_manager_->TimerInit(&cd->timer, when_ts, cd);
      return handle;
    }
  }
  delete cd;
  return TaskHandle::kInvalid;
}

UvEventEngine::TimerClosureData* UvEventEngine::CancelTimerLocked(
    TaskHandle handle) {
  if (!known_handles_.contains(handle)) {
    return nullptr;
  }
  auto* cd = reinterpret_cast<TimerClosureData*>(handle.keys[0]);
  bool cancelled = timer_manager_->TimerCancel(&cd->timer);
  known_handles_.erase(handle);
  return cancelled ? cd : nullptr;
}

bool UvEventEngine::Cancel(TaskHandle handle) {
  TimerClosureData* to_delete = nullptr;
  {
    grpc_core::MutexLock lock(task_mu_);
    to_delete = CancelTimerLocked(handle);
  }
  if (to_delete != nullptr) {
    delete to_delete;
    return true;
  }
  return false;
}

void UvEventEngine::CancelAllPendingTimers() {
  std::vector<TimerClosureData*> to_delete;
  {
    grpc_core::MutexLock lock(task_mu_);
    auto pending = known_handles_;
    to_delete.reserve(pending.size());
    for (const auto& handle : pending) {
      TimerClosureData* cd = CancelTimerLocked(handle);
      if (cd != nullptr) {
        to_delete.push_back(cd);
      }
    }
    disallow_new_timers_ = true;
    GRPC_CHECK(known_handles_.empty());
  }
  for (TimerClosureData* cd : to_delete) {
    delete cd;
  }
}

#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

void UvEventEngine::BeforeFork() {
  for (auto& worker : workers_) {
    worker->PrepareFork();
  }
}

void UvEventEngine::AfterFork(OnForkRole on_fork_role) {
  if (on_fork_role == OnForkRole::kChild) {
    AfterForkInChild();
    return;
  }
  for (auto& worker : workers_) {
    worker->PostforkParent();
  }
}

void UvEventEngine::AfterForkInChild() {
#if GRPC_ARES == 1
  absl::InlinedVector<std::shared_ptr<AresResolver::ReinitHandle>, 10>
      ares_resolvers;
  absl::Cleanup restart_resolvers = [&]() {
    for (const auto& resolver : ares_resolvers) {
      resolver->Restart();
    }
  };
  {
    grpc_core::MutexLock lock(resolver_handles_mu_);
    for (const auto& cb : resolver_handles_) {
      auto locked = cb.lock();
      if (locked != nullptr) {
        locked->Reset(absl::CancelledError("Reset resolver on fork"));
        ares_resolvers.emplace_back(std::move(locked));
      }
    }
  }
#endif
  // Abort and close all in-flight parent `Connect()` attempts in the child so
  // `uv_loop_fork` does not retain parent connect watchers or FDs.
  std::vector<PendingConnectEntry> inherited_connects;
  for (ConnectShard& shard : connect_shards_) {
    grpc_core::MutexLock lock(shard.mu);
    for (auto& [conn_id, entry] : shard.pending) {
      inherited_connects.push_back(std::move(entry));
    }
    shard.pending.clear();
  }
  for (PendingConnectEntry& entry : inherited_connects) {
    if (entry.timeout_handle != TaskHandle::kInvalid) {
      (void)Cancel(entry.timeout_handle);
    }
    entry.state->aborted.store(true, std::memory_order_release);
    entry.state->abort_cmd_ref = entry.state;
    entry.state->worker->Enqueue(&entry.state->abort_cmd);
    thread_pool_->Run(
        [cb = std::move(entry.on_connect),
         engine_keepalive = std::move(entry.engine_ref)]() mutable {
          (void)engine_keepalive;
          cb(absl::CancelledError("Closed on fork"));
        });
  }

  for (auto& worker : workers_) {
    worker->PostforkChild();
  }
}

#endif  // GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK

}  // namespace grpc_event_engine::experimental
