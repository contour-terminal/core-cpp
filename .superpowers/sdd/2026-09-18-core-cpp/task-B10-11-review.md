# Review: Tasks B10 + B11 (3681abd..7cac6f1)

**Verdict: CHANGES** -- one blocker and four should-fix findings.

The code reviewed is the committed range. Every line number below is in `git show 7cac6f1:<path>`.
While the review ran, D:/core-cpp-wt-b11 moved to 10682c8 (7cac6f1 amended with the
ConnReset-on-truncation follow-up), so its working tree does not match these line numbers. The
ConnReset follow-up and the C4737 errors in ReadinessDial_test are out of scope.

## Blocker

### B1. Destroying a TlsSocket while an operation is parked inside `handshake()` or `flushOut()` frees memory twice and resumes freed handles

`src/core/net/Tls.cpp:182-186`, `:584-595`, `:473`, `:533`, `:133-153`

- `~TlsSocket` only calls `SSL_free`. The members are then destroyed in reverse declaration order:
  `_flushing` and `_handshaking` (with their `_waiters` vectors) go first, and `_inner` goes after
  them.
- `~PosixSocket` abandons its parked operation inline (`PosixSocket.cpp:92-137`,
  `close(FdWakePolicy::Cancel)` leading to `abandon()` and then `waiter.resume()`). The TLS frame
  parked in `feedIn()` or in `flushOut()`'s `_inner->write` therefore resumes and throws
  `OperationCancelled`.
- The unwind runs `handshake()`'s or `flushOut()`'s `ScopeGuard`, which calls `leave()` on a
  `SerialGate` that has already been destroyed.
- `std::exchange(_waiters, {})` moves out of a destroyed vector. libstdc++ and MSVC do not null the
  pointers of a destroyed vector, so the local takes the freed buffer:
  - it iterates freed memory;
  - it resumes the handles found there (frames that other flows still own), and each re-enters
    `handshake()` through a dead `this` and calls `SSL_do_handshake` on the freed `SSL`;
  - it frees the buffer a second time when the local is destroyed.
- Even with no waiter, the guard writes `_busy` in a destroyed member.
- Separately, any flow parked on a gate when the socket is destroyed is never resumed and never
  freed. That breaks "a destroyed socket abandons whatever was parked on it", which
  `SplitSocket::close()`'s own comment relies on.

Scenario: this is the ordinary contour/fastcached shape. A read pump and a `WriteQueue` drain both
start on a new TLS connection. The drain drives the handshake and parks in `feedIn()`, and the read
parks on `_handshaking`. The connection object is then dropped before the server answers (a
connect timeout, the user closing the session, or server shutdown), and ASan reports
heap-use-after-free and double-free.

No test destroys a TlsSocket mid-handshake. `Tls_test`'s "cancelled TLS handshake" case cancels
through a stop token, and that path unwinds while the socket is still alive.

Fix direction:
- Take `_inner` down first in `~TlsSocket`, before `SSL_free` and while the gates still exist.
- Before that, mark the socket as going away (for example, set `_handshakeError`, or add a
  `_destroying` flag that `handshake()` and `flushOut()` check) so that a waiter released by the
  unwinding driver returns instead of becoming the new driver over a null `_inner`.
- Add a TlsSocket_test case: a driver parked in `feedIn`, a second operation parked on the gate,
  the socket destroyed, and both flows resolved.

## Should-fix

### S1. `SerialGate::leave()` resumes its waiters inline, so an earlier waiter can destroy the socket under a later one

`src/core/net/Tls.cpp:133-153`

This goes against async-and-net's rule that a queue or a resource never resumes its consumer
inline. The gate hands each waiter a stack on which it runs to wherever its flow goes, and then
the loop continues with the next handle.

Scenario:
- The handshake completes (or fails) with a read R and a write W parked on `_handshaking`.
- `leave()` resumes R first. R's flow sees its result: an error, or a TLS 1.3 server's 0.5-RTT
  greeting that is already in the rbio. The flow then tears the connection down, destroying the
  TlsSocket and the drain that owns W's frame.
- `leave()` continues with `handle.done()` on W's destroyed frame. If the frame survives, it
  resumes W into `SSL_write` on the freed `_ssl`.
- `closeNotify()` as a waiter does the same through `_inner->shutdownWrite()` (`:447`).

Before this range the gate existed only for the handshake. B11 generalised it and added
`_flushing`, which doubles the surface.

TlsSocket has no loop to defer through, and that is the design gap to close. Options:
- Resume through an injected executor or loop, as `AsyncQueue` does.
- At the very least, re-check a liveness token (as `SplitSocket` now does) before every resume
  after the first.

### S2. `SerialGate::Awaiter` ignores stop, so a timed-out TLS read comes back to life later and writes into the caller's abandoned buffer

`src/core/net/Tls.cpp:102-112`

`whenAny` resumes the parent when the first child finishes and only requests stop on the losers.
Every other runtime awaitable honours that stop request, but a frame parked on the gate does not.

Scenario:
- `withTimeout(asTask(tls->read(buf)), 100ms)` runs while a `WriteQueue` drain drives the
  handshake. The timeout wins, `withTimeout` returns `nullopt`, and the caller frees or reuses
  `buf` and issues a new read.
- When the handshake completes, `leave()` resumes the zombie first. Its `handshake()` returns
  success, and `readPlain` calls `SSL_read(_ssl, buf...)`.
- With a TLS 1.3 server that speaks first (0.5-RTT data after its Finished, already fed into the
  rbio), `SSL_read` returns `n > 0`. The greeting is written into the dead buffer and is lost to
  the live read.
- (With no plaintext pending, the zombie's `feedIn` sees the stopped token and throws before
  arming, so there is no double-arm. The data case is the defect.)

Fix: make the awaiter stop-aware. Register a `StopCallback` that removes the handle and resumes it
to throw `OperationCancelled`, going through the loop per S1.

### S3. A `cancelRead()` during the handshake permanently kills the connection, and can kill the other direction's operation

`src/core/net/Tls.cpp:277`, `:493-498`, `:514-517`

- `cancelRead()` retires whatever inner read is parked. If the handshake driver is in `feedIn()`,
  that read completes with `NetErrorCode::Cancelled`, and `handshake()` stores it as the sticky
  `_handshakeError`. Every later `read`, `write` and `handshakeIfNeeded` on the socket then answers
  `Cancelled`.
- `ISocket::cancelRead` documents: "Not a close: the socket stays open and a later read works"
  (`ISocket.hpp:201`). On every plaintext transport that holds.
- It also crosses directions. When a WRITE (the `WriteQueue` drain) drives the handshake and a read
  or watch is parked on the gate, the reader's `cancelRead()` retires the write's inner read. The
  write fails with `Cancelled` and the session is poisoned. The reader's own operation is resolved
  only indirectly, when the gate opens.

A cancelled inner read consumed no ciphertext, so re-driving the handshake is safe. Do not make
`Cancelled` sticky: leave `_handshakeError` empty so the next caller re-drives.

### S4. The IP-literal branch of client verification has no test

`src/core/net/Tls.cpp:956-961`

`makeTlsClientContext(ca, "127.0.0.1")` is not exercised anywhere.
- `TlsContext_test`'s `certificateMatchesIp` checks the generator's SAN through
  `X509_check_ip_asc` in the peer, not the client context's `X509_VERIFY_PARAM_set1_ip_asc` path.
- `Tls_test` covers only the DNS name ("the-real-daemon" / "an-impostor").

This branch is the one this change exists to get right (the file comment: `set1_host` "would look
for it in a dNSName"). It needs the same pair of cases as the DNS one:
- an IP SAN that matches handshakes;
- a different IP fails;
- ideally, a certificate carrying the address only as a DNS SAN fails.

## Nits

- `Tls.cpp:478` vs `:509`: the handshake failure's reason is read by `opensslError()` after
  `co_await flushOut(...)`. If the alert write parks, other connections' `ERR_clear_error()` empty
  the queue, and the reason becomes "unknown TLS error" or a neighbour's error.
  `StrictTlsPeer.cpp:195` captures the reason before its flush; do the same here.
- `Tls.cpp:38-41`: the comment says "most recent OpenSSL error", but `ERR_get_error` returns the
  earliest one.
- `Tls.cpp:940-944`: only the first certificate of `caPem` is trusted. A two-CA bundle silently
  loses the second. This is the same shape as the chain bug fixed in `useServerMaterial`: loop, or
  document "exactly one".
- `Tls.cpp:869`: `static_cast<long>(validity.count())` truncates on Windows, where `long` is
  32-bit, for validities over about 68 years, and a zero or negative validity is accepted. Either
  one yields a certificate that is already expired. Refuse them, or use
  `X509_time_adj_ex(days, seconds)`.
- `Tls.cpp:403`: `probeReadable` answers `0` on `close_notify` without setting `_peerClosed`, so
  `isClosed()` still reports open after a watch said EOF. `readPlain` does set it (`:314`).
- `Tls.cpp:673`: `SSL_OP_NO_RENEGOTIATION` is new behaviour: a TLS 1.2 peer that renegotiates now
  fails. The CHANGELOG does not mention it.
- `AsyncBufferedReader.hpp`: the class is still implicitly copyable and movable. It now copies a
  4 KiB `_chunk`, and the new comment's claim that it "stays at one address for the reader's
  lifetime" is not enforced. Delete copy and move.
- `HttpServer.cpp` `serve()`: `handshakeIfNeeded()` has no deadline. In the documented sequential
  accept loop, one client that connects and sends no ClientHello stalls every later connection.
  This is the same shape as the unbounded `readRequest` that was already there; worth a sentence
  in the `serve` doc next to core-cpp#35.
- `HttpServer_test.cpp:733`: `loop.blockOn(core::net::serve(...))` is an unbounded wait
  (testing.md: every wait is bounded). Only ctest's TIMEOUT stops a regression.
- `testing/StrictTlsPeer.cpp:177-182`: `feed()` treats a read ERROR (for example a reset) as EOF,
  so `readToEnd` reports `Truncated` for a transport error. The oracle should keep them apart
  (`Failed`).
- `TlsSocket_test.cpp:228`: the title says "before any application byte", but the case checks only
  that the handshake completes. It does distinguish from the no-op default (the peer would hang),
  so only the title is off.
- `SocketDecorator_test.cpp:177`: `CHECK(watched.expired())` holds with or without the fix. The
  only signal is the crash, which a Release build may not produce. The comment says so. Consider
  registering it under the ASan preset explicitly.

## Checked and found sound

- **Record pump, one read and one write:** `FlushWait::Skip` for reads, `Join` for writes,
  `closeNotify` and the handshake. The single-flush case really distinguishes (`maxInFlight` 2
  without the gate) and verifies ciphertext order at the peer.
- **Error queue:** `ERR_clear_error()` precedes every classified `SSL_*` call. The stale-error case
  distinguishes, because the handshake is already done, so nothing else clears the queue before
  `SSL_read`.
- **`shutdownWrite`:** `SSL_shutdown`, then `Join` flush, then the inner half-close. A `Join` also
  keeps it off a read's in-flight flush. The strict-peer case checks CloseNotify, a refused write
  after the half-close, and a read after it.
- **Verification:** SNI is omitted deliberately; the host is bound on the CTX param with
  `NO_PARTIAL_WILDCARDS`; the store is empty except for the pinned CA.
- **Material and helpers:** chain serving through `add1`, followed by `ERR_clear_error` for the
  "no start line"; the `constantTimeEquals` length and empty handling over `CRYPTO_memcmp`.
- **No OpenSSL type in a public header:** `Tls.hpp`, `ITlsContext.hpp` and `StrictTlsPeer.hpp` are
  clean.
- **Seam gate:** `core-cpp.openssl-seam` has permitted rows with reasons, stale-row refusal and an
  empty-walk refusal. The self-test's 12 cases match the "twelve" in `tests/CMakeLists.txt` and the
  CHANGELOG. The tests carry the `tree-level` label, the `style` steps carry `covers:` markers, and
  the hygiene allowlist has its source-glob row.
- **Packaging and records:**
  - `ITlsContext.hpp` is in core::net's `FILE_SET`, and `testing/StrictTlsPeer.hpp` is in
    net_tls's.
  - The provenance rows follow the table's established "... Task Bn: ..." convention.
  - The `renames.json` removed rows use the post-rename spelling, like the other fastcached
    removed rows.
  - The CHANGELOG has the SelfSignedOptions break under Breaking with migrations; fixes are under
    Fixed and the serve/reader changes under Changed.
- **`SplitSocket::close()`:** the liveness token is read only through the local `weak_ptr`, and
  nothing else is touched after the first retirement.
- **`WriteQueue_test`:** the `PermitSocket` case enqueues in every drain state and asserts one
  write in flight and one frame per write.
- **`WithTimeout_test`:** the lost race leaves no park, and a later read on the same slot works.
- **No `NOLINT`, diagnostic pragma or `SUCCEED`** anywhere in the range.
