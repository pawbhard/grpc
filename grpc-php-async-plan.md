# gRPC PHP — Async API Design Plan

**PHP baseline: 8.5 (both sync and async)**
**New namespace: `Grpc\Async`**
**Constraint: Zero changes to existing `Grpc\*` sync behavior**

---

## Table of Contents

1. [Design Principles](#1-design-principles)
2. [PHP 8.5 Baseline — What This Unlocks](#2-php-85-baseline--what-this-unlocks)
3. [Async Primitive Options](#3-async-primitive-options)
4. [Architecture Overview](#4-architecture-overview)
5. [C Extension Changes](#5-c-extension-changes)
6. [Core Types and Enums](#6-core-types-and-enums)
7. [Async Channel](#7-async-channel)
8. [Async Client Calls](#8-async-client-calls)
9. [AsyncBaseStub and Generated Stubs](#9-asyncbasestub-and-generated-stubs)
10. [Async Server](#10-async-server)
11. [Error Handling](#11-error-handling)
12. [Interceptors](#12-interceptors)
13. [Event Loop Driver Interface](#13-event-loop-driver-interface)
14. [Code Generation](#14-code-generation)
15. [Implementation Phases](#15-implementation-phases)
16. [Testing Plan](#16-testing-plan)
17. [File Map](#17-file-map)

---

## 1. Design Principles

### What We Are Building

A first-class async API for gRPC PHP using **PHP 8.5 as the minimum version**. The sync API (`Grpc\Channel`, `Grpc\Call`, `Grpc\BaseStub`, etc.) is untouched. Users opt into async by using the `Grpc\Async` namespace.

### Core Decisions

**1. PHP Fibers are the concurrency primitive.**
Fibers (PHP 8.1+) are stackful coroutines — they can suspend from anywhere in the call stack, unlike generators. No external event loop library is required. An optional driver interface allows swapping in Revolt/Amp (§13).

**2. `await()` returns the response directly and throws on error.**
The sync API returns `[$response, $status]` and requires manual status checking. The async API returns the response object directly and throws `Grpc\Async\RpcException` on non-OK status. This matches PHP conventions and makes error handling correct by default.

**3. All async code must run inside `Grpc\Async\run()`.**
The event loop is explicit. Calling async methods outside `run()` throws a clear error.

**4. No shared state between sync and async channels.**
A `Grpc\Async\Channel` is a distinct object from `Grpc\Channel`. They do not share a completion queue.

**5. The async API is marked `@api` from day one.**
Unlike "experimental" labels that create uncertainty, this is a deliberate design with a clear promotion path. The namespace `Grpc\Async` signals intent without hedging.

**6. Generated stubs have an `Async` suffix.**
`GreeterClient` (sync, unchanged) and `GreeterAsyncClient` (new). Both can coexist in the same project.

---

## 2. PHP 8.5 Baseline — What This Unlocks

Requiring PHP 8.5 for the entire codebase (sync + async) allows us to use every modern PHP feature without version guards.

### Features Used in This Design

| Feature | PHP Version | Used For |
|---|---|---|
| Fibers | 8.1 | Core async concurrency primitive |
| Enums | 8.1 | `StatusCode`, `ConnectivityState`, `Compression` |
| Readonly properties | 8.1 | Immutable value objects |
| First-class callables | 8.1 | `MyMessage::decode(...)` in stubs |
| `never` return type | 8.1 | `abort(): never` on context |
| Intersection types | 8.1 | `Channel & Closeable` constraints |
| `#[Attribute]` | 8.0 | `#[Experimental]`, `#[Pure]` annotations |
| Named arguments | 8.0 | Cleaner constructor call sites |
| Match expression | 8.0 | Status code mapping |
| `readonly` classes | 8.2 | `RpcException`, `CallOptions`, event objects |
| Typed class constants | 8.3 | `const int OP_SEND_MESSAGE = ...` |
| `json_validate()` | 8.3 | Service config JSON validation |
| Lazy objects | 8.4 | Deferred channel initialization in stubs |
| Property hooks | 8.4 | `$channel->state { get => ... }` |
| Asymmetric visibility | 8.4 | `public private(set) bool $closed` |

### PHP 8.5 Sync API Improvements (Separate From Async)

The existing sync `Grpc\*` classes gain PHP 8.5 type annotations and enum-based return types as a non-breaking improvement. Concrete changes:

```php
// Old (still works):
$status->code !== \Grpc\STATUS_OK

// New (PHP 8.5 typed):
$status->code !== \Grpc\StatusCode::OK
// or via the enum helper:
$status->statusCode->isOk()
```

The integer `STATUS_*` constants remain registered for backward compatibility. The `StatusCode` enum is additive.

---

## 3. Async Primitive Options

Three options were evaluated. The decision is recorded here permanently.

### Option A — PHP Fibers + Custom Event Loop ✅ Selected

PHP 8.1 Fibers with a lightweight custom event loop. No runtime dependencies.

```
PHP Fiber A ──suspend──► EventLoop ──resume──► PHP Fiber A
PHP Fiber B ──suspend──► EventLoop ──resume──► PHP Fiber B
                              │
                    grpc_async_poll() ◄── C extension
                    (zero-timeout CQ drain)
```

**Why selected:**
- Zero external dependencies — installs everywhere
- Fibers are stackful: `$call->await()` can suspend from any call depth
- Full control over event loop behavior (polling frequency, shutdown)
- Simpler debugging — single PHP thread, deterministic execution order
- Driver interface (§13) allows opting into Revolt later without API changes

**Constraint:** All async code must run inside `Grpc\Async\run()`.

### Option B — Revolt Event Loop (Optional Driver)

`revolt/event-loop` is the PHP async ecosystem standard used by Amp v3 and ReactPHP v3. gRPC async calls become first-class citizens in the same loop as HTTP clients and database drivers.

**Not selected as default**, but fully supported via the `RevoltDriver` (§13). Install `grpc/async-revolt` alongside the core package to enable.

```php
// Same user API, different driver
Grpc\Async\EventLoop::setDriver(new Grpc\Async\Driver\RevoltDriver());
Grpc\Async\run(function(): void { ... }); // now uses Revolt internally
```

### Option C — Swoole Coroutines (Separate Package)

Swoole's C-level coroutine scheduler is faster than PHP Fibers but is a separate PHP extension and incompatible with Fibers. If demand exists, this ships as `grpc/async-swoole` with its own `SwooleChannel` and `SwooleBaseStub` classes that share none of the Fiber code path.

**Not included in this plan.** Tracked separately.

---

## 4. Architecture Overview

### Layer Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│  User Code: Grpc\Async\run(function(): void { ... })             │
├──────────────────────────────────────────────────────────────────┤
│  Grpc\Async\EventLoop        — tick loop, Fiber registry         │
│  Grpc\Async\Channel          — async-capable channel             │
│  Grpc\Async\Call\*           — UnaryCall, StreamingCall, etc.    │
│  Grpc\Async\AsyncBaseStub    — base for generated async stubs    │
│  Grpc\Async\Server\*         — async server, ServicerContext     │
├──────────────────────────────────────────────────────────────────┤
│  C Extension (additive only)                                     │
│  ext/grpc/async_completion_queue.c  — second CQ for_next         │
│  Call::startBatchAsync(int $id, array $batch) — non-blocking     │
│  grpc_async_poll() / grpc_async_poll_blocking(int $µs)           │
├──────────────────────────────────────────────────────────────────┤
│  gRPC C Core (unchanged) — HTTP/2, TLS, LB, retries             │
└──────────────────────────────────────────────────────────────────┘
```

### Why a Second Completion Queue

The sync API uses a single global `grpc_completion_queue_create_for_pluck()`. Each call blocks the thread on `grpc_completion_queue_pluck(queue, tag, gpr_inf_future, NULL)`.

Async uses a **separate** `grpc_completion_queue_create_for_next()`. Operations are submitted non-blocking (`grpc_call_start_batch` returns immediately). The event loop calls `grpc_completion_queue_next(queue, zero_timeout, NULL)` to drain completed events on each tick.

```
Sync path:  startBatch() → pluck(inf) → BLOCKS thread
Async path: startBatchAsync(id, batch) → next(0) on tick → Fiber resumes
```

The two queues are fully independent. Sync and async calls can run in the same PHP process without interfering.

### Fiber Lifecycle for One RPC

```
run() creates root Fiber
  │
  ├─ root Fiber: $call = $stub->SayHello($request)
  │              ↓ startBatchAsync(fiber_id=7, recv_ops)
  │              ↓ EventLoop::get()->park(fiber_id=7, currentFiber)
  │              ↓ Fiber::suspend()   ← yields back to EventLoop
  │
  EventLoop tick: grpc_async_poll() returns event{fiber_id=7, ...}
  │
  ├─ EventLoop: resume(fiber_id=7, event)
  │
  root Fiber: resumes with $event
              $response = deserialize($event->message)
              // continues normally
```

---

## 5. C Extension Changes

All changes are **strictly additive**. No existing function signature, behavior, or constant is modified.

### New File: `ext/grpc/async_completion_queue.h`

```c
#ifndef GRPC_PHP_ASYNC_CQ_H
#define GRPC_PHP_ASYNC_CQ_H

#include "grpc/grpc.h"

/* The async completion queue. Separate from the sync pluck queue. */
extern grpc_completion_queue *grpc_php_async_cq;

void grpc_php_init_async_cq(void);
void grpc_php_shutdown_async_cq(void);

/*
 * Non-blocking poll: checks for one completed event.
 * Returns 1 and fills *ev if an event was ready; returns 0 if queue empty.
 */
int grpc_php_async_poll(grpc_event *ev);

/*
 * Blocking poll: waits up to timeout_us microseconds for one event.
 * Returns 1 and fills *ev if an event arrived; 0 on timeout.
 */
int grpc_php_async_poll_blocking(grpc_event *ev, int64_t timeout_us);

#endif
```

### New File: `ext/grpc/async_completion_queue.c`

```c
#include "async_completion_queue.h"
#include "grpc/support/time.h"

grpc_completion_queue *grpc_php_async_cq = NULL;

void grpc_php_init_async_cq(void) {
    grpc_php_async_cq = grpc_completion_queue_create_for_next(NULL);
}

void grpc_php_shutdown_async_cq(void) {
    grpc_completion_queue_shutdown(grpc_php_async_cq);
    grpc_event ev;
    do {
        ev = grpc_completion_queue_next(
            grpc_php_async_cq,
            gpr_inf_future(GPR_CLOCK_REALTIME),
            NULL);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(grpc_php_async_cq);
    grpc_php_async_cq = NULL;
}

int grpc_php_async_poll(grpc_event *ev) {
    gpr_timespec zero = gpr_time_0(GPR_CLOCK_REALTIME);
    *ev = grpc_completion_queue_next(grpc_php_async_cq, zero, NULL);
    return ev->type != GRPC_QUEUE_TIMEOUT;
}

int grpc_php_async_poll_blocking(grpc_event *ev, int64_t timeout_us) {
    gpr_timespec deadline = gpr_time_add(
        gpr_now(GPR_CLOCK_REALTIME),
        gpr_time_from_micros(timeout_us, GPR_TIMESPAN));
    *ev = grpc_completion_queue_next(grpc_php_async_cq, deadline, NULL);
    return ev->type != GRPC_QUEUE_TIMEOUT;
}
```

### Changes to `ext/grpc/call.c`

One new PHP method. The existing `startBatch` is untouched.

```c
/*
 * Call::startBatchAsync(int $fiberId, array $batch): true
 *
 * Submits a batch of gRPC operations against the async completion queue.
 * Returns immediately (non-blocking). The EventLoop will call grpc_async_poll()
 * and resume the Fiber identified by $fiberId when the operations complete.
 *
 * $fiberId: integer assigned by EventLoop::allocateId(). Used as the
 *           completion tag so the event loop can route the result.
 * $batch:   same format as startBatch().
 */
PHP_METHOD(Call, startBatchAsync) {
    zend_long fiber_id;
    zval *batch_input;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(fiber_id)
        Z_PARAM_ARRAY(batch_input)
    ZEND_PARSE_PARAMETERS_END();

    wrapped_grpc_call *call = Z_WRAPPED_GRPC_CALL_P(getThis());

    if (call->wrapped == NULL) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
            "Cannot startBatchAsync: call has been closed.", 0);
        RETURN_THROWS();
    }

    grpc_op ops[8];
    size_t op_count = 0;

    /* Build ops — extracted into shared helper to avoid duplication with startBatch */
    if (grpc_php_build_ops(batch_input, ops, &op_count) != SUCCESS) {
        RETURN_THROWS(); /* exception already thrown in build_ops */
    }

    void *tag = (void *)(uintptr_t)(zend_ulong)fiber_id;

    grpc_call_error err = grpc_call_start_batch(
        call->wrapped, ops, op_count, tag, NULL);

    if (err != GRPC_CALL_OK) {
        zend_throw_exception_ex(spl_ce_RuntimeException, (long)err,
            "grpc_call_start_batch failed with error %d", (int)err);
        RETURN_THROWS();
    }

    RETURN_TRUE;
}
```

### New PHP-Exposed Functions in `ext/grpc/php_grpc.c`

```c
/*
 * grpc_async_poll(): object|null
 * Non-blocking. Returns a completed event object or null.
 */
PHP_FUNCTION(grpc_async_poll) {
    ZEND_PARSE_PARAMETERS_NONE();
    grpc_event ev;
    if (!grpc_php_async_poll(&ev)) {
        RETURN_NULL();
    }
    grpc_php_event_to_zval(&ev, return_value);
}

/*
 * grpc_async_poll_blocking(int $timeoutMicros): object|null
 * Blocks up to $timeoutMicros. Returns event or null on timeout.
 */
PHP_FUNCTION(grpc_async_poll_blocking) {
    zend_long timeout_us;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(timeout_us)
    ZEND_PARSE_PARAMETERS_END();

    grpc_event ev;
    if (!grpc_php_async_poll_blocking(&ev, (int64_t)timeout_us)) {
        RETURN_NULL();
    }
    grpc_php_event_to_zval(&ev, return_value);
}
```

### Changes to `ext/grpc/php_grpc.c` Init/Shutdown

```c
PHP_MINIT_FUNCTION(grpc) {
    /* ... existing init ... */
    grpc_php_init_completion_queue(TSRMLS_C);  /* existing sync CQ */
    grpc_php_init_async_cq();                  /* NEW async CQ */
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(grpc) {
    grpc_php_shutdown_async_cq();              /* NEW */
    grpc_php_shutdown_completion_queue(TSRMLS_C);
    grpc_shutdown();
    return SUCCESS;
}
```

### Summary of All C Changes

| File | Type | What |
|---|---|---|
| `async_completion_queue.h` | **New** | Async CQ header |
| `async_completion_queue.c` | **New** | `create_for_next`, zero/blocking poll |
| `call.c` | Additive | `startBatchAsync(int $id, array $batch)` |
| `call.h` | Additive | Arginfo for `startBatchAsync` |
| `php_grpc.c` | Additive | Init/shutdown async CQ; `grpc_async_poll`, `grpc_async_poll_blocking` |

---

## 6. Core Types and Enums

These live in `Grpc\` (not `Grpc\Async\`) because they are shared between sync and async, and are additive to the existing API.

### `Grpc\StatusCode` Enum

```php
<?php
declare(strict_types=1);

namespace Grpc;

enum StatusCode: int
{
    case OK                  = 0;
    case CANCELLED           = 1;
    case UNKNOWN             = 2;
    case INVALID_ARGUMENT    = 3;
    case DEADLINE_EXCEEDED   = 4;
    case NOT_FOUND           = 5;
    case ALREADY_EXISTS      = 6;
    case PERMISSION_DENIED   = 7;
    case RESOURCE_EXHAUSTED  = 8;
    case FAILED_PRECONDITION = 9;
    case ABORTED             = 10;
    case OUT_OF_RANGE        = 11;
    case UNIMPLEMENTED       = 12;
    case INTERNAL            = 13;
    case UNAVAILABLE         = 14;
    case DATA_LOSS           = 15;
    case UNAUTHENTICATED     = 16;

    public function isOk(): bool
    {
        return $this === self::OK;
    }

    public function isRetryable(): bool
    {
        return match ($this) {
            self::UNAVAILABLE, self::RESOURCE_EXHAUSTED => true,
            default => false,
        };
    }

    public static function fromInt(int $code): self
    {
        return self::from($code);
    }
}
```

### `Grpc\ConnectivityState` Enum

```php
<?php
declare(strict_types=1);

namespace Grpc;

enum ConnectivityState: int
{
    case IDLE              = 0;
    case CONNECTING        = 1;
    case READY             = 2;
    case TRANSIENT_FAILURE = 3;
    case SHUTDOWN          = 4;
}
```

### `Grpc\Compression` Enum

```php
<?php
declare(strict_types=1);

namespace Grpc;

enum Compression: int
{
    case NONE    = 0;
    case DEFLATE = 1;
    case GZIP    = 2;
}
```

### `Grpc\CallOptions` Readonly Class

Replaces the untyped `$options` array used throughout the sync API.

```php
<?php
declare(strict_types=1);

namespace Grpc;

readonly class CallOptions
{
    public function __construct(
        public readonly int          $timeoutMicros = 0,        // 0 = no deadline
        public readonly array        $metadata      = [],
        public readonly ?Compression $compression   = null,
        public readonly bool         $waitForReady  = false,
        public readonly mixed        $callCredentialsCallback = null,
    ) {}

    public static function withTimeout(int $microseconds): self
    {
        return new self(timeoutMicros: $microseconds);
    }

    public static function withMetadata(array $metadata): self
    {
        return new self(metadata: $metadata);
    }
}
```

---

## 7. Async Channel

### `Grpc\Async\Channel`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async;

use Grpc\CallOptions;
use Grpc\ConnectivityState;

/**
 * Async-capable gRPC channel.
 *
 * Uses a separate completion queue from the sync \Grpc\Channel so that
 * sync and async calls never interfere. One Channel instance should be
 * shared across all stubs talking to the same target.
 */
final class Channel
{
    private \Grpc\Channel $inner;

    public private(set) bool $closed = false;

    public string $target {
        get => $this->inner->getTarget();
    }

    public ConnectivityState $state {
        get => ConnectivityState::fromInt(
            $this->inner->getConnectivityState(false)
        );
    }

    /**
     * @param string $target      "host:port"
     * @param array  $options     Standard channel options array.
     *                            'credentials' key is required.
     *                            'grpc_async' is injected automatically.
     */
    public function __construct(string $target, array $options = [])
    {
        $this->assertPhpVersion();
        $options['grpc_async'] = true;
        $this->inner = new \Grpc\Channel($target, $options);
    }

    /**
     * Block the current Fiber until the channel reaches READY state
     * or the timeout expires. Other Fibers continue running while waiting.
     *
     * Must be called from inside Grpc\Async\run().
     */
    public function waitForReady(float $timeoutSecs): bool
    {
        if ($this->state === ConnectivityState::READY) {
            return true;
        }

        $this->inner->getConnectivityState(true); // trigger connection attempt

        $deadlineUs  = (int)($timeoutSecs * 1_000_000);
        $pollUs      = max(1_000, (int)($deadlineUs / 20)); // poll ~20 times
        $startUs     = (int)(microtime(true) * 1_000_000);

        while (true) {
            $state = $this->state;
            if ($state === ConnectivityState::READY)          return true;
            if ($state === ConnectivityState::SHUTDOWN)       return false;

            $elapsedUs = (int)(microtime(true) * 1_000_000) - $startUs;
            if ($elapsedUs >= $deadlineUs)                    return false;

            \Fiber::suspend(); // yield to event loop; other Fibers run
            usleep(min($pollUs, $deadlineUs - $elapsedUs));
        }
    }

    public function close(): void
    {
        $this->inner->close();
        $this->closed = true;
    }

    /** @internal */
    public function getInnerChannel(): \Grpc\Channel
    {
        return $this->inner;
    }

    private function assertPhpVersion(): void
    {
        if (PHP_VERSION_ID < 80500) {
            throw new \RuntimeException(
                'Grpc\Async requires PHP 8.5 or later. Running: ' . PHP_VERSION
            );
        }
    }
}
```

---

## 8. Async Client Calls

### EventLoop

```php
<?php
declare(strict_types=1);

namespace Grpc\Async;

/**
 * Drives the async gRPC event loop.
 * One instance per PHP process. Not thread-safe (PHP is single-threaded).
 */
final class EventLoop
{
    private static ?self $instance = null;

    /** @var array<int, \Fiber> */
    private array $parked = [];

    /** @var \SplQueue<\Fiber> */
    private \SplQueue $ready;

    private int $nextId   = 1;
    private bool $running = false;

    private ?EventLoopDriverInterface $driver = null;

    private function __construct()
    {
        $this->ready = new \SplQueue();
    }

    public static function get(): self
    {
        return self::$instance ??= new self();
    }

    public static function setDriver(EventLoopDriverInterface $driver): void
    {
        self::get()->driver = $driver;
    }

    /**
     * Run a coroutine to completion.
     * Blocks the calling PHP thread until all spawned Fibers finish.
     */
    public static function run(callable $coroutine): void
    {
        $loop  = self::get();
        $fiber = new \Fiber($coroutine);
        $loop->ready->enqueue($fiber);

        if ($loop->running) return; // nested run() — already ticking

        $loop->running = true;
        try {
            $loop->driver !== null
                ? $loop->driver->run($loop)
                : $loop->tick();
        } finally {
            $loop->running = false;
        }
    }

    /**
     * Spawn a new concurrent Fiber. Enqueued for the next tick.
     * Call from inside run() to start a parallel coroutine.
     */
    public static function spawn(callable $coroutine): \Fiber
    {
        $fiber = new \Fiber($coroutine);
        self::get()->ready->enqueue($fiber);
        return $fiber;
    }

    /** Allocate a unique ID for tagging one gRPC operation batch. */
    public function allocateId(): int
    {
        return $this->nextId++;
    }

    /**
     * Park the current Fiber until a gRPC event with $id arrives.
     * Called by call objects before suspending.
     */
    public function park(int $id, \Fiber $fiber): void
    {
        $this->parked[$id] = $fiber;
    }

    /** Resume a parked Fiber with $value (the completed event object). */
    public function resume(int $id, mixed $value): void
    {
        if (!isset($this->parked[$id])) return;
        $fiber = $this->parked[$id];
        unset($this->parked[$id]);
        if ($fiber->isSuspended()) {
            $fiber->resume($value);
        }
    }

    public function hasWork(): bool
    {
        return !$this->ready->isEmpty() || !empty($this->parked);
    }

    /**
     * One iteration of the default Fiber event loop.
     * Called repeatedly by tick() until no work remains.
     */
    public function step(): void
    {
        // Phase 1 — run every currently-ready Fiber
        $batch = [];
        while (!$this->ready->isEmpty()) {
            $batch[] = $this->ready->dequeue();
        }
        foreach ($batch as $fiber) {
            if (!$fiber->isStarted())        $fiber->start();
            elseif ($fiber->isSuspended())   $fiber->resume();
        }

        // Phase 2 — drain async CQ (non-blocking)
        $drained = 0;
        while ($drained < 128 && ($event = \grpc_async_poll()) !== null) {
            $this->resume((int)$event->fiber_id, $event);
            $drained++;
        }

        // Phase 3 — if all Fibers are parked, do a brief blocking poll
        //           to avoid spinning the CPU with no events pending
        if ($this->ready->isEmpty() && !empty($this->parked) && $drained === 0) {
            $event = \grpc_async_poll_blocking(500); // 0.5ms
            if ($event !== null) {
                $this->resume((int)$event->fiber_id, $event);
            }
        }
    }

    private function tick(): void
    {
        while ($this->hasWork()) {
            $this->step();
        }
    }
}
```

### `AbstractAsyncCall`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Call;

use Grpc\Async\EventLoop;
use Grpc\Async\RpcException;
use Grpc\CallOptions;
use Grpc\StatusCode;

abstract class AbstractAsyncCall
{
    protected \Grpc\Call $call;
    protected ?array $initialMetadata   = null;
    protected ?array $trailingMetadata  = null;

    public function __construct(
        \Grpc\Channel  $channel,
        string         $method,
        protected readonly mixed $deserialize,
        CallOptions    $options = new CallOptions(),
    ) {
        $deadline = $options->timeoutMicros > 0
            ? \Grpc\Timeval::now()->add(new \Grpc\Timeval($options->timeoutMicros))
            : \Grpc\Timeval::infFuture();

        $this->call = new \Grpc\Call($channel, $method, $deadline);

        if ($options->callCredentialsCallback !== null) {
            $this->call->setCredentials(
                \Grpc\CallCredentials::createFromPlugin($options->callCredentialsCallback)
            );
        }
    }

    /**
     * Submit a batch of gRPC operations and suspend the current Fiber
     * until those operations complete.
     *
     * @throws \RuntimeException if called outside EventLoop::run()
     */
    final protected function submit(array $batch): object
    {
        $fiber = \Fiber::getCurrent() ?? throw new \RuntimeException(
            'Async gRPC operations must be called inside Grpc\Async\run(). ' .
            'Wrap your code: Grpc\Async\run(function(): void { ... });'
        );

        $loop = EventLoop::get();
        $id   = $loop->allocateId();

        $this->call->startBatchAsync($id, $batch);
        $loop->park($id, $fiber);

        return \Fiber::suspend();
    }

    final protected function deserialize(?string $bytes): mixed
    {
        return $bytes !== null ? ($this->deserialize)($bytes) : null;
    }

    final protected function assertOk(object $status): void
    {
        $code = StatusCode::fromInt($status->code);
        if (!$code->isOk()) {
            throw RpcException::fromStatus($status);
        }
    }

    public function initialMetadata(): ?array  { return $this->initialMetadata; }
    public function trailingMetadata(): ?array  { return $this->trailingMetadata; }
    public function peer(): string              { return $this->call->getPeer(); }
    public function cancel(): void              { $this->call->cancel(); }
}
```

### `UnaryCall`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Call;

use Grpc\CallOptions;

/**
 * Async unary RPC: one request → one response.
 *
 * Usage:
 *   $call = $stub->SayHello($request, new CallOptions(timeoutMicros: 5_000_000));
 *   $response = $call->await();              // throws RpcException on error
 *   // or, if you want to inspect status:
 *   [$response, $status] = $call->awaitWithStatus();
 */
final class UnaryCall extends AbstractAsyncCall
{
    /**
     * Await the server response.
     * Suspends the current Fiber; resumes with the deserialized response.
     *
     * @throws RpcException on non-OK status
     */
    public function await(): mixed
    {
        [$response, ] = $this->awaitWithStatus();
        return $response;
    }

    /**
     * Await without throwing. Returns [$response, $status].
     * $response is null if the RPC failed.
     */
    public function awaitWithStatus(): array
    {
        $event = $this->submit([
            \Grpc\OP_RECV_INITIAL_METADATA => true,
            \Grpc\OP_RECV_MESSAGE          => true,
            \Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);

        $this->initialMetadata  = $event->metadata;
        $this->trailingMetadata = $event->status->metadata;

        $response = $this->deserialize($event->message);
        $status   = $event->status;

        if (StatusCode::fromInt($status->code) !== StatusCode::OK) {
            throw RpcException::fromStatus($status);
        }

        return [$response, $status];
    }
}
```

### `ServerStreamingCall`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Call;

/**
 * Async server-streaming RPC: one request → stream of responses.
 *
 * Usage:
 *   $call = $stub->ListFeatures($request);
 *   while (($feature = $call->read()) !== null) {
 *       echo $feature->getName();
 *   }
 *   // $call->status() gives final StatusCode after null read
 */
final class ServerStreamingCall extends AbstractAsyncCall
{
    private ?\Grpc\StatusCode $finalStatus = null;

    public function readInitialMetadata(): array
    {
        $event = $this->submit([
            \Grpc\OP_RECV_INITIAL_METADATA => true,
        ]);
        $this->initialMetadata = $event->metadata;
        return $this->initialMetadata;
    }

    /**
     * Read the next message. Returns null when the stream ends.
     * Suspends the Fiber until the message (or end-of-stream) arrives.
     */
    public function read(): mixed
    {
        $event = $this->submit([
            \Grpc\OP_RECV_MESSAGE => true,
        ]);
        return $this->deserialize($event->message);
    }

    /**
     * Await the final status. Call after read() returns null.
     *
     * @throws RpcException on non-OK status
     */
    public function status(): \Grpc\StatusCode
    {
        $event = $this->submit([
            \Grpc\OP_RECV_STATUS_ON_CLIENT => true,
        ]);
        $this->trailingMetadata = $event->status->metadata;
        $code = StatusCode::fromInt($event->status->code);
        if (!$code->isOk()) {
            throw RpcException::fromStatus($event->status);
        }
        return $code;
    }
}
```

### `ClientStreamingCall`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Call;

/**
 * Async client-streaming RPC: stream of requests → one response.
 *
 * Usage:
 *   $call = $stub->RecordRoute();
 *   foreach ($points as $point) {
 *       $call->write($point);
 *   }
 *   $summary = $call->await();
 */
final class ClientStreamingCall extends AbstractAsyncCall
{
    /**
     * Send one message. Suspends until the send is complete.
     */
    public function write(mixed $message, int $flags = 0): void
    {
        $this->submit([
            \Grpc\OP_SEND_MESSAGE => [
                'message' => $message->serializeToString(),
                'flags'   => $flags,
            ],
        ]);
    }

    /**
     * Close the client stream and await the server response.
     *
     * @throws RpcException on non-OK status
     */
    public function await(): mixed
    {
        [$response, ] = $this->awaitWithStatus();
        return $response;
    }

    public function awaitWithStatus(): array
    {
        $event = $this->submit([
            \Grpc\OP_SEND_CLOSE_FROM_CLIENT => true,
            \Grpc\OP_RECV_INITIAL_METADATA  => true,
            \Grpc\OP_RECV_MESSAGE           => true,
            \Grpc\OP_RECV_STATUS_ON_CLIENT  => true,
        ]);

        $this->initialMetadata  = $event->metadata;
        $this->trailingMetadata = $event->status->metadata;

        $code = StatusCode::fromInt($event->status->code);
        if (!$code->isOk()) {
            throw RpcException::fromStatus($event->status);
        }

        return [$this->deserialize($event->message), $event->status];
    }
}
```

### `BidiStreamingCall`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Call;

/**
 * Async bidirectional streaming RPC.
 *
 * Usage:
 *   $call = $stub->RouteChat();
 *   $call->write($note1);
 *   $reply = $call->read();
 *   $call->writesDone();
 *   while (($msg = $call->read()) !== null) { ... }
 *   $call->status();
 */
final class BidiStreamingCall extends AbstractAsyncCall
{
    /** Send one message. Suspends until sent. */
    public function write(mixed $message, int $flags = 0): void
    {
        $this->submit([
            \Grpc\OP_SEND_MESSAGE => [
                'message' => $message->serializeToString(),
                'flags'   => $flags,
            ],
        ]);
    }

    /** Read one message from the server. Returns null at end of stream. */
    public function read(): mixed
    {
        $event = $this->submit([\Grpc\OP_RECV_MESSAGE => true]);
        return $this->deserialize($event->message);
    }

    /** Signal end of client writes. */
    public function writesDone(): void
    {
        $this->submit([\Grpc\OP_SEND_CLOSE_FROM_CLIENT => true]);
    }

    /**
     * Await the final server status. Call after read() returns null.
     *
     * @throws RpcException on non-OK status
     */
    public function status(): StatusCode
    {
        $event = $this->submit([\Grpc\OP_RECV_STATUS_ON_CLIENT => true]);
        $this->trailingMetadata = $event->status->metadata;
        $code = StatusCode::fromInt($event->status->code);
        if (!$code->isOk()) {
            throw RpcException::fromStatus($event->status);
        }
        return $code;
    }
}
```

---

## 9. AsyncBaseStub and Generated Stubs

### `AsyncBaseStub`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async;

use Grpc\Async\Call\{AbstractAsyncCall, UnaryCall, ServerStreamingCall,
                     ClientStreamingCall, BidiStreamingCall};
use Grpc\CallOptions;

/**
 * Base class for async generated stubs.
 * Extend via the generated *AsyncClient classes from grpc_php_plugin.
 */
abstract class AsyncBaseStub
{
    private readonly Channel $channel;

    public function __construct(
        string   $hostname,
        array    $options  = [],
        ?Channel $channel  = null,
    ) {
        $this->channel = $channel ?? new Channel($hostname, $options);
    }

    final public function getTarget(): string { return $this->channel->target; }
    final public function isClosed(): bool    { return $this->channel->closed; }
    final public function close(): void       { $this->channel->close(); }

    final public function waitForReady(float $timeoutSecs): bool
    {
        return $this->channel->waitForReady($timeoutSecs);
    }

    // ── Factory methods called by generated stub methods ─────────────────

    final protected function unaryCall(
        string      $method,
        mixed       $argument,
        callable    $deserialize,
        CallOptions $options = new CallOptions(),
    ): UnaryCall {
        $call = new UnaryCall(
            $this->channel->getInnerChannel(),
            $method,
            $deserialize,
            $options,
        );
        $call->startSend($argument, $options->metadata);
        return $call;
    }

    final protected function serverStreamingCall(
        string      $method,
        mixed       $argument,
        callable    $deserialize,
        CallOptions $options = new CallOptions(),
    ): ServerStreamingCall {
        $call = new ServerStreamingCall(
            $this->channel->getInnerChannel(),
            $method,
            $deserialize,
            $options,
        );
        $call->startSend($argument, $options->metadata);
        return $call;
    }

    final protected function clientStreamingCall(
        string      $method,
        callable    $deserialize,
        CallOptions $options = new CallOptions(),
    ): ClientStreamingCall {
        $call = new ClientStreamingCall(
            $this->channel->getInnerChannel(),
            $method,
            $deserialize,
            $options,
        );
        $call->startSend(null, $options->metadata);
        return $call;
    }

    final protected function bidiStreamingCall(
        string      $method,
        callable    $deserialize,
        CallOptions $options = new CallOptions(),
    ): BidiStreamingCall {
        $call = new BidiStreamingCall(
            $this->channel->getInnerChannel(),
            $method,
            $deserialize,
            $options,
        );
        $call->startSend(null, $options->metadata);
        return $call;
    }
}
```

### Generated Stub Example

For this proto:
```protobuf
package helloworld;
service Greeter {
    rpc SayHello (HelloRequest)        returns (HelloReply);
    rpc SayHellos (HelloRequest)       returns (stream HelloReply);
    rpc CollectHellos (stream HelloRequest) returns (HelloReply);
    rpc ChatHellos (stream HelloRequest)   returns (stream HelloReply);
}
```

Generated `GreeterAsyncClient.php`:
```php
<?php
// AUTO-GENERATED. DO NOT EDIT.
// Generated by grpc_php_plugin --async

declare(strict_types=1);

namespace Helloworld;

use Grpc\Async\AsyncBaseStub;
use Grpc\Async\Call\{UnaryCall, ServerStreamingCall, ClientStreamingCall, BidiStreamingCall};
use Grpc\CallOptions;

/**
 * Async client for helloworld.Greeter.
 * All methods must be called inside Grpc\Async\run().
 */
final class GreeterAsyncClient extends AsyncBaseStub
{
    /**
     * Unary: send one HelloRequest, receive one HelloReply.
     *
     * @throws \Grpc\Async\RpcException on non-OK status
     */
    public function SayHello(
        HelloRequest $argument,
        CallOptions  $options = new CallOptions(),
    ): UnaryCall {
        return $this->unaryCall(
            method:      '/helloworld.Greeter/SayHello',
            argument:    $argument,
            deserialize: HelloReply::decode(...),
            options:     $options,
        );
    }

    /**
     * Server streaming: one request, stream of HelloReply messages.
     */
    public function SayHellos(
        HelloRequest $argument,
        CallOptions  $options = new CallOptions(),
    ): ServerStreamingCall {
        return $this->serverStreamingCall(
            method:      '/helloworld.Greeter/SayHellos',
            argument:    $argument,
            deserialize: HelloReply::decode(...),
            options:     $options,
        );
    }

    /**
     * Client streaming: stream of HelloRequest messages, one HelloReply.
     */
    public function CollectHellos(
        CallOptions $options = new CallOptions(),
    ): ClientStreamingCall {
        return $this->clientStreamingCall(
            method:      '/helloworld.Greeter/CollectHellos',
            deserialize: HelloReply::decode(...),
            options:     $options,
        );
    }

    /**
     * Bidirectional streaming.
     */
    public function ChatHellos(
        CallOptions $options = new CallOptions(),
    ): BidiStreamingCall {
        return $this->bidiStreamingCall(
            method:      '/helloworld.Greeter/ChatHellos',
            deserialize: HelloReply::decode(...),
            options:     $options,
        );
    }
}
```

---

## 10. Async Server

### `Server`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Server;

use Grpc\Async\EventLoop;
use Grpc\Async\RpcException;

/**
 * Async gRPC server.
 *
 * Each incoming RPC runs in its own Fiber, allowing multiple concurrent
 * requests in a single PHP process. No thread pool required.
 */
final class Server
{
    private readonly \Grpc\Server $inner;

    /** @var array<string, array{object, MethodDescriptor}> */
    private array $handlers = [];

    private bool $started  = false;
    private bool $stopping = false;

    public function __construct(array $options = [])
    {
        $this->inner = new \Grpc\Server($options);
    }

    public function addInsecurePort(string $addr): int
    {
        return $this->inner->addHttp2Port($addr);
    }

    public function addSecurePort(string $addr, \Grpc\ServerCredentials $creds): int
    {
        return $this->inner->addSecureHttp2Port($addr, $creds);
    }

    /**
     * Register a servicer object and its method descriptors.
     *
     * @param MethodDescriptor[] $descriptors
     */
    public function addServicer(object $servicer, array $descriptors): void
    {
        foreach ($descriptors as $descriptor) {
            $this->handlers[$descriptor->fullMethodName] = [$servicer, $descriptor];
        }
    }

    public function start(): void
    {
        if (!$this->started) {
            $this->inner->start();
            $this->started = true;
        }
    }

    /**
     * Enter the serve loop. Blocks until shutdown() is called.
     * Each incoming RPC is dispatched as a spawned Fiber.
     */
    public function serve(): void
    {
        $this->start();

        EventLoop::run(function (): void {
            while (!$this->stopping) {
                $callEvent = $this->inner->requestCall();

                if ($callEvent === false || $callEvent === null) {
                    \Fiber::suspend();
                    continue;
                }

                $method = $callEvent->method;

                if (!isset($this->handlers[$method])) {
                    $this->rejectUnimplemented($callEvent->call);
                    continue;
                }

                [$servicer, $descriptor] = $this->handlers[$method];

                EventLoop::spawn(function () use ($servicer, $descriptor, $callEvent): void {
                    $ctx    = new ServicerContext($callEvent);
                    $reader = new ServerCallReader($callEvent->call, $descriptor->requestDeserializer);
                    $writer = new ServerCallWriter($callEvent->call, $descriptor->responseSerializer, $ctx);

                    try {
                        ($servicer->{$descriptor->methodName})($reader, $writer, $ctx);
                    } catch (AbortException $e) {
                        // abort() was called — status already sent
                    } catch (\Throwable $e) {
                        $writer->abort(\Grpc\StatusCode::INTERNAL, $e->getMessage());
                    }
                });
            }
        });
    }

    /**
     * Signal the serve loop to stop after current RPCs drain.
     *
     * @param float $graceSecs  seconds to wait for in-flight RPCs
     */
    public function shutdown(float $graceSecs = 0.0): void
    {
        $this->stopping = true;
        if ($graceSecs > 0.0) {
            usleep((int)($graceSecs * 1_000_000));
        }
    }

    private function rejectUnimplemented(\Grpc\Call $call): void
    {
        $call->startBatch([
            \Grpc\OP_SEND_INITIAL_METADATA  => [],
            \Grpc\OP_SEND_STATUS_FROM_SERVER => [
                'metadata' => [],
                'code'     => \Grpc\STATUS_UNIMPLEMENTED,
                'details'  => 'Method not implemented',
            ],
        ]);
    }
}
```

### `ServicerContext`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Server;

use Grpc\StatusCode;

final class ServicerContext
{
    public private(set) bool $active = true;

    private array  $initialMetadata = [];
    private StatusCode $statusCode  = StatusCode::OK;
    private string $statusDetails   = '';

    public function __construct(private readonly object $callEvent) {}

    public function clientMetadata(): array    { return $this->callEvent->metadata ?? []; }
    public function deadline(): \Grpc\Timeval  { return $this->callEvent->absolute_deadline; }
    public function host(): string             { return $this->callEvent->host ?? ''; }
    public function method(): string           { return $this->callEvent->method ?? ''; }

    public function setInitialMetadata(array $metadata): void
    {
        $this->initialMetadata = $metadata;
    }

    public function setStatus(StatusCode $code, string $details = ''): void
    {
        $this->statusCode    = $code;
        $this->statusDetails = $details;
    }

    /**
     * Immediately terminate the RPC with an error status.
     * Throws AbortException — caught by Server::serve() dispatch.
     */
    public function abort(StatusCode $code, string $details = ''): never
    {
        $this->active        = false;
        $this->statusCode    = $code;
        $this->statusDetails = $details;
        throw new AbortException($code, $details);
    }

    /** @internal used by ServerCallWriter */
    public function getInitialMetadata(): array  { return $this->initialMetadata; }
    public function getStatusCode(): StatusCode  { return $this->statusCode; }
    public function getStatusDetails(): string   { return $this->statusDetails; }
}
```

### `ServerCallWriter`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Server;

final class ServerCallWriter
{
    private bool $initialMetadataSent = false;

    public function __construct(
        private readonly \Grpc\Call   $call,
        private readonly mixed        $serialize,
        private readonly ServicerContext $ctx,
    ) {}

    /**
     * Send initial (response headers) metadata.
     * Called automatically on first write() if not called explicitly.
     */
    public function sendInitialMetadata(array $metadata = []): void
    {
        if ($this->initialMetadataSent) return;
        $this->ctx->setInitialMetadata($metadata);
        $this->call->startBatch([
            \Grpc\OP_SEND_INITIAL_METADATA => $metadata,
        ]);
        $this->initialMetadataSent = true;
    }

    /**
     * Send one response message to the client.
     * Sends initial metadata first if not already sent.
     */
    public function write(mixed $message): void
    {
        if (!$this->initialMetadataSent) {
            $this->sendInitialMetadata($this->ctx->getInitialMetadata());
        }
        $this->call->startBatch([
            \Grpc\OP_SEND_MESSAGE => [
                'message' => ($this->serialize)($message),
            ],
        ]);
    }

    /**
     * Close the stream with an optional final message and status.
     */
    public function finish(?object $finalMessage = null, array $trailingMetadata = []): void
    {
        if (!$this->initialMetadataSent) {
            $this->sendInitialMetadata($this->ctx->getInitialMetadata());
        }

        $batch = [];
        if ($finalMessage !== null) {
            $batch[\Grpc\OP_SEND_MESSAGE] = [
                'message' => ($this->serialize)($finalMessage),
            ];
        }
        $batch[\Grpc\OP_SEND_STATUS_FROM_SERVER] = [
            'metadata' => $trailingMetadata,
            'code'     => $this->ctx->getStatusCode()->value,
            'details'  => $this->ctx->getStatusDetails(),
        ];
        $batch[\Grpc\OP_RECV_CLOSE_ON_SERVER] = true;

        $this->call->startBatch($batch);
    }

    /** Send error status immediately. */
    public function abort(\Grpc\StatusCode $code, string $details): void
    {
        $this->call->startBatch([
            \Grpc\OP_SEND_INITIAL_METADATA   => [],
            \Grpc\OP_SEND_STATUS_FROM_SERVER  => [
                'metadata' => [],
                'code'     => $code->value,
                'details'  => $details,
            ],
            \Grpc\OP_RECV_CLOSE_ON_SERVER     => true,
        ]);
    }
}
```

### Handler Pattern

```php
<?php
use Grpc\Async;
use Grpc\Async\Server\{Server, ServicerContext, ServerCallReader, ServerCallWriter};
use Grpc\StatusCode;

// ── Servicer ──────────────────────────────────────────────────────────────────

class GreeterServicer
{
    // Unary handler
    public function SayHello(
        ServerCallReader $reader,
        ServerCallWriter $writer,
        ServicerContext  $ctx,
    ): void {
        $request  = $reader->read();
        $response = new HelloReply(message: 'Hello, ' . $request->getName());
        $writer->finish($response);
    }

    // Server streaming handler
    public function SayHellos(
        ServerCallReader $reader,
        ServerCallWriter $writer,
        ServicerContext  $ctx,
    ): void {
        $request = $reader->read();
        foreach (['Hello', 'Hi', 'Greetings'] as $greeting) {
            $writer->write(new HelloReply(message: "$greeting, {$request->getName()}"));
        }
        $writer->finish();
    }

    // Client streaming handler
    public function CollectHellos(
        ServerCallReader $reader,
        ServerCallWriter $writer,
        ServicerContext  $ctx,
    ): void {
        $names = [];
        while (($req = $reader->read()) !== null) {
            $names[] = $req->getName();
        }
        $writer->finish(new HelloReply(message: 'Hello: ' . implode(', ', $names)));
    }

    // Bidirectional streaming
    public function ChatHellos(
        ServerCallReader $reader,
        ServerCallWriter $writer,
        ServicerContext  $ctx,
    ): void {
        while (($req = $reader->read()) !== null) {
            $writer->write(new HelloReply(message: 'Echo: ' . $req->getName()));
        }
        $writer->finish();
    }
}

// ── Bootstrap ─────────────────────────────────────────────────────────────────

Async\run(function (): void {
    $server = new Server();
    $server->addInsecurePort('[::]:50051');
    $server->addServicer(new GreeterServicer(), GreeterMethodDescriptors::all());
    echo "Listening on :50051\n";
    $server->serve();
});
```

---

## 11. Error Handling

### `RpcException`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async;

use Grpc\StatusCode;

readonly class RpcException extends \RuntimeException
{
    public function __construct(
        public readonly StatusCode $statusCode,
        public readonly string     $details          = '',
        public readonly array      $trailingMetadata = [],
        public readonly array      $initialMetadata  = [],
        ?\Throwable                $previous         = null,
    ) {
        parent::__construct($details, $statusCode->value, $previous);
    }

    public static function fromStatus(object $status): self
    {
        return new self(
            statusCode:       StatusCode::fromInt($status->code),
            details:          $status->details ?? '',
            trailingMetadata: $status->metadata ?? [],
        );
    }

    public function isRetryable(): bool
    {
        return $this->statusCode->isRetryable();
    }
}
```

### `AbortException` (internal server use)

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Server;

use Grpc\StatusCode;

/** Thrown by ServicerContext::abort(). Caught by Server dispatch loop. */
final class AbortException extends \RuntimeException
{
    public function __construct(
        public readonly StatusCode $statusCode,
        string $details = '',
    ) {
        parent::__construct($details, $statusCode->value);
    }
}
```

### Error Handling Examples

```php
// ── Unary — exception on error ────────────────────────────────────────────────
use Grpc\Async;
use Grpc\Async\RpcException;
use Grpc\StatusCode;

Async\run(function (): void {
    $stub = new GreeterAsyncClient(
        new Async\Channel('localhost:50051', [
            'credentials' => \Grpc\ChannelCredentials::createInsecure(),
        ])
    );

    try {
        $response = $stub->SayHello(new HelloRequest(name: 'World'))->await();
        echo $response->getMessage();

    } catch (RpcException $e) {
        echo match ($e->statusCode) {
            StatusCode::UNAVAILABLE      => "Server unavailable — retry later",
            StatusCode::DEADLINE_EXCEEDED => "Timed out",
            StatusCode::UNAUTHENTICATED  => "Auth failed",
            default                      => "RPC error {$e->statusCode->name}: {$e->getMessage()}",
        };
    }
});

// ── awaitWithStatus() — no exception ─────────────────────────────────────────
Async\run(function (): void {
    $call = $stub->SayHello(new HelloRequest(name: 'World'));
    try {
        [$response, $status] = $call->awaitWithStatus();
        echo $response->getMessage();
    } catch (RpcException $e) {
        // Still throws — use awaitWithStatus only to access status alongside response
    }

    // To truly suppress: catch and inspect
    try {
        $response = $call->await();
    } catch (RpcException $e) {
        // handle
    }
});

// ── Server abort ─────────────────────────────────────────────────────────────
public function SayHello(
    ServerCallReader $reader,
    ServerCallWriter $writer,
    ServicerContext  $ctx,
): void {
    $req = $reader->read();
    if (empty($req->getName())) {
        $ctx->abort(StatusCode::INVALID_ARGUMENT, 'name must not be empty');
        // abort() throws — code below never runs
    }
    $writer->finish(new HelloReply(message: 'Hello, ' . $req->getName()));
}
```

---

## 12. Interceptors

### `AsyncInterceptor`

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Interceptor;

use Grpc\Async\Call\{UnaryCall, ServerStreamingCall, ClientStreamingCall, BidiStreamingCall};
use Grpc\CallOptions;

/**
 * Base class for async client interceptors.
 * Override the methods you need; defaults pass through unchanged.
 */
abstract class AsyncInterceptor
{
    public function interceptUnaryUnary(
        string      $method,
        mixed       $argument,
        callable    $deserialize,
        CallOptions $options,
        callable    $next,          // next($method, $argument, $deserialize, $options): UnaryCall
    ): UnaryCall {
        return $next($method, $argument, $deserialize, $options);
    }

    public function interceptUnaryStream(
        string      $method,
        mixed       $argument,
        callable    $deserialize,
        CallOptions $options,
        callable    $next,
    ): ServerStreamingCall {
        return $next($method, $argument, $deserialize, $options);
    }

    public function interceptStreamUnary(
        string      $method,
        callable    $deserialize,
        CallOptions $options,
        callable    $next,
    ): ClientStreamingCall {
        return $next($method, $deserialize, $options);
    }

    public function interceptStreamStream(
        string      $method,
        callable    $deserialize,
        CallOptions $options,
        callable    $next,
    ): BidiStreamingCall {
        return $next($method, $deserialize, $options);
    }

    /**
     * Wrap a channel (or stub) with one or more interceptors.
     *
     * @param AsyncInterceptor|AsyncInterceptor[] $interceptors
     */
    public static function intercept(
        Async\AsyncBaseStub|Async\Channel $target,
        self|array $interceptors,
    ): InterceptorChannel {
        $list = is_array($interceptors) ? $interceptors : [$interceptors];
        return new InterceptorChannel($target, $list);
    }
}
```

### Interceptor Usage Examples

```php
// ── Auth token interceptor ───────────────────────────────────────────────────
use Grpc\Async\Interceptor\AsyncInterceptor;
use Grpc\Async\Call\UnaryCall;
use Grpc\CallOptions;

class AuthInterceptor extends AsyncInterceptor
{
    public function __construct(private readonly TokenProvider $tokens) {}

    public function interceptUnaryUnary(
        string $method, mixed $argument, callable $deserialize,
        CallOptions $options, callable $next,
    ): UnaryCall {
        $token   = $this->tokens->get();
        $options = new CallOptions(
            ...(array)$options,
            metadata: array_merge($options->metadata, [
                'authorization' => ["Bearer $token"],
            ]),
        );
        return $next($method, $argument, $deserialize, $options);
    }
}

// ── Latency logging interceptor ───────────────────────────────────────────────
class LatencyInterceptor extends AsyncInterceptor
{
    public function interceptUnaryUnary(
        string $method, mixed $argument, callable $deserialize,
        CallOptions $options, callable $next,
    ): UnaryCall {
        $t0   = hrtime(true);
        $call = $next($method, $argument, $deserialize, $options);

        // Wrap the returned call to inject timing on await()
        return new class($call, $method, $t0) extends UnaryCall {
            public function __construct(
                private readonly UnaryCall $inner,
                private readonly string    $method,
                private readonly int       $startNs,
            ) {}

            public function await(): mixed {
                $result = $this->inner->await();
                $ms = round((hrtime(true) - $this->startNs) / 1e6, 2);
                error_log("[gRPC] {$this->method} {$ms}ms");
                return $result;
            }
        };
    }
}

// ── Wiring interceptors ───────────────────────────────────────────────────────
Async\run(function (): void {
    $channel = new Async\Channel('localhost:50051', [
        'credentials' => \Grpc\ChannelCredentials::createInsecure(),
    ]);

    $stub = AsyncInterceptor::intercept(
        new GreeterAsyncClient($channel),
        [new AuthInterceptor($tokenProvider), new LatencyInterceptor()],
    );

    $response = $stub->SayHello(new HelloRequest(name: 'World'))->await();
    echo $response->getMessage();
});
```

---

## 13. Event Loop Driver Interface

The `EventLoop` delegates to a driver when one is set. This makes the Revolt integration a drop-in without any changes to user code.

### Interface

```php
<?php
declare(strict_types=1);

namespace Grpc\Async;

/**
 * Pluggable event loop driver.
 * Implement this to integrate gRPC async with an external event loop.
 */
interface EventLoopDriverInterface
{
    /**
     * Run the event loop until $loop->hasWork() returns false.
     * Must call $loop->step() on every iteration to drain gRPC events
     * and resume parked Fibers.
     */
    public function run(EventLoop $loop): void;
}
```

### Default Driver (built-in — no dep)

```php
// Already embedded in EventLoop::tick():
private function tick(): void
{
    while ($this->hasWork()) {
        $this->step(); // runs ready Fibers + drains gRPC CQ
    }
}
```

### Revolt Driver (optional package: `grpc/async-revolt`)

```php
<?php
declare(strict_types=1);

namespace Grpc\Async\Driver;

use Grpc\Async\{EventLoop, EventLoopDriverInterface};
use Revolt\EventLoop as RevoltLoop;

final class RevoltDriver implements EventLoopDriverInterface
{
    public function run(EventLoop $loop): void
    {
        // Register a recurring microtask that drains gRPC events and runs Fibers
        $callbackId = RevoltLoop::repeat(0.0, function () use ($loop, &$callbackId): void {
            $loop->step();
            if (!$loop->hasWork()) {
                RevoltLoop::cancel($callbackId);
            }
        });

        RevoltLoop::run();
    }
}
```

Usage:
```php
use Grpc\Async;
use Grpc\Async\Driver\RevoltDriver;

// Set once at bootstrap — affects all subsequent Async\run() calls
Async\EventLoop::setDriver(new RevoltDriver());

// Same API as before — now uses Revolt internally
Async\run(function (): void {
    $channel = new Async\Channel('localhost:50051', [...]);
    $stub    = new GreeterAsyncClient($channel);

    // This Fiber co-runs alongside Amp HTTP requests, DB queries, etc.
    $response = $stub->SayHello(new HelloRequest(name: 'World'))->await();
    echo $response->getMessage();
});
```

---

## 14. Code Generation

### `grpc_php_plugin` New Flag

```bash
protoc \
  --php_out=. \
  --grpc_out=async:. \
  --plugin=protoc-gen-grpc=grpc_php_plugin \
  my_service.proto
```

The `async` flag in `--grpc_out` generates **both** client files:

| Generated File | Base Class | Purpose |
|---|---|---|
| `MyServiceClient.php` | `\Grpc\BaseStub` | Existing sync client (always generated) |
| `MyServiceAsyncClient.php` | `\Grpc\Async\AsyncBaseStub` | New async client |

The server interface (abstract class) and the `add_*_to_server()` helper function for the sync server are unchanged. A future iteration will generate async server helpers.

### Changes to `src/compiler/php_generator.cc`

```cpp
// Existing: GenerateStubFile() — unchanged
// New function:
void GenerateAsyncStubFile(
    grpc_generator::File *file,
    grpc_generator::GeneratorContext *context,
    const std::string &parameter) {

    // Produces GreeterAsyncClient.php extending AsyncBaseStub
    // Method bodies use $this->unaryCall(...) / serverStreamingCall() etc.
    // Return types: UnaryCall, ServerStreamingCall, etc.
    // Deserializer: first-class callable MyResponse::decode(...)
    // CallOptions: named argument syntax
    // Class docblock: @generated, @api
}

// In GenerateFile(), add:
if (GetBoolParameter(parameter, "async")) {
    GenerateAsyncStubFile(file, context, parameter);
}
```

---

## 15. Implementation Phases

### Phase 1 — C Extension Foundation (Weeks 1–3)

**Goal:** Non-blocking `startBatchAsync` + async CQ polling available in PHP.

| Task | File(s) | Done When |
|---|---|---|
| Write `async_completion_queue.c/h` | `ext/grpc/` | `grpc_async_poll()` returns null on empty, event on ready |
| Extract op-building into `grpc_php_build_ops()` helper | `ext/grpc/call.c` | `startBatch` and `startBatchAsync` share logic |
| Add `Call::startBatchAsync(int $id, array $batch)` | `ext/grpc/call.c/h` | Non-blocking; returns immediately |
| Expose `grpc_async_poll()` and `grpc_async_poll_blocking()` | `ext/grpc/php_grpc.c` | Callable from PHP |
| Wire init/shutdown in `MINIT`/`MSHUTDOWN` | `ext/grpc/php_grpc.c` | Valgrind clean on shutdown |
| C-level unit tests | `ext/grpc/tests/` | Empty CQ returns null; submitted batch event returns correct tag |

### Phase 2 — EventLoop + Unary (Weeks 4–6)

**Goal:** Concurrent unary calls work end-to-end.

| Task | File(s) |
|---|---|
| `EventLoop.php` — tick, park, resume, spawn | `lib/Grpc/Async/` |
| `Grpc\StatusCode` / `ConnectivityState` / `Compression` enums | `lib/Grpc/` |
| `Grpc\CallOptions` readonly class | `lib/Grpc/` |
| `Channel.php` — async channel with property hooks | `lib/Grpc/Async/` |
| `AbstractAsyncCall.php` + `UnaryCall.php` | `lib/Grpc/Async/Call/` |
| `RpcException.php` | `lib/Grpc/Async/` |
| `functions.php` — `run()`, `spawn()` | `lib/Grpc/Async/` |
| PHPUnit: concurrent unary calls, timeout, cancellation | `tests/unit_tests/async/` |
| Example: `examples/async/greeter_async_client.php` | |

**Milestone:** 10 concurrent `SayHello` calls complete in ~1 RTT, not 10 RTTs.

### Phase 3 — Streaming Calls (Weeks 7–9)

| Task |
|---|
| `ServerStreamingCall.php` |
| `ClientStreamingCall.php` |
| `BidiStreamingCall.php` |
| `AsyncBaseStub.php` with factory methods |
| PHPUnit: all four streaming patterns |
| PHPUnit: `awaitWithStatus()` |
| Examples: streaming patterns |

### Phase 4 — Code Generation (Weeks 10–11)

| Task |
|---|
| `GenerateAsyncStubFile()` in `php_generator.cc` |
| `--async` flag in `php_plugin.cc` |
| First-class callable syntax in generated stubs |
| Named argument syntax in generated stubs |
| Integration test: generate stubs from interop proto, run calls |

### Phase 5 — Async Server (Weeks 12–15)

| Task |
|---|
| `Server/Server.php` — dispatch loop, Fiber-per-RPC |
| `Server/ServicerContext.php` with abort() |
| `Server/ServerCallReader.php` + `ServerCallWriter.php` |
| `Server/MethodDescriptor.php` + `AbortException.php` |
| PHPUnit: unary + streaming server handlers |
| End-to-end test: async client + async server in same test process |
| gRPC interop test server (`tests/interop/async/`) |

**Milestone:** `empty_unary`, `large_unary`, `ping_pong`, `cancel_after_begin`, `timeout_on_sleeping_server` interop tests pass.

### Phase 6 — Interceptors + Revolt Driver (Weeks 16–17)

| Task |
|---|
| `Interceptor/AsyncInterceptor.php` |
| `Interceptor/InterceptorChannel.php` |
| `EventLoopDriverInterface.php` |
| `Driver/RevoltDriver.php` (in optional package `grpc/async-revolt`) |
| PHPUnit: interceptor chain, metadata modification, error recovery |

### Phase 7 — Hardening and Promotion (Weeks 18–20)

| Task |
|---|
| Memory leak audit (10k-iteration test, Valgrind) |
| Performance benchmark: async 10-concurrent vs sync sequential |
| PHP 8.5 linting: readonly, hooks, enums used throughout |
| Remove `@experimental` labels if promotion criteria met |
| CHANGELOG and migration guide |

**Promotion criteria:**
- All gRPC interop tests pass against C++/Go reference servers
- Memory usage stable under sustained load
- Performance: async 10-concurrent ≥ 5× sync single-call throughput
- Revolt driver tested against Amp and ReactPHP ecosystem

---

## 16. Testing Plan

### Unit Tests (`tests/unit_tests/async/`)

| Test Class | Covers |
|---|---|
| `EventLoopTest` | tick, park/resume, spawn, nested run, driver swapping |
| `UnaryCallTest` | await, awaitWithStatus, timeout, cancel, RpcException |
| `ServerStreamingCallTest` | read loop, early termination, status |
| `ClientStreamingCallTest` | write sequence, await, error on non-OK |
| `BidiStreamingCallTest` | interleaved read/write, writesDone, status |
| `ConcurrentCallsTest` | 10 concurrent unary: assert ≤ 2× single-RTT latency |
| `RpcExceptionTest` | fromStatus, isRetryable, message, statusCode |
| `ChannelTest` | waitForReady, state transitions, close, property hooks |
| `AsyncInterceptorTest` | chain of 3, metadata injection, error path |
| `ServerTest` | unary dispatch, streaming dispatch, abort, shutdown |
| `ServicerContextTest` | abort throws, setStatus, clientMetadata |

### Interop Tests (`tests/interop/async/`)

All cases from `src/proto/grpc/testing/test.proto`:

| Case | What It Validates |
|---|---|
| `empty_unary` | Baseline round-trip |
| `large_unary` | Large message serialization |
| `client_streaming` | Full client-streaming flow |
| `server_streaming` | Full server-streaming flow |
| `ping_pong` | Bidi: strict alternating read/write |
| `empty_stream` | Bidi: zero messages |
| `cancel_after_begin` | Cancellation cleans up resources |
| `cancel_after_first_response` | Mid-stream cancel |
| `timeout_on_sleeping_server` | Deadline propagation |
| `concurrent_large_unary` | 10 concurrent 1MB RPCs |
| `status_code_and_message` | Non-OK status propagates correctly |

### Performance Benchmark (`tests/qps/async/`)

| Scenario | Pass Threshold |
|---|---|
| 1 concurrent call, async vs sync | Within 15% |
| 10 concurrent calls, async vs sync sequential | ≥ 5× throughput |
| 50 concurrent streaming calls | No memory growth after warm-up |

---

## 17. File Map

### New Files

```
src/php/
├── ext/grpc/
│   ├── async_completion_queue.h        [NEW]
│   ├── async_completion_queue.c        [NEW]
│   ├── call.h                          [MODIFIED — additive: startBatchAsync arginfo]
│   └── call.c                          [MODIFIED — additive: PHP_METHOD startBatchAsync,
│                                                   extract grpc_php_build_ops helper]
│   └── php_grpc.c                      [MODIFIED — additive: init/shutdown async CQ,
│                                                   grpc_async_poll, grpc_async_poll_blocking]
│
└── lib/
    ├── Grpc/
    │   ├── StatusCode.php              [NEW — enum, additive to Grpc\ namespace]
    │   ├── ConnectivityState.php       [NEW — enum]
    │   ├── Compression.php             [NEW — enum]
    │   └── CallOptions.php             [NEW — readonly class]
    │
    └── Grpc/Async/
        ├── EventLoop.php
        ├── EventLoopDriverInterface.php
        ├── Channel.php
        ├── AsyncBaseStub.php
        ├── RpcException.php
        ├── functions.php               (run, spawn)
        ├── Call/
        │   ├── AbstractAsyncCall.php
        │   ├── UnaryCall.php
        │   ├── ServerStreamingCall.php
        │   ├── ClientStreamingCall.php
        │   └── BidiStreamingCall.php
        ├── Server/
        │   ├── Server.php
        │   ├── ServicerContext.php
        │   ├── ServerCallReader.php
        │   ├── ServerCallWriter.php
        │   ├── MethodDescriptor.php
        │   └── AbortException.php
        └── Interceptor/
            ├── AsyncInterceptor.php
            └── InterceptorChannel.php

src/compiler/
├── php_generator.cc                    [MODIFIED — additive: GenerateAsyncStubFile()]
└── php_plugin.cc                       [MODIFIED — additive: --async flag]

src/php/tests/
├── unit_tests/async/                   [NEW — 11 test classes]
└── interop/async/                      [NEW — interop client + server]
```

### Optional Packages (separate repos)

```
grpc/async-revolt      RevoltDriver + composer dep on revolt/event-loop
grpc/async-swoole      Swoole coroutine driver (future, if demanded)
```

### Modified Files Summary

| File | Change | Breaking? |
|---|---|---|
| `ext/grpc/call.c` | Add `startBatchAsync`, extract build_ops helper | No |
| `ext/grpc/call.h` | Add arginfo | No |
| `ext/grpc/php_grpc.c` | Init async CQ, expose poll functions | No |
| `src/compiler/php_generator.cc` | Add `GenerateAsyncStubFile()` behind flag | No |
| `src/compiler/php_plugin.cc` | Parse `--async` flag | No |
| `src/php/composer.json` | Add `Grpc\\Async\\` PSR-4 entry; PHP ≥ 8.5 in `suggest` | No |

**Every change is additive. No existing behavior is modified.**
