# net

The event loop and its backends, sockets, timers, TLS and an HTTP server. Namespace `core::net`,
directory `src/core/net/`. Three targets:

| Target | Kind | What it has | Builds |
|---|---|---|---|
| `core::net_types` | header-only | `NetError`, `NetErrorCode`, `IoResult` | everywhere, Emscripten included |
| `core::net` | static | everything else below | Linux, macOS, the BSDs, Windows |
| `core::net_tls` | static | `ITlsContext` and the TLS socket | with `CORE_CPP_WITH_TLS`, natively |

!!! note "Status"
    Imported from contour's `src/net` at `6777ff05`, as contour has it but for the namespaces and
    [platform](platform.md) in place of contour's `net/platform/`. The API below is contour's
    `EventSource` design. Phase B of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    replaces it: `IoBackend`, `makeDefaultBackend()` and `Interest` take the place of
    `EventSource`, `makeDefaultEventSource()` and `FdInterest`, IOCP becomes the Windows default,
    fastcached's sockets and dialler are merged in, and the event loop, its timers and a
    host-driven backend join the WebAssembly subset (Tasks B3 to B11). Until then nothing of
    `core::net` but `core::net_types` builds under Emscripten. Task B2 is done: the error
    vocabulary below is already the merged one.

## What it has

| Header | What it has |
|---|---|
| `<core/net/NetError.hpp>` | `NetErrorCode` and its `toString()`, the predicate `isDeadlineExpiry()`, `NetError` (a category, the OS error number and a context string) and `makeNetError()` |
| `<core/net/IoResult.hpp>` | `IoResult`, `std::expected<std::size_t, NetError>`: what every byte transfer returns |
| `<core/net/EventLoop.hpp>` | `EventLoop`: the single-threaded driver that resumes coroutines on descriptor readiness and timers; `blockOn()`, `spawn()`, `post()` (the one member other threads may call), `requestStop()`, `delay()`, `sleepUntil()`, `waitReadable()`, `waitWritable()`, `notifyHandleClosing()`; `pollUntil()` |
| `<core/net/EventSource.hpp>` | `EventSource`, the injected blocking wait the loop drives, and its registry: `FdToken`, `FdInterest`, `FdRegistry`, `WaitOutcome` |
| `<core/net/PollEventSource.hpp>` | the portable `EventSource`: `poll(2)` on POSIX, `WaitForMultipleObjects` on Windows, in chunks past 64 handles |
| `<core/net/DefaultEventSource.hpp>` | `makeDefaultEventSource()`, the best backend here with a fallback to poll; `makeEventSource(EventSourceKind)` for tests. The epoll (Linux) and kqueue (macOS, the BSDs) sources behave as poll does, and a wait costs O(ready) rather than O(registered); their headers are private, so these two functions are how a program gets one |
| `<core/net/ISocket.hpp>`, `<core/net/IListener.hpp>` | the transport interfaces: `read`, `readWithFd`, `write`, `close`; `accept`, `localPort` |
| `<core/net/Sockets.hpp>` | `listen()`, `connect()`, `listenUnix()`, `connectUnix()`, `adoptFd()`, `appendReadChunk()` |
| `<core/net/AsyncBufferedReader.hpp>` | `readLine()`, `readUntil()`, `readExactly()` over an `ISocket`, each buffered byte scanned once |
| `<core/net/WriteQueue.hpp>` | the single writer per connection: whole frames in order, bounded by bytes, with superseding by tag |
| `<core/net/SplitSocket.hpp>` | one duplex `ISocket` from two simplex halves |
| `<core/net/WithTimeout.hpp>` | `withTimeout()`: a task raced against a timer on the loop's clock |
| `<core/net/HttpServer.hpp>` | a minimal HTTP/1.1 server: `serve()`, `readRequest()`, `writeResponse()`; `Content-Length` bodies only, every response closes |
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
`core::net`: `ScriptedEventSource` (scripted readiness and recorded timeouts, no descriptors),
`makeSocketPair()` (a connected pair over a socketpair, or a loopback TCP pair on Windows),
`AllBackends` (every `EventSourceKind`, for tests that run one scenario on each), and
`CoroTestSupport.hpp` (`sleepFor`, `allOf`, `anyOf`, `waitUntil`). contour's `testing/TempDir.hpp`
was not imported: [`core::testing::ScopedTempDir`](testing.md) does the same.

The module's own directory holds only platform-independent code. What one platform needs is
private, and CMake's per-platform source lists choose it: `posix/` (`poll(2)`, the listeners,
`PosixSocket`, the accept loop), `linux/` (epoll), `bsd/` (kqueue, for Apple and the BSDs) and
`windows/` (`WaitForMultipleObjects`, `WindowsSocket` and `WindowsListener` over
`WSAEventSelect`, the loopback pair). No file there guards itself with an `#ifdef` of its
platform. contour's `PollEventSource.cpp` is split along its `#ifdef` into `posix/` and
`windows/`, and `makeSocketPair()` into `testing/posix/` and `testing/windows/`. Two exceptions
remain: `DefaultEventSource.cpp` keeps its `#ifdef`s until Task B3 replaces it, and
`EventSourceParity_test.cpp` keeps two POSIX-only cases (a closed descriptor's registration,
descriptor exhaustion) under `#ifndef _WIN32`. `detail/` has the rest that is private: the
chunking arithmetic of the Windows wait, `PeerAddress.hpp` (which includes `<winsock2.h>`), and
two helpers.

## Invariants

From contour's `src/net/README.md` at `6777ff05`, as far as they hold here:

- **`EventSource` is the extension point.** A new backend implements the blocking wait and the
  registry; the loop keeps the timers and the ready queue. Every backend behaves the same, and
  `EventSourceParity_test.cpp` runs one scenario on each to keep it so.
- **A registration that fails fails the awaitable; it never parks.** `waitReadable()` on a
  descriptor the backend refused throws `FdRegistrationFailed` rather than suspending on an
  interest nothing can resume.
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
  cancelled flow, `FdRegistrationFailed`, and the `std::runtime_error` of an `EventLoop` that
  cannot create its wakeup pipe (descriptor exhaustion).
- **No OpenSSL type crosses a header.** `Tls.cpp` keeps it behind `ITlsContext`, and
  `core::net_tls` links OpenSSL PRIVATE.

## Limits

These are contour's, carried as they are:

- `connect()`, `connectUnix()` and `readUntil()` are coroutines that take a `std::string_view`.
  A `Task` starts when it is awaited, so the string must live until then: awaiting the call in
  the same expression is safe, storing the task and awaiting it after the string is gone is not.
  Phase B's dialler takes a `std::string`.
- `generateSelfSignedCertificate()` defaults its common name to `"contour-daemon"`, and
  `makeSelfSignedServerContext()` uses that default. Phase B's `SelfSignedOptions` names it.
- The event sources take a timeout in milliseconds, so a timer fires no more precisely than
  that. Phase B's `IoBackend::wait()` takes a `SteadyDuration`.

## Tests

| Test | Binary | Labels | What |
|---|---|---|---|
| `core-cpp.net_types` | `core-cpp-net_types-test` | `core-cpp`, `net` | `NetError_test.cpp`; runs under Emscripten too |
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
