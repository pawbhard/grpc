# gRPC Language Wrapper Guide

**Based on analysis of `src/python` — the reference wrapped-language implementation.**

This document explains every concern a new language wrapper must address when binding on top of the gRPC C core library. Use it as a checklist and architectural guide when adding a new language to this repo.

---

## Table of Contents

1. [Overview and Architecture](#1-overview-and-architecture)
2. [C Core FFI / Extension Layer](#2-c-core-ffi--extension-layer)
3. [Channel and Transport](#3-channel-and-transport)
4. [Credentials and Security](#4-credentials-and-security)
5. [Client-Side: Stubs and Callables](#5-client-side-stubs-and-callables)
6. [Server-Side: Server and ServicerContext](#6-server-side-server-and-servicercontext)
7. [Metadata Handling](#7-metadata-handling)
8. [Error Handling and Status Codes](#8-error-handling-and-status-codes)
9. [Streaming RPCs](#9-streaming-rpcs)
10. [Interceptors](#10-interceptors)
11. [Async / Non-blocking Support](#11-async--non-blocking-support)
12. [Code Generation (Protoc Plugin)](#12-code-generation-protoc-plugin)
13. [Build System Integration](#13-build-system-integration)
14. [Testing Patterns](#14-testing-patterns)
15. [XDS and Service Mesh](#15-xds-and-service-mesh)
16. [Load Balancing Policies](#16-load-balancing-policies)
17. [Retry, Hedging, and Service Config](#17-retry-hedging-and-service-config)
18. [Observability: Channelz, OpenTelemetry, and Admin](#18-observability-channelz-opentelemetry-and-admin)
19. [Server Reflection](#19-server-reflection)
20. [Fork Safety](#20-fork-safety)
21. [Custom Name Resolvers](#21-custom-name-resolvers)
22. [Channel and Connection Pool Management](#22-channel-and-connection-pool-management)
23. [Checklist for New Language Wrappers](#23-checklist-for-new-language-wrappers)

---

## 1. Overview and Architecture

### The Three-Layer Model

Every gRPC language wrapper in this repo follows the same three-layer architecture:

```
┌───────────────────────────────────────────────────┐
│  Layer 3: Public Language API                     │
│  Pure-language interfaces, abstractions, helpers  │
│  (grpc/__init__.py, grpc/_channel.py, etc.)       │
├───────────────────────────────────────────────────┤
│  Layer 2: FFI / Extension Binding Layer           │
│  Unsafe, performance-critical C bindings          │
│  (grpc/_cython/_cygrpc/*.pyx.pxi in Python)       │
├───────────────────────────────────────────────────┤
│  Layer 1: gRPC C Core (libgrpc)                   │
│  src/core/ — network I/O, HTTP/2, TLS, etc.       │
└───────────────────────────────────────────────────┘
```

**Why this separation matters:**
- Layer 1 (C core) handles all heavy lifting: HTTP/2 framing, TLS, load balancing, retries, flow control.
- Layer 2 (FFI) translates language-native types to C structs and back. It is inherently unsafe and should be kept minimal.
- Layer 3 (public API) should be idiomatic for the target language. Users never interact directly with Layer 2.

### Python Reference Files

| Concern | Key File(s) |
|---|---|
| Public API | `src/python/grpcio/grpc/__init__.py` |
| Channel | `src/python/grpcio/grpc/_channel.py` |
| Server | `src/python/grpcio/grpc/_server.py` |
| Interceptors | `src/python/grpcio/grpc/_interceptor.py` |
| Common utilities | `src/python/grpcio/grpc/_common.py` |
| Cython entry point | `src/python/grpcio/grpc/_cython/cygrpc.pyx` |
| Cython modules | `src/python/grpcio/grpc/_cython/_cygrpc/*.pxi` |
| Async subpackage | `src/python/grpcio/grpc/aio/` |

---

## 2. C Core FFI / Extension Layer

This is the most critical and language-specific part of any wrapper.

### What the FFI Layer Must Do

The FFI layer translates between your language's type system and gRPC's C API. It is responsible for:

1. **Object mapping** — wrapping `grpc_channel *`, `grpc_server *`, `grpc_call *`, `grpc_completion_queue *` in language-native handles.
2. **Batch operations** — constructing and submitting `grpc_op` arrays via `grpc_call_start_batch()`.
3. **Completion queue polling** — calling `grpc_completion_queue_next()` or `grpc_completion_queue_pluck()` to receive events.
4. **Memory management** — ensuring C structs are freed when language objects are garbage collected or go out of scope.
5. **Thread safety** — the GIL (or equivalent) must be released during blocking C calls.

### Python's Approach: Cython + Modular .pxi Files

Python uses Cython to write C extensions. The entry point is `cygrpc.pyx`, which includes 20+ modular `.pxi` files organized by concern:

| Module | Responsibility |
|---|---|
| `channel.pyx.pxi` | `grpc_channel` creation, channel args |
| `call.pyx.pxi` | `grpc_call` lifecycle |
| `server.pyx.pxi` | `grpc_server`, port binding, call requests |
| `credentials.pyx.pxi` | SSL, call credentials, metadata plugins |
| `completion_queue.pyx.pxi` | Event polling loop |
| `metadata.pyx.pxi` | Metadata encode/decode |
| `operation.pyx.pxi` | `grpc_op` wrappers for all 8 operation types |
| `event.pyx.pxi` | Completion event objects |
| `time.pyx.pxi` | Deadline / `grpc_timespec` conversion |
| `arguments.pyx.pxi` | Channel argument (`grpc_channel_args`) construction |
| `fork.pyx.pxi` | Fork-safety handlers |

### Key Design Decisions for Any Language

**Release the lock during I/O.** Python releases the GIL via `with nogil:` before calling `grpc_call_start_batch()` and `grpc_completion_queue_next()`. Every language with a global lock must do the same.

**Use batch operations.** Multiple `grpc_op` operations (send message, receive message, send status, etc.) can be submitted in a single `grpc_call_start_batch()` call. Python batches them in operation lists for efficiency.

**Completion queue per channel or per call.** Python creates one completion queue per channel and associates all calls on that channel with it. A polling thread processes events and delivers results to waiting call objects.

**Tag-based dispatch.** Each `grpc_call_start_batch()` receives a `void *tag`. When the completion queue fires, the tag routes the event back to the correct call object. Python uses Python object pointers wrapped in capsules as tags.

### The 8 gRPC Operation Types

Every wrapper must implement all 8:

| Op | Direction | Description |
|---|---|---|
| `GRPC_OP_SEND_INITIAL_METADATA` | Client → Server, Server → Client | Request/response headers |
| `GRPC_OP_SEND_MESSAGE` | Both | Payload bytes |
| `GRPC_OP_SEND_CLOSE_FROM_CLIENT` | Client → Server | Half-close (end of client stream) |
| `GRPC_OP_SEND_STATUS_FROM_SERVER` | Server → Client | Trailing metadata + status code |
| `GRPC_OP_RECV_INITIAL_METADATA` | Both | Receive headers |
| `GRPC_OP_RECV_MESSAGE` | Both | Receive payload bytes |
| `GRPC_OP_RECV_STATUS_ON_CLIENT` | Client | Trailing metadata + status |
| `GRPC_OP_RECV_CLOSE_ON_SERVER` | Server | Client half-close signal |

---

## 3. Channel and Transport

### What a Channel Is

A `Channel` is a long-lived connection handle to a single target (host:port). It:
- Manages the underlying HTTP/2 connection.
- Handles reconnects, backoff, and load balancing automatically (via C core).
- Is thread-safe; multiple calls can share one channel.

### Channel Arguments

Channel behavior is customized via `grpc_channel_args` — a list of key/value pairs. Important ones:

| Argument Key | Type | Effect |
|---|---|---|
| `grpc.max_receive_message_length` | int | Max inbound message size (-1 = unlimited) |
| `grpc.max_send_message_length` | int | Max outbound message size |
| `grpc.keepalive_time_ms` | int | Keepalive ping interval |
| `grpc.keepalive_timeout_ms` | int | Keepalive ping timeout |
| `grpc.max_reconnect_backoff_ms` | int | Max reconnect backoff |
| `grpc.lb_policy_name` | string | Load balancing policy (round_robin, grpclb, xds) |
| `grpc.service_config` | string | Service config JSON |

The FFI layer must expose a way to pass arbitrary channel args as key/value pairs.

### Connectivity State

The C core tracks channel connectivity state. Wrappers should expose:
- `get_state(try_to_connect)` → `IDLE | CONNECTING | READY | TRANSIENT_FAILURE | SHUTDOWN`
- `watch_connectivity_state(last_observed_state, deadline, callback)` for reactive monitoring.

Python exposes `channel.subscribe(callback)` and `channel.wait_for_ready(timeout)`.

### Channel Lifecycle

```
insecure_channel(target) / secure_channel(target, credentials)
  → channel.unary_unary(method, request_serializer, response_deserializer)
    → callable(request, timeout, metadata, credentials)
      → RPC invocation
  → channel.close()
```

---

## 4. Credentials and Security

### Two Levels of Credentials

**Channel credentials** — establish the transport security layer (TLS). Applied when creating the channel. Examples:
- `ssl_channel_credentials(root_certs, private_key, cert_chain)`
- `local_channel_credentials()` — for local Unix domain socket or loopback
- `insecure_channel_credentials()` — no TLS (dev/test)

**Call credentials** — attach per-call authentication metadata (e.g. bearer tokens). Applied per-call or composed with channel credentials. Examples:
- `metadata_call_credentials(plugin)` — custom metadata plugin
- `access_token_call_credentials(token)` — static bearer token
- `composite_call_credentials(creds_a, creds_b)` — combine multiple

**Composition:**
```
composite_channel_credentials(channel_creds, call_creds)
```
This attaches call credentials permanently to every call on the channel.

### Metadata Auth Plugin Interface

A metadata plugin is a callable invoked by the C core to retrieve auth metadata before each call. It must:
1. Accept `(auth_metadata_context, callback)`.
2. Return `(metadata_list, StatusCode.OK)` via `callback` (may be called asynchronously).
3. `auth_metadata_context` provides `service_url` and `method_name`.

The C core calls the plugin on a thread it controls. The plugin callback must be thread-safe.

### Server-Side Credentials

Servers use `ServerCredentials`:
- `ssl_server_credentials(private_key_certificate_chain_pairs, root_certificates, require_client_auth)`
- `local_server_credentials()`
- `insecure_server_credentials()`

---

## 5. Client-Side: Stubs and Callables

### The Four RPC Patterns

gRPC defines four call patterns. Each needs a distinct callable type:

| Pattern | Request | Response | Python Class |
|---|---|---|---|
| Unary-Unary | Single | Single | `UnaryUnaryMultiCallable` |
| Unary-Stream | Single | Iterator | `UnaryStreamMultiCallable` |
| Stream-Unary | Iterator | Single | `StreamUnaryMultiCallable` |
| Stream-Stream | Iterator | Iterator | `StreamStreamMultiCallable` |

### Callable Construction

Callables are created by calling methods on the `Channel` object:

```python
stub_method = channel.unary_unary(
    '/package.ServiceName/MethodName',
    request_serializer=MyRequest.SerializeToString,
    response_deserializer=MyResponse.FromString,
)
```

The channel binds:
- The full method name string (used as gRPC path).
- Serializer and deserializer for the request and response types.

### Invocation Parameters

Every call must support:
- `timeout` — deadline (absolute or relative, converted to `grpc_timespec`).
- `metadata` — list of `(key, value)` pairs sent as initial metadata.
- `credentials` — per-call call credentials (overrides channel-level credentials).
- `wait_for_ready` — block until channel is READY before sending (avoids fail-fast).
- `compression` — per-call compression algorithm.

### Call Object (Future Interface)

For unary calls, the result can be retrieved synchronously or via a future:

- `result()` — blocks until response received; raises `RpcError` on failure.
- `exception()` — returns exception if RPC failed, else None.
- `done()` — returns True when RPC complete.
- `cancelled()` — returns True if cancelled.
- `cancel()` — attempts to cancel the in-flight RPC.
- `initial_metadata()` — headers received from server.
- `trailing_metadata()` — trailers received from server.
- `code()` — `StatusCode` from server.
- `details()` — error details string from server.

### In-Flight RPC State

Python tracks each RPC with an `_RPCState` object containing:
- A `due` set: which operations are still pending (e.g. `{RECEIVE_STATUS_ON_CLIENT}`).
- A condition variable for blocking until operations complete.
- Storage for received metadata, message bytes, status code, and details.

This pattern should be replicated in any threaded wrapper.

---

## 6. Server-Side: Server and ServicerContext

### Server Lifecycle

```
server = grpc.server(thread_pool)
server.add_generic_rpc_handlers([MyServicer()])
server.add_insecure_port('[::]:50051')
server.start()
server.wait_for_termination()
# On shutdown:
server.stop(grace_period_seconds)
```

`stop(grace)` sends a graceful shutdown signal and waits up to `grace` seconds for in-flight RPCs to complete before forcefully terminating.

### Handler Registration

Handlers are registered as `RpcMethodHandler` objects:

```python
RpcMethodHandler(
    request_streaming=False,
    response_streaming=False,
    request_deserializer=MyRequest.FromString,
    response_serializer=MyResponse.SerializeToString,
    unary_unary=my_handler_function,   # or unary_stream, stream_unary, stream_stream
)
```

Handlers are grouped by service via `GenericRpcHandler.service_name()` + `service(handler_call_details)`.

The C core provides two call request paths:
1. **Registered method** (`grpc_server_register_method`) — optimized path for known methods; pre-registers method name.
2. **Unregistered call** (`grpc_server_request_call`) — generic fallback for dynamic/unknown methods.

### ServicerContext

The context object passed to every handler function. Must expose:

| Method | Description |
|---|---|
| `is_active()` | Whether client is still connected |
| `time_remaining()` | Seconds until deadline |
| `cancel()` | Abort the RPC from server side |
| `abort(code, details)` | Send error status and stop handler |
| `abort_with_status(status)` | Same with a `Status` object |
| `set_code(code)` | Set trailing status code |
| `set_details(details)` | Set trailing details string |
| `invocation_metadata()` | Client's request metadata |
| `peer()` | Client identity string |
| `peer_identities()` | List of peer identity strings |
| `peer_identity_key()` | Which identity type (SPIFFE URI, x509, etc.) |
| `send_initial_metadata(metadata)` | Explicitly send response headers |
| `set_trailing_metadata(metadata)` | Set trailers (sent with final status) |
| `set_compression(algorithm)` | Set per-call response compression |
| `add_callback(fn)` | Register fn called on RPC termination |
| `disable_next_message_compression()` | Skip compression for one message |

### Threading Model (Sync Server)

Python's sync server uses a `ThreadPoolExecutor`. Each incoming call is dispatched to the pool. The C core's completion queue runs in a dedicated background thread.

**Important:** `context.abort()` must be safe to call from handler threads. Python uses a lock and a cancellation flag to implement this.

---

## 7. Metadata Handling

### Format

gRPC metadata is a list of key/value pairs, similar to HTTP headers:
- **Keys** are ASCII strings, lowercase, no spaces.
- **Values** are either UTF-8 strings or raw bytes.
- **Binary keys** end in the `-bin` suffix. Their values must be raw bytes (the C core base64-encodes them on the wire).
- **Reserved keys** starting with `grpc-` are reserved for gRPC internal use.

### Encoding Rules

```
if key ends with "-bin":
    value must be bytes (passed as-is)
else:
    value must be a UTF-8 string (encoded to bytes before passing to C)
```

Python enforces this in `metadata.pyx.pxi`:

```cython
encoded_key = key.encode('ascii') if isinstance(key, str) else key
if encoded_key.endswith(b'-bin'):
    encoded_value = value           # must already be bytes
else:
    encoded_value = value.encode('utf-8') if isinstance(value, str) else value
```

### Metadata Categories

| Category | Direction | How Set/Retrieved |
|---|---|---|
| Request initial metadata | Client → Server | `call(metadata=[...])` |
| Response initial metadata | Server → Client | `context.send_initial_metadata()` |
| Response trailing metadata | Server → Client | `context.set_trailing_metadata()` |
| Status metadata | Server → Client | Automatic with status code |

### Common Mistakes

- Not treating `-bin` suffix values as raw bytes — causes corruption.
- Sending initial metadata more than once per RPC — the C core rejects this.
- Setting initial metadata after the first response message — not allowed; headers are implicitly sent with the first message.

---

## 8. Error Handling and Status Codes

### The 17 gRPC Status Codes

Every RPC completes with one of these status codes. Wrappers must expose all of them:

| Code | Value | Meaning |
|---|---|---|
| `OK` | 0 | Success |
| `CANCELLED` | 1 | RPC cancelled (by client or server) |
| `UNKNOWN` | 2 | Unknown error |
| `INVALID_ARGUMENT` | 3 | Client sent invalid argument |
| `DEADLINE_EXCEEDED` | 4 | Deadline expired |
| `NOT_FOUND` | 5 | Resource not found |
| `ALREADY_EXISTS` | 6 | Resource already exists |
| `PERMISSION_DENIED` | 7 | Caller lacks permission |
| `RESOURCE_EXHAUSTED` | 8 | Quota exceeded |
| `FAILED_PRECONDITION` | 9 | System state incorrect for operation |
| `ABORTED` | 10 | Concurrency conflict |
| `OUT_OF_RANGE` | 11 | Value outside valid range |
| `UNIMPLEMENTED` | 12 | Method not implemented |
| `INTERNAL` | 13 | Internal error |
| `UNAVAILABLE` | 14 | Service unavailable (retryable) |
| `DATA_LOSS` | 15 | Unrecoverable data loss |
| `UNAUTHENTICATED` | 16 | Request lacks valid auth credentials |

### Bidirectional Mapping

The C core uses integer status codes. The language layer must map these to idiomatic language constructs (enum, constants, objects) and back. Python maintains two dicts:

```python
CYGRPC_STATUS_CODE_TO_STATUS_CODE = { cygrpc.StatusCode.ok: grpc.StatusCode.OK, ... }
STATUS_CODE_TO_CYGRPC_STATUS_CODE  = { grpc.StatusCode.OK: cygrpc.StatusCode.ok, ... }
```

### RpcError Exception

Client-side failures are surfaced as an exception type (Python: `RpcError`). The exception object must also implement the `Call` interface so callers can inspect:
- `code()` — the `StatusCode`
- `details()` — human-readable error message string
- `initial_metadata()` — headers received before failure
- `trailing_metadata()` — trailers received with error status
- `debug_error_string()` — C core debug info (non-OK status only)

### Server-Side Abort

The server context's `abort(code, details)` must:
1. Set the response status code and details.
2. Send `GRPC_OP_SEND_STATUS_FROM_SERVER` immediately.
3. Stop calling the handler (raise a language-internal exception or use a flag).
4. Be callable from any thread (including threads spawned inside the handler).

### Serialization Error Handling

If `response_serializer` or `request_deserializer` throws, the wrapper must:
- Log the error.
- Return `INTERNAL` status to the peer.
- Not propagate the exception to the user's handler code.

---

## 9. Streaming RPCs

### Stream Types

| Pattern | Client Sends | Server Sends |
|---|---|---|
| Unary-Stream | One message, then close | Zero or more messages, then status |
| Stream-Unary | Zero or more messages, then close | One message + status |
| Stream-Stream | Zero or more messages, then close | Zero or more messages, then status |

### Client Streaming API

For sending:
- Iterator/generator passed to the call is consumed and serialized.
- Each item → `GRPC_OP_SEND_MESSAGE`.
- After iterator exhausted → `GRPC_OP_SEND_CLOSE_FROM_CLIENT`.

For receiving:
- Call object is iterable; each `next()` blocks on `GRPC_OP_RECV_MESSAGE`.
- When `GRPC_OP_RECV_STATUS_ON_CLIENT` fires, iteration ends.
- `StopIteration` raised on success, `RpcError` raised on non-OK status.

### Server Streaming API

For receiving:
- Handler receives one deserialized request (or an iterable for client-streaming).
- Handler yields / writes responses one at a time.

For sending:
- Each `yield` / `context.write(message)` → serializes + `GRPC_OP_SEND_MESSAGE`.
- Handler return → `GRPC_OP_SEND_STATUS_FROM_SERVER`.

### Flow Control

The C core implements HTTP/2 flow control automatically. From the wrapper perspective:
- Do not send the next message until the previous `GRPC_OP_SEND_MESSAGE` batch completes (completion queue event fires).
- Do not read the next message until `GRPC_OP_RECV_MESSAGE` completes.

Violating this ordering causes assertion failures inside the C core.

---

## 10. Interceptors

Interceptors are middleware that run before/after each RPC, on client or server side. They are a pure language-layer feature — the C core has no concept of interceptors.

### Client-Side Interceptors

Four interceptor base classes, one per call pattern:

```
UnaryUnaryClientInterceptor.intercept_unary_unary(continuation, client_call_details, request)
UnaryStreamClientInterceptor.intercept_unary_stream(continuation, client_call_details, request)
StreamUnaryClientInterceptor.intercept_stream_unary(continuation, client_call_details, request_iterator)
StreamStreamClientInterceptor.intercept_stream_stream(continuation, client_call_details, request_iterator)
```

`client_call_details` carries mutable call metadata:
- `method` — full method path string
- `timeout` — deadline
- `metadata` — request metadata list
- `credentials` — per-call credentials
- `wait_for_ready`
- `compression`

`continuation(modified_details, request)` — calls the next interceptor or the actual RPC.

Interceptors are chained: the channel wraps itself in a `_InterceptedChannel` that threads calls through each interceptor in order.

### Server-Side Interceptors

One base class:

```
ServerInterceptor.intercept_service(continuation, handler_call_details)
```

`handler_call_details` carries:
- `method` — full method name (e.g. `/package.Service/Method`)
- `invocation_metadata` — client's request metadata

`continuation(handler_call_details)` — returns the next `RpcMethodHandler` (or None).

The interceptor can return a modified handler, wrap the handler function, or return None to decline.

Server interceptors are applied once at server start (not per-call), building a static pipeline.

---

## 11. Async / Non-blocking Support

### Why Async Matters

gRPC is inherently async — the C core uses event loops internally. A wrapper that only exposes synchronous blocking calls forces every caller into thread-per-RPC patterns, which does not scale. A good wrapper exposes both sync (convenience) and async (performance) surfaces.

### Python's grpc.aio Design

Python provides `grpc.aio` as a separate sub-namespace. Key principles:

1. **All I/O returns awaitables.** `await channel.unary_unary(...)` instead of blocking.
2. **Handlers are async functions.** `async def MyMethod(self, request, context)`.
3. **Context methods are async.** `await context.send_initial_metadata(...)`, `await context.write(message)`.
4. **Streaming uses async iteration.** `async for item in call:` instead of blocking `for`.
5. **Server lifecycle is async.** `await server.start()`, `await server.stop(grace)`.
6. **Single event loop integration.** The completion queue runs inside the asyncio event loop rather than a dedicated thread.

### Async Completion Queue Integration

The core challenge: `grpc_completion_queue_next()` is a blocking C call. For async wrappers:

- Run completion queue polling in a thread and translate completions to async futures/awaitables.
- OR use `grpc_completion_queue_create_for_callback()` — fires a callback instead of requiring poll.

Python's `grpc.aio` uses Cython classes in `_cygrpc/aio/` that integrate with the event loop.

### Async Interceptors

The `grpc.aio` interceptors use the same interface as sync interceptors, but all methods are `async def`. This must be a separate set of base classes since mixing sync and async code leads to subtle bugs.

---

## 12. Code Generation (Protoc Plugin)

### The Role of the Plugin

The protoc code generator plugin converts `.proto` service definitions into language-native stub classes. The plugin is a separate binary that protoc executes:

```
protoc --grpc_LANGUAGE_out=. --plugin=protoc-gen-grpc_LANGUAGE=/path/to/plugin my_service.proto
```

### What the Plugin Must Generate

For each service definition, the plugin generates:

**Stub class (client-side):**
```
class MyServiceStub:
    constructor(channel):
        self.MethodName = channel.unary_unary(
            '/package.MyService/MethodName',
            request_serializer   = MyRequest.serialize,
            response_deserializer = MyResponse.deserialize,
        )
```

**Servicer base class (server-side):**
```
class MyServiceServicer:
    MethodName(request, context): raise NotImplementedError()
```

**Registration function:**
```
add_MyServiceServicer_to_server(servicer, server):
    server.add_generic_rpc_handlers([_GenericMethodHandler(servicer)])
```

### Full Method Name Convention

The gRPC method path string format is: `/package.ServiceName/MethodName`

- Package comes from the `package` or `option java_package` declaration in the .proto file.
- ServiceName is the `service` block name.
- MethodName is the `rpc` method name.

This exact string is passed to the channel callable and received by the server for routing. Any deviation will cause routing failures.

### grpcio_tools (Python-Specific)

Python bundles protoc + the gRPC plugin as a Python package (`grpcio_tools`). This allows:

```bash
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. my_service.proto
```

For other languages, the pattern is similar — ship a protoc plugin binary and document how to invoke it.

---

## 13. Build System Integration

### Core Challenge

The wrapper must compile both:
1. The gRPC C core itself (unless linking against a pre-installed shared library).
2. The FFI/extension layer that bridges the language runtime to the C core.

### Two Linking Strategies

| Strategy | Pros | Cons |
|---|---|---|
| **Bundle C core** | No external dependency, predictable version | Large binary, longer build time |
| **Dynamic link** | Smaller binary, shared across packages | Must manage libgrpc version separately |

Python bundles the C core. `grpc_core_dependencies.py` lists all 2700+ C/C++ source files to compile into the extension.

### Cython / FFI Compilation

If using a compiled FFI (Cython, CFFI, JNI, CGo, etc.):

1. **Source compilation path** — requires FFI compiler installed (Cython, etc.).
2. **Pre-generated C file path** — ship the generated `.c` file in the repo so users without the FFI compiler can still build. Python does this with `check_and_update_cythonization()`.

Always provide both paths. Requiring an FFI compiler at install time is a major friction point.

### Compiler Flags

C core requires:
- C++17 for core files
- `-pthread` on Linux/macOS
- MSVC-specific flags on Windows (`/D_WIN32_WINNT=0x0600`, etc.)
- `NOMINMAX`, `WIN32_LEAN_AND_MEAN` on Windows

Python's `commands.py` separates C and C++ files and applies appropriate flags.

### Key Environment Variables to Support

| Variable | Purpose |
|---|---|
| `GRPC_*_BUILD_WITH_CYTHON` | Force FFI source compilation |
| `GRPC_*_OVERRIDE_EXT_SUFFIX` | Cross-compilation suffix override |
| `GRPC_*_BUILD_USE_SHORT_TEMP_DIR_NAME` | Windows path length workaround |

### Version Metadata

The build system must inject the correct gRPC version string into the package. Python's `commands.py` includes a `BuildPy` command that writes version metadata before compilation.

---

## 14. Testing Patterns

### Test Suite Organization

Python tests are in `src/python/grpcio_tests/` with this structure:

```
grpcio_tests/
  tests/          # Synchronous tests
    unit/         # Per-feature unit tests
    integration/  # Full end-to-end RPC tests
  tests_aio/      # Async tests (mirror of tests/)
    unit/
    integration/
```

Every new language wrapper should follow the same split.

### Canonical Test Categories

| Category | What to Test |
|---|---|
| **Channel connectivity** | Insecure and TLS channels connect, get READY state |
| **Unary RPC** | Request/response round-trip, metadata, timeout |
| **Server streaming** | Receive multiple messages, iteration termination |
| **Client streaming** | Send multiple messages, server receives all |
| **Bidirectional streaming** | Interleaved send/receive |
| **Error handling** | Non-OK status codes, abort, deadline exceeded |
| **Metadata** | Binary keys, special characters, large metadata |
| **Credentials** | SSL channel, call credentials, composite |
| **Interceptors** | Client and server interceptors in chain |
| **Compression** | gzip, deflate, snappy at channel and call level |
| **Graceful shutdown** | Server stop with and without grace period |
| **Cancellation** | Client cancel, server cancel, deadline cancel |
| **Flow control** | Large messages, streaming back-pressure |

### Test Server Pattern

Python tests share a common test server helper (`tests_aio/unit/_test_server.py`). For new wrappers:

1. Use `src/proto/grpc/testing/` proto files — these are the standard interop test protos.
2. Implement a test server that handles all interop test methods.
3. Run it in-process on a random available port.
4. Tear it down after each test.

### Interop Testing

gRPC maintains a cross-language interoperability test suite. New language wrappers must pass all interop tests against the reference Go/Java/C++ servers. The interop test definitions are in `src/proto/grpc/testing/test.proto`.

---

## 15. XDS and Service Mesh

### What is xDS?

xDS (the "x Discovery Service" protocol family) is the control-plane API used by service meshes (Envoy, Istio, GCP Traffic Director, etc.) to dynamically push configuration to gRPC clients. A wrapper with xDS support can participate in a service mesh without a sidecar proxy.

The gRPC C core implements a full xDS client. The language wrapper mainly needs to:
1. Expose `xds://` scheme channel credentials.
2. Provide a way for users to opt in to the xDS name resolver.
3. Expose any policy configurations where the C core requires language-level input.

### xDS Credentials

Channels pointed at `xds:///` targets need special credentials that delegate security to the xDS control plane:

```python
# Python
import grpc
channel_creds = grpc.xds_channel_credentials(fallback_credentials)
channel = grpc.secure_channel("xds:///my-service", channel_creds)
```

`xds_channel_credentials` wraps `grpc_xds_credentials_create()`. The fallback is used if xDS doesn't supply a certificate provider.

Similarly for servers:
```python
server_creds = grpc.xds_server_credentials(fallback_credentials)
server.add_secure_port("[::]:443", server_creds)
```

### xDS Bootstrap

The xDS client needs a bootstrap file that tells it how to reach the xDS management server. The path is read from the `GRPC_XDS_BOOTSTRAP` environment variable, or the content directly from `GRPC_XDS_BOOTSTRAP_CONFIG`. The wrapper does not need to manage this — it is handled by the C core.

### xDS Name Resolution Schemes

| URI Scheme | Description |
|---|---|
| `xds:///service-name` | xDS-managed endpoint discovery |
| `dns:///host:port` | Standard DNS SRV lookup |
| `ipv4:address:port` | Direct IP targeting |
| `unix:///path` | Unix domain socket |

The target string passed to channel creation determines which resolver runs.

### xDS Features the C Core Provides

When the xDS name resolver is active, the C core automatically handles:

- **Endpoint Discovery Service (EDS)** — dynamic backend list updates
- **Cluster Discovery Service (CDS)** — cluster configuration
- **Listener Discovery Service (LDS)** — listener and filter chain updates
- **Route Discovery Service (RDS)** — HTTP/gRPC routing rules
- **Load reporting (LRS)** — sends per-locality load stats back to control plane
- **Circuit breaking** — max pending requests, max requests per connection
- **Outlier detection** — ejection of unhealthy backends
- **Retry policy from xDS** — server-controlled retry configuration

### What the Language Wrapper Must Add for xDS

Beyond credentials exposure, advanced wrappers should provide:

1. **Ring hash load balancing override** — channel arg `grpc.lb_policy_name = ring_hash` with `grpc.ring_hash_size_min/max`.
2. **Custom metadata credentials** — many xDS deployments require a per-call service account token.
3. **Proxyless mesh interop test** — run the PSM (Proxyless Service Mesh) interop tests against a real Traffic Director endpoint.

---

## 16. Load Balancing Policies

The C core implements all standard load balancing policies. Wrappers expose them through the `grpc.lb_policy_name` channel argument.

### Built-in Policies

| Policy Name | Description | When to Use |
|---|---|---|
| `pick_first` | Always use first resolved backend | Default for single-address targets |
| `round_robin` | Cycle through backends equally | Default multi-address policy |
| `grpclb` | Cloud Load Balancer (deprecated) | Legacy GCP deployments |
| `xds_cluster_manager_experimental` | xDS-controlled routing | Service mesh targets |
| `ring_hash` | Consistent hash by header key | Session affinity |
| `weighted_round_robin` | Proportional load distribution | Backends with different capacities |

### Setting Load Balancing Policy

```python
# Python: via channel options
channel = grpc.insecure_channel(
    "dns:///my-service.internal:443",
    options=[("grpc.lb_policy_name", "round_robin")]
)
```

### Service Config JSON

More complex policies (with parameters) use a JSON service config:

```python
service_config = json.dumps({
    "loadBalancingConfig": [
        {"round_robin": {}}
    ],
    "methodConfig": [{
        "name": [{"service": "my.Service"}],
        "retryPolicy": {
            "maxAttempts": 3,
            "initialBackoff": "0.1s",
            "maxBackoff": "1s",
            "backoffMultiplier": 2,
            "retryableStatusCodes": ["UNAVAILABLE"]
        }
    }]
})

channel = grpc.insecure_channel(
    target,
    options=[("grpc.service_config", service_config)]
)
```

### Wrapper Responsibility

The wrapper itself does not implement LB — that is entirely in the C core. The wrapper's only job is:
1. Accept `options` (channel args) from user and pass them to `grpc_channel_create()`.
2. Document which option keys control LB behavior.
3. Optionally provide typed helpers for common configurations.

---

## 17. Retry, Hedging, and Service Config

### Retry Policy

gRPC supports automatic transparent retries configured via service config JSON. The C core handles retries — the wrapper just passes the config.

**Retry policy fields:**

| Field | Type | Description |
|---|---|---|
| `maxAttempts` | int | Total attempts including first (max: 5) |
| `initialBackoff` | duration string | First retry wait (e.g. "0.5s") |
| `maxBackoff` | duration string | Backoff ceiling |
| `backoffMultiplier` | float | Exponential growth factor |
| `retryableStatusCodes` | string[] | Codes that trigger retry |

Retries only activate on `UNAVAILABLE`, `RESOURCE_EXHAUSTED`, and any server-reported `grpc-retry-pushback-ms` trailer. `CANCELLED` and `DEADLINE_EXCEEDED` are never retried.

### Hedging Policy

Hedging sends the same request to multiple backends simultaneously and uses the first successful response. It is for latency-sensitive workloads, not fault tolerance.

```json
{
  "hedgingPolicy": {
    "maxAttempts": 3,
    "hedgingDelay": "0.1s",
    "nonFatalStatusCodes": ["UNAVAILABLE"]
  }
}
```

Retry and hedging are mutually exclusive per method.

### Wait for Ready

`wait_for_ready=True` (or equivalent) causes a call to queue rather than fail immediately when the channel is not yet `READY`. This is distinct from retry — it prevents the initial attempt from failing, rather than retrying a failure.

```python
response = stub.MyMethod(request, wait_for_ready=True, timeout=10.0)
```

### Per-Method Config

Service config can apply settings per method, per service, or globally:

```json
{
  "methodConfig": [
    {
      "name": [{"service": "my.Service", "method": "SlowMethod"}],
      "timeout": "30s",
      "maxRequestMessageBytes": 10485760
    },
    {
      "name": [{"service": "my.Service"}],
      "retryPolicy": { ... }
    }
  ]
}
```

---

## 18. Observability: Channelz, OpenTelemetry, and Admin

### Channelz

Channelz is a built-in gRPC introspection mechanism that exposes internal state (channels, subchannels, sockets, servers) via a gRPC service. It is implemented in the C core and requires only a thin language-layer wrapper to register the service.

Python exposes it via the `grpcio_channelz` package:

```python
from grpc_channelz.v1 import channelz
channelz.add_channelz_servicer(server)
```

The Channelz proto is at `src/proto/grpc/channelz/v1/channelz.proto`. The service exposes:
- `GetTopChannels` — list all top-level channels
- `GetServers` — list all servers
- `GetChannel` / `GetSubchannel` — channel details including state, calls started/succeeded/failed
- `GetSocket` — socket-level stats (bytes sent/received, streams active)
- `GetServerSockets` — per-server socket stats

New language wrappers should implement the Channelz service using generated protobuf stubs.

### OpenTelemetry Integration

Python ships `grpcio_observability` which hooks into the C core's experimental observability API. It exposes:

- **Metrics** (via OpenTelemetry SDK):
  - `grpc.client.attempt.started` — attempts per RPC
  - `grpc.client.attempt.duration` — latency histogram
  - `grpc.client.attempt.sent_total_compressed_message_size`
  - `grpc.client.attempt.rcvd_total_compressed_message_size`
  - `grpc.server.call.started`
  - `grpc.server.call.duration`
  - `grpc.server.call.sent_total_compressed_message_size`
  - `grpc.server.call.rcvd_total_compressed_message_size`

- **Configuration:**
```python
from grpc_observability import opentelemetry as grpc_opentelemetry
from opentelemetry.sdk.metrics import MeterProvider

provider = MeterProvider()
with grpc_opentelemetry.OpenTelemetryPlugin(meter_provider=provider):
    channel = grpc.insecure_channel("localhost:50051")
    # All calls through this channel automatically emit metrics
```

For new wrappers, OpenTelemetry integration is optional but strongly recommended for production readiness. The C core exposes `grpc_observability_*` APIs that the wrapper can hook into.

### Admin Server

Python's `grpcio_admin` package bundles Channelz + CSDS (Client Status Discovery Service) behind a single admin port:

```python
from grpc_admin import admin
admin_server = grpc.server(futures.ThreadPoolExecutor())
admin.add_admin_servicer(admin_server)
admin_server.add_insecure_port('[::]:50052')
admin_server.start()
```

This follows the [gRPC Admin Interface spec](https://github.com/grpc/proposal/blob/master/A38-admin-interface-api.md). New wrappers should implement this once Channelz is functional.

### Health Checking

Python's `grpcio_health_checking` implements the [gRPC Health Checking Protocol](https://github.com/grpc/grpc/blob/master/doc/health-checking.md):

```python
from grpc_health.v1 import health, health_pb2, health_pb2_grpc

health_servicer = health.HealthServicer()
health_pb2_grpc.add_HealthServicer_to_server(health_servicer, server)
health_servicer.set('my.Service', health_pb2.HealthCheckResponse.SERVING)
```

Clients can probe `grpc.health.v1.Health/Check` before sending RPCs. Load balancers use this for backend health probes. Any production-quality wrapper should implement this.

---

## 19. Server Reflection

Server Reflection allows clients to dynamically discover what services and methods a server exposes, without having the `.proto` files at compile time. Tools like `grpc_cli` and `grpcurl` rely on reflection.

Python ships `grpcio_reflection`:

```python
from grpc_reflection.v1alpha import reflection
from grpc_reflection.v1alpha.reflection_pb2 import ServerReflectionResponse

reflection.enable_server_reflection([
    'my.Service',
    reflection.SERVICE_NAME,  # reflection service itself
], server)
```

The reflection service (`grpc.reflection.v1alpha.ServerReflection`) is defined at `src/proto/grpc/reflection/v1alpha/reflection.proto`.

**What reflection provides:**
- List all services registered on the server
- Fetch file descriptor protos for any message/service type by name
- Extension number lookup for proto2 extensions

New wrappers implement reflection by generating a server that handles the reflection proto service and can introspect registered service descriptors.

---

## 20. Fork Safety

### Why Fork Safety is Hard

When a process forks, only the calling thread is duplicated — all other threads (including gRPC background threads) vanish. Any mutexes they held are now permanently locked in the child. gRPC's completion queue threads are all gone. The child will deadlock or crash if it tries to use the pre-fork channel/server objects.

### What the Wrapper Must Do

**Before fork (`pthread_atfork` prepare handler):**
1. Acquire all gRPC internal locks.
2. Stop the completion queue poll thread.
3. Drain any in-flight batches.

**After fork in child:**
1. Release locks.
2. Re-initialize gRPC from scratch (`grpc_init()`).
3. Invalidate all channel/call/server objects created before the fork.
4. Optionally clean up persistent channel caches.

**After fork in parent:**
1. Release locks.
2. Resume the completion queue poll thread.

Python registers `atfork` handlers in `fork.pyx.pxi`. It provides:
- `grpc_postfork_child()` — rebuilds gRPC state for child
- `grpc_postfork_parent()` — resumes parent threads
- Channel cleanup on fork if persistence is enabled

Enable via env var:
```bash
GRPC_ENABLE_FORK_SUPPORT=1
```

### Wrapper Implementation

Register callbacks with `pthread_atfork()` during extension initialization:

```c
pthread_atfork(
    grpc_prefork,        // prepare: drain + lock
    grpc_postfork_parent, // parent: unlock + resume
    grpc_postfork_child   // child: reinit
);
```

Expose this as a compile-time or runtime flag. Fork support is not required on platforms without `fork()` (Windows).

---

## 21. Custom Name Resolvers

The C core supports pluggable name resolvers. A name resolver translates a target URI into a list of resolved addresses (with optional service config).

### Built-in Resolvers

| Scheme | Resolver | Notes |
|---|---|---|
| `dns:` | DNS SRV/A | Polls at interval for updates |
| `ipv4:` / `ipv6:` | Static address | No re-resolution |
| `unix:` / `unix-abstract:` | Unix socket | Local IPC |
| `xds:` | xDS client | Full service mesh |

### Writing a Custom Resolver (C API)

Custom resolvers implement `grpc_resolver_factory` and `grpc_resolver`:

```c
static grpc_resolver* my_create_resolver(grpc_resolver_args* args) { ... }
static void my_destroy(grpc_resolver* r) { ... }

grpc_resolver_factory_register(grpc_resolver_factory_create(
    "myscheme",
    my_create_resolver, my_destroy, my_get_default_authority
));
```

### Language Wrapper Surface

Python exposes a high-level Python plugin interface via `grpc.experimental.gevent` and the future `grpc.experimental.resolver` API. The practical approach for most wrappers is:
1. Register custom C resolvers at C-extension initialization time.
2. Document the supported URI schemes.
3. Provide helpers for common patterns (e.g., Consul, Kubernetes endpoints).

---

## 22. Channel and Connection Pool Management

### Persistent Channel Caching

Many PHP and high-level wrappers implement a channel cache to avoid creating a new TCP+TLS connection on every request. Python's `grpc._simple_stubs` module ships a `ChannelCache`:

- Max 256 channels by default (`GRPC_PYTHON_MANAGED_CHANNEL_EVICTION_PERIOD_S`)
- LRU eviction after 10 minutes idle
- Keyed by `(target, credentials, options)` tuple
- Thread-safe with background eviction thread

PHP uses a persistent hashmap keyed by SHA1 of `(target, credentials, args)`:

```c
// PHP ext/grpc/channel.c pattern
char* key = sha1(target + serialize(args));
zend_hash_find(grpc_persistent_list, key, &data);
```

### Connection Pool vs Channel Reuse

A single `grpc_channel` already multiplexes many concurrent RPCs over a pool of HTTP/2 connections. The language layer should:

1. **Not create a new channel per RPC.** A channel represents a logical endpoint, not a single connection. Creating a new channel per RPC defeats HTTP/2 multiplexing and wastes TLS handshakes.
2. **Provide a thread-safe channel cache** if the language is used in web serving mode where each request runs in a fresh context.
3. **Expose `force_new` or equivalent** to let callers bypass the cache when they need isolated channel state.

### Key Channel Lifecycle Methods

| Method | Purpose |
|---|---|
| `get_state(try_to_connect)` | Check current connectivity state |
| `watch_connectivity_state(state, deadline)` | Block until state changes |
| `wait_for_ready(timeout)` | Block until READY or timeout |
| `close()` / `shutdown()` | Trigger graceful teardown |

After `close()` is called no new calls should be allowed and in-flight calls should receive `CANCELLED`.

---

## 23. Checklist for New Language Wrappers

Use this as a go/no-go checklist before considering a wrapper production-ready.

### FFI / C Binding Layer
- [ ] `grpc_channel` wrapped with lifecycle management (create, destroy)
- [ ] `grpc_server` wrapped with lifecycle management
- [ ] `grpc_call` wrapped per-RPC
- [ ] `grpc_completion_queue` polling implemented (thread or event loop)
- [ ] All 8 `grpc_op` types implemented
- [ ] `grpc_channel_args` construction exposed
- [ ] `grpc_timespec` / deadline conversion implemented
- [ ] Lock released during blocking C calls

### Channel
- [ ] `insecure_channel(target, options)` constructor
- [ ] `secure_channel(target, credentials, options)` constructor
- [ ] Channel argument passthrough
- [ ] `get_state()` / connectivity state exposed
- [ ] `close()` / cleanup implemented

### Credentials
- [ ] `ssl_channel_credentials(root_certs, private_key, cert_chain)`
- [ ] `ssl_server_credentials(pairs, root_certs, require_client_auth)`
- [ ] `call_credentials` / metadata plugin interface
- [ ] `composite_channel_credentials(channel, call)`
- [ ] `insecure_channel_credentials()`

### Client-Side
- [ ] `UnaryUnaryMultiCallable` with `call()` and `future()` variants
- [ ] `UnaryStreamMultiCallable` with iterator response
- [ ] `StreamUnaryMultiCallable` with iterator request
- [ ] `StreamStreamMultiCallable` with iterator request/response
- [ ] `timeout`, `metadata`, `credentials`, `wait_for_ready`, `compression` per call
- [ ] `Call` interface: `code()`, `details()`, `initial_metadata()`, `trailing_metadata()`
- [ ] `cancel()` on in-flight calls

### Server-Side
- [ ] `Server` with `start()`, `stop(grace)`, `wait_for_termination()`
- [ ] `add_insecure_port()`, `add_secure_port()`
- [ ] Handler registration (`GenericRpcHandler` or equivalent)
- [ ] `ServicerContext` with all required methods (see §6)
- [ ] All 4 handler types: unary/unary, unary/stream, stream/unary, stream/stream
- [ ] `abort(code, details)` implementation
- [ ] Thread-safe context methods

### Metadata
- [ ] Key/value pair encoding
- [ ] `-bin` suffix → raw bytes passthrough
- [ ] Binary value detection and enforcement
- [ ] `invocation_metadata()` on server context

### Error Handling
- [ ] All 17 `StatusCode` values mapped
- [ ] `RpcError` (or equivalent exception) with `code()`, `details()`
- [ ] `RpcError` also implements `Call` interface
- [ ] Server `abort()` stops handler execution
- [ ] Serialization errors → `INTERNAL` status, not crash

### Streaming
- [ ] Client stream: iterator consumed and sent
- [ ] Server stream: iterable/generator returned from handler
- [ ] Half-close (`SEND_CLOSE_FROM_CLIENT`) sent after iterator exhausted
- [ ] `RECV_CLOSE_ON_SERVER` monitored for cancellation detection
- [ ] Flow control: one op at a time per call

### Interceptors
- [ ] Client interceptor interface for all 4 RPC types
- [ ] Mutable `ClientCallDetails` (method, timeout, metadata, credentials)
- [ ] Server interceptor interface
- [ ] Interceptor chaining (multiple interceptors in order)

### Async (if applicable)
- [ ] Async channel and channel methods
- [ ] Async server with async handler support
- [ ] Async `ServicerContext` methods
- [ ] Async streaming iteration
- [ ] Async interceptors (separate interface from sync)
- [ ] Completion queue integration with event loop

### Code Generation
- [ ] Protoc plugin generates stub class per service
- [ ] Stub binds method name, serializer, deserializer to channel callable
- [ ] Servicer base class generated per service
- [ ] Registration helper generated per service
- [ ] Full method name format: `/package.Service/Method`

### Build System
- [ ] C core compilation or linking against system libgrpc
- [ ] FFI layer compilation with pre-generated C file fallback
- [ ] C++17 flag for core, correct flags for target OS
- [ ] Version metadata injection
- [ ] Package published to language-native registry

### Testing
- [ ] Unit tests for all major features
- [ ] Integration tests using interop test protos
- [ ] gRPC interoperability test suite passing
- [ ] Async tests if async API provided
- [ ] TLS tests with real certificates

### XDS and Service Mesh
- [ ] `xds_channel_credentials(fallback)` exposed
- [ ] `xds_server_credentials(fallback)` exposed
- [ ] `xds://` URI scheme passes through to C core resolver
- [ ] PSM interop tests pass (optional but recommended)

### Load Balancing and Service Config
- [ ] `grpc.lb_policy_name` channel arg accepted
- [ ] `grpc.service_config` (JSON string) channel arg accepted
- [ ] `wait_for_ready` per-call parameter supported

### Observability
- [ ] Channelz service registerable on server
- [ ] Health check service (`grpc.health.v1.Health`) implementable
- [ ] Server reflection service (`grpc.reflection.v1alpha`) optional but recommended
- [ ] OpenTelemetry or language-native metrics hooks (production readiness)

### Fork Safety (POSIX platforms)
- [ ] `pthread_atfork` handlers registered at extension init
- [ ] Child process reinitializes gRPC state
- [ ] Persistent channel caches invalidated on fork
- [ ] Opt-in via env var (`GRPC_ENABLE_FORK_SUPPORT=1`)

### Connection Management
- [ ] Channel reuse (do not create per-RPC)
- [ ] Thread-safe channel cache if needed for web serving pattern
- [ ] `force_new` / bypass-cache option exposed
- [ ] `wait_for_ready(timeout)` implemented

---

## Appendix A: Key C Core APIs Per Concern

| Concern | Primary C API |
|---|---|
| Init / shutdown | `grpc_init()`, `grpc_shutdown()` |
| Insecure channel | `grpc_insecure_channel_create()` |
| Secure channel | `grpc_secure_channel_create()` |
| Channel destroy | `grpc_channel_destroy()` |
| Connectivity | `grpc_channel_check_connectivity_state()`, `grpc_channel_watch_connectivity_state()` |
| SSL channel creds | `grpc_ssl_credentials_create()` |
| SSL server creds | `grpc_ssl_server_credentials_create()` |
| Call credentials | `grpc_metadata_credentials_create_from_plugin()` |
| Composite creds | `grpc_composite_channel_credentials_create()` |
| XDS channel creds | `grpc_xds_credentials_create()` |
| XDS server creds | `grpc_xds_server_credentials_create()` |
| Local creds | `grpc_local_credentials_create()` |
| Server create | `grpc_server_create()` |
| Server port | `grpc_server_add_http2_port()` |
| Server start | `grpc_server_start()` |
| Request call | `grpc_server_request_call()` |
| Register method | `grpc_server_register_method()` |
| Request registered | `grpc_server_request_registered_call()` |
| Server shutdown | `grpc_server_shutdown_and_notify()` |
| Completion queue | `grpc_completion_queue_create_for_next()` |
| CQ for callbacks | `grpc_completion_queue_create_for_callback()` |
| Poll event | `grpc_completion_queue_next()` |
| Pluck event | `grpc_completion_queue_pluck()` |
| Create call | `grpc_channel_create_call()` |
| Start batch | `grpc_call_start_batch()` |
| Cancel call | `grpc_call_cancel()` |
| Unref call | `grpc_call_unref()` |
| Fork prepare | `grpc_prefork()` |
| Fork child | `grpc_postfork_child()` |
| Fork parent | `grpc_postfork_parent()` |
| Resolver register | `grpc_resolver_factory_register()` |

---

## Appendix B: Common Pitfalls

1. **Sending initial metadata twice** — the C core will assert/crash. Guard with a `metadata_sent` flag.
2. **Not releasing the language lock during blocking calls** — deadlocks under concurrency.
3. **Incorrect deadline type** — the C core takes `gpr_timepoint` (absolute, monotonic clock), not duration. Convert `timeout_seconds` → absolute time before passing to C.
4. **Binary metadata not ending in -bin** — silently corrupts base64 encoding on the wire.
5. **Not polling the completion queue** — RPCs hang indefinitely. The polling loop must always be running.
6. **Not implementing `RECV_CLOSE_ON_SERVER`** — server never detects client cancellation; handlers run to completion even after client disconnects.
7. **Forgetting `SEND_CLOSE_FROM_CLIENT`** — server-side stream-unary RPC never receives the final request and waits forever.
8. **Tag lifetime** — the `void *tag` passed to `grpc_call_start_batch()` must remain valid until the completion event fires. Garbage collection can collect it early if not pinned.
9. **Not handling `GRPC_QUEUE_SHUTDOWN`** — completion queue shutdown event must be detected and the polling loop exited cleanly.
10. **Status code 0 vs OK** — always map C integer 0 to the language's `OK` constant; do not expose raw integers to users.
