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

#ifndef GRPC_SRC_CORE_XDS_XDS_CLIENT_CHANNEL_HANDLE_H
#define GRPC_SRC_CORE_XDS_XDS_CLIENT_CHANNEL_HANDLE_H

#include <grpc/channel_factory.h>

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_transport_interface.h"

namespace grpc_core {

// The concrete ChannelHandle that gRPC core interprets.
//
// This is the only handle type gRPC produces, so core can consume any handle
// without knowing which factory produced it. A handle either owns a transport,
// or carries the status of a lame channel when no transport could be produced;
// in the latter case transport() is null and status() is non-OK.
class ChannelHandleImpl final
    : public experimental::ChannelFactory::ChannelHandle {
 public:
  explicit ChannelHandleImpl(RefCountedPtr<XdsTransportInterface> transport)
      : transport_(std::move(transport)) {}

  // Creates a lame handle that carries `status`, which a consumer reports to its
  // caller in place of a transport.
  explicit ChannelHandleImpl(absl::Status status)
      : status_(std::move(status)) {}

  // Null when this handle is lame.
  const RefCountedPtr<XdsTransportInterface>& transport() const {
    return transport_;
  }

  // OK when transport() is non-null.
  const absl::Status& status() const { return status_; }

 private:
  RefCountedPtr<XdsTransportInterface> transport_;
  absl::Status status_;
};

// The bridge gRPC core uses to reach the transport behind a handle.
//
// Returns null if `handle` is null or lame, setting `*status` to a non-OK status
// that describes why. `handle` must have come from a gRPC-provided factory; the
// public ChannelFactory header documents that a handle cannot be constructed
// directly.
RefCountedPtr<XdsTransportInterface> GetXdsTransport(
    experimental::ChannelFactory::ChannelHandle* handle, absl::Status* status);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_XDS_CLIENT_CHANNEL_HANDLE_H
