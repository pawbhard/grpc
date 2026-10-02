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

#include "src/core/xds/grpc/xds_transport_grpc.h"

#include "src/core/xds/grpc/grpc_channel_transport.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/call/composite/composite_call_credentials.h"
#include "src/core/credentials/transport/channel_creds_registry.h"
#include "src/core/credentials/transport/composite/composite_channel_credentials.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_server_grpc_interface.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {

//
// GrpcXdsTransportFactory::GrpcXdsTransport
//

namespace {

RefCountedPtr<Channel> CreateXdsChannel(
    const ChannelArgs& args,
    CertificateProviderStoreInterface& certificate_provider_store,
    const GrpcXdsServerInterface& server) {
  RefCountedPtr<grpc_channel_credentials> channel_creds =
      CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
          server.channel_creds_config(), certificate_provider_store);
  ChannelArgs channel_args = args;
  const grpc_channel_args* child_args =
      args.GetPointer<grpc_channel_args>(GRPC_ARG_CHILD_CHANNEL_ARGS);
  if (child_args != nullptr) {
    channel_args = ChannelArgs::FromC(child_args).UnionWith(args);
  }
  return RefCountedPtr<Channel>(Channel::FromC(
      grpc_channel_create(server.server_uri().c_str(), channel_creds.get(),
                          channel_args.ToC().get())));
}

std::string GetChannelKey(const GrpcXdsServerInterface& server) {
  std::string result = "{server_uri=";
  absl::StrAppend(&result, server.server_uri());
  if (server.channel_creds_config() != nullptr) {
    absl::StrAppend(
        &result,
        ", channel_creds={type=", server.channel_creds_config()->type(),
        ", config=", server.channel_creds_config()->ToString(), "}");
  }
  absl::StrAppend(&result, "}");
  return result;
}

RefCountedPtr<grpc_call_credentials> GetCallCredsForTransport(
    const GrpcXdsServerInterface& server) {
  RefCountedPtr<grpc_call_credentials> call_creds;
  for (const auto& call_creds_config : server.call_creds_configs()) {
    RefCountedPtr<grpc_call_credentials> creds =
        CoreConfiguration::Get().call_creds_registry().CreateCallCreds(
            call_creds_config);
    if (call_creds == nullptr) {
      call_creds = std::move(creds);
    } else {
      call_creds = MakeRefCounted<grpc_composite_call_credentials>(
          std::move(call_creds), std::move(creds));
    }
  }
  return call_creds;
}

}  // namespace

class GrpcXdsTransportFactory::SharedChannel final
    : public RefCounted<SharedChannel> {
 public:
  SharedChannel(std::string key, RefCountedPtr<Channel> channel,
                WeakRefCountedPtr<GrpcXdsTransportFactory> factory)
      : key_(std::move(key)),
        channel_(std::move(channel)),
        factory_(std::move(factory)) {}

  ~SharedChannel() override {
    MutexLock lock(factory_->mu_);
    auto it = factory_->channels_.find(key_);
    if (it != factory_->channels_.end() && it->second == this) {
      factory_->channels_.erase(it);
    }
  }

  Channel* channel() const { return channel_.get(); }

  RefCountedPtr<Channel> channel_ref() const { return channel_; }

 private:
  std::string key_;
  RefCountedPtr<Channel> channel_;
  WeakRefCountedPtr<GrpcXdsTransportFactory> factory_;
};

GrpcXdsTransportFactory::GrpcXdsTransport::GrpcXdsTransport(
    WeakRefCountedPtr<GrpcXdsTransportFactory> factory,
    RefCountedPtr<SharedChannel> channel, const GrpcXdsServerInterface& server,
    absl::Status* status)
    : GrpcChannelTransport([&] {
        // The xDS server description is what makes this transport
        // xDS-specific: resolve its call credentials, metadata and timeout
        // here, then hand the generic transport everything it needs.
        GrpcChannelTransport::Params params;
        params.channel = channel->channel_ref();
        params.call_creds = GetCallCredsForTransport(server);
        params.initial_metadata = server.initial_metadata();
        params.timeout = server.timeout();
        params.interested_parties = factory->interested_parties();
        params.trace = GRPC_TRACE_FLAG_ENABLED(xds_client_refcount)
                           ? "GrpcXdsTransport"
                           : nullptr;
        return params;
      }()),
      factory_(std::move(factory)),
      key_(server.Key()),
      shared_channel_(std::move(channel)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[GrpcXdsTransport " << this << "] created";
  if (shared_channel_->channel()->IsLame()) {
    *status = absl::UnavailableError("xds client has a lame channel");
  }
}

GrpcXdsTransportFactory::GrpcXdsTransport::~GrpcXdsTransport() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[GrpcXdsTransport " << this << "] destroying";
}

void GrpcXdsTransportFactory::GrpcXdsTransport::Orphaned() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[GrpcXdsTransport " << this << "] orphaned";
  {
    MutexLock lock(factory_->mu_);
    auto it = factory_->transports_.find(key_);
    if (it != factory_->transports_.end() && it->second == this) {
      factory_->transports_.erase(it);
    }
  }
  // Do an async hop before unreffing.  This avoids a deadlock upon
  // shutdown in the case where the xDS channel is itself an xDS channel
  // (e.g., when using one control plane to find another control plane).
  grpc_event_engine::experimental::GetDefaultEventEngine()->Run(
      [self = WeakRefAsSubclass<GrpcXdsTransport>()]() mutable {
        ExecCtx exec_ctx;
        self.reset();
      });
}

//
// GrpcXdsTransportFactory
//

namespace {

ChannelArgs ModifyChannelArgs(const ChannelArgs& args) {
  return args.Set(GRPC_ARG_KEEPALIVE_TIME_MS, Duration::Minutes(5).millis());
}

}  // namespace

GrpcXdsTransportFactory::GrpcXdsTransportFactory(
    const ChannelArgs& args,
    RefCountedPtr<CertificateProviderStoreInterface> certificate_provider_store)
    : args_(ModifyChannelArgs(args)),
      certificate_provider_store_(std::move(certificate_provider_store)),
      interested_parties_(grpc_pollset_set_create()) {
  // Calling grpc_init to ensure gRPC does not shut down until the XdsClient is
  // destroyed.
  InitInternally();
}

GrpcXdsTransportFactory::~GrpcXdsTransportFactory() {
  grpc_pollset_set_destroy(interested_parties_);
  // Calling grpc_shutdown to ensure gRPC does not shut down until the XdsClient
  // is destroyed.
  ShutdownInternally();
}

RefCountedPtr<XdsTransportFactory::XdsTransport>
GrpcXdsTransportFactory::GetTransport(
    const XdsBootstrap::XdsServerTarget& server, absl::Status* status) {
  std::string key = server.Key();
  RefCountedPtr<GrpcXdsTransport> transport;
  MutexLock lock(mu_);
  auto it = transports_.find(key);
  if (it != transports_.end()) {
    transport = it->second->RefIfNonZero().TakeAsSubclass<GrpcXdsTransport>();
  }
  if (transport == nullptr) {
    const auto& grpc_server = DownCast<const GrpcXdsServerInterface&>(server);
    std::string channel_key = GetChannelKey(grpc_server);
    auto channel_it = channels_.find(channel_key);
    RefCountedPtr<SharedChannel> channel;
    if (channel_it != channels_.end()) {
      GRPC_TRACE_LOG(xds_client, INFO) << "[GrpcXdsTransportFactory " << this
                                       << "] found cached SharedChannel";
      channel = channel_it->second->RefIfNonZero();
    }
    if (channel == nullptr) {
      RefCountedPtr<Channel> raw_channel =
          CreateXdsChannel(args_, *certificate_provider_store_, grpc_server);
      GRPC_CHECK(raw_channel != nullptr);
      channel = MakeRefCounted<SharedChannel>(
          channel_key, std::move(raw_channel),
          WeakRefAsSubclass<GrpcXdsTransportFactory>());
      channels_[channel_key] = channel.get();
    }
    transport = MakeRefCounted<GrpcXdsTransport>(
        WeakRefAsSubclass<GrpcXdsTransportFactory>(), std::move(channel),
        grpc_server, status);
    transports_[std::move(key)] = transport.get();
  }
  return transport;
}

absl::StatusOr<std::string> GrpcXdsTransportFactory::RegisterTarget(
    std::shared_ptr<const GrpcXdsServerInterface> target) {
  if (target == nullptr) {
    return absl::InvalidArgumentError("xDS server target is null");
  }
  if (target->channel_creds_config() == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("xDS server target ", target->server_uri(),
                     " has no channel credentials config"));
  }
  MutexLock lock(mu_);
  for (const auto& registered : targets_) {
    if (registered.second->Equals(*target)) return registered.first;
  }
  std::string key = absl::StrCat("xds/", next_target_key_++);
  // `key` is copied rather than moved, because it is returned below: moving it
  // into the map would leave the caller with an empty key.
  targets_.emplace(key, std::move(target));
  return key;
}

RefCountedPtr<XdsTransportFactory::XdsTransport>
GrpcXdsTransportFactory::GetTransportByKey(absl::string_view key,
                                           absl::Status* status) {
  std::shared_ptr<const GrpcXdsServerInterface> target;
  {
    MutexLock lock(mu_);
    auto it = targets_.find(std::string(key));
    if (it != targets_.end()) target = it->second;
  }
  if (target == nullptr) {
    *status = absl::NotFoundError(
        absl::StrCat("no xDS server target registered for key ", key));
    return nullptr;
  }
  return GetTransport(*target, status);
}

}  // namespace grpc_core
