# Task A12: Phase A gate, pass 4 findings (`core::net`)

The Phase A gate ran `/code-review` over `e45f730..c4a083a` (Task A6's import of contour's event loop, sockets, TLS and HTTP server, plus its fix round and the module-table work). It found 15 defects.

Fix all of them **test-first**: a case that fails before the fix, in `src/core/net/*_test.cpp`. Phase B rewrites much of this module (B3 replaces EventSource with IoBackend, B6 merges the sockets, B11 merges TLS), which is a reason to fix these **now**, not later: the rewrite should inherit correct behaviour and the tests that pin it, rather than carrying these forward.

## Security and correctness

1. **`src/core/net/HttpServer.cpp:110` — request smuggling through a bare-LF blank line.**
   `parseHead` skips an empty line inside the header block with `continue` instead of ending the head. Given `"GET / HTTP/1.1\n\nHost: evil\r\nContent-Length: 0"`, we parse one request while any front-end that honours bare LF as a terminator sees two. This parser already rejects `Transfer-Encoding` and conflicting `Content-Length` for exactly this reason.
   - The head ends at the first empty line, whichever terminator produced it.
2. **`src/core/net/windows/WindowsListener.cpp:273` — the listener can stop accepting forever.**
   `accept()` calls `WSAResetEvent` on the shared readiness event before parking. A client connecting in the window between `::accept()` returning `WSAEWOULDBLOCK` and that reset leaves `FD_ACCEPT` recorded but unsignalled, and Winsock will not signal again until `accept()` runs — so the coroutine parks forever and the listener goes silent, including for later connections.
   - `WindowsSocket::latchNetworkEvents` already documents and solves this with `WSAEnumNetworkEvents`; the listener is the last bare reset.
3. **`src/core/net/Tls.cpp:353` — an unchecked `BIO_new` becomes a crash.**
   Both results go straight into `SSL_set_bio`, so under memory pressure `wrap()` returns a non-null socket whose first read or write dereferences null — violating its own documented "null on allocation failure". `SSL_new` one line above is checked.
4. **`src/core/net/Tls.cpp:291` — `flushOut()` reports success after dropping ciphertext.**
   The `n <= 0` path is reachable only when `BIO_ctrl_pending` said bytes were queued, so a failed `BIO_read` is reported as "nothing to flush". The handshake then waits for a peer response to a flight that was never written, and both sides hang until an outer timeout. A pending-but-unreadable write BIO is an error; return it.
5. **`src/core/net/posix/PosixSocket.cpp:228` — a zero-length write reads a stale `errno`.**
   `if (n > 0)` consumes only positive returns, so `n == 0` falls through to `auto const err = errno;`, which holds whatever the previous syscall left. Depending on that value the loop spins on an already-writable socket, retries forever, or reports an error that never happened. `read()` at `:98,:103` has the same shape but handles EOF first.
   - Handle the zero return explicitly.
6. **`src/core/net/WriteQueue.cpp:56` — `close()` dereferences a socket pointer nobody validated.**
   The constructor stores a raw `ISocket*` as given, and `close()` is `noexcept`, so a null — which `ITlsContext::wrap` is documented to return on allocation failure — crashes at teardown. `.agent/rules/design-principles.md` asks for the check at construction: a constructed object is usable.
7. **`src/core/net/AsyncBufferedReader.hpp:127` — a stale scan offset skips buffered bytes.**
   `beginScan()` resets the offset only when the scanner *kind* changes, not when `readUntil` is called with a different delimiter. After a `readUntil("\r\n\r\n")` returns early, a following `readUntil("X")` starts searching near the end of the buffer and misses an `X` already in it. The Scanner enum's own comment describes this class of bug for the kind; the delimiter belongs in the key too.

## Contracts and divergences

8. **`src/core/net/ISocket.hpp:85` — `isClosed()` documents something no implementation does.**
   The header says "true once `close()` has been called **or** the peer closed and a read observed EOF", but neither `PosixSocket` nor `WindowsSocket` touches `_closed` on EOF. `SplitSocket::isClosed()` builds on it ("closed once either half is"), so a consumer keeps polling a dead connection.
   - Decide: latch `_closed` on EOF, or narrow the documented contract. Prefer latching — the header's version is what callers need — and say which you chose and why.
9. **`src/core/net/posix/PollEventSource.cpp:48` — the two poll backends disagree about `FdInterest::None`.**
   POSIX submits the registration anyway and routes `POLLHUP`/`POLLERR`/`POLLNVAL` as read-readiness, so a muted descriptor still wakes its flow; Windows excludes such registrations entirely and stays silent. `FdInterest::None` is public API, documented as "mute the fd without detaching it", and `EventSourceParity_test` does not cover it — in the one class whose parity suite exists to keep the backends identical.
   - Make them agree, add the parity coverage, and say which behaviour you chose.
10. **`src/core/net/testing/ScriptedEventSource.hpp:78` — `detach` is not idempotent**, though `EventSource.hpp` documents it as such and every real backend honours it. The loop double-detaches on normal paths (`notifyHandleClosing` then `unregisterFdWaiter`; `requeueForCancellation` and `wakeAllWaiters` before `await_resume`), so the counter under-reports and any future leak assertion against the scripted source passes silently.
11. **`src/core/net/posix/PosixListener.cpp:81` — the listening socket has a window with no `FD_CLOEXEC`.**
   Both `PosixListener::bind` and `UnixListener::bind` call bare `::socket()` and apply CLOEXEC only after `listen()`, while `posix/FdUtils.hpp` — already included by both — provides `makeStreamSocket()`, which sets `SOCK_NONBLOCK|SOCK_CLOEXEC` atomically where the platform allows and which `connect()`/`connectUnix()` already use. A fork and exec from another thread in that window inherits the listening socket, keeping a port or socket file alive after the daemon exits.

## Hygiene and test discipline

12. **`src/core/net/EventLoop.cpp:21` throws `std::runtime_error` with no `<stdexcept>`** in the translation unit, compiling only through a transitive include. Every other thrower in the tree includes it.
13. **`src/core/net/Socket_test.cpp:277` — a `REQUIRE` inside a `whenAll` arm turns a failure into a hang.**
   `whenAll` deliberately does not cancel siblings on an exception; the server arm is parked in `accept()` and nothing closes the listener, so the test spins forever instead of failing. `.agent/rules/testing.md`: a `REQUIRE` above a stop turns a red into a hang.
   - Sweep the module's tests for the same shape, not just this one.
14. **`src/core/net/EventSourceParity_test.cpp:1000` — the descriptor-exhaustion case lowers the process-wide `RLIMIT_NOFILE` and restores it with a plain statement.** Any throw in between — the `attach()` in the middle allocates — leaves every later case in the binary running under a squeezed limit, producing a cascade whose cause is invisible. `detail/ScopeGuard.hpp` exists in this module.
15. **`src/core/net/Tls_test.cpp:60,88,130,151` dereference `makeSocketPair()`'s `std::expected` unchecked**, so a loopback failure is undefined behaviour rather than a test failure. Every other test file in the module writes `REQUIRE(pair.has_value())` first.

## Then

- The presets the constraints name on Windows and WSL, including the sanitizers and TSan; `python scripts/clang-format.py --check`, `ctest -L hygiene`, `mkdocs build --strict`.
- Findings 2, 9 and 11 are platform-specific: say in the report how you verified each where it lives, and use CI for what you cannot run.
- CHANGELOG entries, `Breaking` with a migration for anything a consumer would notice (finding 8's decision in particular), and provenance notes recording the divergence from contour.
- **Two other agents are working this branch** in `src/core/{Utils,Flags,Escape,FNV,Base64}*`, `src/core/cli/`, `src/core/log/` and `src/core/platform/`. Yours is `src/core/net/`. Rebase before pushing, never force-push, and report rather than fix anything of theirs.
- **Staging discipline:** explicit pathspecs are not enough for `CHANGELOG.md` and `.agent/reference/provenance.md`, which all three of you edit. Read every hunk with `git diff -- <file>` before staging, and stage only your own (`git apply --cached` from a trimmed patch; `git add -p` is interactive and unavailable). Confirm with `git diff --cached -- <file>` and check `git show --stat` before pushing.
- Push, watch CI and portability to green.
- Write your report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A12-report.md` with RED/GREEN per finding.
