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
#include <grpc/credentials.h>
#include <grpc/grpc.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/certificate_provider_store.h"
#include "src/core/xds/grpc/xds_channel_factory.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/grpc/xds_transport_grpc.h"
#include "src/core/xds/xds_client/channel_handle.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

// A target string that is never resolved, because nothing here starts a call or
// watches connectivity. Only the string itself matters to these tests.
constexpr absl::string_view kTarget = "localhost:1";

class ChannelFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto json = JsonParse("{\"channel_creds\": [{\"type\": \"insecure\"}]}");
    ASSERT_TRUE(json.ok()) << json.status().ToString();
    ValidationErrors errors;
    channel_creds_config_ =
        ParseXdsBootstrapChannelCreds(*json, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok());
    ASSERT_NE(channel_creds_config_, nullptr);
    auto store = MakeRefCounted<CertificateProviderStore>(
        CertificateProviderStore::PluginDefinitionMap{});
    transport_factory_ =
        MakeRefCounted<GrpcXdsTransportFactory>(ChannelArgs(), std::move(store));
  }

  // An xDS server target with the channel credentials above. Two calls with the
  // same arguments produce targets that compare equal.
  std::shared_ptr<GrpcXdsServerTarget> MakeTarget(
      std::vector<std::pair<std::string, std::string>> initial_metadata = {}) {
    return std::make_shared<GrpcXdsServerTarget>(
        std::string(kTarget), channel_creds_config_,
        /*call_creds_configs=*/std::vector<RefCountedPtr<const CallCredsConfig>>{},
        std::move(initial_metadata), Duration::Seconds(10));
  }

  RefCountedPtr<const ChannelCredsConfig> channel_creds_config_;
  RefCountedPtr<GrpcXdsTransportFactory> transport_factory_;
};

TEST_F(ChannelFactoryTest, RegisterTargetRejectsNullTarget) {
  XdsChannelFactory channel_factory(transport_factory_);
  EXPECT_FALSE(channel_factory.RegisterTarget(nullptr).ok());
}

TEST_F(ChannelFactoryTest, RegisterTargetRejectsTargetWithoutChannelCreds) {
  XdsChannelFactory channel_factory(transport_factory_);
  // This is the shape the resource parser returns on every error path. Channel
  // creation would turn its missing credentials into an insecure channel, so
  // registration must refuse it instead.
  auto target = std::make_shared<GrpcXdsServerTarget>(
      std::string(kTarget), /*channel_creds_config=*/nullptr,
      /*call_creds_configs=*/std::vector<RefCountedPtr<const CallCredsConfig>>{});
  EXPECT_FALSE(channel_factory.RegisterTarget(std::move(target)).ok());
}

TEST_F(ChannelFactoryTest, RegisterTargetDeduplicatesEqualTargets) {
  XdsChannelFactory channel_factory(transport_factory_);
  absl::StatusOr<std::string> key1 = channel_factory.RegisterTarget(MakeTarget());
  ASSERT_TRUE(key1.ok()) << key1.status().ToString();
  absl::StatusOr<std::string> key2 = channel_factory.RegisterTarget(MakeTarget());
  ASSERT_TRUE(key2.ok()) << key2.status().ToString();
  EXPECT_EQ(*key1, *key2);
}

TEST_F(ChannelFactoryTest, RegisterTargetDistinguishesDifferentTargets) {
  XdsChannelFactory channel_factory(transport_factory_);
  absl::StatusOr<std::string> key1 = channel_factory.RegisterTarget(MakeTarget());
  ASSERT_TRUE(key1.ok()) << key1.status().ToString();
  absl::StatusOr<std::string> key2 =
      channel_factory.RegisterTarget(MakeTarget({{"k", "v"}}));
  ASSERT_TRUE(key2.ok()) << key2.status().ToString();
  EXPECT_NE(*key1, *key2);
}

TEST_F(ChannelFactoryTest, GetTransportByKeyRejectsUnknownKey) {
  ExecCtx exec_ctx;
  absl::Status status;
  auto transport = transport_factory_->GetTransportByKey("not-registered",
                                                         &status);
  EXPECT_EQ(transport.get(), nullptr);
  EXPECT_FALSE(status.ok());
}

TEST_F(ChannelFactoryTest, GetTransportByKeyReturnsRegisteredTransport) {
  ExecCtx exec_ctx;
  XdsChannelFactory channel_factory(transport_factory_);
  absl::StatusOr<std::string> key = channel_factory.RegisterTarget(MakeTarget());
  ASSERT_TRUE(key.ok()) << key.status().ToString();
  absl::Status status;
  auto transport = transport_factory_->GetTransportByKey(*key, &status);
  ASSERT_NE(transport.get(), nullptr);
  EXPECT_TRUE(status.ok()) << status.ToString();
}

TEST_F(ChannelFactoryTest, CreateChannelReturnsHandleCarryingTheTransport) {
  ExecCtx exec_ctx;
  XdsChannelFactory channel_factory(transport_factory_);
  absl::StatusOr<std::string> key = channel_factory.RegisterTarget(MakeTarget());
  ASSERT_TRUE(key.ok()) << key.status().ToString();
  absl::Status status;
  auto handle = channel_factory.CreateChannel(*key, &status);
  ASSERT_NE(handle.get(), nullptr);
  EXPECT_TRUE(status.ok()) << status.ToString();
  absl::Status bridge_status;
  auto transport = GetTransportFromHandle(handle.get(), &bridge_status);
  EXPECT_NE(transport.get(), nullptr);
  EXPECT_TRUE(bridge_status.ok()) << bridge_status.ToString();
}

TEST_F(ChannelFactoryTest, CreateChannelNeverReturnsNullForUnknownKey) {
  ExecCtx exec_ctx;
  XdsChannelFactory channel_factory(transport_factory_);
  absl::Status status;
  auto handle = channel_factory.CreateChannel("not-registered", &status);
  ASSERT_NE(handle.get(), nullptr);
  EXPECT_FALSE(status.ok());
  absl::Status bridge_status;
  auto transport = GetTransportFromHandle(handle.get(), &bridge_status);
  EXPECT_EQ(transport.get(), nullptr);
  EXPECT_FALSE(bridge_status.ok());
}

TEST_F(ChannelFactoryTest, LameHandleCarriesItsStatus) {
  ExecCtx exec_ctx;
  auto handle = experimental::CreateLameChannelHandle(
      std::string(kTarget), absl::NotFoundError("no such side channel"));
  ASSERT_NE(handle.get(), nullptr);
  absl::Status status;
  auto transport = GetTransportFromHandle(handle.get(), &status);
  EXPECT_EQ(transport.get(), nullptr);
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

TEST_F(ChannelFactoryTest, CreateChannelHandleRejectsNullCreds) {
  ExecCtx exec_ctx;
  absl::Status status;
  auto handle = experimental::CreateChannelHandle(
      kTarget, /*creds=*/nullptr, /*args=*/nullptr,
      /*initial_metadata=*/{}, &status);
  ASSERT_NE(handle.get(), nullptr);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ChannelFactoryTest, CreateChannelHandleWrapsAGrpcChannel) {
  ExecCtx exec_ctx;
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  absl::Status status;
  auto handle = experimental::CreateChannelHandle(
      kTarget, creds, /*args=*/nullptr, /*initial_metadata=*/{{"k", "v"}},
      &status);
  grpc_channel_credentials_release(creds);
  ASSERT_NE(handle.get(), nullptr);
  EXPECT_TRUE(status.ok()) << status.ToString();
  absl::Status bridge_status;
  auto transport = GetTransportFromHandle(handle.get(), &bridge_status);
  EXPECT_NE(transport.get(), nullptr);
  EXPECT_TRUE(bridge_status.ok()) << bridge_status.ToString();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
