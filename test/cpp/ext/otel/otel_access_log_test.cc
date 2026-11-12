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

#include "src/cpp/ext/otel/otel_access_log.h"

#include <grpc/grpc.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "src/core/filter/access_log/access_log_sink.h"
#include "src/core/filter/access_log/otel_access_log_sink.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest;
using ::opentelemetry::proto::common::v1::KeyValue;

// Records each export request and completes it with a fixed status.
class FakeExportTransport final : public grpc_core::OtelLogsExportTransport {
 public:
  explicit FakeExportTransport(absl::Status status = absl::OkStatus())
      : status_(std::move(status)) {}

  void Export(std::string serialized_request,
              absl::AnyInvocable<void(absl::Status)> on_done) override {
    {
      grpc_core::MutexLock lock(&mu_);
      requests_.push_back(std::move(serialized_request));
    }
    on_done(status_);
  }

  std::vector<ExportLogsServiceRequest> requests() {
    grpc_core::MutexLock lock(&mu_);
    std::vector<ExportLogsServiceRequest> result;
    for (const std::string& serialized : requests_) {
      ExportLogsServiceRequest request;
      EXPECT_TRUE(request.ParseFromString(serialized));
      result.push_back(std::move(request));
    }
    return result;
  }

 private:
  const absl::Status status_;
  grpc_core::Mutex mu_;
  std::vector<std::string> requests_ ABSL_GUARDED_BY(mu_);
};

// Never completes the export.
class HangingExportTransport final : public grpc_core::OtelLogsExportTransport {
 public:
  void Export(std::string /*serialized_request*/,
              absl::AnyInvocable<void(absl::Status)> on_done) override {
    pending_.push_back(std::move(on_done));
  }

 private:
  std::vector<absl::AnyInvocable<void(absl::Status)>> pending_;
};

std::string FindStringAttribute(
    const google::protobuf::RepeatedPtrField<KeyValue>& attributes,
    absl::string_view key) {
  for (const KeyValue& kv : attributes) {
    if (kv.key() == key) return kv.value().string_value();
  }
  return "<missing>";
}

const KeyValue* FindAttribute(
    const google::protobuf::RepeatedPtrField<KeyValue>& attributes,
    absl::string_view key) {
  for (const KeyValue& kv : attributes) {
    if (kv.key() == key) return &kv;
  }
  return nullptr;
}

grpc_core::OtelAccessLogSinkArgs MakeArgs(
    grpc_core::RefCountedPtr<grpc_core::OtelLogsExportTransport> transport) {
  grpc_core::OtelAccessLogSinkArgs args;
  args.transport = std::move(transport);
  args.log_name = "test-log";
  args.resource_attributes.emplace_back("node_name", "node-1");
  args.resource_attributes.emplace_back("replicas", int64_t{3});
  args.buffer_flush_interval = grpc_core::Duration::Milliseconds(10);
  return args;
}

internal::OtelLogRecordExporterFactory MakeExporterFactory() {
  return [](grpc_core::RefCountedPtr<grpc_core::OtelLogsExportTransport>
                transport) {
    return std::make_unique<internal::OtlpGrpcLogRecordExporter>(
        std::move(transport), absl::Seconds(5));
  };
}

TEST(OtelAccessLogTest, SinkExportsOneLogRecordPerLogCall) {
  auto transport = grpc_core::MakeRefCounted<FakeExportTransport>();
  internal::OtelAccessLogSinkFactoryImpl factory(MakeExporterFactory());
  auto sink = factory.CreateSink(MakeArgs(transport));
  ASSERT_NE(sink, nullptr);
  const absl::Time start = absl::FromCivil(
      absl::CivilSecond(2026, 1, 2, 3, 4, 5), absl::UTCTimeZone());
  for (int i = 0; i < 2; ++i) {
    grpc_core::AccessLogRecord record;
    record.timestamp = start;
    record.body = std::string("/pkg.Svc/Method OK");
    record.attributes.emplace_back("ua", std::string("agent"));
    record.attributes.emplace_back("count", int64_t{i});
    record.attributes.emplace_back("ok", true);
    sink->Log(std::move(record));
  }
  // Destroying the sink shuts down the provider, which flushes the batch.
  sink.reset();
  std::vector<ExportLogsServiceRequest> requests = transport->requests();
  int num_records = 0;
  for (const auto& request : requests) {
    ASSERT_EQ(request.resource_logs_size(), 1);
    const auto& resource_logs = request.resource_logs(0);
    EXPECT_EQ(
        FindStringAttribute(resource_logs.resource().attributes(), "node_name"),
        "node-1");
    const KeyValue* replicas =
        FindAttribute(resource_logs.resource().attributes(), "replicas");
    ASSERT_NE(replicas, nullptr);
    EXPECT_EQ(replicas->value().int_value(), 3);
    for (const auto& scope_logs : resource_logs.scope_logs()) {
      EXPECT_EQ(scope_logs.scope().name(), "test-log");
      for (const auto& log_record : scope_logs.log_records()) {
        ++num_records;
        EXPECT_EQ(log_record.body().string_value(), "/pkg.Svc/Method OK");
        EXPECT_EQ(log_record.time_unix_nano(),
                  static_cast<uint64_t>(absl::ToUnixNanos(start)));
        EXPECT_EQ(FindStringAttribute(log_record.attributes(), "ua"), "agent");
        const KeyValue* ok = FindAttribute(log_record.attributes(), "ok");
        ASSERT_NE(ok, nullptr);
        EXPECT_TRUE(ok->value().bool_value());
        EXPECT_NE(FindAttribute(log_record.attributes(), "count"), nullptr);
      }
    }
  }
  EXPECT_EQ(num_records, 2);
}

TEST(OtelAccessLogTest, ExporterReportsFailure) {
  auto transport = grpc_core::MakeRefCounted<FakeExportTransport>(
      absl::UnavailableError("collector down"));
  internal::OtlpGrpcLogRecordExporter exporter(transport, absl::Seconds(5));
  std::vector<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> records;
  records.push_back(exporter.MakeRecordable());
  EXPECT_EQ(
      exporter.Export(opentelemetry::nostd::span<
                      std::unique_ptr<opentelemetry::sdk::logs::Recordable>>(
          records.data(), records.size())),
      opentelemetry::sdk::common::ExportResult::kFailure);
  EXPECT_EQ(transport->requests().size(), 1u);
}

TEST(OtelAccessLogTest, ExporterTimesOut) {
  auto transport = grpc_core::MakeRefCounted<HangingExportTransport>();
  internal::OtlpGrpcLogRecordExporter exporter(transport,
                                               absl::Milliseconds(10));
  std::vector<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> records;
  records.push_back(exporter.MakeRecordable());
  EXPECT_EQ(
      exporter.Export(opentelemetry::nostd::span<
                      std::unique_ptr<opentelemetry::sdk::logs::Recordable>>(
          records.data(), records.size())),
      opentelemetry::sdk::common::ExportResult::kFailure);
}

TEST(OtelAccessLogTest, ExporterFailsAfterShutdown) {
  auto transport = grpc_core::MakeRefCounted<FakeExportTransport>();
  internal::OtlpGrpcLogRecordExporter exporter(transport, absl::Seconds(5));
  EXPECT_TRUE(exporter.Shutdown(std::chrono::microseconds(0)));
  std::vector<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> records;
  records.push_back(exporter.MakeRecordable());
  EXPECT_EQ(
      exporter.Export(opentelemetry::nostd::span<
                      std::unique_ptr<opentelemetry::sdk::logs::Recordable>>(
          records.data(), records.size())),
      opentelemetry::sdk::common::ExportResult::kFailure);
  EXPECT_TRUE(transport->requests().empty());
}

TEST(OtelAccessLogTest, BatchProcessorOptions) {
  grpc_core::OtelAccessLogSinkArgs args;
  args.buffer_flush_interval = grpc_core::Duration::Seconds(2);
  args.buffer_size_bytes = 16384;
  auto options = internal::MakeBatchProcessorOptions(args);
  EXPECT_EQ(options.schedule_delay_millis, std::chrono::milliseconds(2000));
  EXPECT_EQ(options.max_export_batch_size, 64u);
  // Small buffers still export at least one record per batch.
  args.buffer_size_bytes = 1;
  EXPECT_EQ(internal::MakeBatchProcessorOptions(args).max_export_batch_size,
            1u);
  // Large buffers are capped at the queue size.
  args.buffer_size_bytes = 1u << 30;
  options = internal::MakeBatchProcessorOptions(args);
  EXPECT_EQ(options.max_export_batch_size, options.max_queue_size);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
