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

#include "src/core/load_balancing/autosharding/autosharding_client.h"

#include <grpc/event_engine/event_engine.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "proto/autosharding/v1/autosharding.upb.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/wait_for_single_owner.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {
namespace {

using grpc_event_engine::experimental::FuzzingEventEngine;
using ::testing::HasSubstr;

//
// Fake transport
//

class FakeTransport;

class FakeStream final : public XdsTransport::StreamingCall {
 public:
  FakeStream(std::unique_ptr<EventHandler> handler, bool auto_complete_sends)
      : handler_(std::move(handler)),
        auto_complete_sends_(auto_complete_sends) {}

  ~FakeStream() override = default;

  // Make Ref() public so that the transport can keep a reference.
  using StreamingCall::Ref;

  void SendMessage(std::string payload, bool /*send_half_close*/) override {
    sent_messages_.push_back(std::move(payload));
    if (auto_complete_sends_) handler_->OnRequestSent(true);
  }

  void CompleteSend(bool ok = true) { handler_->OnRequestSent(ok); }

  void StartRecvMessage() override { ++reads_started_; }

  void SendHalfClose() override {}

  void Orphan() override { Unref(); }

  const std::vector<std::string>& sent_messages() const {
    return sent_messages_;
  }
  size_t reads_started() const { return reads_started_; }

  // Test helpers.
  void SendMessageToClient(absl::string_view payload) {
    handler_->OnRecvMessage(payload);
  }
  void SendStatus(absl::Status status) {
    handler_->OnStatusReceived(std::move(status));
  }

 private:
  std::unique_ptr<EventHandler> handler_;
  const bool auto_complete_sends_;
  std::vector<std::string> sent_messages_;
  size_t reads_started_ = 0;
};

class FakeTransport final : public XdsTransport {
 public:
  FakeTransport() : XdsTransport("FakeTransport") {}

  void StartConnectivityFailureWatch(
      RefCountedPtr<ConnectivityFailureWatcher> /*watcher*/) override {}
  void StopConnectivityFailureWatch(
      const RefCountedPtr<ConnectivityFailureWatcher>& /*watcher*/) override {}

  OrphanablePtr<StreamingCall> CreateStreamingCall(
      const char* /*method*/,
      std::unique_ptr<StreamingCall::EventHandler> event_handler,
      CallOptions /*options*/) override {
    ++num_streams_created_;
    OrphanablePtr<FakeStream> call = MakeOrphanable<FakeStream>(
        std::move(event_handler), auto_complete_sends_);
    stream_ = call->Ref().TakeAsSubclass<FakeStream>();
    return call;
  }

  void set_auto_complete_sends(bool auto_complete_sends) {
    auto_complete_sends_ = auto_complete_sends;
  }

  void Orphaned() override {}
  void ResetBackoff() override { ++reset_backoff_count_; }

  RefCountedPtr<FakeStream> stream() const { return stream_; }
  void ClearStream() { stream_.reset(); }
  int num_streams_created() const { return num_streams_created_; }
  int reset_backoff_count() const { return reset_backoff_count_; }

 private:
  RefCountedPtr<FakeStream> stream_;
  bool auto_complete_sends_ = true;
  int num_streams_created_ = 0;
  int reset_backoff_count_ = 0;
};

//
// Request parsing helpers
//

struct ParsedRequest {
  bool has_initial_config = false;
  std::string target;
  std::string client_uuid;
  int64_t latest_generation = 0;
  bool has_ack = false;
  int64_t ack_generation = 0;
  bool ack_accepted = false;
  std::string ack_error_message;
};

ParsedRequest ParseRequest(absl::string_view serialized) {
  upb::Arena arena;
  auto* request =
      google_cloud_autosharding_v1_WatchShardingAssignmentRequest_parse(
          serialized.data(), serialized.size(), arena.ptr());
  GRPC_CHECK(request != nullptr);
  ParsedRequest parsed;
  if (google_cloud_autosharding_v1_WatchShardingAssignmentRequest_has_initial_client_config(
          request)) {
    parsed.has_initial_config = true;
    const auto* config =
        google_cloud_autosharding_v1_WatchShardingAssignmentRequest_initial_client_config(
            request);
    parsed.target = UpbStringToStdString(
        google_cloud_autosharding_v1_InitialClientConfig_target(config));
    parsed.client_uuid = UpbStringToStdString(
        google_cloud_autosharding_v1_InitialClientConfig_client_uuid(config));
    parsed.latest_generation =
        google_cloud_autosharding_v1_InitialClientConfig_latest_generation(
            config);
  }
  if (google_cloud_autosharding_v1_WatchShardingAssignmentRequest_has_assignment_ack(
          request)) {
    parsed.has_ack = true;
    const auto* ack =
        google_cloud_autosharding_v1_WatchShardingAssignmentRequest_assignment_ack(
            request);
    parsed.ack_generation =
        google_cloud_autosharding_v1_AssignmentAck_generation(ack);
    parsed.ack_accepted =
        google_cloud_autosharding_v1_AssignmentAck_accepted(ack);
    parsed.ack_error_message = UpbStringToStdString(
        google_cloud_autosharding_v1_AssignmentAck_error_message(ack));
  }
  return parsed;
}

//
// Server message builders
//

// Builds an AssignmentChunk that assigns the whole keyspace to the given
// endpoint names.
std::string MakeChunkMessage(const std::vector<std::string>& endpoint_names) {
  upb::Arena arena;
  auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_new(
          arena.ptr());
  auto* chunk =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_mutable_chunk(
          response, arena.ptr());
  auto* slice_assignment =
      google_cloud_autosharding_v1_AssignmentChunk_add_slice_assignments(
          chunk, arena.ptr());
  // A Slice with no end key covers the whole keyspace.
  google_cloud_autosharding_v1_SliceAssignment_mutable_slice(slice_assignment,
                                                             arena.ptr());
  for (size_t i = 0; i < endpoint_names.size(); ++i) {
    auto* per_slice_endpoint =
        google_cloud_autosharding_v1_SliceAssignment_add_endpoints(
            slice_assignment, arena.ptr());
    google_cloud_autosharding_v1_PerSliceEndpointState_set_endpoint_index(
        per_slice_endpoint, static_cast<int32_t>(i));
    auto* endpoint = google_cloud_autosharding_v1_AssignmentChunk_add_endpoints(
        chunk, arena.ptr());
    google_cloud_autosharding_v1_EndpointState_set_endpoint(
        endpoint, StdStringToUpbString(endpoint_names[i]));
  }
  size_t length;
  char* serialized =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_serialize(
          response, arena.ptr(), &length);
  return std::string(serialized, length);
}

std::string MakeMetadataMessage(int64_t generation) {
  upb::Arena arena;
  auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_new(
          arena.ptr());
  auto* metadata =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_mutable_metadata(
          response, arena.ptr());
  google_cloud_autosharding_v1_AssignmentMetadata_set_generation(metadata,
                                                                 generation);
  size_t length;
  char* serialized =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_serialize(
          response, arena.ptr(), &length);
  return std::string(serialized, length);
}

std::string MakeConfigMessage() {
  upb::Arena arena;
  auto* response =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_new(
          arena.ptr());
  google_cloud_autosharding_v1_WatchShardingAssignmentResponse_mutable_config(
      response, arena.ptr());
  size_t length;
  char* serialized =
      google_cloud_autosharding_v1_WatchShardingAssignmentResponse_serialize(
          response, arena.ptr(), &length);
  return std::string(serialized, length);
}

//
// Test fixture
//

class AutoshardingClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions());
    work_serializer_ = std::make_shared<WorkSerializer>(event_engine_);
    transport_ = MakeRefCounted<FakeTransport>();
  }

  void TearDown() override {
    if (client_ != nullptr) {
      RunInWorkSerializer([this] { client_->Shutdown(); });
      client_.reset();
    }
    transport_->ClearStream();
    transport_.reset();
    work_serializer_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
  }

  // Runs fn on the WorkSerializer and flushes pending callbacks.
  void RunInWorkSerializer(absl::AnyInvocable<void()> fn) {
    work_serializer_->Run(std::move(fn));
    Flush();
  }

  // Advances the event engine by a small amount, which runs the callbacks
  // queued on the WorkSerializer without firing the client's timers.
  void Flush() {
    for (int i = 0; i < 50; ++i) {
      event_engine_->Tick(std::chrono::microseconds(1));
    }
  }

  void StartClient(Duration initial_assignment_timeout = Duration::Hours(1)) {
    AutoshardingClient::Options options;
    options.work_serializer = work_serializer_;
    options.event_engine = event_engine_.get();
    options.transport = transport_;
    options.target = "test_target";
    options.uuid = "test_uuid";
    options.initial_assignment_timeout = initial_assignment_timeout;
    options.on_assignment = [this](AutoshardingAssignment assignment) {
      assignments_.push_back(std::move(assignment));
    };
    options.on_error = [this](absl::Status status) {
      errors_.push_back(std::move(status));
    };
    client_ = MakeRefCounted<AutoshardingClient>(std::move(options));
    RunInWorkSerializer([this] { client_->Start(); });
  }

  RefCountedPtr<FakeStream> stream() const { return transport_->stream(); }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  std::shared_ptr<WorkSerializer> work_serializer_;
  RefCountedPtr<FakeTransport> transport_;
  RefCountedPtr<AutoshardingClient> client_;
  std::vector<AutoshardingAssignment> assignments_;
  std::vector<absl::Status> errors_;
};

TEST_F(AutoshardingClientTest, SendsInitialConfig) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  ASSERT_EQ(stream->sent_messages().size(), 1);
  ParsedRequest request = ParseRequest(stream->sent_messages()[0]);
  EXPECT_TRUE(request.has_initial_config);
  EXPECT_EQ(request.target, "test_target");
  EXPECT_EQ(request.client_uuid, "test_uuid");
  EXPECT_EQ(request.latest_generation, 0);
  EXPECT_GE(stream->reads_started(), 1);
}

TEST_F(AutoshardingClientTest, AcceptsAssignmentAndSendsAck) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  ASSERT_EQ(assignments_.size(), 1);
  EXPECT_TRUE(errors_.empty());
  EXPECT_EQ(assignments_[0].slices.size(), 1);
  EXPECT_EQ(assignments_[0].slices[0].endpoints.size(), 1);
  ASSERT_EQ(assignments_[0].endpoint_names.size(), 1);
  EXPECT_EQ(assignments_[0].endpoint_names[0], "host0");
  const auto& sent = stream->sent_messages();
  ASSERT_GE(sent.size(), 2);
  ParsedRequest ack = ParseRequest(sent.back());
  EXPECT_TRUE(ack.has_ack);
  EXPECT_TRUE(ack.ack_accepted);
  EXPECT_EQ(ack.ack_generation, 1);
  EXPECT_EQ(ack.ack_error_message, "");
}

TEST_F(AutoshardingClientTest, SendsAckForStaleGeneration) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  ASSERT_EQ(assignments_.size(), 1);
  stream->SendMessageToClient(MakeChunkMessage({"host1"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  EXPECT_EQ(assignments_.size(), 1);
  EXPECT_TRUE(errors_.empty());
  ParsedRequest ack = ParseRequest(stream->sent_messages().back());
  EXPECT_TRUE(ack.has_ack);
  EXPECT_FALSE(ack.ack_accepted);
  EXPECT_EQ(ack.ack_generation, 1);
  EXPECT_THAT(ack.ack_error_message,
              HasSubstr("not greater than the latest generation"));
}

TEST_F(AutoshardingClientTest, RejectsInvalidAssignmentAndReportsError) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  // Metadata without any chunks is an invalid assignment.
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  EXPECT_TRUE(assignments_.empty());
  ASSERT_EQ(errors_.size(), 1);
  EXPECT_THAT(errors_[0].message(), HasSubstr("no chunks were received"));
  ParsedRequest ack = ParseRequest(stream->sent_messages().back());
  EXPECT_TRUE(ack.has_ack);
  EXPECT_FALSE(ack.ack_accepted);
  EXPECT_EQ(ack.ack_generation, 1);
  EXPECT_THAT(ack.ack_error_message, HasSubstr("no chunks were received"));
}

TEST_F(AutoshardingClientTest, ReportsInitialAssignmentTimeout) {
  StartClient(/*initial_assignment_timeout=*/Duration::Seconds(1));
  EXPECT_TRUE(errors_.empty());
  event_engine_->TickForDuration(std::chrono::seconds(2));
  Flush();
  ASSERT_EQ(errors_.size(), 1);
  EXPECT_THAT(errors_[0].message(), HasSubstr("initial_assignment_timeout"));
}

TEST_F(AutoshardingClientTest, StreamFailureReportsErrorAndRetries) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  stream->SendStatus(absl::UnavailableError("stream failed"));
  Flush();
  ASSERT_EQ(errors_.size(), 1);
  EXPECT_EQ(transport_->num_streams_created(), 1);
  // The retry is scheduled with the initial backoff.
  event_engine_->TickForDuration(std::chrono::seconds(2));
  Flush();
  EXPECT_EQ(transport_->num_streams_created(), 2);
  EXPECT_EQ(errors_.size(), 1);
}

TEST_F(AutoshardingClientTest, StreamFailureAfterValidAssignmentIsSilent) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  ASSERT_EQ(assignments_.size(), 1);
  stream->SendStatus(absl::UnavailableError("stream failed"));
  Flush();
  EXPECT_TRUE(errors_.empty());
  // The backoff is reset, and the new stream reports the last accepted
  // generation.
  event_engine_->TickForDuration(std::chrono::seconds(2));
  Flush();
  ASSERT_EQ(transport_->num_streams_created(), 2);
  RefCountedPtr<FakeStream> new_stream = this->stream();
  ASSERT_NE(new_stream, nullptr);
  ASSERT_FALSE(new_stream->sent_messages().empty());
  ParsedRequest request = ParseRequest(new_stream->sent_messages()[0]);
  EXPECT_TRUE(request.has_initial_config);
  EXPECT_EQ(request.latest_generation, 1);
}

TEST_F(AutoshardingClientTest, IgnoresLoadReportingConfig) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  stream->SendMessageToClient(MakeConfigMessage());
  Flush();
  EXPECT_TRUE(assignments_.empty());
  EXPECT_TRUE(errors_.empty());
  EXPECT_GE(stream->reads_started(), 2);
}

TEST_F(AutoshardingClientTest, ResetBackoffResetsTransport) {
  StartClient();
  RunInWorkSerializer([this] { client_->ResetBackoff(); });
  EXPECT_EQ(transport_->reset_backoff_count(), 1);
}

TEST_F(AutoshardingClientTest, RejectedAssignmentThenValidAssignment) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  // No chunks: the assignment is invalid.
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  ASSERT_EQ(errors_.size(), 1);
  EXPECT_THAT(errors_[0].message(), HasSubstr("no chunks were received"));
  EXPECT_TRUE(assignments_.empty());
  // A later, valid assignment is accepted.
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  stream->SendMessageToClient(MakeMetadataMessage(2));
  Flush();
  ASSERT_EQ(assignments_.size(), 1);
  EXPECT_EQ(errors_.size(), 1);
  ParsedRequest ack = ParseRequest(stream->sent_messages().back());
  EXPECT_TRUE(ack.has_ack);
  EXPECT_TRUE(ack.ack_accepted);
  EXPECT_EQ(ack.ack_generation, 2);
}

TEST_F(AutoshardingClientTest, StaleThenNewerGeneration) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  ASSERT_EQ(assignments_.size(), 1);
  // A stale generation is NACKed and does not replace the assignment.
  stream->SendMessageToClient(MakeChunkMessage({"host1"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  EXPECT_EQ(assignments_.size(), 1);
  ParsedRequest stale_ack = ParseRequest(stream->sent_messages().back());
  EXPECT_FALSE(stale_ack.ack_accepted);
  // A newer generation is accepted.
  stream->SendMessageToClient(MakeChunkMessage({"host1"}));
  stream->SendMessageToClient(MakeMetadataMessage(2));
  Flush();
  EXPECT_EQ(assignments_.size(), 2);
  ParsedRequest ack = ParseRequest(stream->sent_messages().back());
  EXPECT_TRUE(ack.ack_accepted);
  EXPECT_EQ(ack.ack_generation, 2);
}

TEST_F(AutoshardingClientTest, ResetChunksOnStreamRestart) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  // Receive a partial assignment, then fail the stream.
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  Flush();
  stream->SendStatus(absl::UnavailableError("stream failed"));
  Flush();
  ASSERT_EQ(errors_.size(), 1);
  // The retry opens a new stream.  The chunk from the failed stream must not
  // be reused, so metadata without new chunks is rejected.
  event_engine_->TickForDuration(std::chrono::seconds(2));
  Flush();
  ASSERT_EQ(transport_->num_streams_created(), 2);
  RefCountedPtr<FakeStream> new_stream = this->stream();
  ASSERT_NE(new_stream, nullptr);
  new_stream->SendMessageToClient(MakeMetadataMessage(2));
  Flush();
  EXPECT_TRUE(assignments_.empty());
  ASSERT_EQ(errors_.size(), 2);
  EXPECT_THAT(errors_[1].message(), HasSubstr("no chunks were received"));
}

TEST_F(AutoshardingClientTest, NoTimeoutAfterValidAssignment) {
  StartClient(/*initial_assignment_timeout=*/Duration::Seconds(1));
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  ASSERT_EQ(assignments_.size(), 1);
  // The initial assignment timer must not fire after a valid assignment.
  event_engine_->TickForDuration(std::chrono::seconds(2));
  Flush();
  EXPECT_TRUE(errors_.empty());
}

TEST_F(AutoshardingClientTest, ShutdownWithPendingRetryAndInFlightStream) {
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  // Fail the stream, which schedules a retry.
  stream->SendStatus(absl::UnavailableError("stream failed"));
  Flush();
  ASSERT_EQ(errors_.size(), 1);
  RunInWorkSerializer([this] { client_->Shutdown(); });
  event_engine_->TickForDuration(std::chrono::seconds(2));
  Flush();
  // No retry is attempted after shutdown.
  EXPECT_EQ(transport_->num_streams_created(), 1);
  // Late events from the old stream are ignored.
  stream->SendMessageToClient(MakeChunkMessage({"host0"}));
  stream->SendMessageToClient(MakeMetadataMessage(1));
  stream->SendStatus(absl::InternalError("late"));
  Flush();
  EXPECT_TRUE(assignments_.empty());
  EXPECT_EQ(errors_.size(), 1);
}

TEST_F(AutoshardingClientTest, OneMessageInFlight) {
  transport_->set_auto_complete_sends(false);
  StartClient();
  RefCountedPtr<FakeStream> stream = this->stream();
  ASSERT_NE(stream, nullptr);
  // The initial config was sent but its completion is deferred.
  ASSERT_EQ(stream->sent_messages().size(), 1);
  // Process metadata, which queues the ACK behind the initial config.
  stream->SendMessageToClient(MakeMetadataMessage(1));
  Flush();
  EXPECT_EQ(stream->sent_messages().size(), 1);
  // Completing the initial config sends the ACK.
  stream->CompleteSend();
  Flush();
  ASSERT_EQ(stream->sent_messages().size(), 2);
  ParsedRequest ack = ParseRequest(stream->sent_messages()[1]);
  EXPECT_TRUE(ack.has_ack);
  EXPECT_FALSE(ack.ack_accepted);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
