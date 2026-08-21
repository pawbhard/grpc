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

#include "src/core/load_balancing/autosharding/autoshard.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/transport/connectivity_state.h"
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
#include "src/core/util/ref_counted_string.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kAutoSharding = "autosharding_experimental";

// Default value for the initial_assignment_timeout config field.
constexpr Duration kDefaultInitialAssignmentTimeout = Duration::Seconds(60);

//
// AutoShardingLbConfig
//

class AutoShardingLbConfig final : public LoadBalancingPolicy::Config {
 public:
  AutoShardingLbConfig() = default;

  AutoShardingLbConfig(const AutoShardingLbConfig&) = delete;
  AutoShardingLbConfig& operator=(const AutoShardingLbConfig&) = delete;

  AutoShardingLbConfig(AutoShardingLbConfig&& other) = delete;
  AutoShardingLbConfig& operator=(AutoShardingLbConfig&& other) = delete;

  absl::string_view name() const override { return kAutoSharding; }

  const std::string& channel_factory_key() const {
    return channel_factory_key_;
  }
  const std::string& slicing_target() const { return slicing_target_; }
  const std::string& slice_key_header() const { return slice_key_header_; }
  bool enable_fallback() const { return enable_fallback_; }
  Duration initial_assignment_timeout() const {
    return initial_assignment_timeout_;
  }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<AutoShardingLbConfig>()
            .OptionalField("channelFactoryKey",
                           &AutoShardingLbConfig::channel_factory_key_)
            .OptionalField("slicingTarget",
                           &AutoShardingLbConfig::slicing_target_)
            .OptionalField("sliceKeyHeaderName",
                           &AutoShardingLbConfig::slice_key_header_,
                           "slice_key_header_name")
            .OptionalField("enableFallback",
                           &AutoShardingLbConfig::enable_fallback_)
            .OptionalField("initialAssignmentTimeout",
                           &AutoShardingLbConfig::initial_assignment_timeout_)
            .Finish();
    return loader;
  }

  void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors) {
    {
      ValidationErrors::ScopedField field(errors, ".sliceKeyHeaderName");
      if (!errors->FieldHasErrors() && slice_key_header_.empty()) {
        errors->AddError("must be non-empty");
      }
    }
    {
      ValidationErrors::ScopedField field(errors, ".initialAssignmentTimeout");
      if (!errors->FieldHasErrors() &&
          initial_assignment_timeout_.millis() <= 0) {
        errors->AddError("must be greater than zero");
      }
    }
    // TODO(bpawan): channel_factory_key_ and slicing_target_ are currently
    // left unvalidated (including allowing them to be empty), since the
    // "Channel Factory" and "Shard" stream from gRFC A119 are not yet
    // implemented in C++ (see CreateShardingServiceChannelLocked). Revisit
    // once they are, since an empty channel_factory_key_ will presumably
    // be meaningless to whatever "Channel Factory" implementation consumes
    // it.
  }

 private:
  std::string channel_factory_key_;
  std::string slicing_target_;
  std::string slice_key_header_;
  bool enable_fallback_ = false;
  Duration initial_assignment_timeout_ = kDefaultInitialAssignmentTimeout;
};

//
// autosharding LB policy
//

namespace testing {
class AutoShardingTest;
}

class AutoSharding final : public LoadBalancingPolicy {
 public:
  explicit AutoSharding(Args args);

  absl::string_view name() const override { return kAutoSharding; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

  friend class testing::AutoShardingTest;

 private:
  //
  // Assignment
  //
  // TODO(bpawan): These structures will be populated by the "Shard" stream
  // that communicates with the sharding service using the OSS
  // DynamicSharding gRPC protocol.  The protocol (and the C++
  // representation of the "Channel Factory" used to create the gRPC
  // channel to the sharding service) is not yet defined for C++ (see gRFC
  // A119), so for now nothing populates Assignment, and the policy relies
  // on the initial assignment timer and the fallback mechanism until it is
  // implemented.
  //

  // A key-range in the application-defined keyspace, with the endpoints
  // assigned to it.
  struct SliceAssignment {
    // Inclusive start key.  Empty if the slice covers the start of the
    // keyspace.
    std::string start_key;
    // Exclusive end key.  Empty if the slice covers the end of the keyspace.
    std::string end_key;
    // Indices into Assignment::endpoint_names.
    std::vector<size_t> endpoints;
  };

  // A complete assignment received from the sharding service.
  struct Assignment {
    // List of non-overlapping key-range assignments covering the full
    // keyspace.
    std::vector<SliceAssignment> slices;
    // Complete list of endpoint names in the assignment.
    std::vector<std::string> endpoint_names;
    // Generation number of the assignment.
    int64_t generation = 0;
  };

  //
  // SliceMap
  //
  // An immutable, lookup-optimized representation of an Assignment combined
  // with the current EndpointMap.  Since it is immutable, the Picker can
  // access it without any explicit synchronization with the LB policy.
  //

  class SliceMap final : public RefCounted<SliceMap> {
   public:
    // A key-range in the keyspace.
    struct Entry {
      // Inclusive start key.  Empty if the slice covers the start of the
      // keyspace (-infinity).
      std::string start_key;
      // Exclusive end key.  Empty if the slice covers the end of the
      // keyspace (+infinity).
      std::string end_key;
      // Indices into the Picker's endpoint list.
      std::vector<size_t> endpoints;

      bool Contains(absl::string_view key) const {
        if (!start_key.empty() && key < start_key) return false;
        if (!end_key.empty() && key >= end_key) return false;
        return true;
      }
    };

    struct EndKeyLessThan {
      bool operator()(absl::string_view a, absl::string_view b) const {
        if (a.empty() && b.empty()) return false;
        if (a.empty()) return false;  // a is +infinity, cannot be < b
        if (b.empty()) return true;   // b is +infinity, any finite a is < b
        return a < b;
      }
    };

    // Comparator used by std::upper_bound to compare a request key against a
    // slice's exclusive end_key. Unlike EndKeyLessThan (where empty string
    // means +infinity for BOTH bounds), a request key of "" is a finite string
    // key (the minimum key in the keyspace), whereas an empty end_key
    // represents +infinity.
    struct KeyLessThanSliceEndKey {
      bool operator()(absl::string_view key, const Entry& entry) const {
        if (entry.end_key.empty())
          return true;  // entry.end_key is +infinity, any finite key is <
                        // +infinity
        return key < entry.end_key;
      }
    };

    void SetFallbackPool(std::vector<size_t> fallback_pool) {
      fallback_pool_ = std::move(fallback_pool);
    }

    void AddSlice(Entry entry) { slices_.push_back(std::move(entry)); }

    void SortSlices() {
      std::sort(slices_.begin(), slices_.end(),
                [](const Entry& lhs, const Entry& rhs) {
                  return EndKeyLessThan()(lhs.end_key, rhs.end_key);
                });
    }

    // Validates that slice key ranges do not overlap and cover key space
    // cleanly.
    void CheckSliceMap() const {
      bool contains_overlap = false;
      bool contains_gap = false;
      absl::string_view prev_end;
      bool first = true;
      for (const auto& entry : slices_) {
        if (!first) {
          if (!entry.start_key.empty() && !prev_end.empty() &&
              entry.start_key < prev_end) {
            contains_overlap = true;
          } else if (entry.start_key != prev_end) {
            contains_gap = true;
          }
        }
        prev_end = entry.end_key;
        first = false;
      }
      if (contains_overlap) {
        LOG(ERROR) << "SliceMap contains overlapping key ranges";
      } else if (contains_gap) {
        GRPC_TRACE_LOG(autosharding_lb, INFO)
            << "SliceMap contains gaps in key ranges";
      }
    }

    void SetGeneration(int64_t generation) { generation_ = generation; }

    // Sorted by exclusive end_key using EndKeyLessThan.
    const std::vector<Entry>& slices() const { return slices_; }

    // Indices into the Picker's endpoint list for the resolver endpoints,
    // sorted by endpoint index.
    const std::vector<size_t>& fallback_pool() const { return fallback_pool_; }

    int64_t generation() const { return generation_; }

    // Returns the index into slices() of the slice that covers the given
    // key, or nullopt if there are no slices (i.e., no assignment has been
    // received from the sharding service yet) or if key is not contained.
    //
    // Following the internal slicer implementation, we index/sort slices by
    // their exclusive end_key. Because ranges are [start_key, end_key),
    // finding the first slice whose exclusive end_key is strictly greater
    // than key using std::upper_bound directly identifies the candidate range.
    // We then verify range containment using Contains().
    std::optional<size_t> Lookup(absl::string_view key) const {
      if (slices_.empty()) return std::nullopt;
      auto it = std::upper_bound(slices_.begin(), slices_.end(), key,
                                 KeyLessThanSliceEndKey());
      if (it != slices_.end() && it->Contains(key)) {
        return std::distance(slices_.begin(), it);
      }
      return std::nullopt;
    }

   private:
    std::vector<Entry> slices_;
    std::vector<size_t> fallback_pool_;
    int64_t generation_ = 0;
  };

  // State for a particular endpoint.  Delegates to a lazily-created
  // pick_first child policy.
  class AutoShardingEndpoint final
      : public InternallyRefCounted<AutoShardingEndpoint> {
   public:
    // index is the index of this endpoint within the Name Resolver update.
    AutoShardingEndpoint(RefCountedPtr<AutoSharding> autosharding, size_t index)
        : autosharding_(std::move(autosharding)), index_(index) {}

    void Orphan() override;

    size_t index() const { return index_; }

    absl::Status UpdateLocked(size_t index);

    grpc_connectivity_state connectivity_state() const {
      return connectivity_state_;
    }

    // Returns info about the endpoint to be stored in the picker.
    struct EndpointInfo {
      RefCountedPtr<AutoShardingEndpoint> endpoint;
      RefCountedPtr<SubchannelPicker> picker;
      grpc_connectivity_state state;
      absl::Status status;
    };
    EndpointInfo GetInfoForPicker() {
      return {Ref(), picker_, connectivity_state_, status_};
    }

    void ResetBackoffLocked();

    // If the child policy does not yet exist, creates it; otherwise,
    // asks the child to exit IDLE.
    void RequestConnectionLocked();

   private:
    class Helper;

    void CreateChildPolicy();
    absl::Status UpdateChildPolicyLocked();

    // Called when the child policy reports a connectivity state update.
    void OnStateUpdate(grpc_connectivity_state new_state,
                       const absl::Status& status,
                       RefCountedPtr<SubchannelPicker> picker);

    // Ref to our parent.
    RefCountedPtr<AutoSharding> autosharding_;
    size_t index_;  // Index of this endpoint within the Name Resolver update.

    // The pick_first child policy.  Created lazily, on first use.
    OrphanablePtr<LoadBalancingPolicy> child_policy_;

    grpc_connectivity_state connectivity_state_ = GRPC_CHANNEL_IDLE;
    absl::Status status_;
    RefCountedPtr<SubchannelPicker> picker_;
  };

  // Timer for the initial assignment from the sharding service.  While this
  // timer is active and no valid assignment has been received, the picker
  // queues RPCs; when the timer fires, the policy either falls back to all
  // resolver endpoints (if fallback is enabled) or fails RPCs.
  class InitialAssignmentTimer final
      : public InternallyRefCounted<InitialAssignmentTimer> {
   public:
    InitialAssignmentTimer(RefCountedPtr<AutoSharding> autosharding,
                           Duration timeout);

    void Orphan() override;

   private:
    void OnTimerLocked();

    RefCountedPtr<AutoSharding> autosharding_;
    std::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
        timer_handle_;
  };

  class Picker final : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<AutoSharding> autosharding,
           RefCountedPtr<SliceMap> slice_map, bool assignment_pending);

    PickResult Pick(PickArgs args) override;

   private:
    // Snapshot of the state of a single endpoint, ordered 1:1 by
    // EndpointState.index.

    // A fire-and-forget class that schedules endpoint connection attempts
    // on the control plane WorkSerializer.
    class EndpointConnectionAttempter final {
     public:
      EndpointConnectionAttempter(RefCountedPtr<AutoSharding> autosharding,
                                  RefCountedPtr<AutoShardingEndpoint> endpoint)
          : autosharding_(std::move(autosharding)),
            endpoint_(std::move(endpoint)) {
        // Hop into ExecCtx, so that we're not holding the data plane mutex
        // while we run control-plane code.
        GRPC_CLOSURE_INIT(&closure_, RunInExecCtx, this, nullptr);
        ExecCtx::Run(DEBUG_LOCATION, &closure_, absl::OkStatus());
      }

     private:
      static void RunInExecCtx(void* arg, grpc_error_handle /*error*/) {
        auto* self = static_cast<EndpointConnectionAttempter*>(arg);
        self->autosharding_->work_serializer()->Run([self]() {
          if (!self->autosharding_->shutdown_) {
            self->endpoint_->RequestConnectionLocked();
          }
          delete self;
        });
      }

      RefCountedPtr<AutoSharding> autosharding_;
      RefCountedPtr<AutoShardingEndpoint> endpoint_;
      grpc_closure closure_;
    };

    // Returns true if the pool contains zero valid endpoints or if all
    // endpoints in the pool are in TRANSIENT_FAILURE.
    bool IsPoolInFallback(const std::vector<size_t>& indices) const;

    // Picks an endpoint from the pool of endpoints given by indices,
    // starting at a random position within the pool.
    PickResult PickFromEndpointIndices(const std::vector<size_t>& indices,
                                       PickArgs args);

    RefCountedPtr<AutoSharding> autosharding_;
    RefCountedPtr<SliceMap> slice_map_;
    // Snapshot of the endpoint states, indexed by EndpointState.index.
    std::vector<AutoShardingEndpoint::EndpointInfo> endpoints_;
    // Precomputed per-slice in-fallback status.
    std::vector<bool> slices_in_fallback_;
    RefCountedStringValue slice_key_header_;
    std::string resolution_note_;
    bool fallback_enabled_ = false;
    // True while the policy is waiting for the initial assignment from the
    // sharding service (i.e., the initial assignment timer is active and no
    // valid assignment has been received).  Picks are queued in this case.
    bool assignment_pending_ = false;
  };

  ~AutoSharding() override;

  void ShutdownLocked() override;

  // Updates the aggregate policy's connectivity state based on the
  // number of endpoints in each state, creating a new picker.
  // If the call to this method is triggered by an endpoint entering
  // TRANSIENT_FAILURE, then status is the status reported by the endpoint.
  void UpdateAggregatedConnectivityStateLocked(absl::Status status);

  // Creates a new gRPC channel to the sharding service (via the injected
  // "Channel Factory") and starts the initial assignment timer.
  //
  // TODO(bpawan): The "Channel Factory" (used to create the channel) and the
  // "Shard" stream (used to receive assignments) are not yet implemented in
  // C++ (see gRFC A119).  For now, this only manages the initial assignment
  // timer; the channel creation and stream management will be added along
  // with the OSS DynamicSharding gRPC protocol.
  void CreateShardingServiceChannelLocked();

  // Builds a new SliceMap from the current EndpointMap and the most recent
  // Assignment (if any).
  RefCountedPtr<SliceMap> BuildSliceMapLocked() const;

  // Called when the initial assignment timer fires.
  void OnInitialAssignmentTimeoutLocked();

  // Called by the (future) "Shard" stream when a valid assignment is
  // received from the sharding service.
  //
  // TODO(bpawan): Not yet wired up; will be invoked by the stream code once
  // the OSS DynamicSharding gRPC protocol is implemented in C++.
  void OnAssignmentReceived(Assignment assignment);

  // Endpoint map: endpoint hostname -> endpoint state.
  std::map<std::string, OrphanablePtr<AutoShardingEndpoint>> endpoint_map_;
  // Endpoint list from the most recent Name Resolver update.
  EndpointAddressesList endpoints_;
  // Channel args from the most recent update.
  ChannelArgs args_;
  // Most recent valid assignment from the sharding service, if any.
  std::optional<Assignment> assignment_;
  // Current SliceMap.  Immutable; shared with the current picker.
  RefCountedPtr<SliceMap> slice_map_;
  // Config.
  RefCountedStringValue slice_key_header_;
  bool fallback_enabled_ = false;
  Duration initial_assignment_timeout_ = kDefaultInitialAssignmentTimeout;
  std::string channel_factory_key_;
  std::string slicing_target_;
  // True once we have created (or attempted to create) the gRPC channel to
  // the sharding service.
  bool channel_created_ = false;
  // True while the policy is waiting for the initial assignment from the
  // sharding service.
  bool assignment_pending_ = true;
  // Initial assignment timer.
  OrphanablePtr<InitialAssignmentTimer> initial_assignment_timer_;
  std::string resolution_note_;

  // TODO(bpawan): If we ever change the helper UpdateState() API to not
  // need the status reported for TRANSIENT_FAILURE state (because
  // it's not currently actually used for anything outside of the picker),
  // then we will no longer need this data member.
  absl::Status last_failure_;

  // Indicating if we are shutting down.
  bool shutdown_ = false;
};

//
// AutoSharding::Picker
//

AutoSharding::Picker::Picker(RefCountedPtr<AutoSharding> autosharding,
                             RefCountedPtr<SliceMap> slice_map,
                             bool assignment_pending)
    : autosharding_(std::move(autosharding)),
      slice_map_(std::move(slice_map)),
      endpoints_(autosharding_->endpoints_.size()),
      slice_key_header_(autosharding_->slice_key_header_),
      resolution_note_(autosharding_->resolution_note_),
      fallback_enabled_(autosharding_->fallback_enabled_),
      assignment_pending_(assignment_pending) {
  // Build an immutable snapshot of the PickerEndpoints, ordered 1:1 by
  // EndpointState.index.
  std::vector<std::pair<size_t, AutoShardingEndpoint*>> endpoint_indices;
  endpoint_indices.reserve(autosharding_->endpoint_map_.size());
  for (const auto& [_, endpoint] : autosharding_->endpoint_map_) {
    endpoint_indices.emplace_back(endpoint->index(), endpoint.get());
  }
  std::sort(
      endpoint_indices.begin(), endpoint_indices.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  for (const auto& [index, endpoint] : endpoint_indices) {
    endpoints_[index] = endpoint->GetInfoForPicker();
  }
  // Any slots not filled above correspond to endpoints whose hostname was
  // duplicated by a later endpoint in the resolver update.  They are never
  // referenced by the SliceMap or the fallback pool, but mark them as
  // SHUTDOWN defensively, in case of a bug.
  for (AutoShardingEndpoint::EndpointInfo& endpoint : endpoints_) {
    if (endpoint.endpoint == nullptr) {
      endpoint.state = GRPC_CHANNEL_SHUTDOWN;
    }
  }
  // Precompute in-fallback status for each slice and for the fallback pool.
  slices_in_fallback_.reserve(slice_map_->slices().size());
  for (const auto& slice : slice_map_->slices()) {
    slices_in_fallback_.push_back(IsPoolInFallback(slice.endpoints));
  }
}

bool AutoSharding::Picker::IsPoolInFallback(
    const std::vector<size_t>& indices) const {
  // A pool is in fallback if it contains zero valid endpoints or if all
  // assigned endpoints are in TRANSIENT_FAILURE.
  if (indices.empty()) return true;
  for (size_t index : indices) {
    if (endpoints_[index].state != GRPC_CHANNEL_TRANSIENT_FAILURE) {
      return false;
    }
  }
  return true;
}

AutoSharding::PickResult AutoSharding::Picker::Pick(PickArgs args) {
  // While we are waiting for the initial assignment from the sharding
  // service, queue all picks.  They will be retried when the policy reports
  // a new picker (upon receiving an assignment or when the initial
  // assignment timer fires).
  //
  // TODO(bpawan): Per gRFC A121, the picker should also set delay_type to
  // "autosharding_assignment_pending" while queued in this state.
  if (assignment_pending_) {
    return PickResult::Queue();
  }
  // Extract the sharding key from the request metadata.
  std::string buffer;
  auto key = args.initial_metadata->Lookup(slice_key_header_.as_string_view(),
                                           &buffer);
  if (!key.has_value()) {
    return PickResult::Fail(absl::InternalError(
        absl::StrCat("slice key header \"", slice_key_header_.as_string_view(),
                     "\" not present")));
  }
  // Look up the slice covering the key.
  std::optional<size_t> slice_index = slice_map_->Lookup(*key);
  if (!slice_index.has_value()) {
    // No slice covers this key.  This is only possible when the initial
    // assignment timer has expired and no valid assignments have been
    // received from the sharding service.
    if (fallback_enabled_) {
      return PickFromEndpointIndices(slice_map_->fallback_pool(), args);
    }
    std::string message = "no assignment received from the sharding service";
    if (!resolution_note_.empty()) {
      absl::StrAppend(&message, " (", resolution_note_, ")");
    }
    return PickResult::Fail(absl::UnavailableError(message));
  }
  // If the matching key range is in fallback mode and fallback is enabled,
  // route to the fallback pool.
  if (slices_in_fallback_[*slice_index] && fallback_enabled_) {
    return PickFromEndpointIndices(slice_map_->fallback_pool(), args);
  }
  // Delegate to the endpoints assigned to the matching key range.  When the
  // matching key range is in fallback but fallback is disabled, this will
  // yield a better error message.
  const auto& slice = slice_map_->slices()[*slice_index];
  return PickFromEndpointIndices(slice.endpoints, args);
}

AutoSharding::PickResult AutoSharding::Picker::PickFromEndpointIndices(
    const std::vector<size_t>& indices, PickArgs args) {
  // This can be true only when the matching entry is in fallback mode
  // (due to having zero endpoints) *and* fallback is disabled.
  if (indices.empty()) {
    std::string message = "no endpoints in slice";
    if (!resolution_note_.empty()) {
      absl::StrAppend(&message, " (", resolution_note_, ")");
    }
    return PickResult::Fail(absl::UnavailableError(message));
  }
  // Pick a random starting index within the pool.
  size_t first_index = absl::Uniform<size_t>(SharedBitGen(), 0, indices.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    const auto& endpoint =
        endpoints_[indices[(first_index + i) % indices.size()]];
    switch (endpoint.state) {
      case GRPC_CHANNEL_READY:
        return endpoint.picker->Pick(args);
      case GRPC_CHANNEL_IDLE:
        new EndpointConnectionAttempter(
            autosharding_.Ref(DEBUG_LOCATION, "EndpointConnectionAttempter"),
            endpoint.endpoint);
        [[fallthrough]];
      case GRPC_CHANNEL_CONNECTING:
        return PickResult::Queue();
      default:
        break;
    }
  }
  // All endpoints are in TRANSIENT_FAILURE.  Fail by delegating to the
  // randomly picked endpoint's picker to yield a detailed error message.
  const auto& endpoint = endpoints_[indices[first_index]];
  if (endpoint.picker != nullptr) {
    return endpoint.picker->Pick(args);
  }
  return PickResult::Fail(endpoint.status);
}

//
// AutoSharding::InitialAssignmentTimer
//

AutoSharding::InitialAssignmentTimer::InitialAssignmentTimer(
    RefCountedPtr<AutoSharding> autosharding, Duration timeout)
    : autosharding_(std::move(autosharding)) {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << autosharding_.get()
      << "] starting initial assignment timer for " << timeout.millis() << "ms";
  timer_handle_ =
      autosharding_->channel_control_helper()->GetEventEngine()->RunAfter(
          timeout, [self = Ref(DEBUG_LOCATION, "Timer")]() mutable {
            ExecCtx exec_ctx;
            auto self_ptr = self.get();
            self_ptr->autosharding_->work_serializer()->Run(
                [self = std::move(self)]() { self->OnTimerLocked(); });
          });
}

void AutoSharding::InitialAssignmentTimer::Orphan() {
  if (timer_handle_.has_value()) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << autosharding_.get()
        << "] cancelling initial assignment timer";
    autosharding_->channel_control_helper()->GetEventEngine()->Cancel(
        *timer_handle_);
    timer_handle_.reset();
  }
  Unref();
}

void AutoSharding::InitialAssignmentTimer::OnTimerLocked() {
  if (!timer_handle_.has_value()) return;  // Already fired or cancelled.
  timer_handle_.reset();
  autosharding_->OnInitialAssignmentTimeoutLocked();
}

//
// AutoSharding::AutoShardingEndpoint::Helper
//

class AutoSharding::AutoShardingEndpoint::Helper final
    : public LoadBalancingPolicy::DelegatingChannelControlHelper {
 public:
  explicit Helper(RefCountedPtr<AutoShardingEndpoint> endpoint)
      : endpoint_(std::move(endpoint)) {}

  ~Helper() override { endpoint_.reset(DEBUG_LOCATION, "Helper"); }

  void UpdateState(
      grpc_connectivity_state state, const absl::Status& status,
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
    endpoint_->OnStateUpdate(state, status, std::move(picker));
  }

 private:
  LoadBalancingPolicy::ChannelControlHelper* parent_helper() const override {
    return endpoint_->autosharding_->channel_control_helper();
  }

  RefCountedPtr<AutoShardingEndpoint> endpoint_;
};

//
// AutoSharding::AutoShardingEndpoint
//

void AutoSharding::AutoShardingEndpoint::Orphan() {
  if (child_policy_ != nullptr) {
    // Remove pollset_set linkage.
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     autosharding_->interested_parties());
    child_policy_.reset();
    picker_.reset();
  }
  Unref();
}

absl::Status AutoSharding::AutoShardingEndpoint::UpdateLocked(size_t index) {
  index_ = index;
  if (child_policy_ == nullptr) return absl::OkStatus();
  return UpdateChildPolicyLocked();
}

void AutoSharding::AutoShardingEndpoint::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void AutoSharding::AutoShardingEndpoint::RequestConnectionLocked() {
  if (child_policy_ == nullptr) {
    CreateChildPolicy();
  } else {
    child_policy_->ExitIdleLocked();
  }
}

void AutoSharding::AutoShardingEndpoint::CreateChildPolicy() {
  GRPC_CHECK(child_policy_ == nullptr);
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = autosharding_->work_serializer();
  lb_policy_args.args =
      autosharding_->args_
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX, true);
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  child_policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "pick_first", std::move(lb_policy_args));
  if (GRPC_TRACE_FLAG_ENABLED(autosharding_lb)) {
    const EndpointAddresses& endpoint = autosharding_->endpoints_[index_];
    LOG(INFO) << "[AS " << autosharding_.get() << "] endpoint " << this
              << " (index " << index_ << " of "
              << autosharding_->endpoints_.size() << ", " << endpoint.ToString()
              << "): created child policy " << child_policy_.get();
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(child_policy_->interested_parties(),
                                   autosharding_->interested_parties());
  // If the child policy returns a non-OK status, request re-resolution.
  // Note that this will initially cause fixed backoff delay in the
  // resolver instead of exponential delay.  However, once the
  // resolver returns the initial re-resolution, we will be able to
  // return non-OK from UpdateLocked(), which will trigger
  // exponential backoff instead.
  absl::Status status = UpdateChildPolicyLocked();
  if (!status.ok()) {
    autosharding_->channel_control_helper()->RequestReresolution();
  }
}

absl::Status AutoSharding::AutoShardingEndpoint::UpdateChildPolicyLocked() {
  // Construct pick_first config.
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray(
              {Json::FromObject({{"pick_first", Json::FromObject({})}})}));
  GRPC_CHECK(config.ok());
  // Update child policy.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses = std::make_shared<SingleEndpointIterator>(
      autosharding_->endpoints_[index_]);
  update_args.args = autosharding_->args_;
  update_args.config = std::move(*config);
  return child_policy_->UpdateLocked(std::move(update_args));
}

void AutoSharding::AutoShardingEndpoint::OnStateUpdate(
    grpc_connectivity_state new_state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << autosharding_.get() << "] connectivity changed for endpoint "
      << this << " (" << autosharding_->endpoints_[index_].ToString()
      << ", child_policy=" << child_policy_.get()
      << "): prev_state=" << ConnectivityStateName(connectivity_state_)
      << " new_state=" << ConnectivityStateName(new_state) << " (" << status
      << ")";
  if (child_policy_ == nullptr) return;  // Already orphaned.
  // Update state.
  connectivity_state_ = new_state;
  status_ = status;
  picker_ = std::move(picker);
  // Update the aggregated connectivity state.
  autosharding_->UpdateAggregatedConnectivityStateLocked(status);
}

//
// AutoSharding
//

AutoSharding::AutoSharding(Args args) : LoadBalancingPolicy(std::move(args)) {
  GRPC_TRACE_LOG(autosharding_lb, INFO) << "[AS " << this << "] Created";
}

AutoSharding::~AutoSharding() {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] Destroying AutoSharding policy";
}

void AutoSharding::ShutdownLocked() {
  GRPC_TRACE_LOG(autosharding_lb, INFO) << "[AS " << this << "] Shutting down";
  shutdown_ = true;
  initial_assignment_timer_.reset();
  endpoint_map_.clear();
  slice_map_.reset();
  assignment_.reset();
}

void AutoSharding::ResetBackoffLocked() {
  for (const auto& [_, endpoint] : endpoint_map_) {
    endpoint->ResetBackoffLocked();
  }
}

absl::Status AutoSharding::UpdateLocked(UpdateArgs args) {
  // Check address list.
  if (args.addresses.ok()) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] received update";
    // Save the endpoint list.
    endpoints_.clear();
    (*args.addresses)->ForEach([&](const EndpointAddresses& endpoint) {
      endpoints_.push_back(endpoint);
    });
  } else {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] received update with addresses error: "
        << args.addresses.status();
    // If we already have an endpoint list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (!endpoints_.empty()) return args.addresses.status();
  }
  // Save channel args.
  args_ = std::move(args.args);
  // Save config.
  auto* config = DownCast<AutoShardingLbConfig*>(args.config.get());
  slice_key_header_ = RefCountedStringValue(config->slice_key_header());
  fallback_enabled_ = config->enable_fallback();
  initial_assignment_timeout_ = config->initial_assignment_timeout();
  // If the channel factory key has changed (or if this is the first
  // configuration update), create a new gRPC channel to the sharding service
  // (and a new Shard stream on it).
  if (!channel_created_ ||
      config->channel_factory_key() != channel_factory_key_) {
    channel_factory_key_ = config->channel_factory_key();
    CreateShardingServiceChannelLocked();
  } else if (config->slicing_target() != slicing_target_) {
    // The slicing_target has changed, so create a new Shard stream on the
    // existing channel.
    //
    // TODO(bpawan): Not yet implemented; will be done along with the OSS
    // DynamicSharding gRPC protocol.
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this << "] slicing target changed to \""
        << config->slicing_target() << "\"";
  }
  slicing_target_ = config->slicing_target();
  // Update endpoint map.
  std::map<std::string, OrphanablePtr<AutoShardingEndpoint>> endpoint_map;
  std::vector<std::string> errors;
  for (size_t i = 0; i < endpoints_.size(); ++i) {
    const EndpointAddresses& endpoint = endpoints_[i];
    // The hostname of the endpoint is the value of the endpoint hostname
    // attribute (gRFC A81); if that is not present, the string form of the
    // endpoint's first address serves as the hostname.
    std::string hostname;
    auto hostname_arg = endpoint.args().GetString(GRPC_ARG_ADDRESS_NAME);
    if (hostname_arg.has_value()) {
      hostname = std::string(*hostname_arg);
    } else {
      hostname =
          grpc_sockaddr_to_string(&endpoint.addresses().front(), false).value();
    }
    // If present in the old map, retain it; otherwise, create a new one.
    // Note: We use operator[] (instead of emplace) because the spec requires
    // that when multiple endpoints in the resolver update have the same
    // hostname, the last one wins. We erase from endpoint_map_ as each entry
    // is consumed so that a duplicate hostname later in this same update
    // creates a new endpoint instead of matching (and dereferencing) the
    // same already-moved-from entry.
    auto it = endpoint_map_.find(hostname);
    if (it != endpoint_map_.end()) {
      absl::Status status = it->second->UpdateLocked(i);
      if (!status.ok()) {
        errors.emplace_back(
            absl::StrCat("endpoint ", hostname, ": ", status.ToString()));
      }
      endpoint_map[hostname] = std::move(it->second);
      endpoint_map_.erase(it);
    } else {
      endpoint_map[hostname] = MakeOrphanable<AutoShardingEndpoint>(
          RefAsSubclass<AutoSharding>(), i);
    }
  }
  endpoint_map_ = std::move(endpoint_map);
  // Update resolution note.
  resolution_note_ = std::move(args.resolution_note);
  // If the address list is empty, report TRANSIENT_FAILURE.
  if (endpoints_.empty()) {
    absl::Status status = args.addresses.ok()
                              ? absl::UnavailableError(absl::StrCat(
                                    "empty address list: ", resolution_note_))
                              : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return status;
  }
  // Build a new SliceMap unless we are still waiting for the initial
  // assignment (in which case the picker queues all picks, and the SliceMap
  // will be built when the timer fires or a valid assignment is received).
  if (!assignment_pending_) {
    slice_map_ = BuildSliceMapLocked();
  } else if (slice_map_ == nullptr) {
    // Make sure the picker always has a SliceMap, even while waiting for the
    // initial assignment.
    slice_map_ = MakeRefCounted<SliceMap>();
  }
  // Return a new picker.
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
  if (!errors.empty()) {
    return absl::UnavailableError(absl::StrCat(
        "errors from children: [", absl::StrJoin(errors, "; "), "]"));
  }
  return absl::OkStatus();
}

void AutoSharding::CreateShardingServiceChannelLocked() {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] creating channel to sharding service for key \""
      << channel_factory_key_ << "\"";
  channel_created_ = true;
  // TODO(bpawan): Use the injected "Channel Factory" to create a gRPC channel
  // to the sharding service (see gRFC A119).  The "Channel Factory" is not
  // yet defined for C++, and the OSS DynamicSharding gRPC protocol is not yet
  // implemented, so for now we only manage the initial assignment timer.
  //
  // When a channel is created:
  // - A new "Shard" stream must be created on it, and any previously created
  //   channel must be closed.
  // - The client_uuid and current_generation values for the Init message sent
  //   on the stream must be tracked here.
  // Restart the initial assignment timer.
  initial_assignment_timer_.reset();
  // If we do not have a valid assignment from the previous channel, queue
  // RPCs until we receive one from the new channel or the timer expires.
  // Otherwise, continue using the previous assignment while we wait.
  bool has_valid_assignment =
      slice_map_ != nullptr && !slice_map_->slices().empty();
  assignment_pending_ = !has_valid_assignment;
  initial_assignment_timer_ = MakeOrphanable<InitialAssignmentTimer>(
      RefAsSubclass<AutoSharding>(DEBUG_LOCATION, "InitialAssignmentTimer"),
      initial_assignment_timeout_);
}

RefCountedPtr<AutoSharding::SliceMap> AutoSharding::BuildSliceMapLocked()
    const {
  auto slice_map = MakeRefCounted<SliceMap>();
  // Populate the fallback pool, deterministically sorted by endpoint index.
  std::vector<std::pair<size_t, AutoShardingEndpoint*>> endpoint_indices;
  endpoint_indices.reserve(endpoint_map_.size());
  for (const auto& [_, endpoint] : endpoint_map_) {
    endpoint_indices.emplace_back(endpoint->index(), endpoint.get());
  }
  std::sort(
      endpoint_indices.begin(), endpoint_indices.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  std::vector<size_t> fallback_pool;
  fallback_pool.reserve(endpoint_indices.size());
  for (const auto& [index, _] : endpoint_indices) {
    fallback_pool.push_back(index);
  }
  slice_map->SetFallbackPool(std::move(fallback_pool));
  // If no assignment has been received yet (startup case), return early with
  // no slices.
  if (!assignment_.has_value()) return slice_map;
  slice_map->SetGeneration(assignment_->generation);
  // Precompute a map from assignment endpoint index to EndpointState.index
  // to avoid repeated map lookups per slice endpoint.
  std::vector<std::optional<size_t>> assignment_endpoint_to_picker_index(
      assignment_->endpoint_names.size(), std::nullopt);
  for (size_t i = 0; i < assignment_->endpoint_names.size(); ++i) {
    auto it = endpoint_map_.find(assignment_->endpoint_names[i]);
    if (it != endpoint_map_.end()) {
      assignment_endpoint_to_picker_index[i] = it->second->index();
    }
  }
  // Build an Entry for each Slice in the assignment.
  for (const auto& slice : assignment_->slices) {
    SliceMap::Entry entry;
    entry.start_key = slice.start_key;
    entry.end_key = slice.end_key;
    entry.endpoints.reserve(slice.endpoints.size());
    for (size_t idx : slice.endpoints) {
      if (idx < assignment_endpoint_to_picker_index.size() &&
          assignment_endpoint_to_picker_index[idx].has_value()) {
        entry.endpoints.push_back(*assignment_endpoint_to_picker_index[idx]);
      }
    }
    slice_map->AddSlice(std::move(entry));
  }
  slice_map->SortSlices();
  slice_map->CheckSliceMap();
  return slice_map;
}

void AutoSharding::OnInitialAssignmentTimeoutLocked() {
  initial_assignment_timer_.reset();
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] initial assignment timer expired";
  assignment_pending_ = false;
  // If the endpoint list is empty, the channel is already in
  // TRANSIENT_FAILURE, so there is nothing to do here.
  if (endpoints_.empty()) return;
  // Build a new SliceMap and report a new picker.  If no valid assignment has
  // been received, the new picker will route to the fallback pool (if fallback
  // is enabled) or fail picks (otherwise).
  slice_map_ = BuildSliceMapLocked();
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
}

void AutoSharding::OnAssignmentReceived(Assignment assignment) {
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] received assignment with generation "
      << assignment.generation;
  // Stop the initial assignment timer, if it is running.
  initial_assignment_timer_.reset();
  assignment_ = std::move(assignment);
  assignment_pending_ = false;
  // If the endpoint list is empty, the channel is already in
  // TRANSIENT_FAILURE, so there is nothing to do here.
  if (endpoints_.empty()) return;
  // Build a new SliceMap and report a new picker.
  slice_map_ = BuildSliceMapLocked();
  UpdateAggregatedConnectivityStateLocked(absl::OkStatus());
}

void AutoSharding::UpdateAggregatedConnectivityStateLocked(
    absl::Status status) {
  // Count the number of endpoints in each state.
  size_t num_idle = 0;
  size_t num_connecting = 0;
  size_t num_ready = 0;
  size_t num_transient_failure = 0;
  AutoShardingEndpoint* idle_endpoint = nullptr;
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
  // The overall aggregation rules here are the same as those used by the
  // ring_hash LB policy (gRFC A42):
  // 1. If there is at least one endpoint in READY state, report READY.
  // 2. If there are 2 or more endpoints in TRANSIENT_FAILURE state, report
  //    TRANSIENT_FAILURE.
  // 3. If there is at least one endpoint in CONNECTING state, report
  //    CONNECTING.
  // 4. If there is one endpoint in TRANSIENT_FAILURE state and there is
  //    more than one endpoint, report CONNECTING.
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
  GRPC_TRACE_LOG(autosharding_lb, INFO)
      << "[AS " << this << "] setting connectivity state to "
      << ConnectivityStateName(state) << " (num_idle=" << num_idle
      << ", num_connecting=" << num_connecting << ", num_ready=" << num_ready
      << ", num_transient_failure=" << num_transient_failure
      << ", size=" << endpoints_.size() << ")";
  // In TRANSIENT_FAILURE, report the last reported failure.
  // Otherwise, report OK.
  if (state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    if (!status.ok()) {
      last_failure_ = absl::UnavailableError(absl::StrCat(
          "no reachable endpoints; last error: ", status.message()));
    }
    status = last_failure_;
  } else {
    status = absl::OkStatus();
  }
  // Generate new picker and return it to the channel.
  // Note that we use our own picker regardless of connectivity state.
  channel_control_helper()->UpdateState(
      state, status,
      MakeRefCounted<Picker>(
          RefAsSubclass<AutoSharding>(DEBUG_LOCATION, "AutoShardingPicker"),
          slice_map_, assignment_pending_));
  // This policy establishes connections lazily, in response to picks.
  // However, if it is being used as a child of the priority policy, it will
  // not be getting any picks once it reports TRANSIENT_FAILURE, and in some
  // cases even when it reports CONNECTING, due to the failover timer in the
  // priority policy.  Because it reports TRANSIENT_FAILURE when only two
  // endpoints are failing (aggregation rule 2 above) and CONNECTING when only
  // one endpoint is reporting TRANSIENT_FAILURE (aggregation rule 4 above),
  // this means that the priority policy could fail over to the next priority
  // when the policy is only attempting a small number of endpoints.
  //
  // To work around this, when the aggregated connectivity state is either
  // TRANSIENT_FAILURE or CONNECTING, if we do not have at least one CONNECTING
  // endpoint but we have at least one IDLE endpoint, then we trigger a
  // connection attempt on one of the IDLE endpoints.  This is the same
  // behavior as the ring_hash LB policy.
  if ((state == GRPC_CHANNEL_CONNECTING ||
       state == GRPC_CHANNEL_TRANSIENT_FAILURE) &&
      num_connecting == 0 && idle_endpoint != nullptr) {
    GRPC_TRACE_LOG(autosharding_lb, INFO)
        << "[AS " << this
        << "] triggering internal connection attempt for endpoint "
        << idle_endpoint << " ("
        << endpoints_[idle_endpoint->index()].ToString() << ") (index "
        << idle_endpoint->index() << " of " << endpoints_.size() << ")";
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
        json, JsonArgs(), "errors validating autosharding LB policy config");
  }
};

}  // namespace

void RegisterAutoShardingLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<AutoShardingFactory>());
}

}  // namespace grpc_core
