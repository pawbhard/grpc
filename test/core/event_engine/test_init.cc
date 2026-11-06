// Copyright 2022 The gRPC Authors
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
#include "test/core/event_engine/test_init.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <memory>
#include <string>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#ifdef GRPC_HAS_UV_EVENT_ENGINE
#include "src/core/lib/event_engine/uv_engine/uv_engine.h"
#endif

#if !defined(GPR_WINDOWS) && \
    (!defined(GRPC_CFSTREAM) || (defined(__APPLE__) && TARGET_OS_OSX))
#define GRPC_TEST_HAS_POSIX_ENGINE 1
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#endif

namespace grpc_event_engine {
namespace experimental {

/// Sets the default EventEngine factory, used for testing.
absl::Status InitializeTestingEventEngineFactory(absl::string_view engine) {
  if (engine == "default" || engine.empty()) {
    return absl::OkStatus();
  }
#ifdef GRPC_HAS_UV_EVENT_ENGINE
  if (absl::EqualsIgnoreCase(engine, "uv")) {
    SetEventEngineFactory([]() { return UvEventEngine::MakeUvEventEngine(); });
    return absl::OkStatus();
  }
#endif
#ifdef GRPC_TEST_HAS_POSIX_ENGINE
  if (absl::EqualsIgnoreCase(engine, "posix")) {
    SetEventEngineFactory(
        []() { return PosixEventEngine::MakePosixEventEngine(); });
    return absl::OkStatus();
  }
#endif
  return absl::InvalidArgumentError(
      absl::StrCat("Unknown EventEngine implementation: ", engine));
}

}  // namespace experimental
}  // namespace grpc_event_engine
