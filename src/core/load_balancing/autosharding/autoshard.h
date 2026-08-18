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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARD_H
#define GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARD_H

#include <grpc/support/port_platform.h>

// TODO(bpawan): Once the "Channel Factory" (used to create a gRPC channel to
// the sharding service) is defined for C++ (see gRFC A119), expose the
// relevant types here, similar to how the ring_hash policy exposes
// RequestHashAttribute in its header.

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_AUTOSHARDING_AUTOSHARD_H
