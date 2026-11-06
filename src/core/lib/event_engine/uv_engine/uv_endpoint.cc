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

#include "src/core/lib/event_engine/uv_engine/uv_endpoint.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

#ifndef GPR_WINDOWS
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/uv_engine/uv_error_utils.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/env.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/useful.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_event_engine::experimental {

namespace {

constexpr size_t kDefaultHighWatermark = 2 * 1024 * 1024;
constexpr size_t kMinHighWatermarkBytes = 64 * 1024;
constexpr size_t kMaxFrameSizeHintBytes = 16 * 1024 * 1024;
constexpr size_t kMaxStagedReadSlices = 256;
constexpr size_t kStepDownReadThreshold = 16 * 1024;
constexpr size_t kMinTailBytes = 512;
// Maximum framed byte length for inline ::sendmsg in latency/blend mode.
// Sized at 512 KiB so 256 KiB RPC payloads plus gRPC + HTTP/2 frame headers
// (~263 KiB) remain on the zero-hop inline fast path, while >= 1 MiB bulk
// transfers offload directly to UvLoopWorker in O(1) without stalling callers.
constexpr size_t kMaxInlineWriteBytes = 512 * 1024;
// Safe per-buffer and per-batch write caps so `uv_buf_init(..., unsigned int)`
// and `int uv_try_write(...)` / `uv__write` can never truncate >4 GiB slices or
// sign-flip >=2 GiB write return counts.
constexpr size_t kMaxUvBufChunk = 16 * 1024 * 1024;
constexpr size_t kMaxSingleUvWriteBytes = 1024 * 1024 * 1024;
constexpr size_t kMaxSingleUvWriteBufs = 1024;

#ifndef GPR_WINDOWS
constexpr int kDscpShift = 2;
constexpr int kEcnBitsMask = 0x3;

#if defined(IOV_MAX)
constexpr size_t kMaxWriteIov = IOV_MAX < 1024 ? IOV_MAX : 1024;
#elif defined(UIO_MAXIOV)
constexpr size_t kMaxWriteIov = UIO_MAXIOV < 1024 ? UIO_MAXIOV : 1024;
#else
constexpr size_t kMaxWriteIov = 1024;
#endif
#endif

struct ResolvedReadChunkBounds {
  size_t min_alloc;
  size_t init_alloc;
  size_t max_alloc;
};

ResolvedReadChunkBounds ResolveReadChunkBounds(const PosixTcpOptions& options) {
  if (options.tcp_read_chunk_size == PosixTcpOptions::kDefaultReadChunkSize &&
      options.tcp_min_read_chunk_size ==
          PosixTcpOptions::kDefaultMinReadChunksize &&
      options.tcp_max_read_chunk_size ==
          PosixTcpOptions::kDefaultMaxReadChunksize) {
    return {UvSocketState::kDefaultMinReadAlloc,
            UvSocketState::kDefaultInitReadAlloc,
            UvSocketState::kDefaultMaxReadAlloc};
  }
  const size_t min_alloc = static_cast<size_t>(grpc_core::Clamp(
      options.tcp_min_read_chunk_size, 1, PosixTcpOptions::kMaxChunkSize));
  const size_t max_alloc = std::max(
      min_alloc,
      static_cast<size_t>(grpc_core::Clamp(options.tcp_max_read_chunk_size, 1,
                                           PosixTcpOptions::kMaxChunkSize)));
  const size_t init_alloc = grpc_core::Clamp(
      static_cast<size_t>(std::max(1, options.tcp_read_chunk_size)), min_alloc,
      max_alloc);
  return {min_alloc, init_alloc, max_alloc};
}

size_t ResolveHighWatermark(const PosixTcpOptions& options) {
  if (options.tcp_max_read_buffer_size > 0) {
    return std::max<size_t>(
        kMinHighWatermarkBytes,
        static_cast<size_t>(options.tcp_max_read_buffer_size));
  }
  return kDefaultHighWatermark;
}

size_t ComputeMinProgressBytes(const EventEngine::Endpoint::ReadArgs& args) {
  if (grpc_core::IsTcpFrameSizeTuningEnabled()) {
    return static_cast<size_t>(grpc_core::Clamp<int64_t>(
        args.read_hint_bytes(), 1, kMaxFrameSizeHintBytes));
  }
  return 1u;
}

size_t BuildNextUvWriteBatch(SliceBuffer& slices,
                             absl::InlinedVector<uv_buf_t, 64>* bufs) {
  bufs->clear();
  bufs->reserve(std::min<size_t>(slices.Count(), kMaxSingleUvWriteBufs));
  size_t batch_bytes = 0;
  for (size_t i = 0; i < slices.Count(); ++i) {
    const Slice& sl = slices[i];
    size_t offset = 0;
    while (offset < sl.size() && bufs->size() < kMaxSingleUvWriteBufs &&
           batch_bytes < kMaxSingleUvWriteBytes) {
      const size_t remaining_slice = sl.size() - offset;
      const size_t remaining_batch = kMaxSingleUvWriteBytes - batch_bytes;
      const size_t chunk =
          std::min({remaining_slice, kMaxUvBufChunk, remaining_batch});
      bufs->push_back(uv_buf_init(
          const_cast<char*>(reinterpret_cast<const char*>(sl.data()) + offset),
          static_cast<unsigned int>(chunk)));
      offset += chunk;
      batch_bytes += chunk;
    }
    if (bufs->size() >= kMaxSingleUvWriteBufs ||
        batch_bytes >= kMaxSingleUvWriteBytes) {
      break;
    }
  }
  return batch_bytes;
}

void NormalizeMaybeV4Mapped(EventEngine::ResolvedAddress* addr) {
  EventEngine::ResolvedAddress v4;
  if (ResolvedAddressIsV4Mapped(*addr, &v4)) {
    *addr = v4;
  }
}

class EmptyMetricsSet final : public EventEngine::Endpoint::MetricsSet {
 public:
  bool IsSet(size_t /*key*/) const override { return false; }
};

class UvEndpointTelemetryInfo final
    : public EventEngine::Endpoint::TelemetryInfo {
 public:
  std::vector<size_t> AllWriteMetrics() const override { return {}; }
  std::optional<absl::string_view> GetMetricName(
      size_t /*key*/) const override {
    return std::nullopt;
  }
  std::optional<size_t> GetMetricKey(
      absl::string_view /*name*/) const override {
    return std::nullopt;
  }
  std::shared_ptr<EventEngine::Endpoint::MetricsSet> GetMetricsSet(
      absl::Span<const size_t> /*keys*/) const override {
    return std::make_shared<EmptyMetricsSet>();
  }
  std::shared_ptr<EventEngine::Endpoint::MetricsSet> GetFullMetricsSet()
      const override {
    return std::make_shared<EmptyMetricsSet>();
  }
};

}  // namespace

absl::Status ValidateResolvedAddress(const EventEngine::ResolvedAddress& addr) {
  if (addr.address() == nullptr || addr.size() < sizeof(sa_family_t) ||
      addr.size() > EventEngine::ResolvedAddress::MAX_SIZE_BYTES) {
    return absl::InvalidArgumentError("Invalid ResolvedAddress size");
  }
  const int family = addr.address()->sa_family;
  if (family == AF_INET) {
    if (addr.size() < sizeof(sockaddr_in)) {
      return absl::InvalidArgumentError("Truncated AF_INET ResolvedAddress");
    }
    return absl::OkStatus();
  }
  if (family == AF_INET6) {
    if (addr.size() < sizeof(sockaddr_in6)) {
      return absl::InvalidArgumentError("Truncated AF_INET6 ResolvedAddress");
    }
    return absl::OkStatus();
  }
#if !defined(GPR_WINDOWS) && defined(GRPC_HAVE_UNIX_SOCKET)
  if (family == AF_UNIX) {
    return ExtractUnixSocketPath(addr).status();
  }
#endif
  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported address family: ", family));
}

#ifndef GPR_WINDOWS
absl::StatusOr<UnixSocketPath> ExtractUnixSocketPath(
    const EventEngine::ResolvedAddress& addr) {
  if (addr.address() == nullptr ||
      addr.size() > EventEngine::ResolvedAddress::MAX_SIZE_BYTES ||
      addr.size() <= offsetof(sockaddr_un, sun_path)) {
    return absl::InvalidArgumentError("Invalid AF_UNIX ResolvedAddress size");
  }
  if (addr.address()->sa_family != AF_UNIX) {
    return absl::InvalidArgumentError("Address family is not AF_UNIX");
  }
  const auto* un = reinterpret_cast<const sockaddr_un*>(addr.address());
  const size_t max_path_bytes = std::min<size_t>(
      sizeof(un->sun_path), addr.size() - offsetof(sockaddr_un, sun_path));
  if (un->sun_path[0] == '\0') {
    return UnixSocketPath{un->sun_path, max_path_bytes};
  }
  const size_t len = strnlen(un->sun_path, max_path_bytes);
  if (len == 0) {
    return absl::InvalidArgumentError("Empty AF_UNIX socket path");
  }
  return UnixSocketPath{un->sun_path, len};
}

absl::Status ApplyPosixSocketMutatorAndDscp(uv_os_fd_t fd,
                                            const PosixTcpOptions& options,
                                            grpc_fd_usage usage) {
  if (fd < 0) {
    return absl::OkStatus();
  }
  if (options.dscp != PosixTcpOptions::kDscpNotSet) {
    const int newval = options.dscp << kDscpShift;
    int val = 0;
    socklen_t intlen = sizeof(val);
    if (getsockopt(fd, IPPROTO_IP, IP_TOS, &val, &intlen) == 0) {
      int ip_tos = newval | (val & kEcnBitsMask);
      (void)setsockopt(fd, IPPROTO_IP, IP_TOS, &ip_tos, sizeof(ip_tos));
    }
#ifdef IPV6_TCLASS
    intlen = sizeof(val);
    if (getsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &val, &intlen) == 0) {
      int tclass = newval | (val & kEcnBitsMask);
      (void)setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &tclass, sizeof(tclass));
    }
#endif
  }
  if (options.socket_mutator != nullptr) {
    if (!grpc_socket_mutator_mutate_fd(options.socket_mutator, fd, usage)) {
      return absl::InternalError("grpc_socket_mutator_mutate_fd failed");
    }
  }
  return absl::OkStatus();
}
#endif

absl::Status ApplyUvTcpOptions(uv_tcp_t* tcp, const PosixTcpOptions& options,
                               grpc_fd_usage usage) {
  (void)uv_tcp_nodelay(tcp, 1);
  if (options.keep_alive_time_ms > 0) {
    unsigned int delay_sec = static_cast<unsigned int>(
        std::max(1, options.keep_alive_time_ms / 1000));
    (void)uv_tcp_keepalive(tcp, 1, delay_sec);
  }
  if (options.tcp_receive_buffer_size > 0) {
    int rcv_buf = options.tcp_receive_buffer_size;
    (void)uv_recv_buffer_size(reinterpret_cast<uv_handle_t*>(tcp), &rcv_buf);
  }
#ifndef GPR_WINDOWS
  uv_os_fd_t raw_fd = -1;
  if (uv_fileno(reinterpret_cast<const uv_handle_t*>(tcp), &raw_fd) == 0 &&
      raw_fd >= 0) {
#if defined(__linux__) && defined(TCP_USER_TIMEOUT)
    if (options.keep_alive_timeout_ms > 0) {
      int timeout_ms = options.keep_alive_timeout_ms;
      (void)setsockopt(raw_fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_ms,
                       sizeof(timeout_ms));
    }
#endif
    return ApplyPosixSocketMutatorAndDscp(raw_fd, options, usage);
  }
#else
  (void)usage;
#endif
  return absl::OkStatus();
}

UvWriteMode ResolveUvWriteMode(const EndpointConfig& config) {
  std::optional<std::string> env_mode = grpc_core::GetEnv("GRPC_UV_WRITE_MODE");
  if (env_mode.has_value() && !env_mode->empty()) {
    if (absl::EqualsIgnoreCase(*env_mode, "batch")) {
      return UvWriteMode::kBatchOnLoop;
    }
    if (absl::EqualsIgnoreCase(*env_mode, "inline")) {
      return UvWriteMode::kInlineAuto;
    }
  }
  std::optional<absl::string_view> opt_target =
      config.GetString(GRPC_ARG_OPTIMIZATION_TARGET);
  if (opt_target.has_value() &&
      absl::EqualsIgnoreCase(*opt_target, "throughput")) {
    return UvWriteMode::kBatchOnLoop;
  }
  return UvWriteMode::kInlineAuto;
}

void PopulateUvSocketAddresses(UvSocketState* state) {
  uv_os_fd_t fd = -1;
  if (uv_fileno(state->handle_ptr(), &fd) == 0) {
#ifndef GPR_WINDOWS
    state->raw_fd = static_cast<int>(fd);
    state->wrapped_fd.store(state->raw_fd, std::memory_order_release);
#ifdef SO_NOSIGPIPE
    if (state->raw_fd >= 0) {
      int enable = 1;
      (void)setsockopt(state->raw_fd, SOL_SOCKET, SO_NOSIGPIPE, &enable,
                       sizeof(enable));
    }
#endif
#endif
  }

  if (state->kind == UvSocketState::HandleKind::kTcp) {
    sockaddr_storage ss_local{};
    int local_len = sizeof(ss_local);
    if (uv_tcp_getsockname(&state->stream_handle.tcp,
                           reinterpret_cast<sockaddr*>(&ss_local),
                           &local_len) == 0 &&
        local_len >= static_cast<int>(sizeof(sa_family_t))) {
      const socklen_t clamped_len =
          std::min<socklen_t>(static_cast<socklen_t>(local_len),
                              EventEngine::ResolvedAddress::MAX_SIZE_BYTES);
      state->local_address = EventEngine::ResolvedAddress(
          reinterpret_cast<const sockaddr*>(&ss_local), clamped_len);
      NormalizeMaybeV4Mapped(&state->local_address);
    }
    sockaddr_storage ss_peer{};
    int peer_len = sizeof(ss_peer);
    if (uv_tcp_getpeername(&state->stream_handle.tcp,
                           reinterpret_cast<sockaddr*>(&ss_peer),
                           &peer_len) == 0 &&
        peer_len >= static_cast<int>(sizeof(sa_family_t))) {
      const socklen_t clamped_len =
          std::min<socklen_t>(static_cast<socklen_t>(peer_len),
                              EventEngine::ResolvedAddress::MAX_SIZE_BYTES);
      state->peer_address = EventEngine::ResolvedAddress(
          reinterpret_cast<const sockaddr*>(&ss_peer), clamped_len);
      NormalizeMaybeV4Mapped(&state->peer_address);
    }
  } else {
#ifndef GPR_WINDOWS
    if (state->raw_fd >= 0) {
      sockaddr_storage ss_local{};
      socklen_t local_len = sizeof(ss_local);
      if (getsockname(state->raw_fd, reinterpret_cast<sockaddr*>(&ss_local),
                      &local_len) == 0 &&
          local_len >= sizeof(sa_family_t)) {
        const socklen_t clamped_len = std::min<socklen_t>(
            local_len, EventEngine::ResolvedAddress::MAX_SIZE_BYTES);
        state->local_address = EventEngine::ResolvedAddress(
            reinterpret_cast<const sockaddr*>(&ss_local), clamped_len);
      }
      sockaddr_storage ss_peer{};
      socklen_t peer_len = sizeof(ss_peer);
      if (getpeername(state->raw_fd, reinterpret_cast<sockaddr*>(&ss_peer),
                      &peer_len) == 0 &&
          peer_len > sizeof(sa_family_t)) {
        const socklen_t clamped_len = std::min<socklen_t>(
            peer_len, EventEngine::ResolvedAddress::MAX_SIZE_BYTES);
        state->peer_address = EventEngine::ResolvedAddress(
            reinterpret_cast<const sockaddr*>(&ss_peer), clamped_len);
      } else {
        state->peer_address = state->local_address;
      }
    }
#endif
  }

  {
    grpc_core::MutexLock lock(state->write_mu);
    state->RefreshFastWriteDisabledLocked();
  }
}

UvSocketState::UvSocketState(UvLoopWorker* worker,
                             std::shared_ptr<ThreadPool> pool,
                             std::shared_ptr<EventEngine> event_engine,
                             MemoryAllocator allocator,
                             const PosixTcpOptions& tcp_options,
                             HandleKind handle_kind, UvWriteMode mode)
    : loop_worker(worker),
      thread_pool(std::move(pool)),
      engine(std::move(event_engine)),
      kind(handle_kind),
      write_mode(mode),
      high_watermark(ResolveHighWatermark(tcp_options)),
      min_read_alloc(ResolveReadChunkBounds(tcp_options).min_alloc),
      init_read_alloc(ResolveReadChunkBounds(tcp_options).init_alloc),
      max_read_alloc(ResolveReadChunkBounds(tcp_options).max_alloc),
      memory_allocator(std::move(allocator)),
      loop_alloc_size(init_read_alloc) {}

void SafeDropEngineRefFromLoop(std::shared_ptr<EventEngine> engine,
                               const std::shared_ptr<ThreadPool>& thread_pool) {
  if (engine != nullptr && thread_pool != nullptr &&
      UvLoopWorker::IsAnyLoopThread()) {
    // Never drop the potentially final `EventEngine` reference directly on a
    // `UvLoopWorker` thread (`~UvEventEngine()` joins all `UvLoopWorker`
    // threads). Offload destruction to `thread_pool`, which supports
    // self-quiescing from a pool worker thread.
    thread_pool->Run([eng = std::move(engine)]() mutable { eng.reset(); });
  }
}

UvSocketState::~UvSocketState() {
  grpc_core::CSliceUnref(loop_read_slice);
  SafeDropEngineRefFromLoop(std::move(engine), thread_pool);
}

void UvSocketState::MarkHandleInitializedOnLoop() {
  handle_initialized = true;
  loop_worker->RegisterForkable(this);
}

bool UvSocketState::ShouldDisableFastWriteLocked() const {
#ifdef GPR_WINDOWS
  return true;
#else
  return write_shutdown || shutdown.load(std::memory_order_relaxed) ||
         latched_write_error.has_value() ||
         write_mode == UvWriteMode::kBatchOnLoop || async_write_active ||
         write_cmd_queued || raw_fd < 0;
#endif
}

void UvSocketState::RefreshFastWriteDisabledLocked() {
  if (ShouldDisableFastWriteLocked()) {
    write_fast_state.fetch_or(kFastWriteDisabled, std::memory_order_acq_rel);
  } else {
    write_fast_state.fetch_and(~kFastWriteDisabled, std::memory_order_acq_rel);
  }
}

void UvSocketState::EmitWriteEvents(
    UvSocketState* state,
    const std::shared_ptr<WriteEventSinkState>& sink_state,
    absl::Span<const EventEngine::Endpoint::WriteEvent> events, bool terminal) {
  if (sink_state == nullptr) {
    return;
  }
  EventEngine::Endpoint::WriteEventCallback cb_to_drop;
  bool start_delivery = false;
  {
    grpc_core::MutexLock lock(state->event_cb_mu);
    if (state->endpoint_destroying || sink_state->cb == nullptr ||
        sink_state->terminal_queued) {
      return;
    }
    for (EventEngine::Endpoint::WriteEvent ev : events) {
      if (sink_state->mask.test(static_cast<int>(ev))) {
        sink_state->pending_events.push_back(ev);
      }
    }
    if (terminal) {
      sink_state->terminal_queued = true;
    }
    if (sink_state->delivering) {
      return;
    }
    if (sink_state->pending_events.empty()) {
      if (sink_state->terminal_queued) {
        cb_to_drop = std::move(sink_state->cb);
      }
      // Drop `cb_to_drop` after unlocking `event_cb_mu`.
    } else {
      sink_state->delivering = true;
      ++state->in_flight_event_cbs;
      start_delivery = true;
    }
  }
  if (!start_delivery) {
    return;
  }

  for (;;) {
    EventEngine::Endpoint::WriteEvent next_event;
    {
      grpc_core::MutexLock lock(state->event_cb_mu);
      if (state->endpoint_destroying || sink_state->pending_events.empty()) {
        sink_state->delivering = false;
        if (sink_state->terminal_queued || state->endpoint_destroying) {
          cb_to_drop = std::move(sink_state->cb);
        }
        const bool wake_dtor =
            (--state->in_flight_event_cbs == 0) && state->endpoint_destroying;
        if (wake_dtor) {
          state->event_cb_cv.SignalAll();
        }
        break;
      }
      next_event = sink_state->pending_events.front();
      sink_state->pending_events.erase(sink_state->pending_events.begin());
    }
    sink_state->cb(next_event, absl::Now(), {});
  }
}

void UvSocketState::CompletionClosure::Run() {
  absl::AnyInvocable<void(absl::Status)> local_cb = std::move(cb);
  absl::Status local_status = std::move(status);
  grpc_core::RefCountedPtr<UvSocketState> local_ref = std::move(self_ref);
  if (is_read) {
    state->read_in_flight.store(false, std::memory_order_release);
  } else {
    state->write_in_flight.store(false, std::memory_order_release);
  }
  local_cb(std::move(local_status));
}

void UvSocketState::SyncReadCmd::Execute(uv_loop_t* /*loop*/) {
  grpc_core::RefCountedPtr<UvSocketState> self_ref;
  bool should_have_read_active = false;
  {
    grpc_core::MutexLock lock(state->read_mu);
    state->read_cmd_queued = false;
    self_ref = std::move(state->read_cmd_ref);
    should_have_read_active =
        !state->read_shutdown && !state->latched_read_error.has_value() &&
        (state->read_pending ||
         (state->uv_read_started_desired && !state->paused_by_watermark));
    state->uv_read_active_on_loop = should_have_read_active;
  }
  if (!state->handle_initialized) {
    return;
  }
  uv_stream_t* s = state->stream_ptr();
  if (uv_is_closing(reinterpret_cast<uv_handle_t*>(s))) {
    return;
  }
  if (should_have_read_active && !state->loop_reading_flag) {
    const int rc = uv_read_start(s, &OnUvAllocCb, &OnUvReadCb);
    if (rc == 0) {
      state->loop_reading_flag = true;
    } else {
      bool fire_completion = false;
      {
        grpc_core::MutexLock lock(state->read_mu);
        state->uv_read_active_on_loop = false;
        state->latched_read_error = UvErrorToStatus(rc, "uv_read_start");
        if (state->read_pending) {
          absl::Status read_status = state->staged_read_buffer.Length() > 0
                                         ? absl::OkStatus()
                                         : *state->latched_read_error;
          if (state->staged_read_buffer.Length() > 0) {
            state->read_buffer->Swap(state->staged_read_buffer);
          }
          state->read_buffer = nullptr;
          state->read_pending = false;
          state->read_completion_closure.Arm(std::move(state->active_read_cb),
                                             std::move(read_status),
                                             state->Ref());
          fire_completion = true;
        }
      }
      if (fire_completion) {
        state->thread_pool->Run(&state->read_completion_closure);
      }
    }
  } else if (!should_have_read_active && state->loop_reading_flag) {
    (void)uv_read_stop(s);
    state->loop_reading_flag = false;
  }
}

void UvSocketState::SyncReadCmd::OnRejected() {
  grpc_core::RefCountedPtr<UvSocketState> ref_to_drop;
  {
    grpc_core::MutexLock lock(state->read_mu);
    state->read_cmd_queued = false;
    ref_to_drop = std::move(state->read_cmd_ref);
  }
}

void UvSocketState::OnUvAllocCb(uv_handle_t* handle, size_t /*suggested_size*/,
                                uv_buf_t* buf) {
  auto* state = static_cast<UvSocketState*>(handle->data);
  if (ABSL_PREDICT_FALSE(state->shutdown.load(std::memory_order_acquire))) {
    *buf = uv_buf_init(nullptr, 0);
    return;
  }
  if (GRPC_SLICE_LENGTH(state->loop_read_slice) < kMinTailBytes) {
    grpc_core::CSliceUnref(state->loop_read_slice);
    state->loop_read_slice = grpc_empty_slice();
    const size_t target_alloc = state->loop_alloc_size;
    {
      grpc_core::MutexLock lock(state->alloc_mu);
      if (state->shutdown.load(std::memory_order_acquire)) {
        *buf = uv_buf_init(nullptr, 0);
        return;
      }
      if (state->memory_allocator.IsValid()) {
        state->loop_read_slice = state->memory_allocator.MakeSlice(
            MemoryRequest(state->min_read_alloc, target_alloc));
      } else {
        state->loop_read_slice = GRPC_SLICE_MALLOC(target_alloc);
      }
    }
  }
  *buf = uv_buf_init(
      reinterpret_cast<char*>(GRPC_SLICE_START_PTR(state->loop_read_slice)),
      static_cast<unsigned int>(GRPC_SLICE_LENGTH(state->loop_read_slice)));
}

void UvSocketState::OnUvReadCb(uv_stream_t* stream, ssize_t nread,
                               const uv_buf_t* /*buf*/) {
  auto* state = static_cast<UvSocketState*>(stream->data);
  if (nread == 0) {
    return;
  }

  std::optional<Slice> read_chunk;
  if (nread > 0) {
    const size_t avail_slice_len = GRPC_SLICE_LENGTH(state->loop_read_slice);
    const size_t bytes_read = static_cast<size_t>(nread);
    if (bytes_read == avail_slice_len) {
      if (avail_slice_len >= state->init_read_alloc) {
        state->loop_alloc_size =
            std::min(state->loop_alloc_size * 2, state->max_read_alloc);
      }
    } else if (bytes_read < kStepDownReadThreshold) {
      state->loop_alloc_size = state->init_read_alloc;
    }
    read_chunk.emplace(
        grpc_slice_split_head(&state->loop_read_slice, bytes_read));
    if (GRPC_SLICE_LENGTH(state->loop_read_slice) < kMinTailBytes) {
      grpc_core::CSliceUnref(state->loop_read_slice);
      state->loop_read_slice = grpc_empty_slice();
    }
  } else {
    grpc_core::CSliceUnref(state->loop_read_slice);
    state->loop_read_slice = grpc_empty_slice();
  }

  bool fire_completion = false;
  bool stop_reading = false;
  {
    grpc_core::MutexLock lock(state->read_mu);
    if (state->read_shutdown) {
      state->staged_read_buffer.Clear();
      stop_reading = true;
    } else if (nread > 0) {
      state->staged_read_buffer.AppendIndexed(std::move(*read_chunk));
      if (state->read_pending &&
          (state->staged_read_buffer.Length() >= state->min_progress_bytes ||
           state->staged_read_buffer.Count() >= kMaxStagedReadSlices)) {
        state->read_buffer->Swap(state->staged_read_buffer);
        state->read_buffer = nullptr;
        state->read_pending = false;
        state->read_completion_closure.Arm(std::move(state->active_read_cb),
                                           absl::OkStatus(), state->Ref());
        fire_completion = true;
      } else if (!state->read_pending &&
                 (state->staged_read_buffer.Length() >= state->high_watermark ||
                  state->staged_read_buffer.Count() >= kMaxStagedReadSlices)) {
        state->paused_by_watermark = true;
        state->uv_read_active_on_loop = false;
        stop_reading = true;
      }
    } else {
      state->latched_read_error =
          UvErrorToStatus(static_cast<int>(nread), "uv_read");
      state->uv_read_active_on_loop = false;
      stop_reading = true;
      if (state->read_pending) {
        absl::Status read_status = state->staged_read_buffer.Length() > 0
                                       ? absl::OkStatus()
                                       : *state->latched_read_error;
        if (state->staged_read_buffer.Length() > 0) {
          state->read_buffer->Swap(state->staged_read_buffer);
        }
        state->read_buffer = nullptr;
        state->read_pending = false;
        state->read_completion_closure.Arm(std::move(state->active_read_cb),
                                           std::move(read_status),
                                           state->Ref());
        fire_completion = true;
      }
    }
  }
  if (stop_reading && state->loop_reading_flag) {
    (void)uv_read_stop(stream);
    state->loop_reading_flag = false;
  }
  if (fire_completion) {
    state->thread_pool->Run(&state->read_completion_closure);
  }
}

void UvSocketState::CompletePendingWriteFromLoop(
    absl::Status status,
    absl::Span<const EventEngine::Endpoint::WriteEvent> ok_events) {
  bool should_schedule = false;
  std::shared_ptr<WriteEventSinkState> sink_to_finish;
  {
    grpc_core::MutexLock lock(write_mu);
    async_write_active = false;
    if (!status.ok() && !latched_write_error.has_value()) {
      latched_write_error = status;
    }
    sink_to_finish = std::move(active_write_sink);
    RefreshFastWriteDisabledLocked();
    if (write_pending) {
      write_pending = false;
      write_completion_closure.Arm(std::move(active_write_cb), status, Ref());
      should_schedule = true;
    }
  }
  if (ABSL_PREDICT_FALSE(sink_to_finish != nullptr)) {
    if (status.ok()) {
      EmitWriteEvents(this, sink_to_finish, ok_events, /*terminal=*/true);
    } else {
      const EventEngine::Endpoint::WriteEvent kClosedArr[] = {
          EventEngine::Endpoint::WriteEvent::kClosed};
      EmitWriteEvents(this, sink_to_finish, kClosedArr, /*terminal=*/true);
    }
  }
  if (should_schedule) {
    thread_pool->Run(&write_completion_closure);
  }
}

void UvSocketState::FlushWriteCmd::Execute(uv_loop_t* /*loop*/) {
  grpc_core::RefCountedPtr<UvSocketState> self_ref;
  std::shared_ptr<WriteEventSinkState> sink_ref;
  bool cancelled_before_flush = false;
  {
    grpc_core::MutexLock lock(state->write_mu);
    state->write_cmd_queued = false;
    self_ref = std::move(state->write_cmd_ref);
    if (state->write_shutdown || !state->write_pending) {
      state->staged_write_slices.Clear();
      state->async_write_active = false;
      sink_ref = std::move(state->active_write_sink);
      state->RefreshFastWriteDisabledLocked();
      cancelled_before_flush = true;
    } else {
      state->loop_write_slices.Clear();
      state->loop_write_slices.Swap(state->staged_write_slices);
      state->loop_inflight_write_bytes = 0;
      state->loop_write_scheduled_emitted = false;
      sink_ref = state->active_write_sink;
    }
  }

  if (cancelled_before_flush) {
    if (ABSL_PREDICT_FALSE(sink_ref != nullptr)) {
      const EventEngine::Endpoint::WriteEvent kClosedArr[] = {
          EventEngine::Endpoint::WriteEvent::kClosed};
      EmitWriteEvents(state, sink_ref, kClosedArr, /*terminal=*/true);
    }
    return;
  }

  if (ABSL_PREDICT_FALSE(sink_ref != nullptr)) {
    const EventEngine::Endpoint::WriteEvent kSendMsgArr[] = {
        EventEngine::Endpoint::WriteEvent::kSendMsg};
    EmitWriteEvents(state, sink_ref, kSendMsgArr, /*terminal=*/false);
  }

  if (!state->handle_initialized || uv_is_closing(state->handle_ptr())) {
    state->loop_write_slices.Clear();
    state->CompletePendingWriteFromLoop(absl::CancelledError("Socket closing"),
                                        {});
    return;
  }

  uv_stream_t* s = state->stream_ptr();
  absl::InlinedVector<uv_buf_t, 64> bufs;
  size_t batch_bytes = BuildNextUvWriteBatch(state->loop_write_slices, &bufs);
  if (bufs.empty() || state->loop_write_slices.Length() == 0) {
    state->loop_write_slices.Clear();
    const EventEngine::Endpoint::WriteEvent kSchedSent[] = {
        EventEngine::Endpoint::WriteEvent::kScheduled,
        EventEngine::Endpoint::WriteEvent::kSent};
    state->CompletePendingWriteFromLoop(absl::OkStatus(), kSchedSent);
    return;
  }

  while (!bufs.empty()) {
    const int r =
        uv_try_write(s, bufs.data(), static_cast<unsigned int>(bufs.size()));
    if (r > 0 && static_cast<size_t>(r) == state->loop_write_slices.Length()) {
      state->loop_write_slices.Clear();
      const EventEngine::Endpoint::WriteEvent kSchedSent[] = {
          EventEngine::Endpoint::WriteEvent::kScheduled,
          EventEngine::Endpoint::WriteEvent::kSent};
      state->CompletePendingWriteFromLoop(absl::OkStatus(), kSchedSent);
      return;
    }
    if (r > 0) {
      SliceBuffer written_prefix;
      state->loop_write_slices.MoveFirstNBytesIntoSliceBuffer(
          static_cast<size_t>(r), written_prefix);
      const bool filled_whole_batch = (static_cast<size_t>(r) == batch_bytes);
      batch_bytes = BuildNextUvWriteBatch(state->loop_write_slices, &bufs);
      if (!filled_whole_batch) {
        break;
      }
      continue;
    }
    if (r < 0 && r != UV_EAGAIN && r != UV_ENOSYS) {
      state->loop_write_slices.Clear();
      state->CompletePendingWriteFromLoop(UvErrorToStatus(r, "uv_try_write"),
                                          {});
      return;
    }
    break;
  }

  if (ABSL_PREDICT_FALSE(sink_ref != nullptr)) {
    state->loop_write_scheduled_emitted = true;
    const EventEngine::Endpoint::WriteEvent kScheduledArr[] = {
        EventEngine::Endpoint::WriteEvent::kScheduled};
    EmitWriteEvents(state, sink_ref, kScheduledArr, /*terminal=*/false);
  }

  state->loop_inflight_write_bytes = batch_bytes;
  state->write_req.data = state->Ref().release();
  const int wr = uv_write(&state->write_req, s, bufs.data(),
                          static_cast<unsigned int>(bufs.size()), &OnUvWriteCb);
  if (wr < 0) {
    state->loop_inflight_write_bytes = 0;
    state->loop_write_slices.Clear();
    state->CompletePendingWriteFromLoop(UvErrorToStatus(wr, "uv_write"), {});
    state->Unref();
  }
}

void UvSocketState::FlushWriteCmd::OnRejected() {
  grpc_core::RefCountedPtr<UvSocketState> ref_to_drop;
  std::shared_ptr<WriteEventSinkState> sink_to_close;
  bool schedule_cancel = false;
  {
    grpc_core::MutexLock lock(state->write_mu);
    state->write_cmd_queued = false;
    state->async_write_active = false;
    state->staged_write_slices.Clear();
    sink_to_close = std::move(state->active_write_sink);
    state->RefreshFastWriteDisabledLocked();
    ref_to_drop = std::move(state->write_cmd_ref);
    if (state->write_pending) {
      state->write_pending = false;
      state->write_completion_closure.Arm(
          std::move(state->active_write_cb),
          absl::CancelledError("Loop shut down"), state->Ref());
      schedule_cancel = true;
    }
  }
  if (ABSL_PREDICT_FALSE(sink_to_close != nullptr)) {
    const EventEngine::Endpoint::WriteEvent kClosedArr[] = {
        EventEngine::Endpoint::WriteEvent::kClosed};
    EmitWriteEvents(state, sink_to_close, kClosedArr, /*terminal=*/true);
  }
  if (schedule_cancel) {
    state->thread_pool->Run(&state->write_completion_closure);
  }
}

void UvSocketState::OnUvWriteCb(uv_write_t* req, int status) {
  grpc_core::RefCountedPtr<UvSocketState> req_ref(
      static_cast<UvSocketState*>(req->data));
  req->data = nullptr;

  if (status != 0) {
    req_ref->loop_inflight_write_bytes = 0;
    req_ref->loop_write_slices.Clear();
    req_ref->CompletePendingWriteFromLoop(UvErrorToStatus(status, "uv_write"),
                                          {});
    return;
  }

  if (req_ref->loop_inflight_write_bytes <
      req_ref->loop_write_slices.Length()) {
    if (req_ref->shutdown.load(std::memory_order_acquire) ||
        uv_is_closing(req_ref->handle_ptr())) {
      req_ref->loop_inflight_write_bytes = 0;
      req_ref->loop_write_slices.Clear();
      req_ref->CompletePendingWriteFromLoop(
          absl::CancelledError("Socket closing"), {});
      return;
    }
    SliceBuffer sent_prefix;
    req_ref->loop_write_slices.MoveFirstNBytesIntoSliceBuffer(
        req_ref->loop_inflight_write_bytes, sent_prefix);
    absl::InlinedVector<uv_buf_t, 64> bufs;
    const size_t next_batch_bytes =
        BuildNextUvWriteBatch(req_ref->loop_write_slices, &bufs);
    if (!bufs.empty()) {
      req_ref->loop_inflight_write_bytes = next_batch_bytes;
      req_ref->write_req.data = req_ref->Ref().release();
      const int wr =
          uv_write(&req_ref->write_req, req_ref->stream_ptr(), bufs.data(),
                   static_cast<unsigned int>(bufs.size()), &OnUvWriteCb);
      if (wr < 0) {
        req_ref->loop_inflight_write_bytes = 0;
        req_ref->loop_write_slices.Clear();
        req_ref->CompletePendingWriteFromLoop(UvErrorToStatus(wr, "uv_write"),
                                              {});
        req_ref->Unref();
      }
      return;
    }
  }

  req_ref->loop_inflight_write_bytes = 0;
  req_ref->loop_write_slices.Clear();
  const EventEngine::Endpoint::WriteEvent kSentArr[] = {
      EventEngine::Endpoint::WriteEvent::kSent};
  req_ref->CompletePendingWriteFromLoop(absl::OkStatus(), kSentArr);
}

void UvSocketState::CloseSocketCmd::Execute(uv_loop_t* /*loop*/) {
  grpc_core::RefCountedPtr<UvSocketState> self_ref;
  bool want_release_fd = false;
  {
    grpc_core::MutexLock lock(state->write_mu);
    want_release_fd = state->release_fd_requested;
    self_ref = std::move(state->close_cmd_ref);
  }
  if (!state->handle_initialized) {
#ifndef GPR_WINDOWS
    absl::AnyInvocable<void(absl::StatusOr<int>)> release_cb;
    {
      grpc_core::MutexLock lock(state->write_mu);
      release_cb = std::move(state->on_release_fd);
    }
    if (release_cb != nullptr) {
      state->thread_pool->Run([cb = std::move(release_cb)]() mutable {
        cb(absl::FailedPreconditionError("Socket handle not initialized"));
      });
    }
#endif
    return;
  }

  uv_handle_t* h = state->handle_ptr();
  if (!uv_is_closing(h)) {
    if (state->loop_reading_flag) {
      (void)uv_read_stop(state->stream_ptr());
      state->loop_reading_flag = false;
    }
#ifndef GPR_WINDOWS
    if (state->raw_fd < 0) {
      uv_os_fd_t fd = -1;
      if (uv_fileno(h, &fd) == 0) {
        state->raw_fd = static_cast<int>(fd);
      }
    }
    if (want_release_fd && state->raw_fd >= 0) {
      state->released_dup_fd =
          fcntl(state->raw_fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    }
#else
    (void)want_release_fd;
#endif
    (void)state->Ref().release();
    uv_close(h, &UvSocketState::OnCloseCb);
  }
}

void UvSocketState::CloseSocketCmd::OnRejected() {
  grpc_core::RefCountedPtr<UvSocketState> ref_to_drop;
#ifndef GPR_WINDOWS
  absl::AnyInvocable<void(absl::StatusOr<int>)> release_cb;
#endif
  {
    grpc_core::MutexLock lock(state->write_mu);
#ifndef GPR_WINDOWS
    release_cb = std::move(state->on_release_fd);
#endif
    ref_to_drop = std::move(state->close_cmd_ref);
  }
#ifndef GPR_WINDOWS
  if (release_cb != nullptr) {
    state->thread_pool->Run([cb = std::move(release_cb)]() mutable {
      cb(absl::CancelledError("EventEngine loop shut down"));
    });
  }
#endif
}

void UvSocketState::OnForkInChild(uv_loop_t* /*loop*/) {
  grpc_core::RefCountedPtr<UvSocketState> keepalive = Ref();
  const absl::Status fork_err = absl::CancelledError("Closed on fork");
  shutdown.store(true, std::memory_order_release);
  wrapped_fd.store(-1, std::memory_order_release);

  {
    grpc_core::MutexLock lock(alloc_mu);
    memory_allocator.Reset();
  }

  bool schedule_read_cancel = false;
  {
    grpc_core::MutexLock lock(read_mu);
    read_shutdown = true;
    if (!latched_read_error.has_value()) {
      latched_read_error = fork_err;
    }
    read_buffer = nullptr;
    staged_read_buffer.Clear();
    if (read_pending) {
      read_pending = false;
      read_completion_closure.Arm(std::move(active_read_cb), fork_err, Ref());
      schedule_read_cancel = true;
    }
  }

  bool schedule_write_cancel = false;
  bool close_now = true;
  std::shared_ptr<WriteEventSinkState> sink_to_close;
  grpc_core::RefCountedPtr<UvSocketState> deferred_close_ref_to_drop;
#ifndef GPR_WINDOWS
  absl::AnyInvocable<void(absl::StatusOr<int>)> release_cb;
#endif
  {
    grpc_core::MutexLock lock(write_mu);
    write_shutdown = true;
    if (!latched_write_error.has_value()) {
      latched_write_error = fork_err;
    }
    sink_to_close = std::move(active_write_sink);
#ifndef GPR_WINDOWS
    release_fd_requested = false;
    release_cb = std::move(on_release_fd);
#endif
    staged_write_slices.Clear();
    if (write_pending) {
      write_pending = false;
      write_completion_closure.Arm(std::move(active_write_cb), fork_err, Ref());
      schedule_write_cancel = true;
    }
    const uint32_t prev_fast = write_fast_state.fetch_or(
        kFastWriteDisabled, std::memory_order_acq_rel);
    if ((prev_fast >> 1) != 0) {
      close_now = false;
      if (!deferred_close_pending) {
        deferred_close_pending = true;
        close_cmd_ref = Ref();
      }
    } else if (deferred_close_pending) {
      deferred_close_pending = false;
      deferred_close_ref_to_drop = std::move(close_cmd_ref);
    }
    close_cmd_queued = true;
  }

  // Close the handle on `loop_` BEFORE dispatching cancellation callbacks to
  // `thread_pool` so `uv_close`'s reference is armed even if a callback
  // immediately destroys `UvEndpoint`.
  if (close_now) {
    CloseOnLoop();
  }

  if (ABSL_PREDICT_FALSE(sink_to_close != nullptr)) {
    const EventEngine::Endpoint::WriteEvent kClosedArr[] = {
        EventEngine::Endpoint::WriteEvent::kClosed};
    EmitWriteEvents(this, sink_to_close, kClosedArr, /*terminal=*/true);
  }
#ifndef GPR_WINDOWS
  if (release_cb != nullptr) {
    thread_pool->Run(
        [cb = std::move(release_cb), fork_err]() mutable { cb(fork_err); });
  }
#endif
  if (schedule_read_cancel) {
    thread_pool->Run(&read_completion_closure);
  }
  if (schedule_write_cancel) {
    thread_pool->Run(&write_completion_closure);
  }
}

void UvSocketState::CloseOnLoop() {
  wrapped_fd.store(-1, std::memory_order_release);
  std::shared_ptr<WriteEventSinkState> sink_to_close;
  {
    grpc_core::MutexLock lock(write_mu);
    write_shutdown = true;
    close_cmd_queued = true;
    sink_to_close = std::move(active_write_sink);
    RefreshFastWriteDisabledLocked();
  }
  if (ABSL_PREDICT_FALSE(sink_to_close != nullptr)) {
    const EventEngine::Endpoint::WriteEvent kClosedArr[] = {
        EventEngine::Endpoint::WriteEvent::kClosed};
    EmitWriteEvents(this, sink_to_close, kClosedArr, /*terminal=*/true);
  }
  if (!handle_initialized) {
    return;
  }
  uv_handle_t* h = handle_ptr();
  if (!uv_is_closing(h)) {
    if (loop_reading_flag) {
      (void)uv_read_stop(stream_ptr());
      loop_reading_flag = false;
    }
#ifndef GPR_WINDOWS
    if (raw_fd < 0) {
      uv_os_fd_t fd = -1;
      if (uv_fileno(h, &fd) == 0) {
        raw_fd = static_cast<int>(fd);
      }
    }
#endif
    (void)Ref().release();
    uv_close(h, &UvSocketState::OnCloseCb);
  }
}

void UvSocketState::OnCloseCb(uv_handle_t* handle) {
  auto* state = static_cast<UvSocketState*>(handle->data);
  state->loop_worker->UnregisterForkable(state);
  grpc_core::CSliceUnref(state->loop_read_slice);
  state->loop_read_slice = grpc_empty_slice();
  state->loop_write_slices.Clear();

#ifndef GPR_WINDOWS
  // `libuv` (`uv__stream_close`) skips closing FDs <= STDERR_FILENO (0, 1, 2)
  // after detaching the watcher; close them explicitly so daemonized processes
  // with closed stdio never leak socket FDs or bound ports.
  if (state->raw_fd >= 0 && state->raw_fd <= STDERR_FILENO) {
    ::close(state->raw_fd);
  }
  state->raw_fd = -1;

  absl::AnyInvocable<void(absl::StatusOr<int>)> release_cb;
  int dup_fd = state->released_dup_fd;
  state->released_dup_fd = -1;
  {
    grpc_core::MutexLock lock(state->write_mu);
    release_cb = std::move(state->on_release_fd);
  }
  if (release_cb != nullptr) {
    absl::StatusOr<int> res =
        dup_fd >= 0 ? absl::StatusOr<int>(dup_fd)
                    : absl::InternalError("fcntl(F_DUPFD_CLOEXEC) failed");
    state->thread_pool->Run(
        [cb = std::move(release_cb), res = std::move(res)]() mutable {
          cb(std::move(res));
        });
  } else if (dup_fd >= 0) {
    ::close(dup_fd);
  }
#endif

  state->Unref();
}

UvEndpoint::UvEndpoint(grpc_core::RefCountedPtr<UvSocketState> state,
                       std::shared_ptr<EventEngine> engine)
    : engine_(std::move(engine)), state_(std::move(state)) {}

UvEndpoint::~UvEndpoint() {
  UvSocketState* const state = state_.get();
  ShutdownInternal(absl::CancelledError("Endpoint destroyed"), nullptr);
  if (ABSL_PREDICT_FALSE(
          state->event_sink_used.load(std::memory_order_acquire))) {
    grpc_core::MutexLock lock(state->event_cb_mu);
    state->endpoint_destroying = true;
    while (state->in_flight_event_cbs > 0) {
      state->event_cb_cv.Wait(&state->event_cb_mu);
    }
  }
}

void UvEndpoint::ShutdownInternal(
    absl::Status why,
    absl::AnyInvocable<void(absl::StatusOr<int>)> release_fd_cb) {
  UvSocketState* const state = state_.get();
  state->shutdown.store(true, std::memory_order_release);
  state->wrapped_fd.store(-1, std::memory_order_release);

  {
    grpc_core::MutexLock lock(state->alloc_mu);
    state->memory_allocator.Reset();
  }

  bool schedule_read_cancel = false;
  {
    grpc_core::MutexLock lock(state->read_mu);
    state->read_shutdown = true;
    state->read_buffer = nullptr;
    state->staged_read_buffer.Clear();
    if (state->read_pending) {
      state->read_pending = false;
      state->read_completion_closure.Arm(std::move(state->active_read_cb), why,
                                         state->Ref());
      schedule_read_cancel = true;
    }
  }

  bool schedule_write_cancel = false;
  bool enqueue_close = false;
  std::shared_ptr<UvSocketState::WriteEventSinkState> sink_to_close;
  absl::AnyInvocable<void(absl::StatusOr<int>)> rejected_release_cb;
  {
    grpc_core::MutexLock lock(state->write_mu);
    state->write_shutdown = true;
    sink_to_close = std::move(state->active_write_sink);
    if (release_fd_cb != nullptr) {
      if (!state->close_cmd_queued && !state->release_fd_requested) {
        state->release_fd_requested = true;
        state->on_release_fd = std::move(release_fd_cb);
      } else {
        rejected_release_cb = std::move(release_fd_cb);
      }
    }
    state->staged_write_slices.Clear();
    if (state->write_pending) {
      state->write_pending = false;
      state->write_completion_closure.Arm(std::move(state->active_write_cb),
                                          why, state->Ref());
      schedule_write_cancel = true;
    }
    const uint32_t prev_fast = state->write_fast_state.fetch_or(
        UvSocketState::kFastWriteDisabled, std::memory_order_acq_rel);
    if (!state->close_cmd_queued) {
      state->close_cmd_queued = true;
      state->close_cmd_ref = state->Ref();
      if ((prev_fast >> 1) == 0) {
        enqueue_close = true;
      } else {
        state->deferred_close_pending = true;
      }
    }
  }

  if (ABSL_PREDICT_FALSE(sink_to_close != nullptr)) {
    const EventEngine::Endpoint::WriteEvent kClosedArr[] = {
        EventEngine::Endpoint::WriteEvent::kClosed};
    UvSocketState::EmitWriteEvents(state, sink_to_close, kClosedArr,
                                   /*terminal=*/true);
  }
  if (rejected_release_cb != nullptr) {
    state->thread_pool->Run([cb = std::move(rejected_release_cb)]() mutable {
      cb(absl::FailedPreconditionError("Endpoint already shut down"));
    });
  }
  if (schedule_read_cancel) {
    state->thread_pool->Run(&state->read_completion_closure);
  }
  if (schedule_write_cancel) {
    state->thread_pool->Run(&state->write_completion_closure);
  }
  if (enqueue_close) {
    state->loop_worker->Enqueue(&state->close_cmd);
  }
}

bool UvEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                      SliceBuffer* buffer, ReadArgs args) {
  UvSocketState* const state = state_.get();
  bool need_sync_read_cmd = false;
  bool schedule_immediate_error = false;
  bool sync_completed = false;
  {
    grpc_core::MutexLock lock(state->read_mu);
    GRPC_CHECK(!state->read_in_flight.load(std::memory_order_acquire));
    buffer->Clear();

    if (state->read_shutdown) {
      state->read_in_flight.store(true, std::memory_order_relaxed);
      state->read_completion_closure.Arm(
          std::move(on_read),
          state->latched_read_error.value_or(
              absl::CancelledError("Endpoint shut down")),
          state->Ref());
      schedule_immediate_error = true;
    } else {
      const size_t min_bytes =
          std::min(ComputeMinProgressBytes(args), state->high_watermark);
      const size_t avail = state->staged_read_buffer.Length();

      if (avail >= min_bytes ||
          (avail > 0 &&
           (state->latched_read_error.has_value() ||
            state->staged_read_buffer.Count() >= kMaxStagedReadSlices))) {
        buffer->Swap(state->staged_read_buffer);
        sync_completed = true;
        if (state->paused_by_watermark &&
            !state->latched_read_error.has_value() && !state->read_cmd_queued) {
          state->paused_by_watermark = false;
          state->read_cmd_queued = true;
          state->read_cmd_ref = state->Ref();
          need_sync_read_cmd = true;
        }
      } else if (state->latched_read_error.has_value()) {
        state->read_in_flight.store(true, std::memory_order_relaxed);
        state->read_completion_closure.Arm(
            std::move(on_read), *state->latched_read_error, state->Ref());
        schedule_immediate_error = true;
      } else {
        state->read_in_flight.store(true, std::memory_order_relaxed);
        state->read_pending = true;
        state->read_buffer = buffer;
        state->min_progress_bytes = min_bytes;
        state->active_read_cb = std::move(on_read);
        state->paused_by_watermark = false;
        if ((!state->uv_read_started_desired ||
             !state->uv_read_active_on_loop) &&
            !state->read_cmd_queued) {
          state->uv_read_started_desired = true;
          state->read_cmd_queued = true;
          state->read_cmd_ref = state->Ref();
          need_sync_read_cmd = true;
        }
      }
    }
  }
  if (need_sync_read_cmd) {
    state->loop_worker->Enqueue(&state->sync_read_cmd);
  }
  if (schedule_immediate_error) {
    state->thread_pool->Run(&state->read_completion_closure);
  }
  return sync_completed;
}

bool UvEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                       SliceBuffer* data, WriteArgs args) {
  UvSocketState* const state = state_.get();
  GRPC_CHECK(!state->write_in_flight.load(std::memory_order_acquire));

  const size_t total_len = data->Length();
  if (total_len == 0 && !args.has_metrics_sink() &&
      (state->write_fast_state.load(std::memory_order_acquire) &
       UvSocketState::kFastWriteDisabled) == 0) {
    data->Clear();
    return true;
  }

#ifndef GPR_WINDOWS
  if (state->write_mode == UvWriteMode::kInlineAuto &&
      !args.has_metrics_sink() && total_len > 0 &&
      total_len <= kMaxInlineWriteBytes) {
    uint32_t expected_fast = 0u;
    if ((state->write_fast_state.load(std::memory_order_relaxed) &
         UvSocketState::kFastWriteDisabled) == 0 &&
        state->write_fast_state.compare_exchange_strong(
            expected_fast, UvSocketState::kInlineIoRefUnit,
            std::memory_order_acquire, std::memory_order_relaxed)) {
      state->write_in_flight.store(true, std::memory_order_relaxed);
      const int inline_fd = state->raw_fd;

      size_t total_written = 0;
      size_t slice_idx = 0;
      bool would_block = false;
      bool fallback_to_uv = false;
      int fatal_errno = 0;
      absl::InlinedVector<struct iovec, 64> iov;

      const size_t slice_count = data->Count();
      while (slice_idx < slice_count) {
        iov.clear();
        size_t batch_bytes = 0;
        while (slice_idx < slice_count && iov.size() < kMaxWriteIov) {
          const Slice& sl = (*data)[slice_idx++];
          if (sl.size() == 0) continue;
          struct iovec entry;
          entry.iov_base = const_cast<uint8_t*>(sl.data());
          entry.iov_len = sl.size();
          iov.push_back(entry);
          batch_bytes += sl.size();
        }
        if (iov.empty()) {
          break;
        }

        struct msghdr msg{};
        msg.msg_iov = iov.data();
        msg.msg_iovlen = static_cast<decltype(msg.msg_iovlen)>(iov.size());

        ssize_t sent;
        do {
#ifdef MSG_NOSIGNAL
          sent = ::sendmsg(inline_fd, &msg, MSG_NOSIGNAL);
#else
          sent = ::sendmsg(inline_fd, &msg, 0);
#endif
        } while (sent < 0 && errno == EINTR);

        if (sent < 0) {
          const int err = errno;
          if (err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS) {
            would_block = true;
          } else if (err == ENOTSOCK && total_written == 0) {
            fallback_to_uv = true;
          } else {
            fatal_errno = err;
          }
          break;
        }
        if (sent == 0) {
          would_block = true;
          break;
        }
        const size_t sent_bytes = static_cast<size_t>(sent);
        total_written += sent_bytes;
        if (sent_bytes < batch_bytes) {
          would_block = true;
          break;
        }
      }

      if (!would_block && !fallback_to_uv && fatal_errno == 0) {
        data->Clear();
        state->write_in_flight.store(false, std::memory_order_release);
        const uint32_t prev_after = state->write_fast_state.fetch_sub(
            UvSocketState::kInlineIoRefUnit, std::memory_order_acq_rel);
        if (ABSL_PREDICT_FALSE(
                (prev_after & UvSocketState::kFastWriteDisabled) != 0) &&
            ((prev_after - UvSocketState::kInlineIoRefUnit) >> 1) == 0) {
          UvLoopWorker* close_worker = nullptr;
          UvSocketState::CloseSocketCmd* close_cmd = nullptr;
          {
            grpc_core::MutexLock lock(state->write_mu);
            if (state->deferred_close_pending) {
              state->deferred_close_pending = false;
              close_worker = state->loop_worker;
              close_cmd = &state->close_cmd;
            }
          }
          if (close_cmd != nullptr) {
            close_worker->Enqueue(close_cmd);
          }
        }
        return true;
      }

      if (fatal_errno != 0) {
        data->Clear();
        absl::Status err =
            UvErrorToStatus(uv_translate_sys_error(fatal_errno), "sendmsg");
        UvLoopWorker* close_worker = nullptr;
        UvSocketState::CloseSocketCmd* close_cmd = nullptr;
        {
          grpc_core::MutexLock lock(state->write_mu);
          if (!state->latched_write_error.has_value()) {
            state->latched_write_error = err;
          }
          state->write_fast_state.fetch_or(UvSocketState::kFastWriteDisabled,
                                           std::memory_order_relaxed);
          const uint32_t prev_after = state->write_fast_state.fetch_sub(
              UvSocketState::kInlineIoRefUnit, std::memory_order_acq_rel);
          if (((prev_after - UvSocketState::kInlineIoRefUnit) >> 1) == 0 &&
              state->deferred_close_pending) {
            state->deferred_close_pending = false;
            close_worker = state->loop_worker;
            close_cmd = &state->close_cmd;
          }
          state->write_completion_closure.Arm(std::move(on_writable),
                                              std::move(err), state->Ref());
        }
        if (close_cmd != nullptr) {
          close_worker->Enqueue(close_cmd);
        }
        state->thread_pool->Run(&state->write_completion_closure);
        return false;
      }

      // `would_block || fallback_to_uv`: hand remaining bytes to FlushWriteCmd.
      if (total_written > 0) {
        SliceBuffer trimmed;
        data->MoveFirstNBytesIntoSliceBuffer(total_written, trimmed);
      }
      UvLoopWorker* close_worker = nullptr;
      UvSocketState::CloseSocketCmd* close_cmd = nullptr;
      bool schedule_shutdown_cancel = false;
      bool need_inline_flush_cmd = false;
      {
        grpc_core::MutexLock lock(state->write_mu);
        if (state->write_shutdown) {
          data->Clear();
          state->write_completion_closure.Arm(
              std::move(on_writable),
              absl::CancelledError("Endpoint shut down"), state->Ref());
          schedule_shutdown_cancel = true;
        } else {
          state->write_pending = true;
          state->staged_write_slices.Swap(*data);
          state->active_write_cb = std::move(on_writable);
          state->async_write_active = true;
          if (!state->write_cmd_queued) {
            state->write_cmd_queued = true;
            state->write_cmd_ref = state->Ref();
            need_inline_flush_cmd = true;
          }
        }
        state->write_fast_state.fetch_or(UvSocketState::kFastWriteDisabled,
                                         std::memory_order_relaxed);
        const uint32_t prev_after = state->write_fast_state.fetch_sub(
            UvSocketState::kInlineIoRefUnit, std::memory_order_acq_rel);
        if (((prev_after - UvSocketState::kInlineIoRefUnit) >> 1) == 0 &&
            state->deferred_close_pending) {
          state->deferred_close_pending = false;
          close_worker = state->loop_worker;
          close_cmd = &state->close_cmd;
        }
      }
      if (close_cmd != nullptr) {
        close_worker->Enqueue(close_cmd);
      }
      if (schedule_shutdown_cancel) {
        state->thread_pool->Run(&state->write_completion_closure);
        return false;
      }
      if (need_inline_flush_cmd) {
        state->loop_worker->Enqueue(&state->flush_write_cmd);
      }
      return false;
    }
  }
#endif

  std::shared_ptr<UvSocketState::WriteEventSinkState> incoming_sink;
  if (ABSL_PREDICT_FALSE(args.has_metrics_sink())) {
    state->event_sink_used.store(true, std::memory_order_release);
    auto sink = args.TakeMetricsSink();
    auto cb = sink->TakeEventCallback();
    if (cb != nullptr) {
      incoming_sink = std::make_shared<UvSocketState::WriteEventSinkState>(
          sink->requested_events_mask(), std::move(cb));
    }
  }

  bool need_flush_cmd = false;
  bool schedule_immediate_error = false;
  bool sync_zero_write = false;
  {
    grpc_core::MutexLock lock(state->write_mu);
    if (state->write_shutdown || state->latched_write_error.has_value()) {
      data->Clear();
      state->write_in_flight.store(true, std::memory_order_relaxed);
      absl::Status err = state->write_shutdown
                             ? absl::CancelledError("Endpoint shut down")
                             : *state->latched_write_error;
      state->write_completion_closure.Arm(std::move(on_writable),
                                          std::move(err), state->Ref());
      schedule_immediate_error = true;
    } else if (total_len == 0) {
      data->Clear();
      sync_zero_write = true;
    } else {
      state->write_in_flight.store(true, std::memory_order_relaxed);
      state->write_pending = true;
      state->staged_write_slices.Swap(*data);
      state->active_write_cb = std::move(on_writable);
      state->active_write_sink = std::move(incoming_sink);
      state->async_write_active = true;
      if (!state->write_cmd_queued) {
        state->write_cmd_queued = true;
        state->write_cmd_ref = state->Ref();
        need_flush_cmd = true;
      }
      state->RefreshFastWriteDisabledLocked();
    }
  }
  if (schedule_immediate_error) {
    if (ABSL_PREDICT_FALSE(incoming_sink != nullptr)) {
      const WriteEvent kClosedArr[] = {WriteEvent::kClosed};
      UvSocketState::EmitWriteEvents(state, incoming_sink, kClosedArr,
                                     /*terminal=*/true);
    }
    state->thread_pool->Run(&state->write_completion_closure);
    return false;
  }
  if (sync_zero_write) {
    if (ABSL_PREDICT_FALSE(incoming_sink != nullptr)) {
      const WriteEvent kZeroEvents[] = {
          WriteEvent::kSendMsg, WriteEvent::kScheduled, WriteEvent::kSent};
      UvSocketState::EmitWriteEvents(state, incoming_sink, kZeroEvents,
                                     /*terminal=*/true);
    }
    return true;
  }
  if (need_flush_cmd) {
    state->loop_worker->Enqueue(&state->flush_write_cmd);
  }
  return false;
}

const EventEngine::ResolvedAddress& UvEndpoint::GetPeerAddress() const {
  return state_->peer_address;
}

const EventEngine::ResolvedAddress& UvEndpoint::GetLocalAddress() const {
  return state_->local_address;
}

std::shared_ptr<EventEngine::Endpoint::TelemetryInfo>
UvEndpoint::GetTelemetryInfo() const {
  static absl::NoDestructor<std::shared_ptr<UvEndpointTelemetryInfo>> info(
      std::make_shared<UvEndpointTelemetryInfo>());
  return *info;
}

#ifndef GPR_WINDOWS
int UvEndpoint::GetWrappedFd() {
  return state_->wrapped_fd.load(std::memory_order_acquire);
}

void UvEndpoint::Shutdown(
    absl::AnyInvocable<void(absl::StatusOr<int> release_fd)> on_release_fd) {
  ShutdownInternal(absl::CancelledError("Endpoint shutdown"),
                   std::move(on_release_fd));
}
#endif

}  // namespace grpc_event_engine::experimental
