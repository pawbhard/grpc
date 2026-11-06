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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_NATIVE_UV_DNS_RESOLVER_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_NATIVE_UV_DNS_RESOLVER_H

#include <grpc/event_engine/event_engine.h>

#include <memory>

#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/strings/string_view.h"

namespace grpc_event_engine::experimental {

// Native OS `DNSResolver` backed by libuv's asynchronous `uv_getaddrinfo` API
// on `loop_worker`, with IPv4/IPv6 literal fast-path parsing, RFC 6724 address
// sorting, cancellation on destruction, and callback delivery on `thread_pool`.
class NativeUvDNSResolver final : public EventEngine::DNSResolver {
 public:
  NativeUvDNSResolver(UvLoopWorker* loop_worker,
                      std::shared_ptr<ThreadPool> thread_pool,
                      std::shared_ptr<EventEngine> engine);
  ~NativeUvDNSResolver() override;

  NativeUvDNSResolver(const NativeUvDNSResolver&) = delete;
  NativeUvDNSResolver& operator=(const NativeUvDNSResolver&) = delete;

  void LookupHostname(LookupHostnameCallback on_resolve, absl::string_view name,
                      absl::string_view default_port) override;
  void LookupSRV(LookupSRVCallback on_resolve, absl::string_view name) override;
  void LookupTXT(LookupTXTCallback on_resolve, absl::string_view name) override;

 private:
  struct ResolverState;

  UvLoopWorker* const loop_worker_;
  std::shared_ptr<ThreadPool> thread_pool_;
  std::shared_ptr<EventEngine> engine_;
  grpc_core::RefCountedPtr<ResolverState> state_;
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_UV_ENGINE_NATIVE_UV_DNS_RESOLVER_H
