//
//
// Copyright 2015 gRPC authors.
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
//

#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

CORE_END2END_TEST(CoreEnd2endTests, CancelWithStatus1) {
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  IncomingStatusOnClient server_status;
  c.NewBatch(1).RecvStatusOnClient(server_status);
  char* dynamic_string = gpr_strdup("xyz");
  c.CancelWithStatus(GRPC_STATUS_UNIMPLEMENTED, dynamic_string);
  // The API of \a description allows for it to be a dynamic/non-const
  // string, test this guarantee.
  gpr_free(dynamic_string);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_THAT(server_status.message(), ::testing::HasSubstr("xyz"));
}

CORE_END2END_TEST(CoreEnd2endTests, CancelWithStatus2) {
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .RecvInitialMetadata(server_initial_metadata);
  char* dynamic_string = gpr_strdup("xyz");
  c.CancelWithStatus(GRPC_STATUS_UNIMPLEMENTED, dynamic_string);
  // The API of \a description allows for it to be a dynamic/non-const
  // string, test this guarantee.
  gpr_free(dynamic_string);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_THAT(server_status.message(), ::testing::HasSubstr("xyz"));
}

CORE_END2END_TEST(CoreEnd2endTests, CancelWithStatus3) {
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .RecvInitialMetadata(server_initial_metadata)
      .SendInitialMetadata({});
  char* dynamic_string = gpr_strdup("xyz");
  c.CancelWithStatus(GRPC_STATUS_UNIMPLEMENTED, dynamic_string);
  // The API of \a description allows for it to be a dynamic/non-const
  // string, test this guarantee.
  gpr_free(dynamic_string);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_THAT(server_status.message(), ::testing::HasSubstr("xyz"));
}

CORE_END2END_TEST(CoreEnd2endTests, CancelWithStatus4) {
  auto c = NewClientCall("/foo").Timeout(Duration::Minutes(1)).Create();
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .RecvStatusOnClient(server_status)
      .RecvInitialMetadata(server_initial_metadata)
      .SendInitialMetadata({})
      .SendCloseFromClient();
  char* dynamic_string = gpr_strdup("xyz");
  c.CancelWithStatus(GRPC_STATUS_UNIMPLEMENTED, dynamic_string);
  // The API of \a description allows for it to be a dynamic/non-const
  // string, test this guarantee.
  gpr_free(dynamic_string);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_THAT(server_status.message(), ::testing::HasSubstr("xyz"));
}

}  // namespace
}  // namespace grpc_core
