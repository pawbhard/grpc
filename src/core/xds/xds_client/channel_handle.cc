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

#include "src/core/xds/xds_client/channel_handle.h"

#include "absl/status/status.h"
#include "src/core/util/down_cast.h"

namespace grpc_core {

RefCountedPtr<XdsTransportInterface> GetTransportFromHandle(
    experimental::ChannelFactory::ChannelHandle* handle, absl::Status* status) {
  if (handle == nullptr) {
    *status = absl::InvalidArgumentError("channel handle is null");
    return nullptr;
  }
  ChannelHandleImpl* impl = DownCast<ChannelHandleImpl*>(handle);
  *status = impl->status();
  // Copying the RefCountedPtr takes a new reference. A lame handle holds no
  // transport, so this returns null with a non-OK *status.
  return impl->transport();
}

}  // namespace grpc_core
