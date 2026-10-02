//
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
//

#include "src/core/xds/grpc/side_channel_cache.h"

#include <utility>

#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/util/grpc_check.h"

namespace grpc_core {

bool SideChannelKey::operator<(const SideChannelKey& other) const {
  if (target != other.target) return target < other.target;
  const int creds_cmp = creds->cmp(other.creds.get());
  if (creds_cmp != 0) return creds_cmp < 0;
  if (args != other.args) return args < other.args;
  return initial_metadata < other.initial_metadata;
}

RefCountedPtr<GrpcChannelTransport> SideChannelCache::GetOrCreate(
    const SideChannelKey& key,
    absl::FunctionRef<GrpcChannelTransport::Params()> make_params) {
  {
    MutexLock lock(mu_);
    auto it = transports_.find(key);
    if (it != transports_.end()) {
      RefCountedPtr<GrpcChannelTransport> entry =
          it->second->RefIfNonZero().TakeAsSubclass<GrpcChannelTransport>();
      if (entry != nullptr) return entry;
      // The entry is going away, so its own Orphaned() will not find its row.
      // Drop the row here instead of leaving a dangling pointer behind.
      transports_.erase(it);
    }
  }
  RefCountedPtr<GrpcChannelTransport> transport =
      MakeRefCounted<CachedSideChannelTransport>(Ref(), key, make_params());
  GRPC_CHECK(transport != nullptr);
  {
    MutexLock lock(mu_);
    transports_[key] = transport.get();
  }
  return transport;
}

void SideChannelCache::Remove(const SideChannelKey& key,
                              GrpcChannelTransport* transport) {
  MutexLock lock(mu_);
  auto it = transports_.find(key);
  if (it != transports_.end() && it->second == transport) {
    transports_.erase(it);
  }
}

CachedSideChannelTransport::CachedSideChannelTransport(
    RefCountedPtr<SideChannelCache> cache, SideChannelKey key, Params params)
    : GrpcChannelTransport(std::move(params)),
      cache_(std::move(cache)),
      key_(std::move(key)) {}

void CachedSideChannelTransport::Orphaned() { cache_->Remove(key_, this); }

}  // namespace grpc_core
