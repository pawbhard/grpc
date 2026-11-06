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

#include "src/core/lib/event_engine/uv_engine/uv_error_utils.h"

#include <uv.h>

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_event_engine::experimental {

absl::Status UvErrorToStatus(int uv_status, absl::string_view context) {
  if (uv_status == 0) {
    return absl::OkStatus();
  }
  if (uv_status == UV_EOF) {
    return absl::UnavailableError(
        context.empty() ? "Socket closed"
                        : absl::StrCat(context, ": Socket closed"));
  }
  char err_name_buf[64];
  char err_msg_buf[128];
  const char* err_name =
      uv_err_name_r(uv_status, err_name_buf, sizeof(err_name_buf));
  const char* err_msg =
      uv_strerror_r(uv_status, err_msg_buf, sizeof(err_msg_buf));
  std::string msg = context.empty() ? absl::StrCat(err_name, ": ", err_msg)
                                    : absl::StrCat(context, ": ", err_name,
                                                   " (", err_msg, ")");

  switch (uv_status) {
    case UV_ECANCELED:
      return absl::CancelledError(msg);
    case UV_ETIMEDOUT:
      return absl::DeadlineExceededError(msg);
    case UV_EINVAL:
    case UV_EAI_BADFLAGS:
    case UV_EAI_BADHINTS:
    case UV_EAI_FAMILY:
    case UV_EAI_SERVICE:
    case UV_EAI_SOCKTYPE:
      return absl::InvalidArgumentError(msg);
    case UV_EAI_NONAME:
    case UV_EAI_NODATA:
    case UV_ENOENT:
      return absl::NotFoundError(msg);
    case UV_EACCES:
    case UV_EPERM:
      return absl::PermissionDeniedError(msg);
    case UV_EADDRINUSE:
    case UV_EEXIST:
      return absl::AlreadyExistsError(msg);
    case UV_EMFILE:
    case UV_ENFILE:
    case UV_ENOMEM:
    case UV_ENOBUFS:
    case UV_EAI_MEMORY:
      return absl::ResourceExhaustedError(msg);
    case UV_ENOTSUP:
      return absl::UnimplementedError(msg);
    case UV_ECONNRESET:
    case UV_ECONNREFUSED:
    case UV_ECONNABORTED:
    case UV_EPIPE:
    case UV_ENETDOWN:
    case UV_ENETUNREACH:
    case UV_EHOSTDOWN:
    case UV_EHOSTUNREACH:
    case UV_EADDRNOTAVAIL:
    case UV_ENOTCONN:
    case UV_ESHUTDOWN:
    case UV_EAI_AGAIN:
    case UV_EAI_FAIL:
    case UV_EAI_ADDRFAMILY:
      return absl::UnavailableError(msg);
    default:
      return absl::InternalError(msg);
  }
}

}  // namespace grpc_event_engine::experimental
