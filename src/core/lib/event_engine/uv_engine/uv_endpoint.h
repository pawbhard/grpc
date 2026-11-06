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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_ENDPOINT_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_ENDPOINT_H

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <uv.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace grpc_event_engine::experimental {

enum class UvWriteMode : uint8_t {
  kInlineAuto,
  kBatchOnLoop,
};

UvWriteMode ResolveUvWriteMode(const EndpointConfig& config);

// Shared per-socket state jointly owned by `UvEndpoint` and any in-flight
// `LoopCommand`, `CompletionClosure`, or `uv_close` operation on `loop_worker`.
// Keeps a strong `std::shared_ptr<EventEngine>` so `loop_worker` and
// `thread_pool` outlive every asynchronous socket operation.
struct UvSocketState final : public grpc_core::RefCounted<UvSocketState>,
                             public LoopForkable {
  enum class HandleKind { kTcp, kPipe };

  // `write_fast_state` packs two fields into a single 32-bit atomic word:
  //   - Bit 0 (`kFastWriteDisabled = 1u`): set whenever lock-free inline
  //     `::sendmsg` is disallowed (batch mode, pending async write, error,
  //     shutdown, or invalid FD).
  //   - Bits 31..1 (`kInlineIoRefUnit = 2u` per active inline writer): counts
  //     threads currently executing inline `::sendmsg` on `raw_fd`. When
  //     `ShutdownInternal` sets `kFastWriteDisabled` while `(state >> 1) != 0`,
  //     `CloseSocketCmd` is deferred until the last inline writer decrements
  //     the active count to zero.
  static constexpr uint32_t kFastWriteDisabled = 1u;
  static constexpr uint32_t kInlineIoRefUnit = 2u;
  static_assert(kInlineIoRefUnit == (1u << 1),
                "kInlineIoRefUnit must occupy bit 1 for >> 1 refcount shifts");

  static constexpr size_t kDefaultMinReadAlloc = 8 * 1024;
  static constexpr size_t kDefaultInitReadAlloc = 64 * 1024;
  static constexpr size_t kDefaultMaxReadAlloc = 256 * 1024;

  struct WriteEventSinkState {
    WriteEventSinkState(EventEngine::Endpoint::WriteEventSet requested_mask,
                        EventEngine::Endpoint::WriteEventCallback callback)
        : mask(requested_mask), cb(std::move(callback)) {}

    EventEngine::Endpoint::WriteEventSet mask;
    EventEngine::Endpoint::WriteEventCallback cb;
    absl::InlinedVector<EventEngine::Endpoint::WriteEvent, 4> pending_events;
    bool delivering = false;
    bool terminal_queued = false;
  };

  struct SyncReadCmd final : public LoopCommand {
    explicit SyncReadCmd(UvSocketState* s) : state(s) {}
    void Execute(uv_loop_t* loop) override;
    void OnRejected() override;

    UvSocketState* state;
  };

  struct FlushWriteCmd final : public LoopCommand {
    explicit FlushWriteCmd(UvSocketState* s) : state(s) {}
    void Execute(uv_loop_t* loop) override;
    void OnRejected() override;

    UvSocketState* state;
  };

  struct CloseSocketCmd final : public LoopCommand {
    explicit CloseSocketCmd(UvSocketState* s) : state(s) {}
    void Execute(uv_loop_t* loop) override;
    void OnRejected() override;

    UvSocketState* state;
  };

  struct CompletionClosure final : public EventEngine::Closure {
    CompletionClosure(UvSocketState* s, bool read) : state(s), is_read(read) {}
    void Arm(absl::AnyInvocable<void(absl::Status)> callback, absl::Status st,
             grpc_core::RefCountedPtr<UvSocketState> ref) {
      cb = std::move(callback);
      status = std::move(st);
      self_ref = std::move(ref);
    }
    void Run() override;

    UvSocketState* state;
    bool is_read;
    absl::AnyInvocable<void(absl::Status)> cb;
    absl::Status status;
    grpc_core::RefCountedPtr<UvSocketState> self_ref;
  };

  UvSocketState(UvLoopWorker* worker, std::shared_ptr<ThreadPool> pool,
                std::shared_ptr<EventEngine> event_engine,
                MemoryAllocator allocator, const PosixTcpOptions& tcp_options,
                HandleKind handle_kind, UvWriteMode mode);
  ~UvSocketState() override;

  uv_stream_t* stream_ptr() { return &stream_handle.stream; }
  uv_handle_t* handle_ptr() { return &stream_handle.handle; }

  static void OnUvAllocCb(uv_handle_t* handle, size_t suggested_size,
                          uv_buf_t* buf);
  static void OnUvReadCb(uv_stream_t* stream, ssize_t nread,
                         const uv_buf_t* buf);
  static void OnUvWriteCb(uv_write_t* req, int status);
  static void OnCloseCb(uv_handle_t* handle);

  void MarkHandleInitializedOnLoop();
  void OnForkInChild(uv_loop_t* loop) override
      ABSL_LOCKS_EXCLUDED(alloc_mu, read_mu, write_mu, event_cb_mu);
  void CloseOnLoop() ABSL_LOCKS_EXCLUDED(write_mu, event_cb_mu);
  void CompletePendingWriteFromLoop(
      absl::Status status,
      absl::Span<const EventEngine::Endpoint::WriteEvent> ok_events)
      ABSL_LOCKS_EXCLUDED(write_mu, event_cb_mu);
  static void EmitWriteEvents(
      UvSocketState* state,
      const std::shared_ptr<WriteEventSinkState>& sink_state,
      absl::Span<const EventEngine::Endpoint::WriteEvent> events, bool terminal)
      ABSL_LOCKS_EXCLUDED(write_mu, event_cb_mu);
  bool ShouldDisableFastWriteLocked() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu);
  void RefreshFastWriteDisabledLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu);

  // Immutable / pre-publication socket metadata (clean shared L1 line):
  UvLoopWorker* const loop_worker;
  const std::shared_ptr<ThreadPool> thread_pool;
  std::shared_ptr<EventEngine> engine;
  const HandleKind kind;
  const UvWriteMode write_mode;
  const size_t high_watermark;
  const size_t min_read_alloc;
  const size_t init_read_alloc;
  const size_t max_read_alloc;
  int raw_fd = -1;
  std::atomic<int> wrapped_fd{-1};
  EventEngine::ResolvedAddress local_address;
  EventEngine::ResolvedAddress peer_address;
  std::atomic<bool> shutdown{false};
  std::atomic<bool> event_sink_used{false};

  // Single-purpose allocator lock (acquired only on slice replenishment /
  // accept init / endpoint shutdown; never nested with read_mu or write_mu):
  alignas(GPR_CACHELINE_SIZE) grpc_core::Mutex alloc_mu;
  MemoryAllocator memory_allocator ABSL_GUARDED_BY(alloc_mu);

  // Single-purpose read state & lock (never touched by Write()!):
  alignas(GPR_CACHELINE_SIZE) std::atomic<bool> read_in_flight{false};
  grpc_core::Mutex read_mu;
  bool read_shutdown ABSL_GUARDED_BY(read_mu) = false;
  bool read_pending ABSL_GUARDED_BY(read_mu) = false;
  bool uv_read_started_desired ABSL_GUARDED_BY(read_mu) = false;
  bool uv_read_active_on_loop ABSL_GUARDED_BY(read_mu) = false;
  bool paused_by_watermark ABSL_GUARDED_BY(read_mu) = false;
  bool read_cmd_queued ABSL_GUARDED_BY(read_mu) = false;
  size_t min_progress_bytes ABSL_GUARDED_BY(read_mu) = 1;
  SliceBuffer* read_buffer ABSL_GUARDED_BY(read_mu) = nullptr;
  SliceBuffer staged_read_buffer ABSL_GUARDED_BY(read_mu);
  absl::AnyInvocable<void(absl::Status)> active_read_cb
      ABSL_GUARDED_BY(read_mu);
  std::optional<absl::Status> latched_read_error ABSL_GUARDED_BY(read_mu);
  grpc_core::RefCountedPtr<UvSocketState> read_cmd_ref ABSL_GUARDED_BY(read_mu);
  SyncReadCmd sync_read_cmd{this};
  CompletionClosure read_completion_closure{this, /*read=*/true};

  // Lock-free inline Write() state + single-purpose async write / close lock:
  alignas(GPR_CACHELINE_SIZE) std::atomic<uint32_t> write_fast_state{
      kFastWriteDisabled};
  std::atomic<bool> write_in_flight{false};
  grpc_core::Mutex write_mu;
  bool write_shutdown ABSL_GUARDED_BY(write_mu) = false;
  bool write_pending ABSL_GUARDED_BY(write_mu) = false;
  bool async_write_active ABSL_GUARDED_BY(write_mu) = false;
  bool write_cmd_queued ABSL_GUARDED_BY(write_mu) = false;
  bool close_cmd_queued ABSL_GUARDED_BY(write_mu) = false;
  bool deferred_close_pending ABSL_GUARDED_BY(write_mu) = false;
  bool release_fd_requested ABSL_GUARDED_BY(write_mu) = false;
  SliceBuffer staged_write_slices ABSL_GUARDED_BY(write_mu);
  absl::AnyInvocable<void(absl::Status)> active_write_cb
      ABSL_GUARDED_BY(write_mu);
  std::optional<absl::Status> latched_write_error ABSL_GUARDED_BY(write_mu);
  std::shared_ptr<WriteEventSinkState> active_write_sink
      ABSL_GUARDED_BY(write_mu);
  grpc_core::RefCountedPtr<UvSocketState> write_cmd_ref
      ABSL_GUARDED_BY(write_mu);
  grpc_core::RefCountedPtr<UvSocketState> close_cmd_ref
      ABSL_GUARDED_BY(write_mu);
  absl::AnyInvocable<void(absl::StatusOr<int>)> on_release_fd
      ABSL_GUARDED_BY(write_mu);
  FlushWriteCmd flush_write_cmd{this};
  CloseSocketCmd close_cmd{this};
  CompletionClosure write_completion_closure{this, /*read=*/false};

  // Cold WriteEventSink callback barrier (acquired ONLY when WriteArgs carries
  // a WriteEventSink; ensures ~UvEndpoint waits for in-flight event callbacks
  // without holding write_mu during user callbacks):
  grpc_core::Mutex event_cb_mu;
  grpc_core::CondVar event_cb_cv ABSL_GUARDED_BY(event_cb_mu);
  size_t in_flight_event_cbs ABSL_GUARDED_BY(event_cb_mu) = 0;
  bool endpoint_destroying ABSL_GUARDED_BY(event_cb_mu) = false;

  // Accessed exclusively on `loop_worker`'s uv_loop_t thread:
  alignas(GPR_CACHELINE_SIZE) union StreamUnion {
    StreamUnion() : tcp{} {}
    ~StreamUnion() {}
    uv_tcp_t tcp;
    uv_pipe_t pipe;
    uv_stream_t stream;
    uv_handle_t handle;
  } stream_handle;
  uv_write_t write_req{};
  bool handle_initialized = false;
  bool loop_reading_flag = false;
  bool loop_write_scheduled_emitted = false;
  int released_dup_fd = -1;
  size_t loop_alloc_size = kDefaultInitReadAlloc;
  size_t loop_inflight_write_bytes = 0;
  grpc_slice loop_read_slice = grpc_empty_slice();
  SliceBuffer loop_write_slices;
};

#ifndef GPR_WINDOWS
using UvEndpointBase =
    ExtendedType<EventEngine::Endpoint, EndpointSupportsFdExtension>;
#else
using UvEndpointBase = EventEngine::Endpoint;
#endif

// EventEngine::Endpoint backed by a libuv `uv_tcp_t` or `uv_pipe_t` handle
// bound to a single `UvLoopWorker` shard. Supports lock-free inline `::sendmsg`
// writes in latency/blend mode, watermark-bounded continuous reads, and
// asynchronous `uv_close` teardown decoupled from synchronous `~UvEndpoint()`.
class UvEndpoint final : public UvEndpointBase {
 public:
  UvEndpoint(grpc_core::RefCountedPtr<UvSocketState> state,
             std::shared_ptr<EventEngine> engine);
  ~UvEndpoint() override;

  UvEndpoint(const UvEndpoint&) = delete;
  UvEndpoint& operator=(const UvEndpoint&) = delete;

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            ReadArgs args) override ABSL_LOCKS_EXCLUDED(state_->read_mu);
  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, WriteArgs args) override
      ABSL_LOCKS_EXCLUDED(state_->write_mu, state_->event_cb_mu);
  const EventEngine::ResolvedAddress& GetPeerAddress() const override;
  const EventEngine::ResolvedAddress& GetLocalAddress() const override;
  std::shared_ptr<TelemetryInfo> GetTelemetryInfo() const override;

#ifndef GPR_WINDOWS
  int GetWrappedFd() override;
  void Shutdown(absl::AnyInvocable<void(absl::StatusOr<int> release_fd)>
                    on_release_fd) override
      ABSL_LOCKS_EXCLUDED(state_->alloc_mu, state_->read_mu, state_->write_mu,
                          state_->event_cb_mu);
#endif

 private:
  void ShutdownInternal(
      absl::Status why,
      absl::AnyInvocable<void(absl::StatusOr<int>)> release_fd_cb)
      ABSL_LOCKS_EXCLUDED(state_->alloc_mu, state_->read_mu, state_->write_mu,
                          state_->event_cb_mu);

  std::shared_ptr<EventEngine> engine_;
  grpc_core::RefCountedPtr<UvSocketState> state_;
};

// Validates that `addr` has a supported address family and sufficient length
// for its `sockaddr_*` representation.
absl::Status ValidateResolvedAddress(const EventEngine::ResolvedAddress& addr);

#ifndef GPR_WINDOWS
struct UnixSocketPath {
  const char* path;
  size_t length;
};

// Extracts a validated filesystem or abstract `AF_UNIX` path bounded by both
// `sizeof(sockaddr_un::sun_path)` and `addr.size()`.
absl::StatusOr<UnixSocketPath> ExtractUnixSocketPath(
    const EventEngine::ResolvedAddress& addr);

// Applies DSCP (`IP_TOS` / `IPV6_TCLASS`) and `grpc_socket_mutator` options to
// a POSIX socket descriptor.
absl::Status ApplyPosixSocketMutatorAndDscp(uv_os_fd_t fd,
                                            const PosixTcpOptions& options,
                                            grpc_fd_usage usage);
#endif

// Configures TCP socket options (TCP_NODELAY, keepalive, TCP_USER_TIMEOUT,
// recvbuf, DSCP, mutator) on an initialized `uv_tcp_t` handle on its owner
// loop thread.
absl::Status ApplyUvTcpOptions(uv_tcp_t* tcp, const PosixTcpOptions& options,
                               grpc_fd_usage usage);

// Populates `local_address`, `peer_address`, `raw_fd`, and `wrapped_fd` on
// `state` on its owner loop thread.
void PopulateUvSocketAddresses(UvSocketState* state)
    ABSL_LOCKS_EXCLUDED(state->write_mu);

// Ensures `engine` is never destroyed synchronously on a `UvLoopWorker` thread
// (where `~UvEventEngine()` joining all loop workers would self-join).
void SafeDropEngineRefFromLoop(std::shared_ptr<EventEngine> engine,
                               const std::shared_ptr<ThreadPool>& thread_pool);

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_UV_ENDPOINT_H
