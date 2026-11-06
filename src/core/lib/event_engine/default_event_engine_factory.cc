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

#include "src/core/lib/event_engine/default_event_engine_factory.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <memory>
#include <optional>
#include <string>

#include "src/core/util/crash.h"
#include "src/core/util/env.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

#ifdef GRPC_HAS_UV_EVENT_ENGINE
#include "src/core/lib/event_engine/uv_engine/uv_engine.h"
#endif

#if defined(GPR_WINDOWS)
#include "src/core/lib/event_engine/windows/windows_engine.h"
#elif defined(GRPC_CFSTREAM)
#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#if defined(__APPLE__) && TARGET_OS_OSX
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#endif
#else
#include "src/core/lib/event_engine/posix_engine/posix_engine.h"
#endif

namespace grpc_event_engine::experimental {

namespace {

std::shared_ptr<EventEngine> CreatePlatformDefaultEventEngine() {
#if defined(GRPC_HAS_UV_EVENT_ENGINE) && defined(GRPC_DEFAULT_EVENT_ENGINE_UV)
  VLOG(2) << "Creating default UvEventEngine (grpc_default_event_engine=uv)";
  return UvEventEngine::MakeUvEventEngine();
#elif defined(GPR_WINDOWS)
  VLOG(2) << "Creating default WindowsEventEngine";
  return std::make_shared<WindowsEventEngine>();
#elif defined(GRPC_CFSTREAM)
  VLOG(2) << "Creating default CFEventEngine";
  return std::make_shared<CFEventEngine>();
#else
  VLOG(2) << "Creating default PosixEventEngine";
  return PosixEventEngine::MakePosixEventEngine();
#endif
}

}  // namespace

std::shared_ptr<EventEngine> DefaultEventEngineFactory() {
  std::optional<std::string> env = grpc_core::GetEnv("GRPC_EVENT_ENGINE");
  if (!env.has_value()) {
    return CreatePlatformDefaultEventEngine();
  }
  absl::string_view engine_name = absl::StripAsciiWhitespace(*env);
  if (engine_name.empty() || absl::EqualsIgnoreCase(engine_name, "default")) {
    return CreatePlatformDefaultEventEngine();
  }
#ifdef GRPC_HAS_UV_EVENT_ENGINE
  if (absl::EqualsIgnoreCase(engine_name, "uv")) {
    VLOG(2) << "Creating UvEventEngine via GRPC_EVENT_ENGINE=uv";
    return UvEventEngine::MakeUvEventEngine();
  }
#endif
#if !defined(GPR_WINDOWS) && \
    (!defined(GRPC_CFSTREAM) || (defined(__APPLE__) && TARGET_OS_OSX))
  if (absl::EqualsIgnoreCase(engine_name, "posix")) {
    VLOG(2) << "Creating PosixEventEngine via GRPC_EVENT_ENGINE=posix";
    return PosixEventEngine::MakePosixEventEngine();
  }
#endif
  grpc_core::Crash(
      absl::StrCat("Unknown GRPC_EVENT_ENGINE value: '", engine_name, "'"));
}

}  // namespace grpc_event_engine::experimental
