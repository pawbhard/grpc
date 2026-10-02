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

#ifndef GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_TRANSPORT_H
#define GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_TRANSPORT_H

#include <memory>
#include <string>

#include "src/core/util/dual_ref_counted.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport_interface.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// A factory for creating new XdsTransport instances.
class XdsTransportFactory : public DualRefCounted<XdsTransportFactory> {
 public:
  // Represents a transport for xDS communication (e.g., a gRPC channel).
  // NOTE: the interface itself lives in xds_transport_interface.h; this alias
  // is kept so that existing XdsTransportFactory::XdsTransport spellings keep
  // working and the extraction does not ripple through every caller.
  using XdsTransport = XdsTransportInterface;

  // Returns a transport for the specified server.  If there is already
  // a transport for the server, returns a new ref to that transport;
  // otherwise, creates a new transport.
  //
  // *status will be set if there is an error creating the channel,
  // although the returned channel must still accept calls (which may fail).
  virtual RefCountedPtr<XdsTransport> GetTransport(
      const XdsBootstrap::XdsServerTarget& server, absl::Status* status) = 0;

  // Associates `target` with an opaque key and returns that key, so that a
  // caller holding only the key can reach the transport later. An equal target
  // registered again returns the existing key, so both callers share one
  // channel.
  //
  // Not every factory can hold that association: one built without a bootstrap
  // has nowhere to keep it. The default reports that it is unsupported, which
  // lets callers that never need a key ignore this entirely.
  virtual absl::StatusOr<std::string> RegisterTarget(
      std::shared_ptr<const XdsBootstrap::XdsServerTarget> target) {
    return absl::UnimplementedError(
        "this xDS transport factory cannot register targets");
  }

  // Returns the transport registered under `key`, which must be a key produced
  // by RegisterTarget(). Returns null and sets `*status` when no target is
  // registered under that key.
  virtual RefCountedPtr<XdsTransport> GetTransportByKey(absl::string_view key,
                                                        absl::Status* status) {
    *status = absl::UnimplementedError(
        "this xDS transport factory cannot look up transports by key");
    return nullptr;
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_TRANSPORT_H
