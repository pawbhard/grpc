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

#include "src/core/filter/access_log/access_log_filter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "test/core/filters/filter_test_v2.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace {

using ::testing::_;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

// Stores all records in memory.
class FakeSink final : public AccessLogSink {
 public:
  struct State {
    Mutex mu;
    std::vector<AccessLogRecord> records ABSL_GUARDED_BY(mu);
  };

  explicit FakeSink(std::shared_ptr<State> state) : state_(std::move(state)) {}

  void Log(AccessLogRecord record) override {
    MutexLock lock(&state_->mu);
    state_->records.push_back(std::move(record));
  }

 private:
  std::shared_ptr<State> state_;
};

AccessLogValueTemplate StringTemplate(absl::string_view format) {
  auto formatter = AccessLogFormatter::Parse(format);
  EXPECT_TRUE(formatter.ok()) << formatter.status();
  return std::move(*formatter);
}

class AccessLogFilterTest : public FilterTestV2<AccessLogFilter> {
 protected:
  RefCountedPtr<const FilterConfig> MakeConfig(bool is_client) {
    auto config = MakeRefCounted<AccessLogFilter::Config>();
    config->is_client = is_client;
    AccessLogFilter::Logger logger;
    logger.body = StringTemplate("%REQ(:path)% %GRPC_STATUS%");
    logger.attributes.emplace_back("ua", StringTemplate("%REQ(user-agent)%"));
    logger.attributes.emplace_back("resp", StringTemplate("%RESP(x-resp)%"));
    logger.attributes.emplace_back("trailer",
                                   StringTemplate("%TRAILER(x-trailer)%"));
    logger.attributes.emplace_back(
        "bytes", StringTemplate("%BYTES_SENT%/%BYTES_RECEIVED%"));
    logger.attributes.emplace_back(
        "peer", StringTemplate("%DOWNSTREAM_REMOTE_ADDRESS%"));
    logger.attributes.emplace_back("static_int", int64_t{7});
    logger.attributes.emplace_back("static_bool", true);
    logger.sink = MakeRefCounted<AccessLogFilter::SinkHolder>(
        std::make_unique<FakeSink>(sink_state_));
    config->loggers.push_back(std::move(logger));
    config->ComputeHeaderNames();
    return config;
  }

  std::vector<AccessLogRecord> TakeRecords() {
    MutexLock lock(&sink_state_->mu);
    return std::move(sink_state_->records);
  }

  std::shared_ptr<FakeSink::State> sink_state_ =
      std::make_shared<FakeSink::State>();
};

TEST_F(AccessLogFilterTest, CreateFailsWithoutConfig) {
  auto filter = AccessLogFilter::Create(ChannelArgs(), ChannelFilter::Args());
  EXPECT_EQ(filter.status(),
            absl::InternalError("access_log: filter config not set"));
}

TEST_F(AccessLogFilterTest, HeaderNamesComputed) {
  auto config = MakeConfig(/*is_client=*/true);
  const auto& c = DownCast<const AccessLogFilter::Config&>(*config);
  EXPECT_THAT(c.header_names.request,
              UnorderedElementsAre(":path", "user-agent"));
  EXPECT_THAT(c.header_names.response, UnorderedElementsAre("x-resp"));
  EXPECT_THAT(c.header_names.trailer, UnorderedElementsAre("x-trailer"));
}

TEST_F(AccessLogFilterTest, OneRecordPerCallOnClient) {
  {
    Call call(
        MakeChannel(ChannelArgs(), MakeConfig(/*is_client=*/true)).value());
    EXPECT_EVENT(Started(&call, _));
    call.Start(call.NewClientMetadata(
        {{":path", "/pkg.Svc/Method"}, {"user-agent", "test-agent"}}));
    Step();
    EXPECT_EVENT(ForwardedServerInitialMetadata(&call, _));
    call.ForwardServerInitialMetadata(
        call.NewServerMetadata({{"x-resp", "r1"}}));
    Step();
    EXPECT_EVENT(ForwardedMessageClientToServer(&call, _));
    call.ForwardMessageClientToServer(call.NewMessage("abc"));
    Step();
    EXPECT_EVENT(ForwardedMessageServerToClient(&call, _));
    call.ForwardMessageServerToClient(call.NewMessage("hello"));
    Step();
    EXPECT_EVENT(Finished(&call, _));
    call.FinishNextFilter(
        call.NewServerMetadata({{"grpc-status", "14"}, {"x-trailer", "t1"}}));
    Step();
  }
  // Finalizing the call does not write a second record.
  std::vector<AccessLogRecord> records = TakeRecords();
  ASSERT_EQ(records.size(), 1u);
  const AccessLogRecord& record = records[0];
  ASSERT_TRUE(record.body.has_value());
  EXPECT_EQ(std::get<std::string>(*record.body), "/pkg.Svc/Method Unavailable");
  EXPECT_THAT(
      record.attributes,
      UnorderedElementsAre(Pair("ua", AccessLogValue("test-agent")),
                           Pair("resp", AccessLogValue("r1")),
                           Pair("trailer", AccessLogValue("t1")),
                           Pair("bytes", AccessLogValue("3/5")),
                           Pair("peer", AccessLogValue("-")),
                           Pair("static_int", AccessLogValue(int64_t{7})),
                           Pair("static_bool", AccessLogValue(true))));
}

TEST_F(AccessLogFilterTest, ServerCountsBytesInOtherDirection) {
  {
    Call call(
        MakeChannel(ChannelArgs(), MakeConfig(/*is_client=*/false)).value());
    EXPECT_EVENT(Started(&call, _));
    call.Start(call.NewClientMetadata({{":path", "/pkg.Svc/Method"}}));
    Step();
    EXPECT_EVENT(ForwardedMessageClientToServer(&call, _));
    call.ForwardMessageClientToServer(call.NewMessage("abc"));
    Step();
    EXPECT_EVENT(Finished(&call, _));
    call.FinishNextFilter(call.NewServerMetadata({{"grpc-status", "0"}}));
    Step();
  }
  std::vector<AccessLogRecord> records = TakeRecords();
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(std::get<std::string>(*records[0].body), "/pkg.Svc/Method OK");
  EXPECT_THAT(records[0].attributes,
              ::testing::Contains(Pair("bytes", AccessLogValue("0/3"))));
}

TEST_F(AccessLogFilterTest, CancelledCallStillLogged) {
  {
    Call call(
        MakeChannel(ChannelArgs(), MakeConfig(/*is_client=*/true)).value());
    EXPECT_EVENT(Started(&call, _));
    call.Start(call.NewClientMetadata({{":path", "/pkg.Svc/Method"}}));
    Step();
    call.Cancel();
  }
  std::vector<AccessLogRecord> records = TakeRecords();
  ASSERT_EQ(records.size(), 1u);
}

TEST_F(AccessLogFilterTest, ConfigEquality) {
  auto a = MakeConfig(/*is_client=*/true);
  auto b = MakeConfig(/*is_client=*/true);
  // Different sink holders, so not equal.
  EXPECT_NE(*a, *b);
  EXPECT_EQ(*a, *a);
  EXPECT_THAT(a->ToString(), ::testing::HasSubstr("body={string=\"%REQ(:path)% "
                                                  "%GRPC_STATUS%\"}"));
}

}  // namespace
}  // namespace grpc_core
