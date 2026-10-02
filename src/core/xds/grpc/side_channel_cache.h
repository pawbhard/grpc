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

#ifndef GRPC_SRC_CORE_XDS_GRPC_SIDE_CHANNEL_CACHE_H
#define GRPC_SRC_CORE_XDS_GRPC_SIDE_CHANNEL_CACHE_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/function_ref.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/grpc/grpc_channel_transport.h"

struct grpc_channel_credentials;

namespace grpc_core {

// Everything needed to build a non-xDS side channel, so that two requests
// agreeing on all of it end up sharing one channel.
struct SideChannelKey {
  std::string target;
  RefCountedPtr<grpc_channel_credentials> creds;
  ChannelArgs args;
  std::vector<std::pair<std::string, std::string>> initial_metadata;

  // Ordered by target, then by credential equivalence, then by args, then by
  // metadata. Credentials are compared with the comparison gRPC already uses to
  // decide that two credential objects may be treated as the same; that
  // comparison, and not a string, is the only identity live credentials have.
  bool operator<(const SideChannelKey& other) const;
};

// One transport per distinct non-xDS side channel.
//
// The map owns nothing. It holds a raw pointer to an entry, and each entry
// removes its own row when its last reference goes away, exactly as the xDS
// transport cache does, so the cache drains by itself and never pins a channel
// open. A lookup takes a reference only if the entry is still alive, so a row
// whose entry is being torn down is dropped rather than resurrected.
class SideChannelCache final : public RefCounted<SideChannelCache> {
 public:
  // Returns the transport for `key`, building one from `make_params` on a miss.
  // `make_params` is called without the lock held, because it creates a channel.
  RefCountedPtr<GrpcChannelTransport> GetOrCreate(
      const SideChannelKey& key,
      absl::FunctionRef<GrpcChannelTransport::Params()> make_params);

  // Removes `transport` if it is still the entry for `key`. Called by the entry
  // itself when it is orphaned.
  void Remove(const SideChannelKey& key, GrpcChannelTransport* transport);

 private:
  Mutex mu_;
  std::map<SideChannelKey, GrpcChannelTransport*> transports_
      ABSL_GUARDED_BY(&mu_);
};

// A cached transport, which deregisters itself when its last reference goes
// away. Its key is a copy because it has to find its own row at that point.
class CachedSideChannelTransport final : public GrpcChannelTransport {
 public:
  CachedSideChannelTransport(RefCountedPtr<SideChannelCache> cache,
                             SideChannelKey key, Params params);

  void Orphaned() override;

 private:
  RefCountedPtr<SideChannelCache> cache_;
  SideChannelKey key_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_SIDE_CHANNEL_CACHE_H
