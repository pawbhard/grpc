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
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/grpc_channel_transport.h"
#include "src/core/xds/grpc/side_channel_cache.h"
#include "src/core/xds/xds_client/channel_handle.h"

namespace grpc_core {
namespace experimental {

namespace {

// Mutable because a channel argument stores a non-const key.
char kChannelFactoryArgName[] = "grpc.channel_factory";

// One cache for the whole process.
//
// There is no factory object on this path to own it, and the key is semantic
// rather than a caller-chosen string, so two unrelated callers asking for the
// same target with credentials that compare equal are asking for the same
// channel and correctly meet here. The cache drains itself as its entries die,
// so it holds only channels that something still refers to.
SideChannelCache& Cache() {
  static SideChannelCache* const cache = new SideChannelCache;
  return *cache;
}

}  // namespace

absl::string_view ChannelFactory::ChannelArgName() {
  return kChannelFactoryArgName;
}

grpc_arg CreateChannelFactoryChannelArg(ChannelFactory* factory) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = kChannelFactoryArgName;
  arg.value.pointer.p = factory;
  arg.value.pointer.vtable = ChannelArgTypeTraits<ChannelFactory>::VTable();
  return arg;
}

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
  SideChannelKey key;
  key.target = std::string(target);
  key.creds = creds->Ref();
  key.args = args == nullptr ? ChannelArgs() : ChannelArgs::FromC(args);
  key.initial_metadata.assign(initial_metadata.begin(), initial_metadata.end());
  // The cache owns the channel from here on, so a second request agreeing on
  // the key gets the channel that already exists instead of a second one.
  RefCountedPtr<GrpcChannelTransport> transport = Cache().GetOrCreate(
      key, [&key, creds, args]() -> GrpcChannelTransport::Params {
        GrpcChannelTransport::Params params;
        // grpc_channel_create() never returns null: when the channel cannot be
        // built it returns a lame channel, which is reported below.
        params.channel = RefCountedPtr<Channel>(Channel::FromC(
            grpc_channel_create(key.target.c_str(), creds, args)));
        // The transport sends this metadata on every call, so it is copied.
        params.initial_metadata = key.initial_metadata;
        // No call credentials are passed: `creds` already carries them, because
        // a composite channel credentials includes its call credentials.
        return params;
      });
  if (transport->channel()->IsLame()) {
    *status = absl::UnavailableError("side channel is lame");
  }
  return std::make_unique<ChannelHandleImpl>(std::move(transport));
}

std::unique_ptr<ChannelFactory::ChannelHandle> CreateLameChannelHandle(
    absl::string_view /*target*/, absl::Status status) {
  return std::make_unique<ChannelHandleImpl>(std::move(status));
}

}  // namespace experimental
}  // namespace grpc_core
