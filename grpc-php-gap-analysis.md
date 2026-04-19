# gRPC PHP — API Surface and Gap Analysis

**Compared against the Python reference implementation (`src/python`).**

This document maps the complete PHP API surface, identifies what is implemented, what is partial, and what is entirely missing — relative to the feature set a mature gRPC language wrapper should provide.

---

## Table of Contents

1. [PHP API Surface (What Exists Today)](#1-php-api-surface-what-exists-today)
2. [Client-Side API Comparison](#2-client-side-api-comparison)
3. [Server-Side API Comparison](#3-server-side-api-comparison)
4. [Credentials and Security](#4-credentials-and-security)
5. [Interceptors](#5-interceptors)
6. [Metadata Handling](#6-metadata-handling)
7. [Error Handling and Status Codes](#7-error-handling-and-status-codes)
8. [Streaming RPCs](#8-streaming-rpcs)
9. [XDS and Service Mesh](#9-xds-and-service-mesh)
10. [Load Balancing and Service Config](#10-load-balancing-and-service-config)
11. [Retry and Resilience](#11-retry-and-resilience)
12. [Async and Concurrency](#12-async-and-concurrency)
13. [Compression](#13-compression)
14. [Observability](#14-observability)
15. [Server Reflection](#15-server-reflection)
16. [Health Checking](#16-health-checking)
17. [Fork Safety](#17-fork-safety)
18. [Connection Management](#18-connection-management)
19. [Code Generation](#19-code-generation)
20. [Build System and Packaging](#20-build-system-and-packaging)
21. [Testing Coverage](#21-testing-coverage)
22. [Gap Priority Matrix](#22-gap-priority-matrix)

---

## 1. PHP API Surface (What Exists Today)

### Architecture

```
src/php/
├── ext/grpc/          C extension — wraps gRPC C core types
│   ├── channel.c/h          → Grpc\Channel
│   ├── call.c/h             → Grpc\Call
│   ├── server.c/h           → Grpc\Server
│   ├── channel_credentials.c/h → Grpc\ChannelCredentials
│   ├── call_credentials.c/h    → Grpc\CallCredentials
│   ├── server_credentials.c/h  → Grpc\ServerCredentials
│   ├── timeval.c/h          → Grpc\Timeval
│   ├── completion_queue.c/h (internal)
│   ├── byte_buffer.c/h      (internal)
│   └── php_grpc.c           extension init, constant registration
└── lib/Grpc/          Pure PHP — high-level client and stub layer
    ├── BaseStub.php         base class for generated stubs
    ├── AbstractCall.php     base for all call types
    ├── UnaryCall.php
    ├── ClientStreamingCall.php
    ├── ServerStreamingCall.php
    ├── BidiStreamingCall.php
    ├── Interceptor.php      client interceptor base
    ├── CallInvoker.php      interface for call factories
    ├── DefaultCallInvoker.php
    ├── Internal/
    │   └── InterceptorChannel.php
    ├── Status.php           status helper
    ├── RpcServer.php        [EXPERIMENTAL] server impl
    ├── ServerContext.php    [EXPERIMENTAL]
    ├── ServerCallReader.php [EXPERIMENTAL]
    ├── ServerCallWriter.php [EXPERIMENTAL]
    └── MethodDescriptor.php [EXPERIMENTAL]
```

### C Extension Constants

All 8 op types, all 17 status codes, 5 connectivity states, and write flags are registered as PHP constants in the `Grpc\` namespace:

```
OP_SEND_INITIAL_METADATA, OP_SEND_MESSAGE, OP_SEND_CLOSE_FROM_CLIENT,
OP_SEND_STATUS_FROM_SERVER, OP_RECV_INITIAL_METADATA, OP_RECV_MESSAGE,
OP_RECV_STATUS_ON_CLIENT, OP_RECV_CLOSE_ON_SERVER

STATUS_OK, STATUS_CANCELLED, STATUS_UNKNOWN, STATUS_INVALID_ARGUMENT,
STATUS_DEADLINE_EXCEEDED, STATUS_NOT_FOUND, STATUS_ALREADY_EXISTS,
STATUS_PERMISSION_DENIED, STATUS_RESOURCE_EXHAUSTED,
STATUS_FAILED_PRECONDITION, STATUS_ABORTED, STATUS_OUT_OF_RANGE,
STATUS_UNIMPLEMENTED, STATUS_INTERNAL, STATUS_UNAVAILABLE,
STATUS_DATA_LOSS, STATUS_UNAUTHENTICATED

CHANNEL_IDLE, CHANNEL_CONNECTING, CHANNEL_READY,
CHANNEL_TRANSIENT_FAILURE, CHANNEL_FATAL_FAILURE

WRITE_BUFFER_HINT, WRITE_NO_COMPRESS
```

---

## 2. Client-Side API Comparison

### Channel (`Grpc\Channel`)

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Insecure channel | ✅ `new Channel($target, ['credentials' => ChannelCredentials::createInsecure()])` | ✅ `grpc.insecure_channel(target)` | PHP always requires credentials key |
| Secure / TLS channel | ✅ Pass SSL credentials in options | ✅ `grpc.secure_channel(target, creds)` | |
| Channel args / options | ✅ Passed as PHP array | ✅ Python list of tuples | |
| `getTarget()` | ✅ | ✅ `channel.target()` | |
| `getConnectivityState($try_to_connect)` | ✅ | ✅ `channel.check_connectivity_state()` | |
| `watchConnectivityState($new_state, $deadline)` | ✅ | ✅ | |
| `close()` | ✅ | ✅ | |
| `wait_for_ready(timeout)` | ⚠️ `waitForReady($timeout)` in BaseStub only | ✅ `channel.wait_for_ready()` | PHP wraps this in BaseStub, not Channel directly |
| Persistent channel cache | ✅ SHA1-keyed hashmap, ref-counted | ✅ `ChannelCache` with LRU eviction | PHP: implicit; Python: explicit via `simple_stubs` |
| `force_new` / bypass cache | ✅ `'force_new' => true` option | ✅ Each channel() call creates new | |
| Subscribe to state changes | ❌ | ✅ `channel.subscribe(callback)` | PHP has `watchConnectivityState` but no callback |
| xDS channel (`xds:///`) | ⚠️ Works if `xds_channel_credentials` used | ✅ | Credentials exist but no docs/helpers |

### Stub / Callables

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Unary-Unary | ✅ `UnaryCall` | ✅ `UnaryUnaryMultiCallable` | |
| Unary-Stream | ✅ `ServerStreamingCall` | ✅ `UnaryStreamMultiCallable` | |
| Stream-Unary | ✅ `ClientStreamingCall` | ✅ `StreamUnaryMultiCallable` | |
| Stream-Stream | ✅ `BidiStreamingCall` | ✅ `StreamStreamMultiCallable` | |
| `timeout` per call | ✅ `$options['timeout']` (microseconds) | ✅ `timeout=` param (seconds float) | PHP uses microseconds; Python uses seconds — easy foot-gun |
| `metadata` per call | ✅ `$metadata` array param | ✅ `metadata=` param | |
| `credentials` per call | ✅ `$options['call_credentials_callback']` | ✅ `credentials=` param | PHP uses callback; Python takes CallCredentials object |
| `wait_for_ready` per call | ❌ | ✅ `wait_for_ready=True` | Not exposed in PHP call options |
| `compression` per call | ❌ (only channel-level default) | ✅ `compression=` param | |
| `future()` / async result | ❌ | ✅ `.future()` returns Future | PHP is always synchronous |
| `.with_call()` | ❌ | ✅ Returns (response, call_object) | |
| `cancel()` on in-flight call | ✅ `$call->cancel()` | ✅ | |
| `getPeer()` | ✅ `$call->getPeer()` | ✅ `call.peer()` | |
| Initial metadata from server | ✅ `$call->getMetadata()` | ✅ `call.initial_metadata()` | |
| Trailing metadata from server | ✅ `$call->getTrailingMetadata()` | ✅ `call.trailing_metadata()` | |
| `code()` / `details()` on call | ✅ Via `$status->code` / `$status->details` in `wait()` return | ✅ `call.code()`, `call.details()` | PHP returns status object, not exception; different model |
| `debug_error_string` | ❌ | ✅ Available on non-OK calls | |

### BaseStub (Generated Stub Base)

| Feature | PHP (`BaseStub`) | Python (`BaseStub` / generated) | Notes |
|---|---|---|---|
| Constructor accepts channel | ✅ | ✅ | |
| Constructor creates channel if not given | ✅ Creates `Channel` from hostname | ✅ | |
| Custom `CallInvoker` injection | ✅ `$opts['call_invoker']` | ❌ Python doesn't have this concept | PHP allows replacing call factory |
| Interceptor injection | ✅ `$opts['interceptors']` | ✅ Via `grpc.intercept_channel()` | |

---

## 3. Server-Side API Comparison

This is the most significant gap between PHP and Python.

### Low-Level Server (`Grpc\Server`) — C Extension

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Create server | ✅ `new Server($options)` | ✅ `grpc.server(pool)` | |
| Add insecure port | ✅ `addHttp2Port($addr)` | ✅ `server.add_insecure_port()` | |
| Add secure port | ✅ `addSecureHttp2Port($addr, $creds)` | ✅ `server.add_secure_port()` | |
| Start server | ✅ `start()` | ✅ `server.start()` | |
| Request next call | ✅ `requestCall()` — blocks, returns event | ✅ Internal; not user-facing | Python's server hides this behind handler dispatch |
| Graceful stop | ❌ No grace period; no `stop()` method on `Grpc\Server` | ✅ `server.stop(grace)` | Critical gap — PHP has no graceful shutdown |
| `wait_for_termination()` | ❌ | ✅ | |
| Thread pool injection | ❌ | ✅ `grpc.server(futures.ThreadPoolExecutor(n))` | PHP server is single-threaded |
| Register handler | ❌ (done manually in app loop) | ✅ `server.add_generic_rpc_handlers()` | |

### High-Level RpcServer (`Grpc\RpcServer`) — **Experimental / Incomplete**

The `RpcServer` class in `lib/Grpc/RpcServer.php` is explicitly marked **experimental** and is **not suitable for production use**.

| Feature | PHP `RpcServer` | Python `grpc.server` | Notes |
|---|---|---|---|
| Register servicer object | ✅ `handle($service)` | ✅ `add_generic_rpc_handlers()` | |
| Dispatch by method name | ✅ `run()` loop dispatches | ✅ | |
| Graceful shutdown | ❌ Run loop has no shutdown signal | ✅ `stop(grace)` + `wait_for_termination()` | |
| Thread pool / concurrent calls | ❌ Single-threaded `run()` loop | ✅ Configurable pool | |
| Server-side interceptors | ❌ | ✅ | |
| Deadline enforcement | ⚠️ Deadline passed to handler via `ServerContext` but not enforced | ✅ Auto-enforced | |
| Max inbound message size enforcement | ❌ | ✅ Via channel args | |
| Production use | ❌ NOT RECOMMENDED | ✅ | |

### ServicerContext

| Feature | PHP `ServerContext` | Python `ServicerContext` | Notes |
|---|---|---|---|
| `clientMetadata()` / `invocation_metadata()` | ✅ | ✅ | |
| `deadline()` / `time_remaining()` | ✅ (absolute) | ✅ (seconds remaining) | Different representation |
| `host()` | ✅ | ✅ `peer()` gives more info | |
| `method()` | ✅ | ✅ | |
| `set_code(code)` | ✅ via `setStatus()` | ✅ | |
| `abort(code, details)` | ❌ (no abort support) | ✅ | |
| `send_initial_metadata()` | ✅ `setInitialMetadata()` | ✅ | |
| `set_trailing_metadata()` | ⚠️ Passed via `ServerCallWriter::finish()` | ✅ `set_trailing_metadata()` | Not an explicit context method in PHP |
| `is_active()` | ❌ | ✅ | |
| `peer_identities()` | ❌ | ✅ | |
| `add_callback(fn)` | ❌ | ✅ | |
| `set_compression()` | ❌ | ✅ | |
| `disable_next_message_compression()` | ❌ | ✅ | |

---

## 4. Credentials and Security

| Feature | PHP | Python | Notes |
|---|---|---|---|
| `createSsl()` / `ssl_channel_credentials()` | ✅ | ✅ | |
| `createInsecure()` / `insecure_channel_credentials()` | ✅ | ✅ | |
| `createDefault()` / Google default creds | ✅ | ✅ (via `google-auth` library) | |
| Metadata plugin / call credentials | ✅ `CallCredentials::createFromPlugin(callable)` | ✅ `grpc.metadata_call_credentials(plugin)` | PHP: callback returns modified array; Python: class with `__call__` |
| Composite channel + call creds | ✅ `ChannelCredentials::createComposite()` | ✅ `composite_channel_credentials()` | |
| Composite call + call creds | ✅ `CallCredentials::createComposite()` | ✅ | |
| **xDS credentials** | ✅ `ChannelCredentials::createXds($fallback)` | ✅ `grpc.xds_channel_credentials()` | Both exist; PHP lacks xDS server credentials |
| **xDS server credentials** | ❌ | ✅ `grpc.xds_server_credentials()` | Missing in PHP |
| Local channel credentials | ❌ | ✅ `grpc.local_channel_credentials()` | Useful for Unix socket + loopback |
| Local server credentials | ❌ | ✅ `grpc.local_server_credentials()` | |
| `setDefaultRootsPem()` | ✅ | ✅ `grpc.experimental.set_root_certificates_location()` | |
| SSL session cache | ❌ | ✅ `grpc.SSLSessionCache(capacity)` | |
| TLS SAN override (for testing) | ✅ `grpc.ssl_target_name_override` channel arg | ✅ Same channel arg | |
| Mutual TLS (client cert) | ✅ `createSsl($root, $key, $cert)` | ✅ Same | |
| Auth metadata plugin context (`service_url`, `method_name`) | ✅ Passed to plugin | ✅ `AuthMetadataContext` object | PHP passes raw array; Python passes typed object |

---

## 5. Interceptors

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Client unary-unary interceptor | ✅ `interceptUnaryUnary()` | ✅ `UnaryUnaryClientInterceptor` | |
| Client unary-stream interceptor | ✅ `interceptUnaryStream()` | ✅ `UnaryStreamClientInterceptor` | |
| Client stream-unary interceptor | ✅ `interceptStreamUnary()` | ✅ `StreamUnaryClientInterceptor` | |
| Client stream-stream interceptor | ✅ `interceptStreamStream()` | ✅ `StreamStreamClientInterceptor` | |
| Client interceptor chaining | ✅ `Interceptor::intercept($channel, [$a, $b, $c])` | ✅ `grpc.intercept_channel($ch, *interceptors)` | |
| **Server-side interceptors** | ❌ | ✅ `ServerInterceptor` | Critical production gap |
| Mutable call details in interceptor | ⚠️ Limited — method and args are passed but not a mutable `ClientCallDetails` object | ✅ `ClientCallDetails` object is mutable | PHP interceptors can return modified args, but no typed object |
| Per-message stream interceptor | ❌ | ❌ Python also lacks this at high level | Neither supports message-level stream interception |
| Async interceptors | ❌ | ✅ `grpc.aio` interceptors | N/A for PHP (no async) |

---

## 6. Metadata Handling

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Send request metadata | ✅ `$metadata` param on all call types | ✅ `metadata=` param | |
| Receive response initial metadata | ✅ `$call->getMetadata()` | ✅ `call.initial_metadata()` | |
| Receive trailing metadata | ✅ `$call->getTrailingMetadata()` | ✅ `call.trailing_metadata()` | |
| Binary metadata (keys ending `-bin`) | ✅ Handled in C extension | ✅ | Both handle it; PHP does not document the rule |
| Metadata key validation | ⚠️ Normalized to lowercase, no strict validation | ✅ Strict lowercase + charset enforcement | |
| `grpc-` prefix reserved keys | ⚠️ Not enforced in PHP layer | ✅ Documented, partially enforced | |

---

## 7. Error Handling and Status Codes

| Feature | PHP | Python | Notes |
|---|---|---|---|
| All 17 status codes as constants | ✅ `Grpc\STATUS_*` constants | ✅ `grpc.StatusCode` enum | PHP: integer constants; Python: typed enum |
| `Status` helper object | ✅ `Grpc\Status::status($code, $details, $metadata)` | ✅ `grpc.Status` | PHP has a helper class; Python has a named tuple |
| Exception on RPC failure | ❌ `wait()` returns `[$response, $status]` and `$status->code !== STATUS_OK` | ✅ `RpcError` exception raised | **Different error model** — PHP does NOT throw on RPC failure |
| `RpcError` with `code()` / `details()` | ❌ No exception class | ✅ | PHP user must check status manually |
| Status code as typed enum | ❌ Raw integer constants | ✅ `grpc.StatusCode` enum with `.value` and `.name` | |
| `debug_error_string` | ❌ | ✅ Available on call object | |
| Server `abort(code, details)` | ❌ | ✅ `context.abort()` | PHP has no way to immediately abort from handler |

### Error Model Difference

This is one of the most significant API design differences. Python raises `RpcError`:

```python
try:
    response = stub.MyMethod(request)
except grpc.RpcError as e:
    print(e.code(), e.details())
```

PHP returns a two-element tuple and requires manual checking:

```php
[$response, $status] = $stub->MyMethod($request)->wait();
if ($status->code !== Grpc\STATUS_OK) {
    echo $status->code . ': ' . $status->details;
}
```

This means PHP code cannot use standard exception-based error handling and every call site must manually check status.

---

## 8. Streaming RPCs

### Client-Side Streaming

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Server streaming — receive messages | ✅ `responses()` generator | ✅ iterator | |
| Client streaming — send messages | ✅ `write($data)` + `wait()` | ✅ Iterator passed to call | Python: pass iterator once; PHP: explicit write loop |
| Bidi streaming — read/write | ✅ `read()` + `write()` + `writesDone()` + `getStatus()` | ✅ Iterator in + iterator out | |
| Half-close (writesDone) | ✅ `writesDone()` | ✅ Iterator exhaustion | |
| Early termination detection | ⚠️ `getStatus()` after last `read()` | ✅ `StopIteration` on iterator | |
| Cancel streaming call | ✅ `$call->cancel()` | ✅ | |
| Flow control (backpressure) | ⚠️ Implicit via blocking calls | ✅ Implicit | |

### Server-Side Streaming (via Experimental RpcServer)

| Feature | PHP | Python | Notes |
|---|---|---|---|
| `ServerCallWriter::write($data)` | ✅ | ✅ `yield` or `context.write()` in aio | |
| `ServerCallReader::read()` | ✅ | ✅ Iteration | |
| `ServerCallWriter::finish($data, $options)` | ✅ | ✅ Handler return | |
| Interleaved read/write in handler | ✅ (manual) | ✅ | |
| Send initial metadata before first message | ✅ `start($data = null)` | ✅ `context.send_initial_metadata()` | |

---

## 9. XDS and Service Mesh

| Feature | PHP | Python | Notes |
|---|---|---|---|
| `xds://` URI scheme resolution | ✅ Passes through to C core | ✅ | |
| `createXds($fallback)` / `xds_channel_credentials()` | ✅ `ChannelCredentials::createXds()` | ✅ | |
| **xDS server credentials** | ❌ | ✅ `grpc.xds_server_credentials()` | PHP cannot run a server in a proxyless mesh |
| xDS bootstrap environment variables | ✅ Inherited from C core (`GRPC_XDS_BOOTSTRAP`) | ✅ | |
| PSM interop test client | ✅ `tests/interop/xds_client.php` | ✅ | |
| PSM interop test server | ❌ (server is experimental) | ✅ | |
| Ring hash load balancing config | ❌ No typed API; must pass raw service config JSON | ✅ `grpc.experimental.gevent` + service config | |
| Outlier detection config | ❌ | ✅ Via service config | |
| Circuit breaking config | ❌ | ✅ Via service config | |
| Weighted round-robin | ❌ | ✅ Via `lb_policy_name=weighted_round_robin` | |
| LRS (load reporting) | ✅ C core handles automatically | ✅ | Not language-layer, but PHP has no visibility into it |

---

## 10. Load Balancing and Service Config

| Feature | PHP | Python | Notes |
|---|---|---|---|
| `grpc.lb_policy_name` channel arg | ✅ Pass-through via options array | ✅ | Neither language implements LB itself — C core does |
| `grpc.service_config` channel arg (JSON) | ✅ Pass-through | ✅ | |
| Typed helpers for service config | ❌ Raw JSON string | ❌ Also raw JSON | Neither Python nor PHP has typed helpers for service config |
| `round_robin` policy | ✅ Via channel arg | ✅ | |
| `pick_first` policy | ✅ Via channel arg | ✅ | |
| `ring_hash` policy | ✅ Via channel arg | ✅ | |
| `grpclb` (legacy) | ✅ | ✅ | Deprecated |
| `wait_for_ready` per call | ❌ | ✅ | PHP cannot set this per call |

---

## 11. Retry and Resilience

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Automatic retry via service config | ⚠️ Service config JSON accepted but retry not documented or tested | ✅ Documented; works via `grpc.service_config` | |
| Retry in application code | ✅ Manual with loops | ✅ But usually deferred to C core retry | |
| Hedging policy via service config | ⚠️ Passes to C core but untested | ✅ | |
| `wait_for_ready` | ❌ | ✅ Per-call | |
| Deadline / timeout per call | ✅ `$options['timeout']` in microseconds | ✅ `timeout=` in seconds | Unit mismatch: PHP uses µs, Python uses s |
| `grpc-retry-pushback-ms` trailer | ✅ Handled by C core | ✅ | |

---

## 12. Async and Concurrency

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Async channel / calls | ❌ | ✅ `grpc.aio` | PHP has no async I/O story |
| Async server | ❌ | ✅ `grpc.aio.server()` | |
| Promises / Futures | ❌ | ✅ `call.future()` | |
| Event loop integration | ❌ | ✅ asyncio | |
| Concurrent RPCs in one process | ❌ (must use separate processes or rely on web server) | ✅ Thread pool + async | |
| PHP-FPM concurrency | ✅ (per-process isolation via PHP-FPM) | N/A | PHP's practical concurrency model is multi-process |
| Coroutine support (Swoole, ReactPHP) | ❌ Not supported officially | N/A | Community extensions exist but not in this repo |

**Practical implication:** PHP's recommended deployment is PHP-FPM where each request is handled by an independent process. The persistent channel cache means each worker reuses its connection, so the per-process model is workable for client-only use cases. For server-side, this model is fundamentally incompatible with a single-process gRPC server.

---

## 13. Compression

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Deflate / gzip at channel level | ✅ `grpc.default_compression_algorithm` channel arg (0–3) | ✅ `compression=` param | PHP: integer constant; Python: `grpc.Compression` enum |
| Compression level | ✅ `grpc.default_compression_level` channel arg | ✅ | |
| **Per-call compression** | ❌ | ✅ `compression=grpc.Compression.Gzip` per call | PHP has no per-call override |
| `WRITE_NO_COMPRESS` per-message flag | ✅ Pass in write options | ✅ | |
| Compression algorithm enum / constants | ❌ Raw integers | ✅ `grpc.Compression` enum | PHP has no typed compression API |
| Server-side compression response | ✅ C core negotiates automatically | ✅ | |
| `context.set_compression()` in handler | ❌ | ✅ | |

---

## 14. Observability

| Feature | PHP | Python | Notes |
|---|---|---|---|
| **Channelz service** | ❌ | ✅ `grpcio_channelz` package | No introspection in PHP |
| **OpenTelemetry metrics** | ❌ | ✅ `grpcio_observability` package | |
| **Admin server** | ❌ | ✅ `grpcio_admin` (Channelz + CSDS) | |
| `grpc.grpc_verbosity` INI setting | ✅ C-level logging verbosity | ✅ env `GRPC_VERBOSITY` | PHP exposes via php.ini |
| `grpc.grpc_trace` INI setting | ✅ Trace flag string | ✅ env `GRPC_TRACE` | |
| `grpc.poll_strategy` INI setting | ✅ `epoll1`, `poll`, `none` | ✅ env `GRPC_POLL_STRATEGY` | |
| Built-in stats per call | ❌ | ✅ Via OpenTelemetry plugin | |
| `getChannelInfo()` | ✅ Returns debug info string | ❌ Not a public Python API | PHP-specific channel info helper |
| `getPersistentList()` | ✅ PHP-specific | ❌ | |

---

## 15. Server Reflection

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Server Reflection service | ❌ | ✅ `grpcio_reflection` package | |
| `grpcurl` / `grpc_cli` compatibility | ❌ | ✅ | Without reflection, tooling is limited |
| Proto descriptor introspection | ❌ | ✅ | |

---

## 16. Health Checking

| Feature | PHP | Python | Notes |
|---|---|---|---|
| gRPC Health Checking Protocol | ❌ | ✅ `grpcio_health_checking` package | |
| `grpc.health.v1.Health/Check` endpoint | ❌ | ✅ | |
| `grpc.health.v1.Health/Watch` endpoint | ❌ | ✅ | |
| Health status management | ❌ | ✅ `health_servicer.set(service, status)` | |

Health checking is important for Kubernetes liveness/readiness probes and load balancer backend health checks. Its absence means PHP gRPC services cannot participate in standard health check infrastructure without a sidecar or custom HTTP endpoint.

---

## 17. Fork Safety

| Feature | PHP | Python | Notes |
|---|---|---|---|
| `pthread_atfork` handlers | ✅ Registered when `grpc.enable_fork_support=1` | ✅ Registered automatically | |
| Child process reinitialization | ✅ `grpc_postfork_child()` called | ✅ | |
| Parent process resume | ✅ `grpc_postfork_parent()` called | ✅ | |
| Persistent channel cleanup on fork | ✅ Channels cleared in child | ✅ | |
| Opt-in via INI setting | ✅ `grpc.enable_fork_support = 1` | ✅ `GRPC_ENABLE_FORK_SUPPORT=1` | PHP uses php.ini; Python uses env var |
| Documented for PHP-FPM (process pool) | ✅ README mentions this | ✅ | Fork safety is more critical for PHP than Python due to FPM |

PHP's fork safety is actually **well-implemented** — arguably better documented for FPM use than Python, since FPM fork is the primary PHP concurrency model.

---

## 18. Connection Management

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Persistent channel cache | ✅ Global hashmap with SHA1 key | ✅ `ChannelCache` in `simple_stubs` | |
| `cleanPersistentList()` | ✅ Static method to purge cache | ⚠️ No direct equivalent | |
| `getPersistentList()` | ✅ Introspect cached channels | ❌ | PHP-specific debugging aid |
| LRU eviction | ❌ PHP cache has no eviction; entries live until `cleanPersistentList()` | ✅ 10-min idle eviction | |
| Max channel pool size | ❌ | ✅ 256 default | |
| Thread-safe cache | ✅ Mutex-protected hashmap | ✅ `threading.Lock` | |
| `grpc_target_persist_bound` | ✅ Connection pool limit per target | ❌ Not exposed in Python | PHP-specific |
| `wait_for_ready` (channel-level) | ✅ `waitForReady()` in BaseStub | ✅ `channel.wait_for_ready()` | |

---

## 19. Code Generation

| Feature | PHP | Python | Notes |
|---|---|---|---|
| Protoc plugin (`grpc_php_plugin`) | ✅ `src/compiler/php_plugin.cc` | ✅ `grpc_python_plugin` | |
| Client stub generation | ✅ Extends `BaseStub` | ✅ Extends channel callables | |
| Server abstract class generation | ✅ Interface with abstract methods | ✅ Servicer base class | |
| Registration helper generation | ✅ `add_*_to_server()` function | ✅ Same pattern | |
| Full method name format | ✅ `/package.Service/Method` | ✅ | |
| PHP 7 namespace support | ✅ | N/A | |
| Message classes | ✅ Via `google/protobuf` PHP package | ✅ Via `protobuf` Python package | |
| Bundled protoc (`grpcio_tools`) | ❌ Must install `protoc` separately | ✅ `python -m grpc_tools.protoc` | |
| Proto file → PHP: request streaming | ✅ `method.client_streaming()` detected | ✅ | |
| Proto file → PHP: response streaming | ✅ `method.server_streaming()` detected | ✅ | |

---

## 20. Build System and Packaging

| Feature | PHP | Python | Notes |
|---|---|---|---|
| PHP extension (`.so`) | ✅ Autoconf + phpize | N/A | |
| Composer package | ✅ `grpc/grpc` on Packagist | ✅ `grpcio` on PyPI | |
| PECL install | ✅ `pecl install grpc` | N/A | |
| Bundled C core | ✅ (when building from source) | ✅ | |
| Dynamic link to system libgrpc | ✅ `--enable-grpc=/path` | ✅ | |
| Pre-built binaries | ✅ Via PECL binary distributions | ✅ PyPI wheels | |
| Windows support | ⚠️ Limited; documented as experimental | ✅ Wheels available | |
| Cross-compilation | ❌ Not documented | ✅ `GRPC_PYTHON_OVERRIDE_EXT_SUFFIX` | |
| PHP version requirement | PHP 7.1+ | Python 3.8+ | |
| C standard | C11 | C++17 (core), C++14 (Cython layer) | |
| php.ini configuration | ✅ 4 INI settings | ❌ Python uses env vars | |

---

## 21. Testing Coverage

### Existing PHP Test Coverage

| Test File | What It Covers |
|---|---|
| `CallTest.php` | Low-level `Grpc\Call` batch operations, metadata |
| `ChannelTest.php` | Channel creation, state machine, persistence, cleanup, xDS credentials |
| `ChannelCredentialsTest.php` | SSL credential creation |
| `CallCredentialsTest.php` | Plugin-based auth metadata |
| `EndToEndTest.php` | Full unary + streaming round-trips |
| `SecureEndToEndTest.php` | TLS channel tests |
| `InterceptorTest.php` | Client interceptor chain |
| `RpcServerTest.php` | Experimental server |
| `ServerCallTest.php` | Server request/response flow |
| `TimevalTest.php` | Deadline arithmetic |

### Missing Test Coverage

| Area | Status |
|---|---|
| Server-side interceptors | ❌ Not implemented |
| Per-call compression | ❌ Not implemented |
| XDS full integration | ❌ No test server |
| Health checking | ❌ Not implemented |
| Channelz | ❌ Not implemented |
| Retry policy via service config | ❌ Untested |
| Hedging policy | ❌ Untested |
| `wait_for_ready` per call | ❌ Not implemented |
| Fork safety end-to-end with FPM | ⚠️ Manual testing only |
| Connection pool eviction | ❌ No eviction logic |
| Async / concurrent calls | ❌ Not applicable to PHP |

---

## 22. Gap Priority Matrix

Gaps ranked by impact on production readiness.

### Critical (Blocks Production Use)

| Gap | Impact | Effort to Fix |
|---|---|---|
| **RpcServer not production-ready** | Cannot run a gRPC server in PHP safely | High — needs threading model design |
| **No graceful server shutdown** | Running server cannot drain in-flight RPCs | Medium — add `stop(grace)` to `Grpc\Server` |
| **No exception-based error handling** | Every call site must check status manually; breaks standard PHP patterns | Medium — wrap `wait()` to throw on non-OK |
| **No `abort()` on ServicerContext** | Server handler cannot immediately terminate with error status | Low-Medium — add to `ServerContext` |
| **No server-side interceptors** | Cannot centralize auth, logging, rate-limiting | High — requires server rework |

### High Priority (Significant Feature Gaps)

| Gap | Impact | Effort to Fix |
|---|---|---|
| **No health checking** | Cannot use standard K8s probes or LB health checks | Medium — implement Health proto service |
| **No Channelz** | No runtime introspection; hard to debug in production | Medium — implement Channelz proto service |
| **No server reflection** | `grpcurl` / `grpc_cli` don't work without proto files | Medium — implement Reflection proto service |
| **No per-call compression** | Cannot override compression per RPC | Low — add `compression` to call options |
| **No `wait_for_ready` per call** | Calls fail immediately if channel not READY | Low — add to call options |
| **xDS server credentials missing** | PHP server cannot participate in proxyless mesh | Medium — wrap `grpc_xds_server_credentials_create()` |
| **Connection pool no eviction** | Persistent cache leaks channels for stale targets | Medium — add TTL eviction |

### Medium Priority (Quality of Life)

| Gap | Impact | Effort to Fix |
|---|---|---|
| **Timeout unit inconsistency (µs vs s)** | Easy foot-gun vs Python and Go conventions | Low — add seconds-based helper |
| **Status codes as integers, not enum** | Less type-safe; no `.name` property | Low — add `StatusCode` class |
| **No typed `ClientCallDetails`** | Interceptor API less ergonomic | Low — add class |
| **No `debug_error_string` on calls** | Less diagnostic info | Low — expose from C extension |
| **No OpenTelemetry metrics** | Cannot observe RPC metrics natively | High effort — requires observability API hookup |
| **Bundled `protoc` / grpcio_tools equivalent** | Friction in build pipelines | Medium — packaging decision |
| **No SSL session cache** | Extra TLS handshake overhead per reconnect | Low — wrap `grpc_ssl_session_cache_create()` |

### Low Priority (Nice to Have)

| Gap | Impact | Effort to Fix |
|---|---|---|
| Async / event-loop support | Not idiomatic in PHP; limited demand | Very High |
| Custom name resolvers | Advanced use case | Medium |
| Typed compression enum | Minor ergonomics | Low |
| Admin server bundle | Nice observability package | Medium |
| Typed service config helpers | Reduces JSON errors | Medium |

---

## Summary Table

| Category | PHP Status | Python Status |
|---|---|---|
| Client API (stubs, 4 call types) | ✅ Complete | ✅ Complete |
| Channel management | ✅ Good | ✅ Complete |
| TLS / SSL credentials | ✅ Good | ✅ Complete |
| Call credentials / auth plugin | ✅ Good | ✅ Complete |
| Client interceptors | ✅ Implemented | ✅ Complete |
| Metadata handling | ✅ Works | ✅ Complete |
| Status codes | ⚠️ Integer constants only | ✅ Typed enum |
| Error model | ⚠️ Manual check (no exceptions) | ✅ `RpcError` exception |
| **Production server** | ❌ Experimental only | ✅ Production-ready |
| **Server interceptors** | ❌ Missing | ✅ Implemented |
| **Graceful server shutdown** | ❌ Missing | ✅ `stop(grace)` |
| Streaming (all 4 types) | ✅ Implemented | ✅ Complete |
| Compression | ⚠️ Channel-level only | ✅ Per-call support |
| **Async / non-blocking** | ❌ Not applicable | ✅ Full `grpc.aio` |
| XDS client credentials | ✅ `createXds()` | ✅ | 
| **XDS server credentials** | ❌ Missing | ✅ |
| Load balancing | ✅ Via channel args | ✅ Via channel args |
| Service config / retry | ⚠️ Pass-through, untested | ✅ Documented + tested |
| `wait_for_ready` per call | ❌ Missing | ✅ |
| **Health checking** | ❌ Missing | ✅ `grpcio_health_checking` |
| **Server reflection** | ❌ Missing | ✅ `grpcio_reflection` |
| **Channelz** | ❌ Missing | ✅ `grpcio_channelz` |
| **OpenTelemetry metrics** | ❌ Missing | ✅ `grpcio_observability` |
| Fork safety | ✅ Good (critical for FPM) | ✅ |
| Persistent channel cache | ✅ Implemented | ✅ (simple_stubs) |
| Code generation plugin | ✅ `grpc_php_plugin` | ✅ |
| Bundled protoc | ❌ | ✅ `grpcio_tools` |
| Testing coverage | ⚠️ Unit + basic E2E | ✅ Unit + interop + async |
