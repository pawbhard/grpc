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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_CHANNEL_FACTORY_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_CHANNEL_FACTORY_H

#include <grpc/channel_factory.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_transport.h"

namespace grpc_core {

// The xDS implementation of the public ChannelFactory.
//
// It is deliberately thin. The association from an opaque key to an xDS server
// target, and the caching of the transports themselves, both live in
// GrpcXdsTransportFactory, next to the caches it already keeps. This class only
// gives that machinery the shape the public API requires, so that there is one
// owner of xDS keying and one place where its lifetime is decided.
class XdsChannelFactory final : public experimental::ChannelFactory {
 public:
  explicit XdsChannelFactory(
      RefCountedPtr<XdsTransportFactory> transport_factory);

  // Registers `target` and returns the key that resolves to it.
  //
  // See XdsTransportFactory::RegisterTarget() for the dedup and validation
  // rules. This only forwards, so that callers of the public API never need the
  // concrete transport factory type.
  absl::StatusOr<std::string> RegisterTarget(
      std::shared_ptr<const XdsBootstrap::XdsServerTarget> target);

  std::unique_ptr<ChannelHandle> CreateChannel(
      absl::string_view key, absl::Status* status) override;

 private:
  RefCountedPtr<XdsTransportFactory> transport_factory_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_CHANNEL_FACTORY_H
