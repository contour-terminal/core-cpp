# net

The event loop and its backends, sockets, timers, TLS and an HTTP server. Namespace `core::net`,
directory `src/core/net/`. Three targets:

| Target | Kind | What it has | Builds |
|---|---|---|---|
| `core::net_types` | header-only | `NetError`, `NetErrorCode`, `IoResult` | everywhere, Emscripten included |
| `core::net` | static | everything else below | Linux, macOS, the BSDs, Windows; under single-threaded WebAssembly, the `IoBackend` contract, the host-driven backend, the event loop and its timers |
| `core::net_tls` | static | `ITlsContext` and the TLS socket | with `CORE_CPP_WITH_TLS`, natively |

!!! note "Status"
    Imported from contour's `src/net` at `6777ff05`, as contour has it but for the namespaces and
    [platform](platform.md) in place of contour's `net/platform/`, and being merged with
    fastcached's async and networking layer by Phase B of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md).
    Done so far: Task B2's merged error vocabulary; Task B3's `IoBackend`, which replaces
    `EventSource` — a backend dispatches readiness to the callbacks a caller registers, instead of
    reporting tokens for the caller to route; Task B4's `EventLoop`, `PlatformLoop` and
    `testing::TestLoop`; and Task B5's timers. Still to come: IOCP as the Windows default, and
    fastcached's sockets and dialler (Tasks B6 to B11). Under Emscripten `core::net_types` builds,
    and so does the WebAssembly subset of `core::net`: `IoBackend`, `IHostScheduler`,
    `HostDrivenBackend`, the test doubles, and — since Tasks B4 and B5 — the event loop and its
    timers. The sockets do not. A loop there has no thread to block and no descriptor to poll, so
    it is PUMPED by the host and neither `run()` nor `blockOn()` may be called on it; both assert.
    `tests/wasm/HostDrivenTimer_smoke.cpp` is that path run under node.

## What it has

| Header | What it has |
|---|---|
| `<core/net/NetError.hpp>` | `NetErrorCode` and its `toString()`, the predicate `isDeadlineExpiry()`, `NetError` (a category, the OS error number and a context string) and `makeNetError()` |
| `<core/net/IoResult.hpp>` | `IoResult`, `std::expected<std::size_t, NetError>`: what every byte transfer returns |
| `<core/net/EventLoop.hpp>` | `EventLoop`: the single-threaded driver that resumes coroutines on descriptor readiness and timers; `run()`, `runOnce()`, `runUntilIdle()`, `blockOn()`, `spawn()`, `post()`, `submit()`, `schedule()`, `cancelPending()` (the thread-safe members), `stop()`, `requestStop()`, `delay()`, `sleepUntil()`, `addTimer()`/`cancelTimer()` and `TimerId`, `waitReadable()`, `waitWritable()`, `notifyHandleClosing()`; `pollUntil()` |
| `<core/net/SleepUntil.hpp>` | `sleepUntil(EventLoop*, tp)`, the free form for a caller whose loop may be null — a null loop or a deadline already gone resolves inline, without suspending; and `nextWakeStep()`, the arithmetic of a bounded wait, which core-cpp itself no longer needs |
| `<core/net/InterruptibleSleep.hpp>` | `interruptibleSleepUntil()` and `WakeReason`: sleep to a deadline or until a stop token is stopped, whichever comes first. It parks ONCE and the stop callback wakes it; the wait does not poll |
| `<core/net/DeadlineTimer.hpp>` | `DeadlineTimer`: a deadline as an object, disarmed by `disarm()` or by destruction, for a timeout that has to tear an operation down rather than merely stop waiting for it. No coroutine frame, no allocation and no poll interval — an armed timer is what bounds the loop's next wait |
| `<core/net/IoBackend.hpp>` | `IoBackend`, the injected blocking wait the loop drives and the readiness dispatcher behind it: `ReadinessHandler` (a handle, an owner and the callbacks a backend invokes), `Interest`, `HandleKind`, `Readiness`, `selectReadinessCallback()`, `BackendKind`, `WaitResult`; and the factories `makeDefaultBackend()`, `makeBackend(BackendKind)` and `preferredBackendKind()`. Every backend's own header is private, so the factories are how a program gets one: poll(2) on POSIX, epoll on Linux, kqueue on macOS and the BSDs, and on Windows both `WSAEventSelect` + `WaitForMultipleObjects` (which `preferredBackendKind()` still answers) and an I/O completion port, reachable as `makeBackend(BackendKind::Iocp)`. `completionPort()` answers non-null on a completion-based backend and `nullptr` on every other, which is how a socket knows whether to issue overlapped operations or to park on readiness |
| `<core/net/IHostScheduler.hpp>` | `IHostScheduler::callAfter()`, the one thing a host event loop has to lend core-cpp's, and `HostCallback` |
| `<core/net/HostDrivenBackend.hpp>` | `HostDrivenBackend`: the backend for a loop that is PUMPED rather than one that blocks. It has no readiness (`attach` and `setInterest` answer `Unsupported`), its `wait()` never blocks, `wake()` and `armWakeAt()` ask the host for a pump and coalesce, and `isHostDriven()` is true. Portable, and the browser is only one of its hosts |
| `<core/net/ISocket.hpp>`, `<core/net/IListener.hpp>` | the transport interfaces. `read`, `readWithFd`, `write`, `writeVectored`, `waitReadable`, `handshakeIfNeeded`, `cancelRead`, `shutdownWrite`, `setReceiveDeadline`, `close`; `accept`, `localPort`. Every operation is a frame-free, stop-aware awaitable, not a `Task` |
| `<core/net/IoAwaitable.hpp>` | `ResultAwaitable<R>` and its byte-count alias `IoAwaitable`: what a socket operation resolves through, and why it allocates nothing. `core::async::asTask` is the escape hatch for a caller that must store one |
| `<core/net/SocketContract.hpp>` | `core::net::contract` — the socket contract's Debug tripwires (`requireReadBuffer`, `claimReadSlot`, `claimWriteSlot`, `assertTeardownIsSerialisedWithDispatch`), public so a transport outside this library gets them too |
| `<core/net/Sockets.hpp>` | `listen()`, `connect()`, `listenUnix()`, `connectUnix()`, `adoptFd()`, `appendReadChunk()` |
| `<core/net/AsyncBufferedReader.hpp>` | `readLine()`, `readUntil()`, `readExactly()` over an `ISocket`, each buffered byte scanned once |
| `<core/net/WriteQueue.hpp>` | the single writer per connection: whole frames in order, bounded by bytes, with superseding by tag |
| `<core/net/SplitSocket.hpp>` | one duplex `ISocket` from two simplex halves |
| `<core/net/WithTimeout.hpp>` | `withTimeout()`: a task raced against a timer on the loop's clock |
| `<core/net/HttpServer.hpp>` | a minimal HTTP/1.1 server: `serve()`, `readRequest()`, `writeResponse()`; `Content-Length` bodies only, every response closes |
| `<core/net/IDatagramSocket.hpp>`, `<core/net/UdpSocket.hpp>` | `IDatagramSocket` (`send`, a bounded `receive`, `close`, `boundAddress`), `DatagramAddress`, `ReceivedDatagram`, `DatagramWait`; `openUdpSocket()` with `BroadcastMode` and `PortSharing`, answering WHY a bind failed. Blocking, on a thread of its own: a datagram socket is not an `ISocket` |
| `<core/net/SharedPortDatagram.hpp>` | `answerFromOwnAddress()` and `openSharedPortUdpSocket()`: hear the segment on a shared port, send and be answered from an address only this node holds |
| `<core/net/BlockingSocket.hpp>`, `<core/net/BlockingConnector.hpp>` | the transports for threads that may block: `BlockingSocket`, whose every awaitable is already settled, and `BlockingConnector`, an `IConnector` that dials on the calling thread within its budget and arms `BlockingConnectorOptions::ioTimeout` before handing the socket over. Driven by `core::async::syncRun`; never on a loop thread |
| `<core/net/TcpClient.hpp>` | the one TCP client: `connectTcp()`, `sendAll()`, `receiveExactly()` |
| `<core/net/HealthProbe.hpp>` | `probeHttpStatus()`, which returns the status an HTTP endpoint answered, and `httpHealthProbe()`, which is whether it was 200 |
| `<core/net/testing/InMemorySocket.hpp>` | the deterministic fake: `InMemoryPipe`, `InMemorySocket`, `InMemorySocketPair`, `InMemoryListener` -- no descriptor, no loop, completed inline, and pinned to a real socket in every closed state by `SocketClosedStates_test.cpp`. Not `testing/InMemoryTransport.hpp`, whose `makeSocketPair()` is a pair of REAL sockets |
| `<core/net/testing/InMemoryDatagram.hpp>`, `<core/net/testing/DatagramPayload.hpp>` | `DatagramBus`, a network segment in one process with scripted loss, and the text-to-payload helpers |
| `<core/net/testing/SocketDecorator.hpp>`, `<core/net/testing/ParkingReadableSocket.hpp>` | `SocketDecorator`, which forwards every verb so a double overrides only the one it stages; `ParkingReadableSocket` and `ParkingWritableSocket`, which park until the test decides and count how each parked operation ended |
| `<core/net/Diagnostics.hpp>` | `setDiagnosticSink()`: where a failure nobody can be handed goes (a wait that fails mid-sweep); discarded by default |
| `<core/net/Tls.hpp>` (`core::net_tls`) | `ITlsContext::wrap()`, a TLS `ISocket` over any other, driven through memory BIOs on the same loop; `makeTlsServerContext()`, `makeSelfSignedServerContext()`, `makeTlsClientContext()` (a pinned CA and a host name, or trust on first use), `generateSelfSignedCertificate()`, `constantTimeEquals()` |

## The error vocabulary

`NetErrorCode` is the union of the two vocabularies this module was merged from, contour's
`net::NetErrorCode` and fastcached's `FastCache::NetErrorCode`, so a caller of either lineage still
has a code for every failure it used to distinguish. `toString(NetErrorCode)` is a `constexpr`
switch with no `default`, which is what makes a compiler name it when a code is added.

| Code | `toString()` | What it says |
|---|---|---|
| `Ok` | `ok` | No failure. It is not stored in an error result; it is what a `NetErrorCode` variable holds before anything has failed |
| `Eof` | `end of stream` | The peer finished sending — it closed its write side, which is not the same as being gone |
| `Cancelled` | `cancelled` | The resource cancelled the operation (`close()`, `cancelRead()`, a closed listener). A cancel from the flow's own stop token throws `core::async::OperationCancelled` instead |
| `Timeout` | `timed out` | A deadline elapsed. On Winsock this is also how an armed poll or `SO_RCVTIMEO` reports its expiry |
| `WouldBlock` | `would block` | The operation would block. On POSIX this is also how an armed poll or `SO_RCVTIMEO` reports its expiry |
| `BadHandle` | `bad handle` | The socket, descriptor or handle is closed or invalid |
| `ConnReset` | `connection reset` | The peer reset the connection mid-flight |
| `ConnRefused` | `connection refused` | The peer actively refused a connect |
| `AddressInUse` | `address in use` | A bind found the endpoint taken |
| `AddressNotAvail` | `address not available` | A bind found the address not available locally |
| `AddressError` | `address error` | Address resolution or parsing failed |
| `HostUnreach` | `host unreachable` | The network reports the destination as unreachable |
| `PermissionDenied` | `permission denied` | The OS refused the operation — a low-numbered port without privileges, a firewall's `EACCES` |
| `Unsupported` | `unsupported` | The operation is not supported on this platform or transport |
| `MessageTooLarge` | `message too large` | A framed unit (line, PDU, datagram) exceeded its configured bound |
| `SystemError` | `system error` | An OS error nothing classified further; read `NetError::systemCode` |
| `Last` | `unknown error` | Not a code: the number of codes above it, so a table or a test covers every one without restating the list. Never constructed, never returned |

A new code goes **above** `Last`, never below. One appended after it still satisfies the
`default`-less switch and still leaves `Last` looking like a count, while every check that walks
`[0, Last)` misses it; `NetError_test.cpp`'s "No code hides above Last" is what refuses that.

`AddressNotAvail`, `HostUnreach` and `PermissionDenied` have no producer in core-cpp yet. The errno
and WSA tables that classify a socket failure gain their rows when fastcached's sockets and dialler
are merged in (Tasks B6 to B8); until then the codes exist and nothing returns them.

`isDeadlineExpiry(code)` answers "did this operation run out of time", and it is `Timeout` **or**
`WouldBlock`, because a deadline armed with `SO_RCVTIMEO`/`SO_SNDTIMEO` or a poll timeout expires as
`EAGAIN` on POSIX and as `WSAETIMEDOUT` on Winsock. Both operands are load-bearing at every caller;
the header says what narrowing it to `Timeout` costs
([fastcached#824](https://github.com/LASTRADA-Software/fastcached/issues/824)).

`NetError::toString()` renders words, not an enumerator's position:
`connection reset (recv) [errno 104]`. The context and the `[errno …]` are each omitted when empty
or zero, so `makeNetError(NetErrorCode::Eof)` renders as `end of stream`.

The test doubles are public, in `testing/` and namespace `core::net::testing`, and compiled into
`core::net`: `ScriptedBackend` (readiness scripted against a `HandlerId`, and recorded timeouts,
with no descriptors), `NullBackend` (accepts registrations, reports nothing, never blocks),
`ManualHostScheduler` (a host that does nothing until a case tells it to, which is what makes
`HostDrivenBackend` testable on every platform rather than only in a browser),
`makeSocketPair()` (a connected pair over a socketpair, or a loopback TCP pair on Windows),
`BackendMatrix` (every `BackendKind` `makeBackend` can build, for tests that run one scenario on
each), and
`CoroTestSupport.hpp` (`sleepFor`, `allOf`, `anyOf`, `waitUntil`). contour's `testing/TempDir.hpp`
was not imported: [`core::testing::ScopedTempDir`](testing.md) does the same.

The module's own directory holds only platform-independent code. What one platform needs is
private, and CMake's per-platform source lists choose it: `posix/` (`poll(2)`, the listeners,
`PosixSocket`, the accept loop), `linux/` (epoll), `bsd/` (kqueue, for Apple and the BSDs) and
`windows/` (`WfmoBackend`, `IocpBackend` and its wait-completion-packet probe, `WindowsSocket`
and `WindowsListener` over `WSAEventSelect`, the loopback pair). No file there guards itself with an `#ifdef` of its platform. contour's
`PollEventSource.cpp` is split along its `#ifdef` into `posix/PollBackend.cpp` and
`windows/WfmoBackend.cpp`, and `makeSocketPair()` into `testing/posix/` and `testing/windows/`.
`DefaultBackend.cpp` sits in each of those and in `emscripten/` (the browser's own host, over
`emscripten_async_call`), and the `CMakeLists.txt` names exactly one of the five, because that is
what "no `#ifdef` chooses a backend" means: the platform is asked once, where the source lists are. One exception remains: `BackendParity_test.cpp` keeps three POSIX-only cases (a
closed descriptor's registration, descriptor exhaustion, a muted registration whose peer hangs up)
under `#ifndef _WIN32`. `detail/` has the rest that is private: the ready batch every backend
dispatches through, the wakeup channel every blocking one is woken by, the timeout conversion, the
chunking arithmetic of the Windows wait, `PeerAddress.hpp` (which includes `<winsock2.h>`), and two
helpers.

## Invariants

From contour's `src/net/README.md` at `6777ff05`, as far as they hold here:

- **Backends dispatch, the loop resumes.** A backend's `wait()` invokes the callbacks on the
  `ReadinessHandler`s it holds, and those callbacks only ENQUEUE; every coroutine is resumed by
  the loop, on the loop's thread, after the wait has returned. Resuming from inside a backend's
  walk over its own ready list lets the resumed frame free the object whose entry the walk has not
  reached yet, so `EventLoop::drainReadyQueue()` asserts that no dispatch is in flight.
- **At most one callback per registration per wait,** for the same reason, and a registration
  detached during a dispatch is withdrawn from the batch rather than called later in it. Level
  triggering reports whatever was skipped on the next wait.
- **`IoBackend` is the extension point.** A new backend implements the wait and the dispatch; the
  loop keeps the timers and the ready queue. Every backend behaves the same, and
  `BackendParity_test.cpp` runs one scenario on each to keep it so.
- **A registration that fails fails the awaitable; it never parks.** `waitReadable()` on a
  descriptor the backend refused throws `FdRegistrationFailed` rather than suspending on an
  interest nothing can resume. `setInterest()` is what reports a kernel's refusal — `attach()`
  only says that the handler and the backend are usable together, because kqueue has no
  "register with no filters" operation and so cannot answer more than that.
- **Readiness is level-triggered.** The sockets and the accept loop assume a descriptor that
  stays ready is reported again.
- **Time is the injected `core::platform::IClock`,** which the loop refreshes before it computes
  a wait's timeout and after the wait returns, so a `CachedClock` serves each turn the instant its
  wait ended at. (contour's loop did not refresh; its clock had no `refresh()`.)
- **A descriptor is announced before it closes** (`EventLoop::notifyHandleClosing()`), because
  epoll and kqueue cannot report a closed one. A socket's `close()` resumes a flow parked on it on
  its normal path; its destructor resumes it with `OperationCancelled`, so the flow never reads the
  dead socket.
- **I/O errors are `std::expected`.** Exceptions are `core::async::OperationCancelled`, for a
  cancelled flow, `FdRegistrationFailed`, and the `std::runtime_error` of a backend that cannot
  create its wakeup channel (descriptor exhaustion). That channel belongs to the backend, because
  `IoBackend::wake()` is the one member of the interface another thread may call, and it is what
  `EventLoop::post()` uses to break a wait in flight.
- **No OpenSSL type crosses a header.** `Tls.cpp` keeps it behind `ITlsContext`, and
  `core::net_tls` links OpenSSL PRIVATE.
- **A loop that does not own its thread is pumped, not blocked.** `HostDrivenBackend` has no wait:
  it asks its `IHostScheduler` for a pump and returns, and the host is what waits. Several
  requests before the host gets a turn become ONE pump, or a burst of `post()`s would queue a
  browser timer each; a request EARLIER than the one already out is scheduled beside it, because a
  host's timer cannot be retracted and a spurious pump costs an empty turn where a missed one is a
  hang.

## Limits

These are contour's, carried as they are:

- `connect()`, `connectUnix()` and `readUntil()` are coroutines that take a `std::string_view`.
  A `Task` starts when it is awaited, so the string must live until then: awaiting the call in
  the same expression is safe, storing the task and awaiting it after the string is gone is not.
  Phase B's dialler takes a `std::string`.
- `generateSelfSignedCertificate()` defaults its common name to `"contour-daemon"`, and
  `makeSelfSignedServerContext()` uses that default. Phase B's `SelfSignedOptions` names it.
- `IoBackend::wait()` takes a `SteadyDuration`, but every backend's native wait but kqueue's
  takes milliseconds, so a timer still fires no more precisely than that. A positive duration
  under a millisecond rounds UP to one rather than truncating to zero, which would turn the wait
  into a poll and spin the loop.

## Tests

| Test | Binary | Labels | What |
|---|---|---|---|
| `core-cpp.net_types` | `core-cpp-net_types-test` | `core-cpp`, `net` | `NetError_test.cpp`; runs under Emscripten too |
| `core-cpp.net_backend` | `core-cpp-net_backend-test` | `core-cpp`, `net` | the backend contract that needs no kernel (`selectReadinessCallback`, the ready batch, the timeout conversion) and the host-driven backend over `ManualHostScheduler`; runs under Emscripten too |
| `core-cpp.net` | `core-cpp-net-test` | `core-cpp`, `net`, `loopback` | the event loop, the backends' parity, sockets, AF_UNIX and descriptor passing (POSIX), the buffered reader, the write queue, the HTTP server |
| `core-cpp.net_tls` | `core-cpp-net_tls-test` | `core-cpp`, `net`, `loopback` | `Tls_test.cpp`, with `CORE_CPP_WITH_TLS` |

Nearly every case of the last two moves bytes over a socket, a socketpair or, on Windows, the
loopback TCP pair behind `SystemPipe` and `makeSocketPair()`, hence `loopback` on the binaries.
`core-cpp.net_tls` runs wherever `CORE_CPP_WITH_TLS` is on: every Linux, macOS and BSD preset
(and so CI's Linux, macOS and sanitizer jobs and the nightly FreeBSD build) and `cl-release-tls`
on Windows.

See [Threading](../design/threading.md) and
[Coroutines and lifetimes](../design/coroutines-and-lifetimes.md). The rules that govern this
module are in
[`.agent/rules/async-and-net.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/async-and-net.md).

Depends on [async](async.md) and [platform](platform.md), and links `Threads::Threads` PUBLIC,
because its headers use `std::mutex`. `core::net_tls` also links OpenSSL, from the system, as a
private dependency.
