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

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

#ifndef GPR_WINDOWS
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/extensions/supports_fd.h"
#include "src/core/lib/event_engine/memory_allocator_factory.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/uv_engine/uv_endpoint.h"
#include "src/core/lib/event_engine/uv_engine/uv_engine.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/env.h"
#include "src/core/util/fork.h"
#include "src/core/util/notification.h"
#include "src/core/util/sync.h"
#include "test/core/event_engine/test_init.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#if !defined(GPR_WINDOWS) && !defined(GRPC_CFSTREAM)
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#endif

namespace {

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EndpointSupportsFdExtension;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::EventEngineFactoryReset;
using ::grpc_event_engine::experimental::EventEngineSupportsFdExtension;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::InitializeTestingEventEngineFactory;
using ::grpc_event_engine::experimental::MemoryAllocator;
using ::grpc_event_engine::experimental::MemoryQuotaBasedMemoryAllocatorFactory;
using ::grpc_event_engine::experimental::QueryExtension;
using ::grpc_event_engine::experimental::ResolvedAddressSetPort;
using ::grpc_event_engine::experimental::ResolveUvWriteMode;
using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;
using ::grpc_event_engine::experimental::URIToResolvedAddress;
using ::grpc_event_engine::experimental::UvEventEngine;
using ::grpc_event_engine::experimental::UvWriteMode;

void RestoreOptionalEnv(const char* name,
                        const std::optional<std::string>& saved) {
  if (saved.has_value()) {
    grpc_core::SetEnv(name, *saved);
  } else {
    grpc_core::UnsetEnv(name);
  }
}

std::string DrainSliceBufferToString(SliceBuffer* buf) {
  std::string out(buf->Length(), '\0');
  if (!out.empty()) {
    buf->MoveFirstNBytesIntoBuffer(out.size(), out.data());
  }
  return out;
}

class UvEventEngineFactoryTest : public testing::Test {
 protected:
  void SetUp() override {
    saved_engine_env_ = grpc_core::GetEnv("GRPC_EVENT_ENGINE");
    saved_loops_env_ = grpc_core::GetEnv("GRPC_UV_LOOPS");
    saved_write_mode_env_ = grpc_core::GetEnv("GRPC_UV_WRITE_MODE");
    EventEngineFactoryReset();
  }

  void TearDown() override {
    EventEngineFactoryReset();
    RestoreOptionalEnv("GRPC_EVENT_ENGINE", saved_engine_env_);
    RestoreOptionalEnv("GRPC_UV_LOOPS", saved_loops_env_);
    RestoreOptionalEnv("GRPC_UV_WRITE_MODE", saved_write_mode_env_);
  }

 private:
  std::optional<std::string> saved_engine_env_;
  std::optional<std::string> saved_loops_env_;
  std::optional<std::string> saved_write_mode_env_;
};

bool IsUvEngine(const std::shared_ptr<EventEngine>& ee) {
  return dynamic_cast<UvEventEngine*>(ee.get()) != nullptr;
}

#if !defined(GPR_WINDOWS) && !defined(GRPC_CFSTREAM)
bool IsPosixEngine(const std::shared_ptr<EventEngine>& ee) {
  return dynamic_cast<grpc_event_engine::experimental::PosixEventEngine*>(
             ee.get()) != nullptr;
}

void ExpectPlatformDefaultEngine(const std::shared_ptr<EventEngine>& ee) {
  ASSERT_NE(ee, nullptr);
#if defined(GRPC_HAS_UV_EVENT_ENGINE) && defined(GRPC_DEFAULT_EVENT_ENGINE_UV)
  EXPECT_TRUE(IsUvEngine(ee));
  EXPECT_FALSE(IsPosixEngine(ee));
#else
  EXPECT_TRUE(IsPosixEngine(ee));
  EXPECT_FALSE(IsUvEngine(ee));
#endif
}

TEST_F(UvEventEngineFactoryTest, UnsetUsesPlatformDefault) {
  grpc_core::UnsetEnv("GRPC_EVENT_ENGINE");
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ExpectPlatformDefaultEngine(ee);
#if defined(GRPC_HAS_UV_EVENT_ENGINE) && defined(GRPC_DEFAULT_EVENT_ENGINE_UV)
  EventEngineFactoryReset();
  grpc_core::SetEnv("GRPC_UV_LOOPS", "3");
  std::shared_ptr<EventEngine> ee_loops = GetDefaultEventEngine();
  auto* uv_ee = dynamic_cast<UvEventEngine*>(ee_loops.get());
  ASSERT_NE(uv_ee, nullptr);
  EXPECT_EQ(uv_ee->num_loops(), 3u);
#endif
}

TEST_F(UvEventEngineFactoryTest, DefaultKeywordUsesPlatformDefault) {
  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "default");
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ExpectPlatformDefaultEngine(ee);
}

TEST_F(UvEventEngineFactoryTest, ExplicitPosixUsesPosixEventEngine) {
  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "posix");
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ASSERT_NE(ee, nullptr);
  EXPECT_TRUE(IsPosixEngine(ee));
  EXPECT_FALSE(IsUvEngine(ee));
}

TEST_F(UvEventEngineFactoryTest, EmptyAndWhitespaceTrimmedEnvSelection) {
  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "");
  {
    std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
    ExpectPlatformDefaultEngine(ee);
  }
  EventEngineFactoryReset();

  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "  DEFAULT \t");
  {
    std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
    ExpectPlatformDefaultEngine(ee);
  }
  EventEngineFactoryReset();

  grpc_core::SetEnv("GRPC_EVENT_ENGINE", " Posix ");
  {
    std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
    ASSERT_NE(ee, nullptr);
    EXPECT_TRUE(IsPosixEngine(ee));
    EXPECT_FALSE(IsUvEngine(ee));
  }
}
#elif defined(GRPC_HAS_UV_EVENT_ENGINE) && defined(GRPC_DEFAULT_EVENT_ENGINE_UV)
TEST_F(UvEventEngineFactoryTest, UnsetUsesPlatformDefault) {
  grpc_core::UnsetEnv("GRPC_EVENT_ENGINE");
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ASSERT_NE(ee, nullptr);
  EXPECT_TRUE(IsUvEngine(ee));
}

TEST_F(UvEventEngineFactoryTest, DefaultKeywordUsesPlatformDefault) {
  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "default");
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ASSERT_NE(ee, nullptr);
  EXPECT_TRUE(IsUvEngine(ee));
}
#endif

TEST_F(UvEventEngineFactoryTest, ExplicitUvCreatesUvEventEngine) {
  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "uv");
  grpc_core::SetEnv("GRPC_UV_LOOPS", "3");
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ASSERT_NE(ee, nullptr);
  auto* uv_ee = dynamic_cast<UvEventEngine*>(ee.get());
  ASSERT_NE(uv_ee, nullptr);
  EXPECT_EQ(uv_ee->num_loops(), 3u);
}

TEST_F(UvEventEngineFactoryTest, CaseInsensitiveAndTrimmedUvSelection) {
  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "  UV \t");
  grpc_core::SetEnv("GRPC_UV_LOOPS", "2");
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ASSERT_NE(ee, nullptr);
  auto* uv_ee = dynamic_cast<UvEventEngine*>(ee.get());
  ASSERT_NE(uv_ee, nullptr);
  EXPECT_EQ(uv_ee->num_loops(), 2u);
}

TEST_F(UvEventEngineFactoryTest, UvLoopsClampingAndInvalidValues) {
  grpc_core::SetEnv("GRPC_EVENT_ENGINE", "uv");

  grpc_core::SetEnv("GRPC_UV_LOOPS", "999");
  {
    std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
    auto* uv_ee = dynamic_cast<UvEventEngine*>(ee.get());
    ASSERT_NE(uv_ee, nullptr);
    EXPECT_EQ(uv_ee->num_loops(), 64u);
  }
  EventEngineFactoryReset();

  for (const char* invalid_val : {"0", "-2", "not_a_number"}) {
    grpc_core::SetEnv("GRPC_UV_LOOPS", invalid_val);
    std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
    auto* uv_ee = dynamic_cast<UvEventEngine*>(ee.get());
    ASSERT_NE(uv_ee, nullptr);
    EXPECT_GE(uv_ee->num_loops(), 1u);
    EXPECT_LE(uv_ee->num_loops(), 16u);
    EventEngineFactoryReset();
  }
}

TEST_F(UvEventEngineFactoryTest, TestInitSupportsUvAndRejectsUnknownEngine) {
  ASSERT_TRUE(InitializeTestingEventEngineFactory("uv").ok());
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  ASSERT_NE(ee, nullptr);
  EXPECT_TRUE(IsUvEngine(ee));

  EventEngineFactoryReset();
  absl::Status unknown_status =
      InitializeTestingEventEngineFactory("unknown_nonexistent_engine");
  EXPECT_EQ(unknown_status.code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(UvEventEngineFactoryTest, ResolveUvWriteModeRespectsEnvAndChannelArgs) {
  grpc_core::UnsetEnv("GRPC_UV_WRITE_MODE");
  ChannelArgsEndpointConfig default_cfg{grpc_core::ChannelArgs()};
  EXPECT_EQ(ResolveUvWriteMode(default_cfg), UvWriteMode::kInlineAuto);

  ChannelArgsEndpointConfig throughput_cfg{
      grpc_core::ChannelArgs().Set(GRPC_ARG_OPTIMIZATION_TARGET, "throughput")};
  EXPECT_EQ(ResolveUvWriteMode(throughput_cfg), UvWriteMode::kBatchOnLoop);

  ChannelArgsEndpointConfig latency_cfg{
      grpc_core::ChannelArgs().Set(GRPC_ARG_OPTIMIZATION_TARGET, "latency")};
  EXPECT_EQ(ResolveUvWriteMode(latency_cfg), UvWriteMode::kInlineAuto);

  grpc_core::SetEnv("GRPC_UV_WRITE_MODE", "batch");
  EXPECT_EQ(ResolveUvWriteMode(latency_cfg), UvWriteMode::kBatchOnLoop);

  grpc_core::SetEnv("GRPC_UV_WRITE_MODE", "inline");
  EXPECT_EQ(ResolveUvWriteMode(throughput_cfg), UvWriteMode::kInlineAuto);
}

TEST_F(UvEventEngineFactoryTest,
       IsWorkerThreadDistinguishesCallerAndCallbacks) {
  std::shared_ptr<UvEventEngine> ee = UvEventEngine::MakeUvEventEngine(2);
  ASSERT_NE(ee, nullptr);
  EXPECT_FALSE(ee->IsWorkerThread());

  grpc_core::Notification run_done;
  std::atomic<bool> in_run_worker{false};
  ee->Run([&]() {
    in_run_worker.store(ee->IsWorkerThread(), std::memory_order_relaxed);
    run_done.Notify();
  });
  run_done.WaitForNotification();
  EXPECT_TRUE(in_run_worker.load(std::memory_order_relaxed));

  grpc_core::Notification timer_done;
  std::atomic<bool> in_timer_worker{false};
  ee->RunAfter(std::chrono::milliseconds(1), [&]() {
    in_timer_worker.store(ee->IsWorkerThread(), std::memory_order_relaxed);
    timer_done.Notify();
  });
  timer_done.WaitForNotification();
  EXPECT_TRUE(in_timer_worker.load(std::memory_order_relaxed));
}

#ifndef GPR_WINDOWS
TEST_F(UvEventEngineFactoryTest, EndpointSupportsFdExtensionReleasesLiveFd) {
  std::shared_ptr<UvEventEngine> ee = UvEventEngine::MakeUvEventEngine(1);
  ASSERT_NE(ee, nullptr);
  auto* engine_fd_ext =
      QueryExtension<EventEngineSupportsFdExtension>(ee.get());
  ASSERT_NE(engine_fd_ext, nullptr);

  int sv[2] = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

  ChannelArgsEndpointConfig cfg{grpc_core::ChannelArgs()};
  absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep_or =
      engine_fd_ext->CreatePosixEndpointFromFd(sv[0], cfg, MemoryAllocator());
  ASSERT_TRUE(ep_or.ok()) << ep_or.status();
  std::unique_ptr<EventEngine::Endpoint> ep = std::move(*ep_or);

  auto* ep_fd_ext = QueryExtension<EndpointSupportsFdExtension>(ep.get());
  ASSERT_NE(ep_fd_ext, nullptr);
  EXPECT_EQ(ep_fd_ext->GetWrappedFd(), sv[0]);

  grpc_core::Notification released_note;
  absl::StatusOr<int> released_fd_or = absl::InternalError("uninitialized");
  ep_fd_ext->Shutdown([&](absl::StatusOr<int> fd_or) {
    released_fd_or = std::move(fd_or);
    released_note.Notify();
  });
  EXPECT_EQ(ep_fd_ext->GetWrappedFd(), -1);

  grpc_core::Notification second_note;
  absl::StatusOr<int> second_fd_or = -1;
  ep_fd_ext->Shutdown([&](absl::StatusOr<int> fd_or) {
    second_fd_or = std::move(fd_or);
    second_note.Notify();
  });
  second_note.WaitForNotification();
  EXPECT_EQ(second_fd_or.status().code(),
            absl::StatusCode::kFailedPrecondition);

  ep.reset();
  released_note.WaitForNotification();
  ASSERT_TRUE(released_fd_or.ok()) << released_fd_or.status();
  const int released_fd = *released_fd_or;
  EXPECT_GT(released_fd, STDERR_FILENO);

  const char kPing[] = "ping";
  ASSERT_EQ(::send(released_fd, kPing, sizeof(kPing), 0),
            static_cast<ssize_t>(sizeof(kPing)));
  char buf[sizeof(kPing)] = {};
  ASSERT_EQ(::recv(sv[1], buf, sizeof(buf), 0),
            static_cast<ssize_t>(sizeof(buf)));
  EXPECT_EQ(std::memcmp(buf, kPing, sizeof(kPing)), 0);

  ::close(released_fd);
  ::close(sv[1]);
}
#endif  // !GPR_WINDOWS

TEST_F(UvEventEngineFactoryTest,
       BatchModeWriteEventSinkDeliversOrderedAndClosedEventsSafely) {
  grpc_core::SetEnv("GRPC_UV_WRITE_MODE", "batch");
  std::shared_ptr<UvEventEngine> ee = UvEventEngine::MakeUvEventEngine(2);
  ASSERT_NE(ee, nullptr);

  grpc_core::Notification accepted_note;
  std::unique_ptr<EventEngine::Endpoint> server_ep;
  ChannelArgsEndpointConfig cfg{grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default())};

  auto listener_or = ee->CreateListener(
      [&](std::unique_ptr<EventEngine::Endpoint> ep, MemoryAllocator) {
        server_ep = std::move(ep);
        accepted_note.Notify();
      },
      [](absl::Status) {}, cfg,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          grpc_core::ResourceQuota::Default()->memory_quota()));
  ASSERT_TRUE(listener_or.ok()) << listener_or.status();
  std::unique_ptr<EventEngine::Listener> listener = std::move(*listener_or);

  auto bind_addr_or = URIToResolvedAddress("ipv4:127.0.0.1:0");
  ASSERT_TRUE(bind_addr_or.ok()) << bind_addr_or.status();
  absl::StatusOr<int> bound_port = listener->Bind(*bind_addr_or);
  ASSERT_TRUE(bound_port.ok()) << bound_port.status();
  ASSERT_TRUE(listener->Start().ok());

  EventEngine::ResolvedAddress connect_addr = *bind_addr_or;
  ResolvedAddressSetPort(connect_addr, *bound_port);

  grpc_core::Notification connected_note;
  std::unique_ptr<EventEngine::Endpoint> client_ep;
  ee->Connect(
      [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep_or) {
        ASSERT_TRUE(ep_or.ok()) << ep_or.status();
        client_ep = std::move(*ep_or);
        connected_note.Notify();
      },
      connect_addr, cfg,
      grpc_core::ResourceQuota::Default()
          ->memory_quota()
          ->CreateMemoryAllocator("client"),
      std::chrono::seconds(10));

  connected_note.WaitForNotification();
  accepted_note.WaitForNotification();
  ASSERT_NE(client_ep, nullptr);
  ASSERT_NE(server_ep, nullptr);

  using WriteEvent = EventEngine::Endpoint::WriteEvent;
  using WriteEventSink = EventEngine::Endpoint::WriteEventSink;
  using WriteMetric = EventEngine::Endpoint::WriteMetric;

  grpc_core::Mutex events_mu;
  std::vector<WriteEvent> recorded_events;
  grpc_core::Notification sent_event_note;

  EventEngine::Endpoint::WriteArgs write_args;
  write_args.set_metrics_sink(WriteEventSink(
      /*requested_metrics=*/nullptr,
      {WriteEvent::kSendMsg, WriteEvent::kScheduled, WriteEvent::kSent,
       WriteEvent::kClosed},
      [&](WriteEvent event, absl::Time, std::vector<WriteMetric>) {
        // Re-entering endpoint queries from inside `WriteEventCallback` must
        // never deadlock on `write_mu`.
        EXPECT_GT(client_ep->GetLocalAddress().size(), 0u);
        EXPECT_GT(client_ep->GetPeerAddress().size(), 0u);
        bool saw_sent = false;
        {
          grpc_core::MutexLock lock(events_mu);
          recorded_events.push_back(event);
          saw_sent = (event == WriteEvent::kSent);
        }
        if (saw_sent) {
          sent_event_note.Notify();
        }
      }));

  SliceBuffer write_buf;
  write_buf.Append(Slice::FromCopiedString("hello-uv-batch-metrics"));
  grpc_core::Notification write_done;
  const bool wrote_sync = client_ep->Write(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok()) << s;
        write_done.Notify();
      },
      &write_buf, std::move(write_args));
  if (wrote_sync) {
    write_done.Notify();
  }

  SliceBuffer read_buf;
  grpc_core::Notification read_done;
  const bool read_sync = server_ep->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok()) << s;
        read_done.Notify();
      },
      &read_buf, EventEngine::Endpoint::ReadArgs{});
  if (read_sync) {
    read_done.Notify();
  }

  write_done.WaitForNotification();
  read_done.WaitForNotification();
  sent_event_note.WaitForNotification();
  EXPECT_EQ(DrainSliceBufferToString(&read_buf), "hello-uv-batch-metrics");

  {
    grpc_core::MutexLock lock(events_mu);
    ASSERT_EQ(recorded_events.size(), 3u);
    EXPECT_EQ(recorded_events[0], WriteEvent::kSendMsg);
    EXPECT_EQ(recorded_events[1], WriteEvent::kScheduled);
    EXPECT_EQ(recorded_events[2], WriteEvent::kSent);
  }

#ifndef GPR_WINDOWS
  // Also verify that shutting down an endpoint from inside a `kSendMsg`
  // callback delivers `kClosed` without deadlock.
  grpc_core::Notification closed_event_note;
  std::vector<WriteEvent> shutdown_events;
  EventEngine::Endpoint::WriteArgs shutdown_write_args;
  shutdown_write_args.set_metrics_sink(WriteEventSink(
      /*requested_metrics=*/nullptr,
      {WriteEvent::kSendMsg, WriteEvent::kClosed},
      [&](WriteEvent event, absl::Time, std::vector<WriteMetric>) {
        bool is_closed = false;
        {
          grpc_core::MutexLock lock(events_mu);
          shutdown_events.push_back(event);
          is_closed = (event == WriteEvent::kClosed);
        }
        if (event == WriteEvent::kSendMsg) {
          if (auto* fd_ext =
                  QueryExtension<EndpointSupportsFdExtension>(client_ep.get());
              fd_ext != nullptr) {
            fd_ext->Shutdown(nullptr);
          }
        }
        if (is_closed) {
          closed_event_note.Notify();
        }
      }));

  SliceBuffer second_write_buf;
  second_write_buf.Append(Slice::FromCopiedString("reentrant-shutdown-write"));
  grpc_core::Notification second_write_done;
  if (client_ep->Write([&](absl::Status) { second_write_done.Notify(); },
                       &second_write_buf, std::move(shutdown_write_args))) {
    second_write_done.Notify();
  }
  second_write_done.WaitForNotification();
  closed_event_note.WaitForNotification();

  {
    grpc_core::MutexLock lock(events_mu);
    EXPECT_NE(std::find(shutdown_events.begin(), shutdown_events.end(),
                        WriteEvent::kSendMsg),
              shutdown_events.end());
    EXPECT_NE(std::find(shutdown_events.begin(), shutdown_events.end(),
                        WriteEvent::kClosed),
              shutdown_events.end());
  }
#endif  // !GPR_WINDOWS

  client_ep.reset();
  server_ep.reset();
  listener.reset();
}

#ifndef GPR_WINDOWS
TEST_F(UvEventEngineFactoryTest,
       PosixUnconnectedFdAndExternalListenerConnectionWorkEndToEnd) {
  std::shared_ptr<UvEventEngine> ee = UvEventEngine::MakeUvEventEngine(2);
  ASSERT_NE(ee, nullptr);
  auto* engine_fd_ext =
      QueryExtension<EventEngineSupportsFdExtension>(ee.get());
  ASSERT_NE(engine_fd_ext, nullptr);

  ChannelArgsEndpointConfig cfg{grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default())};

  grpc_core::Notification server_accepted;
  std::unique_ptr<EventEngine::Endpoint> server_ep;
  bool was_external = false;
  std::string pre_read_payload;
  auto listener_or = engine_fd_ext->CreatePosixListener(
      [&](int /*listener_fd*/, std::unique_ptr<EventEngine::Endpoint> ep,
          bool is_external, MemoryAllocator, SliceBuffer* pending_data) {
        server_ep = std::move(ep);
        was_external = is_external;
        if (pending_data != nullptr) {
          pre_read_payload = DrainSliceBufferToString(pending_data);
        }
        server_accepted.Notify();
      },
      [](absl::Status) {}, cfg,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          grpc_core::ResourceQuota::Default()->memory_quota()));
  ASSERT_TRUE(listener_or.ok()) << listener_or.status();
  std::unique_ptr<EventEngine::Listener> listener = std::move(*listener_or);

  auto* listener_fd_ext = QueryExtension<
      grpc_event_engine::experimental::ListenerSupportsFdExtension>(
      listener.get());
  ASSERT_NE(listener_fd_ext, nullptr);

  int bound_listener_fd = -1;
  auto bind_addr_or = URIToResolvedAddress("ipv4:127.0.0.1:0");
  ASSERT_TRUE(bind_addr_or.ok());
  absl::StatusOr<int> bound_port = listener_fd_ext->BindWithFd(
      *bind_addr_or, [&](absl::StatusOr<int> lfd_or) {
        if (lfd_or.ok()) {
          bound_listener_fd = *lfd_or;
        }
      });
  ASSERT_TRUE(bound_port.ok()) << bound_port.status();
  EXPECT_GE(bound_listener_fd, 0);
  ASSERT_TRUE(listener->Start().ok());

  // 1. Test `CreateEndpointFromUnconnectedFd` connecting to the listener.
  int raw_client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(raw_client_fd, 0);
  EventEngine::ResolvedAddress connect_addr = *bind_addr_or;
  ResolvedAddressSetPort(connect_addr, *bound_port);

  grpc_core::Notification client_connected;
  std::unique_ptr<EventEngine::Endpoint> client_ep;
  engine_fd_ext->CreateEndpointFromUnconnectedFd(
      raw_client_fd,
      [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep_or) {
        ASSERT_TRUE(ep_or.ok()) << ep_or.status();
        client_ep = std::move(*ep_or);
        client_connected.Notify();
      },
      connect_addr, cfg,
      grpc_core::ResourceQuota::Default()
          ->memory_quota()
          ->CreateMemoryAllocator("unconnected-fd-client"),
      std::chrono::seconds(10));

  client_connected.WaitForNotification();
  server_accepted.WaitForNotification();
  ASSERT_NE(client_ep, nullptr);
  ASSERT_NE(server_ep, nullptr);
  EXPECT_FALSE(was_external);
  client_ep.reset();
  server_ep.reset();

  // 2. Test `HandleExternalConnection` with pending pre-read data.
  int sv[2] = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  grpc_core::Notification external_accepted;
  std::unique_ptr<EventEngine::Endpoint> external_ep;
  auto ext_listener_or = engine_fd_ext->CreatePosixListener(
      [&](int /*listener_fd*/, std::unique_ptr<EventEngine::Endpoint> ep,
          bool is_external, MemoryAllocator, SliceBuffer* pending_data) {
        external_ep = std::move(ep);
        was_external = is_external;
        if (pending_data != nullptr) {
          pre_read_payload = DrainSliceBufferToString(pending_data);
        }
        external_accepted.Notify();
      },
      [](absl::Status) {}, cfg,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          grpc_core::ResourceQuota::Default()->memory_quota()));
  ASSERT_TRUE(ext_listener_or.ok()) << ext_listener_or.status();
  std::unique_ptr<EventEngine::Listener> ext_listener =
      std::move(*ext_listener_or);
  auto* ext_listener_fd = QueryExtension<
      grpc_event_engine::experimental::ListenerSupportsFdExtension>(
      ext_listener.get());
  ASSERT_NE(ext_listener_fd, nullptr);

  SliceBuffer initial_data;
  initial_data.Append(Slice::FromCopiedString("handshake-prefix"));
  ASSERT_TRUE(
      ext_listener_fd
          ->HandleExternalConnection(bound_listener_fd, sv[0], &initial_data)
          .ok());
  external_accepted.WaitForNotification();
  ASSERT_NE(external_ep, nullptr);
  EXPECT_TRUE(was_external);
  EXPECT_EQ(pre_read_payload, "handshake-prefix");

  external_ep.reset();
  ::close(sv[1]);
  ext_listener.reset();
  listener.reset();
}

// Regression test for D1: when a `WriteEventSink` mask filters out early events
// (`kSendMsg`/`kScheduled` excluded so `pending_events` stays empty on the loop
// thread) while another thread concurrently calls `Shutdown()` (emitting
// `kClosed` and claiming delivery), `EmitWriteEvents` must never double-enter
// the delivery loop or race on `sink_state->cb` / `in_flight_event_cbs`.
TEST_F(UvEventEngineFactoryTest,
       FilteredWriteEventMaskConcurrentWithShutdownIsRaceFree) {
  grpc_core::SetEnv("GRPC_UV_WRITE_MODE", "batch");
  std::shared_ptr<UvEventEngine> ee = UvEventEngine::MakeUvEventEngine(2);
  ASSERT_NE(ee, nullptr);
  auto* engine_fd_ext =
      QueryExtension<EventEngineSupportsFdExtension>(ee.get());
  ASSERT_NE(engine_fd_ext, nullptr);
  ChannelArgsEndpointConfig cfg{grpc_core::ChannelArgs()};

  using WriteEvent = EventEngine::Endpoint::WriteEvent;
  using WriteEventSink = EventEngine::Endpoint::WriteEventSink;
  using WriteMetric = EventEngine::Endpoint::WriteMetric;

  for (int iter = 0; iter < 32; ++iter) {
    int sv[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    auto ep_or =
        engine_fd_ext->CreatePosixEndpointFromFd(sv[0], cfg, MemoryAllocator());
    ASSERT_TRUE(ep_or.ok()) << ep_or.status();
    std::unique_ptr<EventEngine::Endpoint> ep = std::move(*ep_or);

    std::atomic<int> closed_count{0};
    EventEngine::Endpoint::WriteArgs write_args;
    // Request ONLY `kClosed` so `kSendMsg` and `kScheduled` are masked out!
    write_args.set_metrics_sink(WriteEventSink(
        /*requested_metrics=*/nullptr, {WriteEvent::kClosed},
        [&](WriteEvent event, absl::Time, std::vector<WriteMetric>) {
          if (event == WriteEvent::kClosed) {
            closed_count.fetch_add(1, std::memory_order_relaxed);
          }
        }));

    SliceBuffer payload;
    payload.Append(Slice::FromCopiedString(std::string(64 * 1024, 'x')));
    grpc_core::Notification write_done;
    if (ep->Write([&](absl::Status) { write_done.Notify(); }, &payload,
                  std::move(write_args))) {
      write_done.Notify();
    }

    if (auto* ep_fd = QueryExtension<EndpointSupportsFdExtension>(ep.get());
        ep_fd != nullptr) {
      ep_fd->Shutdown(nullptr);
    }
    ep.reset();
    write_done.WaitForNotification();
    EXPECT_LE(closed_count.load(std::memory_order_relaxed), 1);
    ::close(sv[1]);
  }
}

#if GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
TEST_F(UvEventEngineFactoryTest,
       SimulatedChildForkCancelsPendingIoAndPreservesParentUnixSocket) {
  std::shared_ptr<UvEventEngine> ee = UvEventEngine::MakeUvEventEngine(2);
  ASSERT_NE(ee, nullptr);
  ChannelArgsEndpointConfig cfg{grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default())};

  // 1. Bind a Unix domain socket listener so we verify `AfterFork(kChild)`
  //    closes the child's handle WITHOUT unlinking the parent's UDS path!
  const std::string uds_path =
      absl::StrCat("/tmp/grpc_uv_fork_uds_", ::getpid(), ".sock");
  ::unlink(uds_path.c_str());
  auto uds_addr_or = URIToResolvedAddress(absl::StrCat("unix:", uds_path));
  ASSERT_TRUE(uds_addr_or.ok()) << uds_addr_or.status();

  auto uds_listener_or = ee->CreateListener(
      [](std::unique_ptr<EventEngine::Endpoint>, MemoryAllocator) {},
      [](absl::Status) {}, cfg,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          grpc_core::ResourceQuota::Default()->memory_quota()));
  ASSERT_TRUE(uds_listener_or.ok()) << uds_listener_or.status();
  std::unique_ptr<EventEngine::Listener> uds_listener =
      std::move(*uds_listener_or);
  ASSERT_TRUE(uds_listener->Bind(*uds_addr_or).ok());
  ASSERT_TRUE(uds_listener->Start().ok());
  struct stat st{};
  ASSERT_EQ(::stat(uds_path.c_str(), &st), 0);

  // 2. Establish a TCP connection with an active Read and a large backpressured
  //    Write in flight on the loop.
  grpc_core::Notification accepted_note;
  std::unique_ptr<EventEngine::Endpoint> server_ep;
  auto tcp_listener_or = ee->CreateListener(
      [&](std::unique_ptr<EventEngine::Endpoint> ep, MemoryAllocator) {
        server_ep = std::move(ep);
        accepted_note.Notify();
      },
      [](absl::Status) {}, cfg,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          grpc_core::ResourceQuota::Default()->memory_quota()));
  ASSERT_TRUE(tcp_listener_or.ok()) << tcp_listener_or.status();
  std::unique_ptr<EventEngine::Listener> tcp_listener =
      std::move(*tcp_listener_or);

  auto bind_addr_or = URIToResolvedAddress("ipv4:127.0.0.1:0");
  ASSERT_TRUE(bind_addr_or.ok());
  auto port_or = tcp_listener->Bind(*bind_addr_or);
  ASSERT_TRUE(port_or.ok());
  ASSERT_TRUE(tcp_listener->Start().ok());

  EventEngine::ResolvedAddress connect_addr = *bind_addr_or;
  ResolvedAddressSetPort(connect_addr, *port_or);
  grpc_core::Notification connected_note;
  std::unique_ptr<EventEngine::Endpoint> client_ep;
  ee->Connect(
      [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep_or) {
        ASSERT_TRUE(ep_or.ok()) << ep_or.status();
        client_ep = std::move(*ep_or);
        connected_note.Notify();
      },
      connect_addr, cfg,
      grpc_core::ResourceQuota::Default()
          ->memory_quota()
          ->CreateMemoryAllocator("fork-sim-client"),
      std::chrono::seconds(10));
  connected_note.WaitForNotification();
  accepted_note.WaitForNotification();

  // Also start a pending Read on `client_ep` whose cancellation callback
  // immediately destroys `client_ep` on a `ThreadPool` worker while
  // `OnForkInChild` is executing (regression test for D3).
  SliceBuffer client_read_buf;
  grpc_core::Notification client_read_done;
  absl::Status client_read_status;
  ASSERT_FALSE(client_ep->Read(
      [&](absl::Status s) {
        client_read_status = std::move(s);
        client_ep.reset();
        client_read_done.Notify();
      },
      &client_read_buf, EventEngine::Endpoint::ReadArgs{}));

  SliceBuffer read_buf;
  grpc_core::Notification read_done;
  absl::Status read_status;
  ASSERT_FALSE(server_ep->Read(
      [&](absl::Status s) {
        read_status = std::move(s);
        read_done.Notify();
      },
      &read_buf, EventEngine::Endpoint::ReadArgs{}));

  SliceBuffer write_buf;
  write_buf.Append(Slice::FromCopiedString(std::string(8 * 1024 * 1024, 'z')));
  grpc_core::Notification write_done;
  absl::Status write_status;
  const bool wrote_sync = server_ep->Write(
      [&](absl::Status s) {
        write_status = std::move(s);
        write_done.Notify();
      },
      &write_buf, EventEngine::Endpoint::WriteArgs{});

  ee->BeforeFork();
  ee->AfterFork(UvEventEngine::OnForkRole::kChild);

  client_read_done.WaitForNotification();
  EXPECT_EQ(client_read_status.code(), absl::StatusCode::kCancelled);
  read_done.WaitForNotification();
  EXPECT_EQ(read_status.code(), absl::StatusCode::kCancelled);
  if (!wrote_sync) {
    write_done.WaitForNotification();
    EXPECT_EQ(write_status.code(), absl::StatusCode::kCancelled);
  }

  // Post-fork Read and Write on inherited endpoints fail with CancelledError.
  grpc_core::Notification post_read_done;
  absl::Status post_read_status;
  ASSERT_FALSE(server_ep->Read(
      [&](absl::Status s) {
        post_read_status = std::move(s);
        post_read_done.Notify();
      },
      &read_buf, EventEngine::Endpoint::ReadArgs{}));
  post_read_done.WaitForNotification();
  EXPECT_EQ(post_read_status.code(), absl::StatusCode::kCancelled);

  // The parent's Unix domain socket file MUST still exist on disk!
  EXPECT_EQ(::stat(uds_path.c_str(), &st), 0);
  ::unlink(uds_path.c_str());

  client_ep.reset();
  server_ep.reset();
  tcp_listener.reset();
  uds_listener.reset();
}

TEST_F(UvEventEngineFactoryTest,
       RealForkIsolatesInheritedParentSocketsAndWorksInParentAndChild) {
  const bool prev_fork_enabled = grpc_core::Fork::Enabled();
  grpc_core::Fork::Enable(true);

  std::shared_ptr<UvEventEngine> ee = UvEventEngine::MakeUvEventEngine(2);
  ASSERT_NE(ee, nullptr);
  auto* engine_fd_ext =
      QueryExtension<EventEngineSupportsFdExtension>(ee.get());
  ASSERT_NE(engine_fd_ext, nullptr);
  ChannelArgsEndpointConfig cfg{grpc_core::ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA, grpc_core::ResourceQuota::Default())};

  grpc_core::Notification accepted_note;
  std::unique_ptr<EventEngine::Endpoint> server_ep;
  auto listener_or = engine_fd_ext->CreatePosixListener(
      [&](int /*listener_fd*/, std::unique_ptr<EventEngine::Endpoint> ep,
          bool /*is_external*/, MemoryAllocator, SliceBuffer*) {
        server_ep = std::move(ep);
        accepted_note.Notify();
      },
      [](absl::Status) {}, cfg,
      std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
          grpc_core::ResourceQuota::Default()->memory_quota()));
  ASSERT_TRUE(listener_or.ok()) << listener_or.status();
  std::unique_ptr<EventEngine::Listener> listener = std::move(*listener_or);

  auto* listener_fd_ext = QueryExtension<
      grpc_event_engine::experimental::ListenerSupportsFdExtension>(
      listener.get());
  ASSERT_NE(listener_fd_ext, nullptr);

  std::vector<int> bound_listen_fds;
  auto bind_addr_or = URIToResolvedAddress("ipv4:127.0.0.1:0");
  ASSERT_TRUE(bind_addr_or.ok());
  auto port_or =
      listener_fd_ext->BindWithFd(*bind_addr_or, [&](absl::StatusOr<int> lfd) {
        if (lfd.ok()) {
          bound_listen_fds.push_back(*lfd);
        }
      });
  ASSERT_TRUE(port_or.ok()) << port_or.status();
  ASSERT_FALSE(bound_listen_fds.empty());
  ASSERT_TRUE(listener->Start().ok());

  EventEngine::ResolvedAddress connect_addr = *bind_addr_or;
  ResolvedAddressSetPort(connect_addr, *port_or);
  grpc_core::Notification connected_note;
  std::unique_ptr<EventEngine::Endpoint> client_ep;
  ee->Connect(
      [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep_or) {
        ASSERT_TRUE(ep_or.ok()) << ep_or.status();
        client_ep = std::move(*ep_or);
        connected_note.Notify();
      },
      connect_addr, cfg,
      grpc_core::ResourceQuota::Default()
          ->memory_quota()
          ->CreateMemoryAllocator("fork-parent-client"),
      std::chrono::seconds(10));
  connected_note.WaitForNotification();
  accepted_note.WaitForNotification();

  const int parent_client_fd =
      QueryExtension<EndpointSupportsFdExtension>(client_ep.get())
          ->GetWrappedFd();
  const int parent_server_fd =
      QueryExtension<EndpointSupportsFdExtension>(server_ep.get())
          ->GetWrappedFd();
  ASSERT_GE(parent_client_fd, 0);
  ASSERT_GE(parent_server_fd, 0);

  // Start an active Read on `server_ep` BEFORE `fork()` — before our D2 fix,
  // the child loop stayed subscribed to `parent_server_fd` and stole inbound
  // bytes written by `client_ep` in the parent!
  SliceBuffer parent_read_buf;
  grpc_core::Notification parent_read_done;
  absl::Status parent_read_status;
  ASSERT_FALSE(server_ep->Read(
      [&](absl::Status s) {
        parent_read_status = std::move(s);
        parent_read_done.Notify();
      },
      &parent_read_buf, EventEngine::Endpoint::ReadArgs{}));

  const pid_t pid = ::fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    // Child process: verify EVERY inherited parent socket FD was closed in the
    // child's kernel FD table during `pthread_atfork` `PostForkInChild`!
    if (::fcntl(parent_client_fd, F_GETFD) != -1 || errno != EBADF) {
      _exit(11);
    }
    if (::fcntl(parent_server_fd, F_GETFD) != -1 || errno != EBADF) {
      _exit(12);
    }
    for (int lfd : bound_listen_fds) {
      if (::fcntl(lfd, F_GETFD) != -1 || errno != EBADF) {
        _exit(13);
      }
    }

    // Verify the child's `UvEventEngine` works cleanly for brand-new sockets!
    grpc_core::Notification child_accepted;
    std::unique_ptr<EventEngine::Endpoint> child_server_ep;
    auto child_listener_or = ee->CreateListener(
        [&](std::unique_ptr<EventEngine::Endpoint> ep, MemoryAllocator) {
          child_server_ep = std::move(ep);
          child_accepted.Notify();
        },
        [](absl::Status) {}, cfg,
        std::make_unique<MemoryQuotaBasedMemoryAllocatorFactory>(
            grpc_core::ResourceQuota::Default()->memory_quota()));
    if (!child_listener_or.ok()) _exit(14);
    std::unique_ptr<EventEngine::Listener> child_listener =
        std::move(*child_listener_or);
    auto child_port_or = child_listener->Bind(*bind_addr_or);
    if (!child_port_or.ok() || !child_listener->Start().ok()) _exit(15);

    EventEngine::ResolvedAddress child_addr = *bind_addr_or;
    ResolvedAddressSetPort(child_addr, *child_port_or);
    grpc_core::Notification child_connected;
    std::unique_ptr<EventEngine::Endpoint> child_client_ep;
    ee->Connect(
        [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep_or) {
          if (ep_or.ok()) {
            child_client_ep = std::move(*ep_or);
          }
          child_connected.Notify();
        },
        child_addr, cfg,
        grpc_core::ResourceQuota::Default()
            ->memory_quota()
            ->CreateMemoryAllocator("fork-child-client"),
        std::chrono::seconds(10));
    child_connected.WaitForNotification();
    child_accepted.WaitForNotification();
    if (child_client_ep == nullptr || child_server_ep == nullptr) _exit(16);

    SliceBuffer cw;
    cw.Append(Slice::FromCopiedString("child-postfork-msg"));
    grpc_core::Notification cw_done;
    if (child_client_ep->Write([&](absl::Status) { cw_done.Notify(); }, &cw,
                               EventEngine::Endpoint::WriteArgs{})) {
      cw_done.Notify();
    }
    SliceBuffer cr;
    grpc_core::Notification cr_done;
    if (child_server_ep->Read([&](absl::Status) { cr_done.Notify(); }, &cr,
                              EventEngine::Endpoint::ReadArgs{})) {
      cr_done.Notify();
    }
    cw_done.WaitForNotification();
    cr_done.WaitForNotification();
    if (DrainSliceBufferToString(&cr) != "child-postfork-msg") _exit(17);

    child_client_ep.reset();
    child_server_ep.reset();
    child_listener.reset();
    client_ep.reset();
    server_ep.reset();
    listener.reset();
    ee.reset();
    _exit(0);
  }

  // Parent process: send multiple messages across the PRE-FORK connection while
  // the child is running; none may be stolen by the child!
  for (int i = 0; i < 16; ++i) {
    const std::string expected = absl::StrCat("parent-round-trip-", i);
    grpc_core::Notification iter_read_done;
    absl::Status iter_read_status = absl::OkStatus();
    if (i > 0) {
      parent_read_buf.Clear();
      if (server_ep->Read(
              [&](absl::Status s) {
                iter_read_status = std::move(s);
                iter_read_done.Notify();
              },
              &parent_read_buf, EventEngine::Endpoint::ReadArgs{})) {
        iter_read_done.Notify();
      }
    }
    SliceBuffer pw;
    pw.Append(Slice::FromCopiedString(expected));
    grpc_core::Notification pw_done;
    if (client_ep->Write(
            [&](absl::Status s) {
              EXPECT_TRUE(s.ok()) << s;
              pw_done.Notify();
            },
            &pw, EventEngine::Endpoint::WriteArgs{})) {
      pw_done.Notify();
    }
    pw_done.WaitForNotification();
    if (i == 0) {
      parent_read_done.WaitForNotification();
      ASSERT_TRUE(parent_read_status.ok()) << parent_read_status;
      EXPECT_EQ(DrainSliceBufferToString(&parent_read_buf), expected);
    } else {
      iter_read_done.WaitForNotification();
      ASSERT_TRUE(iter_read_status.ok()) << iter_read_status;
      EXPECT_EQ(DrainSliceBufferToString(&parent_read_buf), expected);
    }
  }

  int child_status = 0;
  ASSERT_EQ(::waitpid(pid, &child_status, 0), pid);
  ASSERT_TRUE(WIFEXITED(child_status)) << "status=" << child_status;
  EXPECT_EQ(WEXITSTATUS(child_status), 0);

  client_ep.reset();
  server_ep.reset();
  listener.reset();
  grpc_core::Fork::Enable(prev_fork_enabled);
}
#endif  // GRPC_ENABLE_FORK_SUPPORT && GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK
#endif  // !GPR_WINDOWS

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
