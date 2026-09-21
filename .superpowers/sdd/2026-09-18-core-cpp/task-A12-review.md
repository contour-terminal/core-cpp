# Task A12 review: Phase A gate, pass 4 (`core::net`)

Reviewed `c4a083a..origin/master` filtered to `src/core/net/` and `CHANGELOG.md`, plus the
production files themselves, `.agent/reference/provenance.md`, and the consumer checkouts
`D:\contour` and `D:\endo`. Read-only: nothing in the tree, index, HEAD or any branch was touched.

### Finding Verdicts

| # | Verdict | Where | Would the test fail without the fix? |
|---|---|---|---|
| 1 | **FIXED** | `src/core/net/HttpServer.cpp:126-131` | Yes. `HttpServer_test.cpp:347` — old code folded `Host: evil` into request one and `seen` was set; `REQUIRE_FALSE(seen.has_value())` fails. |
| 2 | **FIXED** | `src/core/net/windows/WindowsListener.cpp:282`, `windows/NetworkEvents.cpp:7` | Not as a behavioural RED. `windows/NetworkEvents_test.cpp` pins the *primitive* (and its control arm reproduces the lost wake-up); the production call site is covered by inspection plus a guard that hangs rather than fails. See Issues. |
| 3 | **FIXED** | `src/core/net/Tls.cpp:376-385` | No test, and "unreachable" holds — see below. |
| 4 | **FIXED** | `src/core/net/Tls.cpp:306-308` | No test, and "unreachable" holds — see below. |
| 5 | **FIXED** | `src/core/net/posix/PosixSocket.cpp:233` (capture), `:245-247` (zero return) | No test. "Unreachable" holds for the transports in scope, but a seam was available — see Issues. |
| 6 | **FIXED** | `src/core/net/WriteQueue.hpp:66,176` | Yes. `WriteQueue_test.cpp:411` — old code constructs cleanly, `REQUIRE_THROWS_AS` fails (no crash: `~WriteQueue` does not touch the socket). |
| 7 | **FIXED** | `src/core/net/AsyncBufferedReader.hpp:133-140`, `.cpp:74` | Yes. `AsyncBufferedReader_test.cpp:532` — old code resumes at offset 4 and reports `Eof` for the `X` at index 3. |
| 8 | **FIXED** (latch) | `posix/PosixSocket.hpp:47`, `.cpp:102,116,192`; `windows/WindowsSocket.hpp:58`, `.cpp:139`; `Tls.cpp:163` | Yes, both cases. `Socket_test.cpp:268` and `:297` — old code answers `false` after a read observed EOF. |
| 9 | **FIXED** (mute) | `posix/PollEventSource.cpp:56`, `linux/EpollEventSource.cpp:95`, `bsd/KqueueEventSource.cpp:165`; `windows/PollEventSource.cpp:100` already did | Partly. `EventSourceParity_test.cpp:920` (the hang-up case) is a genuine RED on poll and epoll. `:877` (the portable case) **passes against the old code on all four backends** — a muted registration on a merely-readable fd was already silent everywhere; it is a guard, not a RED. |
| 10 | **FIXED** | `testing/ScriptedEventSource.hpp:89` | Yes. `EventLoop_test.cpp:293` — old counter goes 2→1→0, `CHECK(attachedCount() == 1)` fails. |
| 11 | **FIXED** | `posix/PosixListener.cpp:80`, `posix/UnixListener.cpp:179` | No. `posix/UnixSocket_test.cpp:295` characterises `makeStreamSocket()`, which was not changed, so it passes against the old code. Correctly labelled as characterization. |
| 12 | **FIXED** | `src/core/net/EventLoop.cpp:8` | N/A (a build-only defect). |
| 13 | **FIXED** as stated | `Socket_test.cpp:421-435`, `:373-383`, `:627-643` | No assertion remains inside any `whenAll` arm in the module (verified by sweep). The *sibling shape* the fix's own comments describe survives in four other places — see Issues. |
| 14 | **FIXED** | `EventSourceParity_test.cpp:1090` | N/A. Guard ordering is correct: both `REQUIRE`s that could throw run before the limit is lowered or after it is already restored. |
| 15 | **FIXED** | `Tls_test.cpp:61,91,135,158` | No — it converts UB into a test failure. Four sites was the complete set; no unchecked `makeSocketPair()` deref remains anywhere in the module. |

**On "no real transport can reach it" (3, 4, 5, 12).**

- **3 and 4 are genuinely undriveable here.** `TlsSocket` and `TlsContext` live in an anonymous
  namespace inside `Tls.cpp`, so `flushOut()` and `wrap()`'s allocation path have no seam at all;
  and both branches need OpenSSL to misbehave (`OPENSSL_malloc` failure, `BIO_read` failing on a
  memory BIO that `BIO_ctrl_pending` just said holds bytes). `CRYPTO_set_mem_functions` cannot be
  installed after the library has initialised, and `core::testing_main` is shared, so even the
  allocator route is closed. Accepted.
- **12** is correct: no case can observe a transitive include.
- **5 is the weak one.** "POSIX gives `write(2)`/`send(2)` no way to return 0" is true for the
  stream sockets, pipes and PTY masters `PosixSocket` serves. But the branch *is* driveable behind
  a seam, and this repository ruled in the same round that an untestable branch gets one:
  `core::platform::NativeFileSystem` now takes a `RenameFunction` at construction precisely because
  the two-hop recase was unreachable from any test (CHANGELOG, controller ruling R53). An injected
  write primitive on `PosixSocket` would have made finding 5 a one-line RED and would have been the
  consistent answer. Not a blocker — Phase B3's `IoBackend` is the natural home — but the report
  should not present it as being in the same class as 3 and 4.

### The Two Decisions

**Finding 8 — latch on EOF, in a separate flag. Right call, and implemented cleanly.**
The split between `_closed` (gates `read`/`write`) and `_peerClosed` (answers `isClosed()` only) is
exactly what keeps the half-close working, and it is documented on both socket classes and in
`ISocket.hpp:82-88`. `TlsSocket` latching its own `close_notify` rather than only forwarding is
correct — the TLS session can end while the TCP connection is open. Nothing in `core::net`
production code calls `isClosed()` (grep: only `SplitSocket`, the two implementations and tests),
so `WriteQueue`'s drain and the HTTP server are untouched. I confirmed the migration note's claim
independently: neither `D:\contour` nor `D:\endo` calls `net::ISocket::isClosed()` outside their own
vendored `src/net/` copies (contour's `isClosed()` hits are `vtpty::Pty`/session, a different type).

The one thing the CHANGELOG under-states is `SplitSocket`. It says `SplitSocket::isClosed()`
"inherited that", which reads as "it was also broken". What actually changes for a consumer is the
opposite direction: a `SplitSocket` built from a tmux client's stdin/stdout now reports **closed as
soon as the read half sees EOF**, while the write half is still perfectly writable. A consumer that
polls `isClosed()` to decide whether to keep flushing output would now stop flushing at the moment
stdin ends — which is the one case `SplitSocket` exists for. That deserves an explicit sentence in
the migration, not an inference.

**Finding 9 — mute everywhere. Right call, and the epoll argument settles it.**
Level-triggered `EPOLLHUP` on a registered descriptor really does make "wake while muted" a
spinning pump, and `None` that behaves like `Read` would differ from `Read` only in name. The
per-backend implementations are all correct and each closes a second, unreported hole:

- poll(2) submits `fd = -1` and keeps the entry, so the `fds[i]`/`registrations[i]` pairing at
  `PollEventSource.cpp:79-80` still holds.
- epoll and kqueue return before `_registered` is populated, which additionally means a *later* real
  registration on the same descriptor is no longer taken for a duplicate — and on kqueue it removes
  a real latent bug: under the old code `detach()` of a muted registration called `dropFilters(fd)`
  and tore down a live `Read` registration on the same descriptor.
- Windows already excluded it from the wait set, and `collectSignalled` already keyed on
  `hasInterest`, so it is unchanged.

`FdInterest::None` is used nowhere in production in this repo or in contour/endo, so the break is
theoretical. Parity coverage is complete for what can be covered: the portable case runs on every
backend `makeEventSource` builds on the host, and the hang-up case (the only one that actually
discriminates) is POSIX-only for the stated, correct reason.

**What they make harder later.** Little. B3's `IoBackend` inherits a three-way per-backend
implementation of one public value, but the two parity cases pin the contract, which is the point of
fixing it now. B6's merged socket inherits one extra bool with a documented reason. `_peerClosed`
also has to survive the merge of `PosixSocket`/`WindowsSocket`, and `consumeNetworkEvents` is a good
extraction for it. The one small friction is finding 6: a constructor that *throws* does not compose
with the `std::expected` style the rest of the module uses, and a named factory
(`WriteQueue::create(...) -> std::expected<WriteQueue, NetError>`) would have been the guideline
answer. It is defensible as a contract guard and is recorded under Breaking, so I do not ask for a
change now, only that B does not propagate the throw.

### Strengths

- **The HTTP fix chooses refusal over re-framing, and that is the right trade.** Ending the head
  and leaving the remainder would have swapped one desync for another; refusing is the same posture
  the parser already takes for `Transfer-Encoding` and a conflicting `Content-Length`. I checked the
  interaction the brief asked about: `readUntil("\r\n\r\n")` has already consumed the bytes behind a
  bare-LF blank line, and the `break` arm is taken only when `lineStart >= headerText.size()`, i.e.
  when the blank line really is the end of the block — so the head never ends with bytes silently
  dropped. And it cannot desync downstream at all, because `readRequest` builds a fresh reader per
  call, `writeResponse` always sends `Connection: close`, and `serve()` closes the connection after
  one request. The framing is single-request by construction.
- **Finding 2 uses the module's own primitive, not a second mechanism.** `consumeNetworkEvents` is
  now the single place a Winsock indication is taken off an event, `WindowsSocket::latchNetworkEvents`
  was refactored onto it, no `WSAResetEvent` remains anywhere in `src/core/net/`, and the header
  says why it never will. The recorded-but-unsignalled state is now *impossible* rather than
  unlikely: `WSAEnumNetworkEvents` clears record and event in one step, and every park in
  `accept()` is reached only through a clear, so any indication raised afterwards both records and
  signals.
- **Finding 4's error really does reach the caller.** I traced all five `flushOut()` call sites
  (`Tls.cpp:91,95,127,134,138,246`); every one propagates, including the handshake's, which records
  it as the sticky `_handshakeError` that parked waiters observe.
- **Finding 3 leaks nothing.** `BIO_free(nullptr)` is a no-op, so the one-sided case is handled, and
  the `SSL` is freed before returning null — ownership has not transferred at that point.
- Finding 7's fix is complete in a way the finding did not ask for: `readExactly` already clamps
  `_scanOffset = max(_scanOffset, _consumed)` (`AsyncBufferedReader.cpp:114`), so the remaining
  "offset behind the cursor" shape cannot underflow `found - _consumed`.
- CHANGELOG coverage is genuinely complete: all fifteen appear, the three API breaks are under
  **Breaking** with migrations, and `.agent/reference/provenance.md:164-166` has rows for all three
  new files.

### Issues

#### Critical

None.

#### Important

- **`Socket_test.cpp:233` — the new regression guard for finding 2 hangs instead of failing.** If
  the Windows listener ever goes silent again, `acceptSequentially` parks in accept #N, its
  `whenAll` sibling has already finished, nobody closes the listener, and `loop.blockOn` never
  returns. The case's own comment says so ("it goes SILENT … the suite's timeout is what reports
  it"), and `core_cpp_add_test` sets no `TIMEOUT` (`cmake/CoreCppTargets.cmake:317-320`), so ctest's
  default 1500 s applies: the failure costs 25 minutes of silence and reports "Timeout" without
  saying what it waited for. This is the same rule finding 13 exists to enforce
  (`.agent/rules/testing.md`: every wait is bounded and says what it waited for). Bound it — race
  the run against `loop.delay(...)` with `whenAny`, or have `connectSequentially` close the listener
  once it has made its last connection.
- **Finding 13's sweep is incomplete.** No `REQUIRE` remains in a `whenAll` arm, so the finding as
  written is closed, but the shape the fix's own comments name — "an arm that returns early without
  a stop turns a red into a hang" — survives in four places that were not touched:
  `Socket_test.cpp:91` (`echoClient`), `posix/UnixSocket_test.cpp:103` (`connectAndProbe`),
  `EventSourceParity_test.cpp:104` (`connectAndSend`) all `co_return` on a failed connect while
  their sibling is parked in `accept()`; and `Tls_test.cpp:258` / `:333` join a server thread that
  is parked in `accept()` on its own loop, with no path that releases it if the client arm returns
  early or `wrap()` returns null. The three fixed call sites and these four differ only in which
  ones the implementer happened to edit.
- **The HTTP head still diverges from a front-end on whitespace before the colon.** `parseHead`
  does `trim(line.substr(0, colon))` (`HttpServer.cpp:144`), so `"Content-Length : 5"` is accepted
  as a `Content-Length`. RFC 9112 §5.1 makes rejecting this a MUST for exactly the reason finding 1
  gives, and it is the same family as the obs-fold rejection two lines above. Exploitability here
  is low — this server is one-request-per-connection and always closes — but the brief asked
  whether framing can still differ from a front-end's, and this is where it can. A one-line refusal
  (`if (colon > 0 && (line[colon-1] == ' ' || line[colon-1] == '\t')) return std::nullopt;`) closes
  it. Worth a follow-up ticket rather than a re-open of A12.

#### Minor

- **The `SplitSocket` consequence of finding 8 is not in the migration.** As above: the CHANGELOG
  entry describes `SplitSocket` as a victim of the old bug, not as the place where the new answer
  changes what a consumer sees. One sentence — "a split socket whose read half observed EOF now
  reports closed although its write half is still usable" — would make it accurate.
- **A zero-length read now latches the EOF flag.** `PosixSocket::read` with an empty span makes
  `::recv(fd, p, 0, 0)` return 0, which sets `_peerClosed` (`PosixSocket.cpp:100-104`); same on
  `WindowsSocket.cpp:138-142` and `readWithFd` (`:190`). A caller error is thereby recorded as a
  peer hang-up, permanently. Nothing in the tree reads zero bytes, so this is latent only.
- **`wait()`'s "nothing to watch" guard now counts registrations no backend watches.**
  `PollEventSource.cpp:65` tests `fds.empty()` and `EpollEventSource.cpp:153` tests
  `_registry.size() == 0`, both of which are false for a source holding only muted registrations —
  so `wait(-1)` blocks forever there, while the Windows backend (`windows/PollEventSource.cpp:105`)
  returns a benign timeout because it filters `None` out of `handles` first. Unreachable through
  `EventLoop` (its post self-pipe is always a `Read` registration), but it is one more place the
  backends disagree about `None`, in the class whose parity suite exists to stop that.
- **`consumeNetworkEvents` returns 0 when `WSAEnumNetworkEvents` itself fails**, leaving the event
  possibly still signalled. `WindowsListener::accept` then parks, wakes immediately, retries accept,
  fails again — a tight retry loop where the old code parked for ever. Better than a hang, but the
  header's "the caller parks, and the ordinary readiness path resolves it" is optimistic; it is
  worth one sentence saying the enumeration failing repeatedly spins.
- **`beginScan` lost its `noexcept`** and now stores the delimiter in a `std::string` member
  (`AsyncBufferedReader.hpp:133,148`). Correct — the key has to include the delimiter — but it
  allocates whenever the delimiter changes and exceeds SSO, and the member is now an unconditional
  `std::string` on every reader. A `std::string` was the simple choice; B's HTTP work may want to
  key on a hash or a small fixed buffer.
- **`core_cpp_add_test` sets no ctest `TIMEOUT`.** Independent of this task, but it is what turns
  every one of the hang shapes above into a 25-minute stall rather than a prompt red.

### Assessment

**Task quality: Approved.** All fifteen findings are genuinely fixed at the file:line the gate named,
the two judgement calls (latch on EOF, mute everywhere) are the right ones and are implemented
consistently across all four backends and all three socket classes, and the CHANGELOG records every
break with a usable migration. The gaps are in the test discipline rather than the production fixes:
the finding-2 regression guard hangs instead of failing, finding 13's sweep stopped at the call
sites it edited, and two of the report's claims are overstated — six findings have a behavioural RED,
not nine, and finding 5's "no seam exists" sits awkwardly beside the injected rename primitive this
same round added for the same reason.
