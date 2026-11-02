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

#include "src/core/filter/access_log/otel_access_log_sink.h"

#include <memory>
#include <utility>

#include "src/core/util/no_destruct.h"
#include "src/core/util/sync.h"

namespace grpc_core {

namespace {

struct FactoryState {
  Mutex mu;
  std::shared_ptr<OtelAccessLogSinkFactory> factory ABSL_GUARDED_BY(mu);
};

FactoryState& GetFactoryState() {
  static NoDestruct<FactoryState> state;
  return *state;
}

}  // namespace

void SetOtelAccessLogSinkFactory(
    std::shared_ptr<OtelAccessLogSinkFactory> factory) {
  FactoryState& state = GetFactoryState();
  MutexLock lock(&state.mu);
  state.factory = std::move(factory);
}

std::shared_ptr<OtelAccessLogSinkFactory> GetOtelAccessLogSinkFactory() {
  FactoryState& state = GetFactoryState();
  MutexLock lock(&state.mu);
  return state.factory;
}

}  // namespace grpc_core
