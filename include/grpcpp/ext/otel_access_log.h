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

#ifndef GRPCPP_EXT_OTEL_ACCESS_LOG_H
#define GRPCPP_EXT_OTEL_ACCESS_LOG_H

namespace grpc {
namespace experimental {

/// EXPERIMENTAL API.
///
/// Enables OpenTelemetry access logs that the xDS control plane configures.
/// The control plane sends an OpenTelemetryAccessLogConfig in the
/// HttpConnectionManager access_log list.  gRPC then writes one OpenTelemetry
/// LogRecord per RPC, with the OpenTelemetry Logs SDK, and exports the records
/// over OTLP/gRPC to the collector in the config.
///
/// Requirements:
/// - Set the environment variable GRPC_EXPERIMENTAL_XDS_OTEL_ACCESS_LOG=true.
/// - Call this function before you create xDS channels or servers.
///
/// Only the first call has an effect.
void EnableOpenTelemetryXdsAccessLogging();

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_EXT_OTEL_ACCESS_LOG_H
