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

#include "src/core/lib/event_engine/uv_engine/native_uv_dns_resolver.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <uv.h>

#ifdef GPR_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/event_engine/ares_resolver.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/uv_engine/uv_error_utils.h"
#include "src/core/lib/event_engine/uv_engine/uv_loop_worker.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/util/address_sorting_init.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/host_port.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace grpc_event_engine::experimental {
namespace {

constexpr absl::string_view kDefaultHttpPort = "80";
constexpr absl::string_view kDefaultHttpsPort = "443";
constexpr int kMaxTcpUdpPort = 65535;

absl::Status UvGetAddrInfoErrorToStatus(int uv_status, absl::string_view name) {
  char err_buf[128];
  const std::string detail =
      absl::StrFormat("Address lookup failed for %s: %s", name,
                      uv_strerror_r(uv_status, err_buf, sizeof(err_buf)));
  switch (uv_status) {
    case UV_ECANCELED:
    case UV_EAI_CANCELED:
      return absl::CancelledError(absl::StrCat("address lookup failed for ",
                                               name, ": DNS query cancelled"));
    case UV_EAI_NONAME:
    case UV_EAI_NODATA:
      return absl::NotFoundError(detail);
    case UV_EAI_AGAIN:
      return absl::UnavailableError(detail);
    case UV_EAI_MEMORY:
    case UV_ENOMEM:
      return absl::ResourceExhaustedError(detail);
    case UV_EAI_BADFLAGS:
    case UV_EAI_BADHINTS:
    case UV_EAI_FAMILY:
    case UV_EAI_SERVICE:
    case UV_EAI_SOCKTYPE:
    case UV_EINVAL:
      return absl::InvalidArgumentError(detail);
    default:
      return UvErrorToStatus(uv_status, detail);
  }
}

std::vector<EventEngine::ResolvedAddress> OrderResolvedAddresses(
    const std::vector<EventEngine::ResolvedAddress>& addresses) {
#if GRPC_ARES == 1
  return SortAddresses(addresses);
#else
  std::vector<EventEngine::ResolvedAddress> sorted;
  sorted.reserve(addresses.size());
  for (const auto& addr : addresses) {
    if (addr.address()->sa_family == AF_INET6) {
      sorted.emplace_back(addr);
    }
  }
  for (const auto& addr : addresses) {
    if (addr.address()->sa_family != AF_INET6) {
      sorted.emplace_back(addr);
    }
  }
  return sorted;
#endif
}

}  // namespace

struct NativeUvDNSResolver::ResolverState final
    : public grpc_core::RefCounted<ResolverState> {
  struct LookupRequest final : public grpc_core::RefCounted<LookupRequest> {
    LookupRequest(uint64_t id, std::string orig_name, std::string target_host,
                  std::string target_service,
                  grpc_core::RefCountedPtr<ResolverState> owner,
                  std::shared_ptr<ThreadPool> pool, LookupHostnameCallback cb)
        : request_id(id),
          original_name(std::move(orig_name)),
          host(std::move(target_host)),
          service(std::move(target_service)),
          resolver_state(std::move(owner)),
          thread_pool(std::move(pool)),
          callback(std::move(cb)) {}

    LookupHostnameCallback ClaimCallback(bool mark_cancelled)
        ABSL_LOCKS_EXCLUDED(mu) {
      grpc_core::MutexLock lock(mu);
      if (mark_cancelled) {
        cancelled = true;
      }
      return std::move(callback);
    }

    bool IsCancelled() const ABSL_LOCKS_EXCLUDED(mu) {
      grpc_core::MutexLock lock(mu);
      return cancelled;
    }

    const uint64_t request_id;
    const std::string original_name;
    const std::string host;
    const std::string service;
    const grpc_core::RefCountedPtr<ResolverState> resolver_state;
    const std::shared_ptr<ThreadPool> thread_pool;

    // Accessed exclusively on `UvLoopWorker`'s `uv_loop_t` thread:
    uv_getaddrinfo_t req{};
    struct addrinfo hints{};
    bool uv_started_on_loop = false;

    mutable grpc_core::Mutex mu;
    LookupHostnameCallback callback ABSL_GUARDED_BY(mu);
    bool cancelled ABSL_GUARDED_BY(mu) = false;
  };

  class StartLookupCmd final : public LoopCommand {
   public:
    explicit StartLookupCmd(grpc_core::RefCountedPtr<LookupRequest> request)
        : request_(std::move(request)) {}

    void Execute(uv_loop_t* loop) override {
      grpc_core::RefCountedPtr<LookupRequest> req_state = std::move(request_);
      delete this;
      if (req_state->IsCancelled()) {
        req_state->resolver_state->RemoveActiveRequest(req_state->request_id);
        return;
      }
      std::memset(&req_state->hints, 0, sizeof(req_state->hints));
      req_state->hints.ai_family = AF_UNSPEC;
      req_state->hints.ai_socktype = SOCK_STREAM;
      req_state->hints.ai_flags = AI_PASSIVE;
      req_state->req.data = req_state->Ref().release();
      const int rc =
          uv_getaddrinfo(loop, &req_state->req, &ResolverState::OnGetAddrInfoCb,
                         req_state->host.c_str(), req_state->service.c_str(),
                         &req_state->hints);
      if (rc != 0) {
        grpc_core::RefCountedPtr<LookupRequest> reclaim(
            static_cast<LookupRequest*>(req_state->req.data));
        req_state->resolver_state->RemoveActiveRequest(req_state->request_id);
        LookupHostnameCallback cb =
            req_state->ClaimCallback(/*mark_cancelled=*/false);
        if (cb != nullptr) {
          absl::Status status =
              UvGetAddrInfoErrorToStatus(rc, req_state->original_name);
          req_state->thread_pool->Run(
              [cb = std::move(cb), status = std::move(status)]() mutable {
                cb(std::move(status));
              });
        }
        return;
      }
      req_state->uv_started_on_loop = true;
    }

    void OnRejected() override {
      grpc_core::RefCountedPtr<LookupRequest> req_state = std::move(request_);
      delete this;
      req_state->resolver_state->RemoveActiveRequest(req_state->request_id);
      LookupHostnameCallback cb =
          req_state->ClaimCallback(/*mark_cancelled=*/true);
      if (cb != nullptr) {
        absl::Status status = absl::CancelledError(
            absl::StrCat("address lookup failed for ", req_state->original_name,
                         ": EventEngine loop shut down"));
        req_state->thread_pool->Run(
            [cb = std::move(cb), status = std::move(status)]() mutable {
              cb(std::move(status));
            });
      }
    }

   private:
    grpc_core::RefCountedPtr<LookupRequest> request_;
  };

  class CancelLookupsCmd final : public HeapLoopCommand {
   public:
    explicit CancelLookupsCmd(
        std::vector<grpc_core::RefCountedPtr<LookupRequest>> requests)
        : requests_(std::move(requests)) {}

    void Run(uv_loop_t* /*loop*/) override {
      for (auto& req_state : requests_) {
        if (req_state->uv_started_on_loop) {
          (void)uv_cancel(reinterpret_cast<uv_req_t*>(&req_state->req));
        }
      }
    }

   private:
    std::vector<grpc_core::RefCountedPtr<LookupRequest>> requests_;
  };

  static void OnGetAddrInfoCb(uv_getaddrinfo_t* req, int status,
                              struct addrinfo* res) {
    grpc_core::RefCountedPtr<LookupRequest> req_state(
        static_cast<LookupRequest*>(req->data));
    req_state->uv_started_on_loop = false;
    req_state->resolver_state->RemoveActiveRequest(req_state->request_id);

    LookupHostnameCallback cb =
        req_state->ClaimCallback(/*mark_cancelled=*/false);
    if (cb == nullptr) {
      if (res != nullptr) {
        uv_freeaddrinfo(res);
      }
      return;
    }

    absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> outcome;
    if (status == 0) {
      std::vector<EventEngine::ResolvedAddress> addresses;
      for (const struct addrinfo* resp = res; resp != nullptr;
           resp = resp->ai_next) {
        if (resp->ai_addr == nullptr ||
            resp->ai_addrlen < sizeof(sa_family_t) ||
            static_cast<size_t>(resp->ai_addrlen) >
                EventEngine::ResolvedAddress::MAX_SIZE_BYTES) {
          continue;
        }
        addresses.emplace_back(resp->ai_addr, resp->ai_addrlen);
      }
      if (res != nullptr) {
        uv_freeaddrinfo(res);
        res = nullptr;
      }
      if (addresses.empty()) {
        outcome = absl::NotFoundError(
            absl::StrCat("No addresses found for ", req_state->original_name));
      } else {
        outcome = OrderResolvedAddresses(addresses);
      }
    } else {
      if (res != nullptr) {
        uv_freeaddrinfo(res);
        res = nullptr;
      }
      outcome = UvGetAddrInfoErrorToStatus(status, req_state->original_name);
    }

    req_state->thread_pool->Run(
        [cb = std::move(cb), outcome = std::move(outcome)]() mutable {
          cb(std::move(outcome));
        });
  }

  void RemoveActiveRequest(uint64_t request_id) ABSL_LOCKS_EXCLUDED(mu) {
    grpc_core::MutexLock lock(mu);
    active_requests.erase(request_id);
  }

  grpc_core::Mutex mu;
  bool shutting_down ABSL_GUARDED_BY(mu) = false;
  uint64_t next_request_id ABSL_GUARDED_BY(mu) = 0;
  absl::flat_hash_map<uint64_t, grpc_core::RefCountedPtr<LookupRequest>>
      active_requests ABSL_GUARDED_BY(mu);
};

NativeUvDNSResolver::NativeUvDNSResolver(
    UvLoopWorker* loop_worker, std::shared_ptr<ThreadPool> thread_pool,
    std::shared_ptr<EventEngine> engine)
    : loop_worker_(loop_worker),
      thread_pool_(std::move(thread_pool)),
      engine_(std::move(engine)),
      state_(grpc_core::MakeRefCounted<ResolverState>()) {
  GRPC_CHECK_NE(loop_worker_, nullptr);
  GRPC_CHECK_NE(thread_pool_, nullptr);
  GRPC_CHECK_NE(engine_, nullptr);
#if GRPC_ARES == 1
  grpc_core::AddressSortingInitOnce();
#endif
}

NativeUvDNSResolver::~NativeUvDNSResolver() {
  std::vector<grpc_core::RefCountedPtr<ResolverState::LookupRequest>> to_cancel;
  {
    grpc_core::MutexLock lock(state_->mu);
    state_->shutting_down = true;
    to_cancel.reserve(state_->active_requests.size());
    for (auto& [id, req_state] : state_->active_requests) {
      (void)id;
      to_cancel.push_back(std::move(req_state));
    }
    state_->active_requests.clear();
  }
  if (to_cancel.empty()) {
    return;
  }
  for (auto& req_state : to_cancel) {
    LookupHostnameCallback cb =
        req_state->ClaimCallback(/*mark_cancelled=*/true);
    if (cb != nullptr) {
      absl::Status cancelled_status = absl::CancelledError(
          absl::StrCat("address lookup failed for ", req_state->original_name,
                       ": DNS query cancelled"));
      thread_pool_->Run(
          [cb = std::move(cb), status = std::move(cancelled_status)]() mutable {
            cb(std::move(status));
          });
    }
  }
  loop_worker_->Enqueue(
      new ResolverState::CancelLookupsCmd(std::move(to_cancel)));
}

void NativeUvDNSResolver::LookupHostname(LookupHostnameCallback on_resolve,
                                         absl::string_view name,
                                         absl::string_view default_port) {
  // Synchronously copy `name` and `default_port` before returning so callers
  // may immediately destroy backing storage
  // (`StressTestQueryARecordWithNameDeletion`).
  std::string name_copy(name);
  std::string default_port_copy(default_port);

  std::string host;
  std::string port;
  if (!grpc_core::SplitHostPort(name_copy, &host, &port) || host.empty()) {
    absl::Status err = absl::InvalidArgumentError(
        absl::StrCat("Unparsable name: ", name_copy));
    engine_->Run(
        [on_resolve = std::move(on_resolve), err = std::move(err)]() mutable {
          on_resolve(std::move(err));
        });
    return;
  }
  if (port.empty()) {
    if (default_port_copy.empty()) {
      absl::Status err = absl::InvalidArgumentError(absl::StrFormat(
          "No port in name %s or default_port argument", name_copy));
      engine_->Run(
          [on_resolve = std::move(on_resolve), err = std::move(err)]() mutable {
            on_resolve(std::move(err));
          });
      return;
    }
    port = std::move(default_port_copy);
  }

  std::string service;
  if (port == "http") {
    service = std::string(kDefaultHttpPort);
  } else if (port == "https") {
    service = std::string(kDefaultHttpsPort);
  } else {
    service = std::move(port);
  }

  // Fast-path IPv4 and IPv6 literals without touching `uv_getaddrinfo`'s
  // global worker threadpool.
  int numeric_port = -1;
  if (absl::SimpleAtoi(service, &numeric_port) && numeric_port >= 0 &&
      numeric_port <= kMaxTcpUdpPort) {
    grpc_resolved_address resolved_addr;
    const std::string hostport = grpc_core::JoinHostPort(host, numeric_port);
    if (grpc_parse_ipv4_hostport(hostport, &resolved_addr,
                                 /*log_errors=*/false) ||
        grpc_parse_ipv6_hostport(hostport, &resolved_addr,
                                 /*log_errors=*/false)) {
      std::vector<EventEngine::ResolvedAddress> result;
      result.emplace_back(reinterpret_cast<const sockaddr*>(resolved_addr.addr),
                          resolved_addr.len);
      engine_->Run([on_resolve = std::move(on_resolve),
                    result = std::move(result)]() mutable {
        on_resolve(std::move(result));
      });
      return;
    }
  }

  grpc_core::RefCountedPtr<ResolverState::LookupRequest> req_state;
  {
    grpc_core::MutexLock lock(state_->mu);
    if (state_->shutting_down) {
      absl::Status err = absl::CancelledError(absl::StrCat(
          "address lookup failed for ", name_copy, ": DNS query cancelled"));
      engine_->Run(
          [on_resolve = std::move(on_resolve), err = std::move(err)]() mutable {
            on_resolve(std::move(err));
          });
      return;
    }
    const uint64_t id = ++state_->next_request_id;
    req_state = grpc_core::MakeRefCounted<ResolverState::LookupRequest>(
        id, std::move(name_copy), std::move(host), std::move(service), state_,
        thread_pool_, std::move(on_resolve));
    state_->active_requests.emplace(id, req_state);
  }

  loop_worker_->Enqueue(
      new ResolverState::StartLookupCmd(std::move(req_state)));
}

void NativeUvDNSResolver::LookupSRV(LookupSRVCallback on_resolve,
                                    absl::string_view /*name*/) {
  engine_->Run([on_resolve = std::move(on_resolve)]() mutable {
    on_resolve(absl::UnimplementedError(
        "The Native resolver does not support looking up SRV records"));
  });
}

void NativeUvDNSResolver::LookupTXT(LookupTXTCallback on_resolve,
                                    absl::string_view /*name*/) {
  engine_->Run([on_resolve = std::move(on_resolve)]() mutable {
    on_resolve(absl::UnimplementedError(
        "The Native resolver does not support looking up TXT records"));
  });
}

}  // namespace grpc_event_engine::experimental
