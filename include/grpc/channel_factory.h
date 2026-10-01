// Copyright 2026 The gRPC Authors
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

#ifndef GRPC_CHANNEL_FACTORY_H
#define GRPC_CHANNEL_FACTORY_H

#include <grpc/grpc.h>
#include <grpc/impl/grpc_types.h>

#ifdef __cplusplus

#include <memory>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace experimental {

/**
 * EXPERIMENTAL API - Subject to change
 */
///
/// A factory for creating channels from an opaque key.
///
/// A ChannelFactory is how a caller supplies channel creation to gRPC for
/// side-channel communication. In the non-xDS case the application implements
/// this interface and passes it to gRPC through channel args. In the xDS case
/// gRPC supplies the implementation itself, following the same flow as the xDS
/// transport factory.
///
/// CreateChannel() is the only thing an implementation provides. A
/// ChannelHandle cannot be constructed directly; to return one, call the
/// gRPC-provided CreateChannelHandle() or CreateLameChannelHandle() below.
class ChannelFactory {
 public:
  /**
   * EXPERIMENTAL API - Subject to change
   */
  ///
  /// An opaque handle to a channel created by a ChannelFactory.
  ///
  /// The caller holds the handle and only gRPC core interprets it: internally
  /// it stands for a transport, a gRPC channel adapted to one, or a lame
  /// channel. Callers must treat it as a token; do not derive from it, downcast
  /// it, or depend on what it points at.
  class ChannelHandle {
   public:
    virtual ~ChannelHandle() = default;
  };

  virtual ~ChannelFactory() = default;

  /**
   * EXPERIMENTAL API - Subject to change
   */
  ///
  /// Returns a handle for the channel identified by `key`.
  ///
  /// `key` carries everything needed to create the channel -- the target, the
  /// credentials and any per-channel metadata -- in an encoding chosen by the
  /// implementation and shared with the code that builds the key. The key is
  /// read only for the duration of this call.
  ///
  /// A null handle is never returned, so callers do not need a null check. If
  /// the channel cannot be created, the returned handle stands for a lame
  /// channel: every call on it fails with the error described by `*status`,
  /// which is set to a non-OK status. On success `*status` is left OK.
  ///
  /// An implementation usually decodes `key` and then hands the decoded target,
  /// credentials and channel args to CreateChannelHandle(), or returns
  /// CreateLameChannelHandle() if `key` cannot be decoded.
  ///
  /// Implementations must ensure that calls with the same key refer to the same
  /// underlying channel, which is created on first use and kept alive by the
  /// handles that refer to it.
  ///
  /// The returned handle owns everything it needs and remains valid for as long
  /// as it is held, including after this factory is destroyed.
  ///
  /// This method is thread-safe and may be called concurrently.
  virtual std::unique_ptr<ChannelHandle> CreateChannel(
      absl::string_view key, absl::Status* status) = 0;
};

/**
 * EXPERIMENTAL API - Subject to change
 */
///
/// gRPC-provided helper for implementing ChannelFactory::CreateChannel().
///
/// Creates a channel for `target` and returns a handle that owns it. This is
/// not an interface and not virtual: gRPC supplies it, and an implementation of
/// CreateChannel() only calls it to hand back a working channel.
///
/// `creds` may be null for an insecure channel, and `args` may be null. Both are
/// read only for the duration of this call; the returned handle keeps whatever
/// it needs from them alive.
///
/// A null handle is never returned. If the channel cannot be created, the
/// returned handle stands for a lame channel and `*status` is set to a non-OK
/// status describing the failure; on success `*status` is left OK. This mirrors
/// grpc_channel_create(), which also yields a lame channel rather than null, but
/// reports the reason through `*status` so that the caller can propagate it.
std::unique_ptr<ChannelFactory::ChannelHandle> CreateChannelHandle(
    absl::string_view target, grpc_channel_credentials* creds,
    const grpc_channel_args* args, absl::Status* status);

/**
 * EXPERIMENTAL API - Subject to change
 */
///
/// gRPC-provided helper for implementing ChannelFactory::CreateChannel().
///
/// Creates a handle for a channel on which every call fails with `status`. An
/// implementation calls it when it cannot create a channel at all -- for example
/// because it could not decode `key`.
///
/// `target` only identifies the channel in logs and traces; when `key` is the
/// most specific identifier available, pass it as `target`. `status` must be
/// non-OK.
std::unique_ptr<ChannelFactory::ChannelHandle> CreateLameChannelHandle(
    absl::string_view target, absl::Status status);

}  // namespace experimental
}  // namespace grpc_core

#endif  // __cplusplus

#endif  // GRPC_CHANNEL_FACTORY_H
