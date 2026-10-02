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

#include <grpc/channel_factory.h>
#include <grpc/grpc.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/grpc_channel_transport.h"
#include "src/core/xds/xds_client/channel_handle.h"

namespace grpc_core {
namespace experimental {

std::unique_ptr<ChannelFactory::ChannelHandle> CreateChannelHandle(
    absl::string_view target, grpc_channel_credentials* creds,
    const grpc_channel_args* args,
    absl::Span<const std::pair<std::string, std::string>> initial_metadata,
    absl::Status* status) {
  // Credentials are required. Treating null as "insecure" would silently open a
  // plaintext side channel, which is the outcome the xDS target registry
  // refuses as well; a caller that wants an insecure channel creates insecure
  // credentials explicitly.
  if (creds == nullptr) {
    *status = absl::InvalidArgumentError(
        "channel credentials are required to create a side channel; create "
        "insecure credentials explicitly for an insecure channel");
    return std::make_unique<ChannelHandleImpl>(*status);
  }
  // grpc_channel_create() never returns null. When the channel cannot be built
  // it returns a lame channel, which is reported through `*status` below rather
  // than by returning null.
  RefCountedPtr<Channel> channel = RefCountedPtr<Channel>(Channel::FromC(
      grpc_channel_create(std::string(target).c_str(), creds, args)));
  GrpcChannelTransport::Params params;
  params.channel = std::move(channel);
  // No call credentials are passed to the transport: `creds` already carries
  // them, because a composite channel credentials includes its call credentials
  // and the channel applies them itself.
  params.initial_metadata.assign(initial_metadata.begin(),
                                 initial_metadata.end());
  // The transport's defaults apply: no deadline, and because no polling
  // entities are supplied it creates and owns its own.
  if (params.channel->IsLame()) {
    *status = absl::UnavailableError("side channel is lame");
  }
  return std::make_unique<ChannelHandleImpl>(
      MakeRefCounted<GrpcChannelTransport>(std::move(params)));
}

std::unique_ptr<ChannelFactory::ChannelHandle> CreateLameChannelHandle(
    absl::string_view /*target*/, absl::Status status) {
  return std::make_unique<ChannelHandleImpl>(std::move(status));
}

}  // namespace experimental
}  // namespace grpc_core
