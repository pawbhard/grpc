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

#include <grpc/grpc.h>
#include <stdint.h>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/load_balancing/autosharding/autoshard.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

class AutoShardingTest : public LoadBalancingPolicyTest {
 protected:
  AutoShardingTest() : LoadBalancingPolicyTest("autosharding_experimental") {}

  // Builds an autosharding config with the given field values.  An empty
  // initial_assignment_timeout means the field is omitted (default of 60s).
  static RefCountedPtr<LoadBalancingPolicy::Config> MakeAutoShardingConfig(
      const std::string& slice_key_header_name = "x-slice-key",
      bool enable_fallback = true,
      const std::string& initial_assignment_timeout = "1s",
      const std::string& channel_factory_key = "sharding_service",
      const std::string& slicing_target = "my_slicing_target") {
    Json::Object fields;
    fields["channelFactoryKey"] = Json::FromString(channel_factory_key);
    fields["slicingTarget"] = Json::FromString(slicing_target);
    fields["sliceKeyHeaderName"] = Json::FromString(slice_key_header_name);
    fields["enableFallback"] = Json::FromBool(enable_fallback);
    if (!initial_assignment_timeout.empty()) {
      fields["initialAssignmentTimeout"] =
          Json::FromString(initial_assignment_timeout);
    }
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"autosharding_experimental", Json::FromObject(fields)}})}));
  }

  // Applies an update with the given addresses and config, and expects the
  // policy to start in IDLE state with a picker that queues picks while it
  // waits for the initial assignment from the sharding service.
  RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> ApplyUpdateAndExpectIdle(
      absl::Span<const absl::string_view> addresses,
      RefCountedPtr<LoadBalancingPolicy::Config> config) {
    EXPECT_EQ(
        ApplyUpdate(BuildUpdate(addresses, std::move(config)), lb_policy()),
        absl::OkStatus());
    auto picker = ExpectState(GRPC_CHANNEL_IDLE);
    // While waiting for the initial assignment, picks should be queued, and
    // no connections should be attempted (child policies are created
    // lazily).
    ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
    for (absl::string_view address : addresses) {
      EXPECT_EQ(FindSubchannel(address), nullptr);
    }
    return picker;
  }

  // The metadata used for picks.  The value is arbitrary, since the policy
  // will not have received any assignment from the sharding service in these
  // tests, so all keys will fall into the fallback pool.
  static const std::map<std::string, std::string> kSliceKeyMetadata;

  static constexpr std::array<absl::string_view, 2> kAddresses = {
      "ipv4:127.0.0.1:441", "ipv4:127.0.0.1:442"};
};

const std::map<std::string, std::string> AutoShardingTest::kSliceKeyMetadata = {
    {"x-slice-key", "some_key"}};

TEST_F(AutoShardingTest, QueuesPicksUntilInitialAssignmentTimeoutExpires) {
  SetExpectedTimerDuration(std::chrono::seconds(1));
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  // The policy should report a new picker that uses the fallback pool.
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The pick should trigger a connection attempt on exactly one endpoint.
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  SubchannelState* subchannel = nullptr;
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr) {
      ASSERT_EQ(subchannel, nullptr);
      subchannel = sc;
    }
  }
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  // No other subchannels should have been created.
  for (absl::string_view address : kAddresses) {
    auto* sc = FindSubchannel(address);
    if (sc != nullptr) EXPECT_EQ(sc, subchannel);
  }
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  ASSERT_TRUE(address.has_value());
  EXPECT_THAT(*address, ::testing::AnyOf(kAddresses[0], kAddresses[1]));
}

TEST_F(AutoShardingTest, FallbackDisabledFailsPicksAfterTimeout) {
  auto picker = ApplyUpdateAndExpectIdle(
      kAddresses,
      MakeAutoShardingConfig(/*slice_key_header_name=*/"x-slice-key",
                             /*enable_fallback=*/false));
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  // Note: Can't use ExpectPickFail() here, because it does not pass the
  // slice key metadata that the picker needs in order to get past the key
  // extraction step.
  auto pick_result = DoPick(picker.get(), {}, kSliceKeyMetadata);
  auto* fail =
      std::get_if<LoadBalancingPolicy::PickResult::Fail>(&pick_result.result);
  ASSERT_NE(fail, nullptr);
  EXPECT_EQ(fail->status,
            absl::UnavailableError(
                "no assignment received from the sharding service"));
}

TEST_F(AutoShardingTest, MissingSliceKeyHeaderFails) {
  auto picker = ApplyUpdateAndExpectIdle(kAddresses, MakeAutoShardingConfig());
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(picker.get(), [&](const absl::Status& status) {
    EXPECT_EQ(status, absl::InternalError(
                          "slice key header \"x-slice-key\" not present"));
  });
}

TEST_F(AutoShardingTest, EmptyEndpointList) {
  const std::vector<absl::string_view> kNoAddresses;
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kNoAddresses, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::UnavailableError("empty address list: "));
  ExpectTransientFailureUpdate(absl::UnavailableError("empty address list: "));
}

TEST_F(AutoShardingTest, EndpointsWithDuplicateHostnamesAreCollapsed) {
  // Two endpoints with the same hostname attribute.  The last one should win.
  const std::vector<EndpointAddresses> endpoints = {
      MakeEndpointAddresses({kAddresses[0]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1")),
      MakeEndpointAddresses({kAddresses[1]},
                            ChannelArgs().Set(GRPC_ARG_ADDRESS_NAME, "host1"))};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(endpoints, MakeAutoShardingConfig()),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  // Expire the initial assignment timer.
  IncrementTimeBy(Duration::Seconds(1));
  picker = ExpectState(GRPC_CHANNEL_IDLE);
  // The pick should trigger a connection attempt on the endpoint that won
  // (the one with index 1).
  ExpectPickQueued(picker.get(), {}, kSliceKeyMetadata);
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  EXPECT_EQ(FindSubchannel(kAddresses[0]), nullptr);
  auto* subchannel = FindSubchannel(kAddresses[1]);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, kSliceKeyMetadata);
  EXPECT_EQ(address, kAddresses[1]);
}

TEST_F(AutoShardingTest, ConfigFailsWithoutSliceKeyHeader) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"enableFallback", Json::FromBool(true)}})}})}));
  EXPECT_FALSE(config.ok());
}

TEST_F(AutoShardingTest, ConfigFailsWithZeroInitialAssignmentTimeout) {
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental",
                Json::FromObject(
                    {{"sliceKeyHeaderName", Json::FromString("x-slice-key")},
                     {"initialAssignmentTimeout",
                      Json::FromString("0s")}})}})}));
  EXPECT_FALSE(config.ok());
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
