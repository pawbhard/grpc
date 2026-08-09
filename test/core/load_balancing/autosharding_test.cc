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

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/load_balancing/lb_policy.h"
#include "src/core/util/json/json.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/load_balancing/lb_policy_test_lib.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

//
// Tests for the AutoSharding LB policy, exercised entirely through the
// public UpdateLocked()/picker interface -- all of the policy's data
// structures (SliceMap, Picker, etc.) are private nested classes, per
// gRFC A119 review feedback (see
// https://github.com/grpc/grpc/pull/43061), so this mirrors ring_hash_test
// in only testing black-box behavior.
//
// Since receiving a real Assignment requires the (not yet implemented)
// `Shard` stream -- see the file comment in autosharding.cc -- these only
// cover the endpoint management, connectivity aggregation, and
// fallback-pool picking paths that don't depend on it. Once that stream
// exists, tests exercising the matched-slice picking path (via a real
// Assignment fed through UpdateLocked()) should be added here.
//

class AutoShardingTest : public LoadBalancingPolicyTest {
 protected:
  AutoShardingTest() : LoadBalancingPolicyTest("autosharding_experimental") {}

  static RefCountedPtr<LoadBalancingPolicy::Config> MakeAutoShardingConfig(
      absl::string_view slice_key_header_name = "x-shard-key",
      bool enable_fallback = false) {
    Json::Object fields{
        {"sliceKeyHeaderName",
         Json::FromString(std::string(slice_key_header_name))},
        {"enableFallback", Json::FromBool(enable_fallback)},
    };
    return MakeConfig(Json::FromArray({Json::FromObject(
        {{"autosharding_experimental", Json::FromObject(fields)}})}));
  }
};

TEST_F(AutoShardingTest, ConfigRequiresSliceKeyHeaderName) {
  auto status_or_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray({Json::FromObject(
              {{"autosharding_experimental", Json::FromObject({})}})}));
  EXPECT_FALSE(status_or_config.ok());
  EXPECT_TRUE(absl::StrContains(status_or_config.status().message(),
                                "sliceKeyHeaderName"))
      << status_or_config.status();
}

TEST_F(AutoShardingTest, MissingShardKeyHeaderFails) {
  constexpr absl::string_view kAddress = "ipv4:127.0.0.1:441";
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddress}, MakeAutoShardingConfig(
                                                    "x-shard-key",
                                                    /*enable_fallback=*/true)),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickFail(picker.get(), [](const absl::Status& status) {
    EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
    EXPECT_TRUE(absl::StrContains(status.message(), "x-shard-key")) << status;
  });
}

TEST_F(AutoShardingTest, FailsWhenFallbackDisabledAndNoAssignment) {
  constexpr absl::string_view kAddress = "ipv4:127.0.0.1:441";
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddress}, MakeAutoShardingConfig(
                                                    "x-shard-key",
                                                    /*enable_fallback=*/false)),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  auto pick_result = DoPick(picker.get(), {}, {{"x-shard-key", "user-123"}});
  auto* fail =
      std::get_if<LoadBalancingPolicy::PickResult::Fail>(&pick_result.result);
  ASSERT_NE(fail, nullptr);
  EXPECT_EQ(fail->status.code(), absl::StatusCode::kUnavailable);
  EXPECT_TRUE(absl::StrContains(fail->status.message(), "no assignment"))
      << fail->status;
}

// Since no Assignment can be received yet, the only way for a pick to ever
// succeed in this initial implementation is via the fallback pool (i.e.,
// all endpoints provided by the name resolver).
TEST_F(AutoShardingTest, RoutesToFallbackPoolWhenFallbackEnabled) {
  constexpr absl::string_view kAddress = "ipv4:127.0.0.1:441";
  EXPECT_EQ(ApplyUpdate(BuildUpdate({kAddress}, MakeAutoShardingConfig(
                                                    "x-shard-key",
                                                    /*enable_fallback=*/true)),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, {{"x-shard-key", "user-123"}});
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddress);
  ASSERT_NE(subchannel, nullptr);
  EXPECT_TRUE(subchannel->ConnectionRequested());
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {}, {{"x-shard-key", "user-123"}});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address =
      ExpectPickComplete(picker.get(), {}, {{"x-shard-key", "user-123"}});
  EXPECT_EQ(address, kAddress);
}

TEST_F(AutoShardingTest, DuplicateHostnameKeepsFirstEndpoint) {
  constexpr absl::string_view kAddress = "ipv4:127.0.0.1:441";
  EXPECT_EQ(
      ApplyUpdate(BuildUpdate({kAddress, kAddress},
                              MakeAutoShardingConfig("x-shard-key",
                                                     /*enable_fallback=*/true)),
                  lb_policy()),
      absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, {{"x-shard-key", "k"}});
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  auto* subchannel = FindSubchannel(kAddress);
  ASSERT_NE(subchannel, nullptr);
  subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  ExpectPickQueued(picker.get(), {}, {{"x-shard-key", "k"}});
  subchannel->SetConnectivityState(GRPC_CHANNEL_READY);
  picker = ExpectState(GRPC_CHANNEL_READY);
  auto address = ExpectPickComplete(picker.get(), {}, {{"x-shard-key", "k"}});
  EXPECT_EQ(address, kAddress);
}

TEST_F(AutoShardingTest, ConnectivityStateAggregation) {
  const std::array<absl::string_view, 2> kAddresses = {"ipv4:127.0.0.1:441",
                                                       "ipv4:127.0.0.1:442"};
  EXPECT_EQ(ApplyUpdate(BuildUpdate(kAddresses, MakeAutoShardingConfig(
                                                    "x-shard-key",
                                                    /*enable_fallback=*/true)),
                        lb_policy()),
            absl::OkStatus());
  auto picker = ExpectState(GRPC_CHANNEL_IDLE);
  ExpectPickQueued(picker.get(), {}, {{"x-shard-key", "k"}});
  WaitForWorkSerializerToFlush();
  WaitForWorkSerializerToFlush();
  // The picker picks a random starting endpoint within the fallback pool,
  // so we don't know in advance which of the two addresses it will
  // connect to first.
  auto* subchannel0 = FindSubchannel(kAddresses[0]);
  auto* subchannel1 = FindSubchannel(kAddresses[1]);
  ASSERT_TRUE(subchannel0 != nullptr || subchannel1 != nullptr);
  SubchannelState* connecting_subchannel =
      subchannel0 != nullptr && subchannel0->ConnectionRequested()
          ? subchannel0
          : subchannel1;
  ASSERT_NE(connecting_subchannel, nullptr);
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectState(GRPC_CHANNEL_CONNECTING);
  connecting_subchannel->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                              absl::UnavailableError("ugh1"));
  ExpectReresolutionRequest();
  // Only 1 of 2 endpoints in TRANSIENT_FAILURE -> aggregation rule 4:
  // report CONNECTING, and trigger a connection attempt on the other
  // (IDLE) endpoint even without any picks.
  picker = ExpectState(GRPC_CHANNEL_CONNECTING);
  SubchannelState* other_subchannel = connecting_subchannel == subchannel0
                                          ? FindSubchannel(kAddresses[1])
                                          : FindSubchannel(kAddresses[0]);
  ASSERT_NE(other_subchannel, nullptr);
  EXPECT_TRUE(other_subchannel->ConnectionRequested());
  subchannel1 = other_subchannel;
  subchannel1->SetConnectivityState(GRPC_CHANNEL_CONNECTING);
  ExpectState(GRPC_CHANNEL_CONNECTING);
  subchannel1->SetConnectivityState(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    absl::UnavailableError("ugh2"));
  ExpectReresolutionRequest();
  // Both endpoints in TRANSIENT_FAILURE -> aggregation rule 2.
  ExpectState(GRPC_CHANNEL_TRANSIENT_FAILURE,
              absl::UnavailableError(
                  "no reachable endpoints; last error: UNAVAILABLE: ugh2"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
