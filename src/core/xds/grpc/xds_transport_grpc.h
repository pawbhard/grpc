//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_GRPC_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_GRPC_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <memory>
#include <string>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/xds/grpc/certificate_provider_store_interface.h"
#include "src/core/xds/grpc/grpc_channel_transport.h"
#include "src/core/xds/grpc/xds_server_grpc_interface.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class GrpcXdsTransportFactory final : public XdsTransportFactory {
 public:
  class GrpcXdsTransport;

  GrpcXdsTransportFactory(const ChannelArgs& args,
                          RefCountedPtr<CertificateProviderStoreInterface>
                              certificate_provider_store);
  ~GrpcXdsTransportFactory() override;

  void Orphaned() override {}

  RefCountedPtr<XdsTransport> GetTransport(
      const XdsBootstrap::XdsServerTarget& server,
      absl::Status* status) override;

  // Associates `target` with a key and returns that key. An equal target
  // registered again returns the existing key, so both callers share one
  // channel. Callers that hold only a key use GetTransportByKey() to reach the
  // transport.
  //
  // Fails if `target` is null or carries no channel credentials config, because
  // channel creation turns missing channel credentials into an insecure channel
  // rather than an error, so such a target must never reach it.
  absl::StatusOr<std::string> RegisterTarget(
      std::shared_ptr<const XdsBootstrap::XdsServerTarget> target) override;

  // Returns the transport registered under `key`. Returns null and sets
  // `*status` to a non-OK status when no target is registered under that key;
  // the public ChannelFactory adapter turns that into a lame handle.
  RefCountedPtr<XdsTransport> GetTransportByKey(absl::string_view key,
                                                absl::Status* status) override;

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

 private:
  class SharedChannel;

  ChannelArgs args_;
  RefCountedPtr<CertificateProviderStoreInterface> certificate_provider_store_;
  grpc_pollset_set* interested_parties_;

  Mutex mu_;
  absl::flat_hash_map<std::string /*XdsServerTarget key*/, GrpcXdsTransport*>
      transports_ ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_map<std::string /*Channel key*/, SharedChannel*> channels_
      ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_map<std::string /*Registered key*/,
                      std::shared_ptr<const XdsBootstrap::XdsServerTarget>>
      targets_ ABSL_GUARDED_BY(&mu_);
  uint64_t next_target_key_ ABSL_GUARDED_BY(&mu_) = 0;
};

// The xDS transport: a GrpcChannelTransport plus membership in the factory's
// transport cache. Everything about streaming, connectivity watching and
// backoff lives in the base class.
class GrpcXdsTransportFactory::GrpcXdsTransport final
    : public GrpcChannelTransport {
 public:
  GrpcXdsTransport(WeakRefCountedPtr<GrpcXdsTransportFactory> factory,
                   RefCountedPtr<SharedChannel> channel,
                   const GrpcXdsServerInterface& server, absl::Status* status);
  ~GrpcXdsTransport() override;

  void Orphaned() override;

 private:
  WeakRefCountedPtr<GrpcXdsTransportFactory> factory_;
  std::string key_;
  // Named apart from the base class's channel_ to keep it obvious which one is
  // the Channel and which is the cache entry that owns it.
  RefCountedPtr<SharedChannel> shared_channel_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_GRPC_H
