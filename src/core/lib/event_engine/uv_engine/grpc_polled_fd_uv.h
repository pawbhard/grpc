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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_GRPC_POLLED_FD_UV_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_GRPC_POLLED_FD_UV_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#if GRPC_ARES == 1

#include <memory>

#include "src/core/lib/event_engine/grpc_polled_fd.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"

namespace grpc_event_engine::experimental {

// Creates a libuv `uv_poll_t`-backed `GrpcPolledFdFactory` bound to
// `loop_worker` for driving `AresResolver` sockets asynchronously and
// dispatching readiness closures onto `thread_pool`.
std::unique_ptr<GrpcPolledFdFactory> MakeGrpcPolledFdFactoryLibuv(
    UvLoopWorker* loop_worker, std::shared_ptr<ThreadPool> thread_pool);

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_ARES == 1

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_GRPC_POLLED_FD_UV_H
