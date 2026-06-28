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

// Minimal reproduction for the inbound-message-bypass bug in the v1
// promise-based filter (ClientCallData) receive path.
//
// Scenario:
//   1. A promise-based filter stalls server initial metadata (it never lets the
//      metadata through its interceptor) and mutates every server->client
//      message.
//   2. While initial metadata is stalled, the transport delivers a message.
//      ClientCallData correctly parks the message (AllowRecvMessage() == false),
//      so it is NOT yet pushed through the filter's message interceptor.
//   3. The transport then delivers trailing metadata. This drives
//      ClientCallData::ReceiveMessage::Done(), moving the parked message to the
//      kCompletedWhileBatchCompleted state, whose handler closes the interceptor
//      pipe and delivers the *raw* buffered message straight to the application.
//
// The bug: the message bypasses the filter's mutation interceptor entirely. The
// application receives the un-mutated payload.
//
// This test asserts the *correct* behavior (the message should be mutated by the
// filter), so it FAILS on the current code, demonstrating the bug. It should
// pass once the bypass is fixed.

#include "src/core/lib/channel/promise_based_filter.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

constexpr char kMutationSuffix[] = "_INTERCEPTED";

// Controls the releasable stall of server initial metadata. When
// g_release_initial_md is false, the filter's server-initial-metadata
// interceptor parks (Pending) and stashes the activity waker so the test can
// resume it after flipping the flag.
std::atomic<bool> g_release_initial_md{false};
Waker g_init_md_waker;

// A v1 promise-based (client) filter that:
//  * stalls server initial metadata until the test releases it (modelling a
//    filter that holds initial metadata while it waits on something else),
//  * appends kMutationSuffix to every server->client message.
class StallInitialMetadataMutateMessageFilter final : public ChannelFilter {
 public:
  static absl::string_view TypeName() { return "stall_mutate_test_filter"; }

  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    // Stall server initial metadata until released by the test.
    args.server_initial_metadata->InterceptAndMap([](ServerMetadataHandle md) {
      return [md = std::move(md)]() mutable -> Poll<ServerMetadataHandle> {
        if (g_release_initial_md.load(std::memory_order_acquire)) {
          return std::move(md);
        }
        g_init_md_waker = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      };
    });
    // Mutate every inbound (server->client) message. This is the hook that the
    // buggy bypass path skips.
    args.server_to_client_messages->InterceptAndMap([](MessageHandle msg) {
      msg->payload()->Append(Slice::FromCopiedString(kMutationSuffix));
      return msg;
    });
    return next(std::move(args));
  }

  static absl::StatusOr<
      std::unique_ptr<StallInitialMetadataMutateMessageFilter>>
  Create(const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<StallInitialMetadataMutateMessageFilter>();
  }
};

const grpc_channel_filter kStallFilter =
    MakePromiseBasedFilter<StallInitialMetadataMutateMessageFilter,
                           FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages>();

// A minimal terminating "transport" filter that captures the recv closures /
// payload pointers that ClientCallData forwards down, so the test can complete
// them in a controlled order (as a real transport would).
struct MockTransport {
  CallCombiner* call_combiner = nullptr;

  grpc_metadata_batch* recv_initial_metadata = nullptr;
  grpc_closure* recv_initial_metadata_ready = nullptr;

  std::optional<SliceBuffer>* recv_message = nullptr;
  uint32_t* recv_message_flags = nullptr;
  grpc_closure* recv_message_ready = nullptr;

  grpc_metadata_batch* recv_trailing_metadata = nullptr;
  grpc_closure* recv_trailing_metadata_ready = nullptr;
};

MockTransport* g_mock = nullptr;

void MockStartBatch(grpc_call_element*,
                    grpc_transport_stream_op_batch* op) {
  MockTransport* m = g_mock;
  if (op->recv_initial_metadata) {
    m->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    m->recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
  }
  if (op->recv_message) {
    m->recv_message = op->payload->recv_message.recv_message;
    m->recv_message_flags = op->payload->recv_message.flags;
    m->recv_message_ready = op->payload->recv_message.recv_message_ready;
  }
  if (op->recv_trailing_metadata) {
    m->recv_trailing_metadata =
        op->payload->recv_trailing_metadata.recv_trailing_metadata;
    m->recv_trailing_metadata_ready =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  }
  // The mock has no async sends, so complete any non-recv work immediately.
  if (op->on_complete != nullptr) {
    GRPC_CALL_COMBINER_START(m->call_combiner, op->on_complete,
                             absl::OkStatus(), "mock_on_complete");
  }
  // As the terminal transport, relinquish the call combiner that was passed
  // down with this batch (mirrors connected_channel.cc).
  GRPC_CALL_COMBINER_STOP(m->call_combiner, "mock passed batch to transport");
}

void MockStartTransportOp(grpc_channel_element*, grpc_transport_op* op) {
  if (op->on_consumed != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
  }
}

grpc_error_handle MockInitCallElem(grpc_call_element*,
                                   const grpc_call_element_args*) {
  return absl::OkStatus();
}
void MockDestroyCallElem(grpc_call_element*, const grpc_call_final_info*,
                         grpc_closure*) {}
grpc_error_handle MockInitChannelElem(grpc_channel_element*,
                                      grpc_channel_element_args*) {
  return absl::OkStatus();
}
void MockDestroyChannelElem(grpc_channel_element*) {}

const grpc_channel_filter kMockTransportFilter = {
    MockStartBatch,
    MockStartTransportOp,
    0,  // sizeof_call_data
    MockInitCallElem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    MockDestroyCallElem,
    0,  // sizeof_channel_data
    MockInitChannelElem,
    grpc_channel_stack_no_post_init,
    MockDestroyChannelElem,
    grpc_channel_next_get_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("mock_transport"),
};

// Test-side context for the application closures. Every closure that
// ClientCallData hands back to "the surface" runs holding the call combiner and
// must yield it again, so each of these stops the combiner.
struct AppCtx {
  CallCombiner* combiner = nullptr;
  bool init_md_ready_called = false;
  bool msg_ready_called = false;
  bool trailing_ready_called = false;
  std::optional<SliceBuffer>* recv_message = nullptr;
  std::string captured_payload;
};

void OnRecvInitialMetadataReady(void* arg, grpc_error_handle) {
  auto* ctx = static_cast<AppCtx*>(arg);
  ctx->init_md_ready_called = true;
  GRPC_CALL_COMBINER_STOP(ctx->combiner, "app:recv_initial_metadata_ready");
}

void OnRecvMessageReady(void* arg, grpc_error_handle) {
  auto* ctx = static_cast<AppCtx*>(arg);
  ctx->msg_ready_called = true;
  if (ctx->recv_message->has_value()) {
    ctx->captured_payload = (*ctx->recv_message)->JoinIntoString();
  }
  GRPC_CALL_COMBINER_STOP(ctx->combiner, "app:recv_message_ready");
}

void OnRecvTrailingMetadataReady(void* arg, grpc_error_handle) {
  auto* ctx = static_cast<AppCtx*>(arg);
  ctx->trailing_ready_called = true;
  GRPC_CALL_COMBINER_STOP(ctx->combiner, "app:recv_trailing_metadata_ready");
}

void OnComplete(void* arg, grpc_error_handle) {
  GRPC_CALL_COMBINER_STOP(static_cast<CallCombiner*>(arg), "app:on_complete");
}

// Issues a batch into the top filter element while holding the call combiner
// (ClientCallData's Flusher yields the combiner when the batch finishes).
struct StartBatchCtx {
  grpc_call_element* elem = nullptr;
  grpc_transport_stream_op_batch* batch = nullptr;
};
void DoStartBatch(void* arg, grpc_error_handle) {
  auto* c = static_cast<StartBatchCtx*>(arg);
  c->elem->filter->start_transport_stream_op_batch(c->elem, c->batch);
}

// Drives: server initial metadata (stalled by the filter) -> inbound message
// (parked) -> trailing metadata with `trailing_status` -> release of initial
// metadata. Asserts the message is delivered only after initial metadata, and
// only after passing through the filter's mutation interceptor.
void RunStalledMessageScenario(grpc_status_code trailing_status) {
  g_release_initial_md.store(false, std::memory_order_release);
  g_init_md_waker = Waker();
  ExecCtx exec_ctx;

  // ---- Build channel stack: [stall filter][mock transport] ----
  std::vector<FilterAndConfig> filters = {{&kStallFilter, nullptr},
                                          {&kMockTransportFilter, nullptr}};
  auto channel_args = CoreConfiguration::Get()
                          .channel_args_preconditioning()
                          .PreconditionChannelArgs(nullptr);
  auto* channel_stack = static_cast<grpc_channel_stack*>(
      gpr_malloc(grpc_channel_stack_size(filters)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR(
      "grpc_channel_stack_init",
      grpc_channel_stack_init(
          1,
          [](void* p, grpc_error_handle) {
            grpc_channel_stack_destroy(static_cast<grpc_channel_stack*>(p));
            gpr_free(p);
          },
          channel_stack, filters, channel_args, "test", channel_stack)));

  // ---- Init a call ----
  CallCombiner call_combiner;
  auto arena = SimpleArenaAllocator()->MakeArena();
  MockTransport mock;
  mock.call_combiner = &call_combiner;
  g_mock = &mock;

  auto* call_stack =
      static_cast<grpc_call_stack*>(gpr_malloc(channel_stack->call_stack_size));
  const grpc_call_element_args call_args = {
      call_stack,
      nullptr,
      gpr_get_cycle_counter(),
      Timestamp::InfFuture(),
      arena.get(),
      &call_combiner,
  };
  ASSERT_TRUE(grpc_call_stack_init(
                  channel_stack, 1,
                  [](void* p, grpc_error_handle) {
                    grpc_call_stack_destroy(static_cast<grpc_call_stack*>(p),
                                            nullptr, nullptr);
                    gpr_free(p);
                  },
                  call_stack, &call_args)
                  .ok());

  grpc_call_element* top = grpc_call_stack_element(call_stack, 0);

  AppCtx app;
  app.combiner = &call_combiner;

  auto run_on_combiner = [&](grpc_closure* c) {
    GRPC_CALL_COMBINER_START(&call_combiner, c, absl::OkStatus(), "test");
    ExecCtx::Get()->Flush();
  };

  // ---- Application-side targets / closures ----
  grpc_metadata_batch client_md;
  grpc_metadata_batch recv_init_md;
  grpc_metadata_batch recv_trail_md;
  std::optional<SliceBuffer> recv_message;
  uint32_t recv_message_flags = 0;
  app.recv_message = &recv_message;

  grpc_closure init_md_ready;
  grpc_closure msg_ready;
  grpc_closure trail_ready;
  grpc_closure on_complete;
  GRPC_CLOSURE_INIT(&init_md_ready, OnRecvInitialMetadataReady, &app, nullptr);
  GRPC_CLOSURE_INIT(&msg_ready, OnRecvMessageReady, &app, nullptr);
  GRPC_CLOSURE_INIT(&trail_ready, OnRecvTrailingMetadataReady, &app, nullptr);
  GRPC_CLOSURE_INIT(&on_complete, OnComplete, &call_combiner, nullptr);

  // ---- Batch A: send_initial_metadata + recv_initial_metadata ----
  // (send_initial_metadata kick-starts the promise.)
  grpc_transport_stream_op_batch batch_a;
  grpc_transport_stream_op_batch_payload payload_a{};
  batch_a.payload = &payload_a;
  batch_a.send_initial_metadata = true;
  payload_a.send_initial_metadata.send_initial_metadata = &client_md;
  batch_a.recv_initial_metadata = true;
  payload_a.recv_initial_metadata.recv_initial_metadata = &recv_init_md;
  payload_a.recv_initial_metadata.recv_initial_metadata_ready = &init_md_ready;
  batch_a.on_complete = &on_complete;
  StartBatchCtx start_a{top, &batch_a};
  grpc_closure start_a_closure;
  GRPC_CLOSURE_INIT(&start_a_closure, DoStartBatch, &start_a, nullptr);
  run_on_combiner(&start_a_closure);

  // ---- Batch B: recv_message ----
  grpc_transport_stream_op_batch batch_b;
  grpc_transport_stream_op_batch_payload payload_b{};
  batch_b.payload = &payload_b;
  batch_b.recv_message = true;
  payload_b.recv_message.recv_message = &recv_message;
  payload_b.recv_message.flags = &recv_message_flags;
  payload_b.recv_message.recv_message_ready = &msg_ready;
  StartBatchCtx start_b{top, &batch_b};
  grpc_closure start_b_closure;
  GRPC_CLOSURE_INIT(&start_b_closure, DoStartBatch, &start_b, nullptr);
  run_on_combiner(&start_b_closure);

  // ---- Batch C: recv_trailing_metadata ----
  grpc_transport_stream_op_batch batch_c;
  grpc_transport_stream_op_batch_payload payload_c{};
  batch_c.payload = &payload_c;
  batch_c.recv_trailing_metadata = true;
  payload_c.recv_trailing_metadata.recv_trailing_metadata = &recv_trail_md;
  payload_c.recv_trailing_metadata.recv_trailing_metadata_ready = &trail_ready;
  StartBatchCtx start_c{top, &batch_c};
  grpc_closure start_c_closure;
  GRPC_CLOSURE_INIT(&start_c_closure, DoStartBatch, &start_c, nullptr);
  run_on_combiner(&start_c_closure);

  ASSERT_NE(mock.recv_initial_metadata_ready, nullptr);
  ASSERT_NE(mock.recv_message_ready, nullptr);
  ASSERT_NE(mock.recv_trailing_metadata_ready, nullptr);

  // ---- 1) Transport delivers server initial metadata; filter stalls it. ----
  mock.recv_initial_metadata->Set(HttpStatusMetadata(), 200);
  run_on_combiner(mock.recv_initial_metadata_ready);
  // The filter is stalling: initial metadata has NOT been delivered up.
  EXPECT_FALSE(app.init_md_ready_called);

  // ---- 2) Transport delivers a message; it must be parked behind the
  //         stalled initial metadata, not delivered. ----
  {
    SliceBuffer sb;
    sb.Append(Slice::FromCopiedString("hello"));
    *mock.recv_message = std::move(sb);
    *mock.recv_message_flags = 0;
  }
  run_on_combiner(mock.recv_message_ready);
  EXPECT_FALSE(app.msg_ready_called)
      << "message should be parked while initial metadata is stalled";

  // ---- 3) Transport delivers trailing metadata (OK or non-OK). ----
  recv_trail_md.Set(GrpcStatusMetadata(), trailing_status);
  run_on_combiner(mock.recv_trailing_metadata_ready);

  // With the fix, the buffered message stays parked: it has not yet gone through
  // the filter's interceptor, so it must NOT have been delivered. (With the bug,
  // it would already have been delivered raw here.)
  EXPECT_FALSE(app.msg_ready_called)
      << "message delivered before passing through the filter interceptor "
         "(bypass bug)";

  // ---- 4) Release server initial metadata. The filter lets it through; the
  //         parked message is now flushed through the interceptor (mutated) and
  //         delivered, after initial metadata. ----
  g_release_initial_md.store(true, std::memory_order_release);
  g_init_md_waker.Wakeup();
  ExecCtx::Get()->Flush();

  EXPECT_TRUE(app.init_md_ready_called);
  EXPECT_TRUE(app.msg_ready_called);
  // CORRECT behavior: the message must have gone through the filter's
  // interceptor and been mutated.
  EXPECT_EQ(app.captured_payload, std::string("hello") + kMutationSuffix)
      << "inbound message bypassed the filter's mutation interceptor";

  // ---- Teardown ----
  g_init_md_waker = Waker();
  GRPC_CALL_STACK_UNREF(call_stack, "done");
  ExecCtx::Get()->Flush();
  GRPC_CHANNEL_STACK_UNREF(channel_stack, "done");
  g_mock = nullptr;
}

// Clean completion: server sends a message then OK trailing metadata while the
// filter stalls server initial metadata. The message must be delivered through
// the filter (mutated), not bypassed.
TEST(RecvMessageFilterBypassTest,
     InboundMessageFilteredWhenInitialMetadataStalledOkTrailing) {
  RunStalledMessageScenario(GRPC_STATUS_OK);
}

// Same, but the server completes with a non-OK status. A non-OK trailing status
// is a normal RPC outcome (not an out-of-band cancellation), so the received
// message must still be delivered through the filter (mutated), not dropped or
// bypassed.
TEST(RecvMessageFilterBypassTest,
     InboundMessageFilteredWhenInitialMetadataStalledNonOkTrailing) {
  RunStalledMessageScenario(GRPC_STATUS_UNAVAILABLE);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
