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

#include "src/core/filter/access_log/access_log_formatter.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace testing {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

AccessLogCallData MakeCallData() {
  AccessLogCallData data;
  data.start_time = absl::FromCivil(absl::CivilSecond(2026, 1, 2, 3, 4, 5),
                                    absl::UTCTimeZone()) +
                    absl::Milliseconds(678);
  data.duration = Duration::Milliseconds(42);
  data.request_headers[":path"] = "/pkg.Service/Method";
  data.request_headers["user-agent"] = "grpc-c++/1.0";
  data.response_headers["content-type"] = "application/grpc";
  data.response_trailers["grpc-message"] = "all good";
  data.grpc_status = GRPC_STATUS_UNAVAILABLE;
  data.bytes_received = 10;
  data.bytes_sent = 20;
  data.downstream_remote_address = "ipv4:127.0.0.1:1234";
  return data;
}

std::string FormatOrDie(absl::string_view format,
                        const AccessLogCallData& data) {
  auto formatter = AccessLogFormatter::Parse(format);
  EXPECT_TRUE(formatter.ok()) << formatter.status();
  if (!formatter.ok()) return "";
  return formatter->Format(data);
}

TEST(AccessLogFormatterTest, LiteralOnly) {
  EXPECT_EQ(FormatOrDie("hello world", MakeCallData()), "hello world");
}

TEST(AccessLogFormatterTest, EscapedPercent) {
  EXPECT_EQ(FormatOrDie("100%% done", MakeCallData()), "100% done");
}

TEST(AccessLogFormatterTest, EmptyFormat) {
  EXPECT_EQ(FormatOrDie("", MakeCallData()), "");
}

TEST(AccessLogFormatterTest, AllOperators) {
  EXPECT_EQ(FormatOrDie("%START_TIME% %DURATION% %REQ(:PATH)% "
                        "%RESP(content-type)% %TRAILER(grpc-message)% "
                        "%GRPC_STATUS% %GRPC_STATUS_NUMBER% %BYTES_RECEIVED% "
                        "%BYTES_SENT% %DOWNSTREAM_REMOTE_ADDRESS% %PROTOCOL%",
                        MakeCallData()),
            "2026-01-02T03:04:05.678Z 42 /pkg.Service/Method application/grpc "
            "all good Unavailable 14 10 20 ipv4:127.0.0.1:1234 HTTP/2");
}

TEST(AccessLogFormatterTest, StartTimeCustomFormat) {
  EXPECT_EQ(FormatOrDie("%START_TIME(%Y/%m/%d %H:%M:%S.%3f)%", MakeCallData()),
            "2026/01/02 03:04:05.678");
  EXPECT_EQ(FormatOrDie("%START_TIME(%s)%", MakeCallData()), "1767323045");
  EXPECT_EQ(FormatOrDie("%START_TIME(%f)%", MakeCallData()), "678000000");
}

TEST(AccessLogFormatterTest, HeaderNamesAreCaseInsensitive) {
  EXPECT_EQ(FormatOrDie("%REQ(User-Agent)%", MakeCallData()), "grpc-c++/1.0");
}

TEST(AccessLogFormatterTest, AlternativeHeader) {
  EXPECT_EQ(FormatOrDie("%REQ(x-missing?user-agent)%", MakeCallData()),
            "grpc-c++/1.0");
  EXPECT_EQ(FormatOrDie("%REQ(user-agent?:path)%", MakeCallData()),
            "grpc-c++/1.0");
}

TEST(AccessLogFormatterTest, Truncation) {
  EXPECT_EQ(FormatOrDie("%REQ(:path):4%", MakeCallData()), "/pkg");
  EXPECT_EQ(FormatOrDie("%REQ(:path):100%", MakeCallData()),
            "/pkg.Service/Method");
  EXPECT_EQ(FormatOrDie("%GRPC_STATUS:3%", MakeCallData()), "Una");
}

TEST(AccessLogFormatterTest, MissingValuesPrintDash) {
  AccessLogCallData data;
  EXPECT_EQ(FormatOrDie("%START_TIME%|%DURATION%|%REQ(x)%|%RESP(x)%|"
                        "%TRAILER(x)%|%GRPC_STATUS%|%BYTES_RECEIVED%|"
                        "%BYTES_SENT%|%DOWNSTREAM_REMOTE_ADDRESS%",
                        data),
            "-|-|-|-|-|-|-|-|-");
}

TEST(AccessLogFormatterTest, GrpcStatusFormats) {
  AccessLogCallData data;
  data.grpc_status = GRPC_STATUS_DEADLINE_EXCEEDED;
  EXPECT_EQ(FormatOrDie("%GRPC_STATUS(CAMEL_STRING)%", data),
            "DeadlineExceeded");
  EXPECT_EQ(FormatOrDie("%GRPC_STATUS(SNAKE_STRING)%", data),
            "DEADLINE_EXCEEDED");
  EXPECT_EQ(FormatOrDie("%GRPC_STATUS(NUMBER)%", data), "4");
  data.grpc_status = GRPC_STATUS_OK;
  EXPECT_EQ(FormatOrDie("%GRPC_STATUS%", data), "OK");
  data.grpc_status = static_cast<grpc_status_code>(99);
  EXPECT_EQ(FormatOrDie("%GRPC_STATUS%", data), "99");
}

TEST(AccessLogFormatterTest, ReferencedHeaders) {
  auto formatter =
      AccessLogFormatter::Parse("%REQ(A?B)% %RESP(C)% %TRAILER(D)% %DURATION%");
  ASSERT_TRUE(formatter.ok()) << formatter.status();
  AccessLogHeaderNames names;
  formatter->AddReferencedHeaders(&names);
  EXPECT_THAT(names.request, ElementsAre("a", "b"));
  EXPECT_THAT(names.response, ElementsAre("c"));
  EXPECT_THAT(names.trailer, ElementsAre("d"));
}

TEST(AccessLogFormatterTest, Equality) {
  auto a = AccessLogFormatter::Parse("%REQ(a)%");
  auto b = AccessLogFormatter::Parse("%REQ(a)%");
  auto c = AccessLogFormatter::Parse("%REQ(b)%");
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(b.ok());
  ASSERT_TRUE(c.ok());
  EXPECT_EQ(*a, *b);
  EXPECT_FALSE(*a == *c);
  EXPECT_EQ(a->format(), "%REQ(a)%");
}

struct InvalidFormatCase {
  const char* format;
  const char* error;
};

class AccessLogFormatterInvalidTest
    : public ::testing::TestWithParam<InvalidFormatCase> {};

TEST_P(AccessLogFormatterInvalidTest, Rejected) {
  auto formatter = AccessLogFormatter::Parse(GetParam().format);
  ASSERT_FALSE(formatter.ok());
  EXPECT_EQ(formatter.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(formatter.status().message(), HasSubstr(GetParam().error));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidFormats, AccessLogFormatterInvalidTest,
    ::testing::Values(
        InvalidFormatCase{"%", "expected operator name"},
        InvalidFormatCase{"abc %lower%", "expected operator name"},
        InvalidFormatCase{"%DURATION", "missing closing '%'"},
        InvalidFormatCase{"%REQ(a%", "missing ')'"},
        InvalidFormatCase{"%REQ(a):%", "invalid length"},
        InvalidFormatCase{"%REQ(a):x%", "invalid length"},
        InvalidFormatCase{"%UNKNOWN_OP%", "unsupported access log operator"},
        InvalidFormatCase{"%REQ%", "requires a header"},
        InvalidFormatCase{"%REQ()%", "requires a header"},
        InvalidFormatCase{"%REQ(a?b?c)%", "invalid header"},
        InvalidFormatCase{"%REQ(?b)%", "invalid header"},
        InvalidFormatCase{"%REQ(a?)%", "invalid header"},
        InvalidFormatCase{"%DURATION(x)%", "takes no argument"},
        InvalidFormatCase{"%GRPC_STATUS(BAD)%", "supports only"}));

}  // namespace
}  // namespace testing
}  // namespace grpc_core
