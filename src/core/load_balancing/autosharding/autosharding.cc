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

// Implementation of the autosharding_experimental LB policy, as described
// in gRFC A119 (https://github.com/grpc/proposal/pull/551). Modeled
// directly on src/core/load_balancing/ring_hash/ring_hash.cc, per
// discussion on that gRFC: the LB policy talks to an external sharding
// service to receive an Assignment (key-range -> endpoint-set mapping),
// and routes each RPC by extracting a key from a configured header,
// finding the matching key-range, and delegating to a (uniform-)randomly
// chosen endpoint from it, or from a fallback pool of all endpoints.
//
// TODO(https://github.com/grpc/proposal/pull/551): This initial pass
// implements everything that doesn't depend on the `Shard` gRPC stream
// used to receive Assignments from the sharding service, since that
// depends on the (currently TBD) wire proto and the C++/C-core "Channel
// Factory" API used to reach the sharding service. Until that lands,
// nothing ever populates assignment_, so this policy always operates in
// fallback mode (or fails picks, if fallback is disabled).

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/pick_first/pick_first.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kAutoSharding = "autosharding_experimental";

class AutoShardingLbConfig final : public LoadBalancingPolicy::Config {
 public:
  AutoShardingLbConfig() = default;

  AutoShardingLbConfig(const AutoShardingLbConfig&) = delete;
  AutoShardingLbConfig& operator=(const AutoShardingLbConfig&) = delete;
  AutoShardingLbConfig(AutoShardingLbConfig&&) = delete;
  AutoShardingLbConfig& operator=(AutoShardingLbConfig&&) = delete;

  absl::string_view name() const override { return kAutoSharding; }

  // TODO(https://github.com/grpc/proposal/pull/551): Unused until the
  // `Shard` stream and the C++/C-core "Channel Factory" API used to reach
  // the sharding service exist.
  const std::string& channel_factory_key() const {
    return channel_factory_key_;
  }
  const std::string& slicing_target() const { return slicing_target_; }
  // TODO(https://github.com/grpc/proposal/pull/551): Unused until the
  // fallback-at-startup timer is implemented (its trigger point, per the
  // gRFC, is tied to `Shard` stream/channel creation, which doesn't exist
  // yet).
  Duration initial_assignment_timeout() const {
    return initial_assignment_timeout_;
  }

  const std::string& slice_key_header_name() const {
    return slice_key_header_name_;
  }
  bool enable_fallback() const { return enable_fallback_; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<AutoShardingLbConfig>()
            .OptionalField("channelFactoryKey",
                           &AutoShardingLbConfig::channel_factory_key_)
            .OptionalField("slicingTarget",
                           &AutoShardingLbConfig::slicing_target_)
            .OptionalField("sliceKeyHeaderName",
                           &AutoShardingLbConfig::slice_key_header_name_)
            .OptionalField("enableFallback",
                           &AutoShardingLbConfig::enable_fallback_)
            .OptionalField(
                "initialAssignmentTimeout",
                &AutoShardingLbConfig::initial_assignment_timeout_)
            .Finish();
    return loader;
  }

  void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors) {
    if (slice_key_header_name_.empty()) {
      ValidationErrors::ScopedField field(errors, ".sliceKeyHeaderName");
      errors->AddError("field not present");
    }
  }

 private:
  std::string channel_factory_key_;
  std::string slicing_target_;
  std::string slice_key_header_name_;
  bool enable_fallback_ = false;
  Duration initial_assignment_timeout_ = Duration::Seconds(60);
};

//
// AutoSharding LB policy
//
// Per gRFC A119 review feedback (see
// https://github.com/grpc/grpc/pull/43061), all of the policy's data
// structures live inside this class -- mirroring ring_hash's Ring /
// RingHashEndpoint / Picker -- rather than as free-standing types in the
// grpc_core namespace.
//

class AutoSharding final : public LoadBalancingPolicy {
 public:
  explicit AutoSharding(Args args);

  absl::string_view name() const override { return kAutoSharding; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  // A single non-overlapping key-range within an Assignment, together
  // with the endpoints (by index into Assignment::endpoint_names) that
  // serve it.
  struct Slice {
    std::string start_key;  // Inclusive.
    std::vector<size_t> endpoint_indices;  // Indices into
                                            // Assignment::endpoint_names.
  };

  // A complete key-range -> endpoint assignment, as would be received (in
  // chunked form) from a sharding service over the `Shard` stream.
  //
  // TODO(https://github.com/grpc/proposal/pull/551): Nothing constructs
  // one of these yet; see the file comment above.
  struct Assignment {
    std::vector<Slice> slices;  // Sorted by start_key; no gaps.
    std::vector<std::string> endpoint_names;
    int64_t generation = 0;
  };

  // A read-only, order-preserving view of the current endpoint set, giving
  // SliceMap construction access to the hostname -> endpoint-index mapping
  // without depending on EndpointState/pick_first management.
  struct EndpointIndexMap {
    absl::flat_hash_map<std::string, size_t> index_by_hostname;
    size_t num_endpoints = 0;
  };

  // One key-range entry in a SliceMap. Endpoints are referenced by index
  // into the same endpoint list that EndpointIndexMap and PickerEndpoint
  // indices refer to.
  struct SliceEntry {
    std::string start_key;
    std::vector<size_t> endpoint_indices;
  };

  // An immutable, lookup-optimized view of an Assignment (or, absent one,
  // just the fallback pool of all known endpoints). Safe for the Picker to
  // use without synchronizing with the LB policy. Ref-counted (rather than
  // copied) so that a new Picker built in response to a child connectivity
  // update -- which reuses the existing SliceMap -- doesn't need to copy
  // it; this mirrors ring_hash's Ring.
  class SliceMap final : public RefCounted<SliceMap> {
   public:
    SliceMap(const EndpointIndexMap& endpoints, const Assignment* assignment);

    // Returns the index into slices() of the entry covering `key`, or
    // std::nullopt if no assignment has been received yet (i.e., there
    // are no slices at all).
    std::optional<size_t> Lookup(absl::string_view key) const;

    const std::vector<SliceEntry>& slices() const { return slices_; }
    const std::vector<size_t>& fallback_pool() const { return fallback_pool_; }

   private:
    std::vector<SliceEntry> slices_;
    std::vector<size_t> fallback_pool_;
    int64_t generation_ = 0;
  };

  // State for a particular endpoint. Delegates to a pick_first child
  // policy, created lazily.
  class Endpoint final : public InternallyRefCounted<Endpoint> {
   public:
    Endpoint(RefCountedPtr<AutoSharding> parent, size_t index)
        : parent_(std::move(parent)), index_(index) {}

    void Orphan() override;

    size_t index() const { return index_; }

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    // index is this endpoint's new index into AutoSharding::endpoints_.
    absl::Status UpdateLocked(size_t index);

    void ResetBackoffLocked();

    // If the child policy does not yet exist, creates it; otherwise, asks
    // it to exit IDLE. Must be called from within the control-plane
    // work_serializer.
    void RequestConnectionLocked();

    // Information about this endpoint to be stored in the picker.
    struct PickerEndpoint {
      grpc_connectivity_state state;
      RefCountedPtr<SubchannelPicker> picker;
      RefCountedPtr<Endpoint> endpoint;
    };
    PickerEndpoint GetInfoForPicker() {
      return {connectivity_state_, picker_, Ref(DEBUG_LOCATION, "picker")};
    }

   private:
    class Helper;

    void CreateChildPolicy();
    absl::Status UpdateChildPolicyLocked();

    void OnStateUpdate(grpc_connectivity_state new_state,
                        const absl::Status& status,
                        RefCountedPtr<SubchannelPicker> picker);

    RefCountedPtr<AutoSharding> parent_;
    size_t index_;

    OrphanablePtr<LoadBalancingPolicy> child_policy_;
    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    RefCountedPtr<SubchannelPicker> picker_;
  };

  using PickerEndpoint = Endpoint::PickerEndpoint;

  class Picker final : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<AutoSharding> parent, RefCountedPtr<SliceMap> slice_map,
           std::vector<PickerEndpoint> endpoints,
           std::string slice_key_header_name, bool fallback_enabled,
           std::string resolution_note);

    PickResult Pick(PickArgs args) override;

   private:
    // A fire-and-forget class that schedules an endpoint connection
    // attempt on the control-plane WorkSerializer. Mirrors ring_hash's
    // EndpointConnectionAttempter.
    class EndpointConnectionAttempter final {
     public:
      EndpointConnectionAttempter(RefCountedPtr<AutoSharding> parent,
                                  RefCountedPtr<Endpoint> endpoint)
          : parent_(std::move(parent)), endpoint_(std::move(endpoint)) {
        // Hop into ExecCtx, so that we're not holding the data plane
        // mutex while we run control-plane code.
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

     private:
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<EndpointConnectionAttempter*>(arg);
        self->parent_->work_serializer()->Run([self]() {
          if (!self->parent_->shutdown_) {
            self->endpoint_->RequestConnectionLocked();
          }
          delete self;
        });
      }

      RefCountedPtr<AutoSharding> parent_;
      RefCountedPtr<Endpoint> endpoint_;
      grpc_closure closure_;
    };

    bool PoolInFallback(const std::vector<size_t>& indices) const;
    PickResult PickFromEndpointIndices(const std::vector<size_t>& indices,
                                       PickArgs args);
    // Appends resolution_note_ to status's message, if non-empty.
    absl::Status AddResolutionNote(absl::Status status) const;

    RefCountedPtr<AutoSharding> parent_;
    RefCountedPtr<SliceMap> slice_map_;
    std::vector<PickerEndpoint> endpoints_;
    std::string slice_key_header_name_;
    bool fallback_enabled_;
    std::string resolution_note_;
    std::vector<bool> slice_in_fallback_;  // Precomputed, indexed like
                                            // slice_map_->slices().
  };

  ~AutoSharding() override = default;

  void ShutdownLocked() override;

  static std::string HostnameForEndpoint(const EndpointAddresses& endpoint);

  EndpointIndexMap BuildEndpointIndexMap() const;
  std::vector<PickerEndpoint> BuildPickerEndpoints() const;
  void UpdateAggregatedConnectivityStateLocked(absl::Status status);

  // Current endpoint list and channel args.
  EndpointAddressesList endpoints_;
  ChannelArgs args_;
  RefCountedPtr<AutoShardingLbConfig> config_;

  // TODO(https://github.com/grpc/proposal/pull/551): Never populated in
  // this initial implementation; see the file comment above.
  std::optional<Assignment> assignment_;
  RefCountedPtr<SliceMap> slice_map_;

  // Keyed by endpoint hostname, per gRFC A119.
  std::map<std::string, OrphanablePtr<Endpoint>> endpoint_map_;
  std::string resolution_note_;

  absl::Status last_failure_;
  bool shutdown_ = false;
};

//
// AutoSharding::SliceMap
//

AutoSharding::SliceMap::SliceMap(const EndpointIndexMap& endpoints,
                                 const Assignment* assignment) {
  fallback_pool_.reserve(endpoints.num_endpoints);
  for (size_t i = 0; i < endpoints.num_endpoints; ++i) {
    fallback_pool_.push_back(i);
  }
  // If no assignment has been received yet (startup/fallback case), leave
  // slices_ empty; Lookup() will then always return std::nullopt.
  if (assignment == nullptr) return;
  generation_ = assignment->generation;
  slices_.reserve(assignment->slices.size());
  for (const Slice& slice : assignment->slices) {
    SliceEntry entry;
    entry.start_key = slice.start_key;
    entry.endpoint_indices.reserve(slice.endpoint_indices.size());
    for (size_t idx : slice.endpoint_indices) {
      if (idx >= assignment->endpoint_names.size()) continue;
      const std::string& hostname = assignment->endpoint_names[idx];
      auto it = endpoints.index_by_hostname.find(hostname);
      // Drop hostnames not present in the current endpoint set.
      if (it != endpoints.index_by_hostname.end()) {
        entry.endpoint_indices.push_back(it->second);
      }
    }
    slices_.push_back(std::move(entry));
  }
}

std::optional<size_t> AutoSharding::SliceMap::Lookup(
    absl::string_view key) const {
  // Handle the startup/fallback case where there are no assignments.
  if (slices_.empty()) return std::nullopt;
  // Binary search for the first slice whose start_key is greater than key.
  auto it = std::upper_bound(
      slices_.begin(), slices_.end(), key,
      [](absl::string_view k, const SliceEntry& entry) {
        return k < entry.start_key;
      });
  // Assignments are required to have no gaps and to cover the full key
  // range, so this should never happen for a valid assignment. Guard
  // against it anyway, since validation isn't implemented yet (it happens
  // when an Assignment is received over the `Shard` stream; see the file
  // comment above).
  if (it == slices_.begin()) return std::nullopt;
  // key falls in [slices_[idx - 1].start_key, slices_[idx].start_key).
  return static_cast<size_t>(it - slices_.begin()) - 1;
}

//
// AutoSharding::Endpoint::Helper
//

class AutoSharding::Endpoint::Helper final
    : public LoadBalancingPolicy::DelegatingChannelControlHelper {
 public:
  explicit Helper(RefCountedPtr<Endpoint> endpoint)
      : endpoint_(std::move(endpoint)) {}

  ~Helper() override { endpoint_.reset(DEBUG_LOCATION, "Helper"); }

  void UpdateState(
      grpc_connectivity_state state, const absl::Status& status,
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
    endpoint_->OnStateUpdate(state, status, std::move(picker));
  }

 private:
  LoadBalancingPolicy::ChannelControlHelper* parent_helper() const override {
    return endpoint_->parent_->channel_control_helper();
  }

  RefCountedPtr<Endpoint> endpoint_;
};

//
// AutoSharding::Endpoint
//

void AutoSharding::Endpoint::Orphan() {
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     parent_->interested_parties());
    child_policy_.reset();
    picker_.reset();
  }
  Unref();
}

absl::Status AutoSharding::Endpoint::UpdateLocked(size_t index) {
  index_ = index;
  if (child_policy_ == nullptr) return absl::OkStatus();
  return UpdateChildPolicyLocked();
}

void AutoSharding::Endpoint::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void AutoSharding::Endpoint::RequestConnectionLocked() {
  if (child_policy_ == nullptr) {
    CreateChildPolicy();
  } else {
    child_policy_->ExitIdleLocked();
  }
}

void AutoSharding::Endpoint::CreateChildPolicy() {
  GRPC_CHECK(child_policy_ == nullptr);
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = parent_->work_serializer();
  lb_policy_args.args =
      parent_->args_
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX, true);
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  child_policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "pick_first", std::move(lb_policy_args));
  // Add our interested_parties pollset_set to that of the newly created
  // child policy, so the child policy progresses upon activity tied to
  // the application's call.
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   parent_->interested_parties());
  absl::Status status = UpdateChildPolicyLocked();
  if (!status.ok()) {
    parent_->channel_control_helper()->RequestReresolution();
  }
}

absl::Status AutoSharding::Endpoint::UpdateChildPolicyLocked() {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray(
              {Json::FromObject({{"pick_first", Json::FromObject({})}})}));
  GRPC_CHECK(config.ok());
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses =
      std::make_shared<SingleEndpointIterator>(parent_->endpoints_[index_]);
  update_args.args = parent_->args_;
  update_args.config = std::move(*config);
  return child_policy_->UpdateLocked(std::move(update_args));
}

void AutoSharding::Endpoint::OnStateUpdate(grpc_connectivity_state new_state,
                                            const absl::Status& status,
                                            RefCountedPtr<SubchannelPicker> picker) {
  if (child_policy_ == nullptr) return;  // Already orphaned.
  connectivity_state_ = new_state;
  picker_ = std::move(picker);
  parent_->UpdateAggregatedConnectivityStateLocked(status);
}

//
// AutoSharding::Picker
//

AutoSharding::Picker::Picker(RefCountedPtr<AutoSharding> parent,
                             RefCountedPtr<SliceMap> slice_map,
                             std::vector<PickerEndpoint> endpoints,
                             std::string slice_key_header_name,
                             bool fallback_enabled,
                             std::string resolution_note)
    : parent_(std::move(parent)),
      slice_map_(std::move(slice_map)),
      endpoints_(std::move(endpoints)),
      slice_key_header_name_(std::move(slice_key_header_name)),
      fallback_enabled_(fallback_enabled),
      resolution_note_(std::move(resolution_note)) {
  slice_in_fallback_.reserve(slice_map_->slices().size());
  for (const SliceEntry& entry : slice_map_->slices()) {
    slice_in_fallback_.push_back(PoolInFallback(entry.endpoint_indices));
  }
}

bool AutoSharding::Picker::PoolInFallback(
    const std::vector<size_t>& indices) const {
  // A pool is in fallback if it contains zero endpoints, or if all
  // endpoints assigned to it are in TRANSIENT_FAILURE.
  if (indices.empty()) return true;
  return absl::c_all_of(indices, [this](size_t i) {
    return endpoints_[i].state == GRPC_CHANNEL_TRANSIENT_FAILURE;
  });
}

absl::Status AutoSharding::Picker::AddResolutionNote(
    absl::Status status) const {
  if (resolution_note_.empty()) return status;
  return absl::Status(status.code(),
                      absl::StrCat(status.message(), " (", resolution_note_,
                                   ")"));
}

LoadBalancingPolicy::PickResult AutoSharding::Picker::Pick(PickArgs args) {
  std::string buffer;
  auto key = args.initial_metadata->Lookup(slice_key_header_name_, &buffer);
  if (!key.has_value()) {
    return PickResult::Fail(AddResolutionNote(absl::UnavailableError(absl::StrCat(
        "autosharding: request is missing required header \"",
        slice_key_header_name_, "\""))));
  }
  std::optional<size_t> slice_idx = slice_map_->Lookup(*key);
  // No assignment covers this key. This is expected any time a valid
  // assignment hasn't been received yet from the sharding service (which,
  // in this initial implementation, is always -- see the file comment
  // above).
  if (!slice_idx.has_value()) {
    if (fallback_enabled_) {
      return PickFromEndpointIndices(slice_map_->fallback_pool(), args);
    }
    return PickResult::Fail(AddResolutionNote(
        absl::UnavailableError("autosharding: no assignment available")));
  }
  // Matching key range is in fallback mode and fallback is enabled.
  if (slice_in_fallback_[*slice_idx] && fallback_enabled_) {
    return PickFromEndpointIndices(slice_map_->fallback_pool(), args);
  }
  // Delegate to the assigned endpoints for the matching key range. When
  // the matching key range is in fallback but fallback is disabled, this
  // yields a better error message than failing outright here.
  return PickFromEndpointIndices(
      slice_map_->slices()[*slice_idx].endpoint_indices, args);
}

LoadBalancingPolicy::PickResult AutoSharding::Picker::PickFromEndpointIndices(
    const std::vector<size_t>& indices, PickArgs args) {
  // This can be true only when the matching entry is in fallback mode (due
  // to having zero endpoints) and fallback is disabled.
  if (indices.empty()) {
    return PickResult::Fail(AddResolutionNote(
        absl::UnavailableError("autosharding: no endpoints available")));
  }
  size_t first_index = absl::Uniform<size_t>(SharedBitGen(), 0, indices.size());
  bool requested_connection = false;
  bool found_connecting = false;
  for (size_t i = 0; i < indices.size(); ++i) {
    const PickerEndpoint& endpoint =
        endpoints_[indices[(first_index + i) % indices.size()]];
    // If READY, use immediately (happy path).
    if (endpoint.state == GRPC_CHANNEL_READY) {
      return endpoint.picker->Pick(args);
    }
    if (endpoint.state == GRPC_CHANNEL_CONNECTING) {
      found_connecting = true;
    }
    // If IDLE, trigger a connection attempt (at most one per pick).
    if (!requested_connection && endpoint.state == GRPC_CHANNEL_IDLE) {
      new EndpointConnectionAttempter(parent_, endpoint.endpoint);
      requested_connection = true;
    }
  }
  // No READY endpoint was found, but we either triggered a connection
  // attempt or found one already in progress; queue the pick.
  if (requested_connection || found_connecting) {
    return PickResult::Queue();
  }
  // All endpoints are in TRANSIENT_FAILURE. Delegate to the picker of the
  // randomly chosen starting endpoint to get a detailed error message.
  return endpoints_[indices[first_index]].picker->Pick(args);
}

//
// AutoSharding
//

AutoSharding::AutoSharding(Args args) : LoadBalancingPolicy(std::move(args)) {}

void AutoSharding::ShutdownLocked() {
  shutdown_ = true;
  endpoint_map_.clear();
}

void AutoSharding::ResetBackoffLocked() {
  for (const auto& [_, endpoint] : endpoint_map_) {
    endpoint->ResetBackoffLocked();
  }
}

std::string AutoSharding::HostnameForEndpoint(
    const EndpointAddresses& endpoint) {
  // TODO(https://github.com/grpc/proposal/pull/551): Prefer gRFC A81's
  // endpoint hostname attribute once it exists in this codebase. In the
  // meantime, GRPC_ARG_ADDRESS_NAME (populated by the xDS resolver from
  // EDS, and already consumed the same way by xds_cluster_impl.cc) is the
  // closest existing equivalent. Only fall back to the endpoint's first
  // address -- exactly what the gRFC specifies for a missing hostname --
  // when neither is available.
  auto name = endpoint.args().GetString(GRPC_ARG_ADDRESS_NAME);
  if (name.has_value()) return std::string(*name);
  return grpc_sockaddr_to_string(&endpoint.addresses().front(), false)
      .value();
}

AutoSharding::EndpointIndexMap AutoSharding::BuildEndpointIndexMap() const {
  EndpointIndexMap map;
  map.num_endpoints = endpoints_.size();
  for (const auto& [hostname, endpoint] : endpoint_map_) {
    map.index_by_hostname[hostname] = endpoint->index();
  }
  return map;
}

std::vector<AutoSharding::PickerEndpoint> AutoSharding::BuildPickerEndpoints()
    const {
  std::vector<std::optional<PickerEndpoint>> slots(endpoints_.size());
  for (const auto& [_, endpoint] : endpoint_map_) {
    slots[endpoint->index()] = endpoint->GetInfoForPicker();
  }
  std::vector<PickerEndpoint> picker_endpoints;
  picker_endpoints.reserve(slots.size());
  for (auto& slot : slots) {
    picker_endpoints.push_back(std::move(*slot));
  }
  return picker_endpoints;
}

absl::Status AutoSharding::UpdateLocked(UpdateArgs args) {
  std::vector<std::string> hostnames;
  if (args.addresses.ok()) {
    endpoints_.clear();
    std::map<std::string, size_t> hostname_indices;
    (*args.addresses)->ForEach([&](const EndpointAddresses& endpoint) {
      std::string hostname = HostnameForEndpoint(endpoint);
      auto [it, inserted] =
          hostname_indices.emplace(hostname, endpoints_.size());
      if (!inserted) return;  // Duplicate hostname; keep the first one.
      hostnames.push_back(std::move(hostname));
      endpoints_.push_back(endpoint);
    });
  } else {
    // If we already have a good endpoint list, keep using it, but still
    // report that the update was not accepted.
    if (!endpoints_.empty()) return args.addresses.status();
  }
  args_ = std::move(args.args);
  config_ = args.config.TakeAsSubclass<AutoShardingLbConfig>();
  // Update endpoint map, preserving pick_first children across updates.
  std::map<std::string, OrphanablePtr<Endpoint>> endpoint_map;
  std::vector<std::string> errors;
  for (size_t i = 0; i < endpoints_.size(); ++i) {
    const std::string& hostname = hostnames[i];
    auto it = endpoint_map_.find(hostname);
    if (it != endpoint_map_.end()) {
      absl::Status status = it->second->UpdateLocked(i);
      if (!status.ok()) {
        errors.emplace_back(
            absl::StrCat("endpoint ", hostname, ": ", status.ToString()));
      }
      endpoint_map.emplace(hostname, std::move(it->second));
    } else {
      endpoint_map.emplace(hostname, MakeOrphanable<Endpoint>(
                                          RefAsSubclass<AutoSharding>(), i));
    }
  }
  endpoint_map_ = std::move(endpoint_map);
  resolution_note_ = std::move(args.resolution_note);
  // If the address list is empty, report TRANSIENT_FAILURE.
  if (endpoints_.empty()) {
    absl::Status status =
        args.addresses.ok()
            ? absl::UnavailableError(
                  absl::StrCat("empty address list: ", resolution_note_))
            : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return status;
  }
  slice_map_ = MakeRefCounted<SliceMap>(
      BuildEndpointIndexMap(), assignment_.has_value() ? &*assignment_ : nullptr);
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
  if (!errors.empty()) {
    return absl::UnavailableError(absl::StrCat(
        "errors from children: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

void AutoSharding::UpdateAggregatedConnectivityStateLocked(
    absl::Status status) {
  // Count the number of endpoints in each state.
  size_t num_idle = 0;
  size_t num_connecting = 0;
  size_t num_ready = 0;
  size_t num_transient_failure = 0;
  Endpoint* idle_endpoint = nullptr;
  for (const auto& [_, endpoint] : endpoint_map_) {
    switch (endpoint->connectivity_state()) {
      case GRPC_CHANNEL_READY:
        ++num_ready;
        break;
      case GRPC_CHANNEL_IDLE:
        ++num_idle;
        if (idle_endpoint == nullptr) idle_endpoint = endpoint.get();
        break;
      case GRPC_CHANNEL_CONNECTING:
        ++num_connecting;
        break;
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
        ++num_transient_failure;
        break;
      default:
        Crash("child policy should never report SHUTDOWN");
    }
  }
  // Same aggregation rules as ring_hash (gRFC A42), per gRFC A119:
  // 1. If there is at least one endpoint in READY state, report READY.
  // 2. If there are 2 or more endpoints in TRANSIENT_FAILURE state, report
  //    TRANSIENT_FAILURE.
  // 3. If there is at least one endpoint in CONNECTING state, report
  //    CONNECTING.
  // 4. If there is one endpoint in TRANSIENT_FAILURE and more than one
  //    endpoint total, report CONNECTING.
  // 5. If there is at least one endpoint in IDLE state, report IDLE.
  // 6. Otherwise, report TRANSIENT_FAILURE.
  grpc_connectivity_state state;
  if (num_ready > 0) {
    state = GRPC_CHANNEL_READY;
  } else if (num_transient_failure >= 2) {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  } else if (num_connecting > 0) {
    state = GRPC_CHANNEL_CONNECTING;
  } else if (num_transient_failure == 1 && endpoints_.size() > 1) {
    state = GRPC_CHANNEL_CONNECTING;
  } else if (num_idle > 0) {
    state = GRPC_CHANNEL_IDLE;
  } else {
    state = GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  // In TRANSIENT_FAILURE, report the last reported failure. Otherwise,
  // report OK.
  if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    if (!status.ok()) {
      last_failure_ = absl::UnavailableError(absl::StrCat(
          "no reachable endpoints; last error: ", status.message()));
    }
    status = last_failure_;
  } else {
    status = absl::OkStatus();
  }
  channel_control_helper()->UpdateState(
      state, status,
      MakeRefCounted<Picker>(RefAsSubclass<AutoSharding>(), slice_map_,
                             BuildPickerEndpoints(),
                             config_->slice_key_header_name(),
                             config_->enable_fallback(), resolution_note_));
  // Same workaround as ring_hash (see gRFC A42): when we're not getting
  // picks (e.g., as a child of the priority policy after failing over
  // away from), make sure we still make progress connecting, so we can
  // recover and be failed back over to.
  if ((state == GRPC_CHANNEL_CONNECTING ||
       state == GRPC_CHANNEL_TRANSIENT_FAILURE) &&
      num_connecting == 0 && idle_endpoint != nullptr) {
    idle_endpoint->RequestConnectionLocked();
  }
}

//
// factory
//

class AutoShardingFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<AutoSharding>(std::move(args));
  }

  absl::string_view name() const override { return kAutoSharding; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<AutoShardingLbConfig>>(
        json, JsonArgs(),
        "errors validating autosharding_experimental LB policy config");
  }
};

}  // namespace

void RegisterAutoShardingLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<AutoShardingFactory>());
}

}  // namespace grpc_core
