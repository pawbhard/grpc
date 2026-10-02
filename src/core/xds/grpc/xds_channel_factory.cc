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

#include "src/core/xds/grpc/xds_channel_factory.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/xds/xds_client/channel_handle.h"

namespace grpc_core {

XdsChannelFactory::XdsChannelFactory(
    RefCountedPtr<XdsTransportFactory> transport_factory)
    : transport_factory_(std::move(transport_factory)) {}

absl::StatusOr<std::string> XdsChannelFactory::RegisterTarget(
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> target) {
  return transport_factory_->RegisterTarget(std::move(target));
}

std::unique_ptr<experimental::ChannelFactory::ChannelHandle>
XdsChannelFactory::CreateChannel(absl::string_view key, absl::Status* status) {
  RefCountedPtr<XdsTransportInterface> transport =
      transport_factory_->GetTransportByKey(key, status);
  if (transport == nullptr) {
    // No target is registered under this key. The public API promises a handle
    // rather than null, so the failure is reported through a lame handle.
    return std::make_unique<ChannelHandleImpl>(*status);
  }
  return std::make_unique<ChannelHandleImpl>(std::move(transport));
}

}  // namespace grpc_core
