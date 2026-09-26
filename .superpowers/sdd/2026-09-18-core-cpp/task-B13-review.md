# Task B13 review -- 8f1c70f..5686d6b (22 commits)

Reviewer: review-B13. Read-only. File:line references are at 5686d6b unless a commit is named.

**Verdict: CHANGES** -- no blocker; four should-fix items, one of them a library defect.

## Should-fix

### S1. WindowsSocket: a retired read that unwinds leaves `_readRetired` set, and the socket's NEXT read completes `Cancelled` for nothing

- **Where:** `src/core/net/windows/WindowsSocket.cpp:113-116` (`cancelRead`) and `:157-164` (`parkUntilReady`). Introduced by 670a7ed; d6912e4 kept it.
- **Mechanism:** `cancelRead` sets `_readRetired = true` and then resumes the waiter inline. The waiter resumes into `WaitHandleAwaiter::await_resume` (`EventLoop.hpp`), which throws `OperationCancelled` whenever `_token.stop_requested()` holds. The `catch (...)` then clears `_readWaiter` and rethrows. It never consumes `_readRetired`, so the flag is still `true` on a live socket.
- **Failure scenario:** a flow reads under `withTimeout`. The timer wins and stops the read's token, and `requestCancel` queues the park, or step 1 has already moved the waiter into the ready queue. In the same turn the timeout handler calls `sock->cancelRead()`, which is the documented way to retire a stale read. `cancelPending` answers true for both states, the inline resume throws, and the flow unwinds as it should. The socket now carries `_readRetired == true`. The next `read()` on that socket parks and is woken by real data. Line 164 then consumes the stale flag and returns `ParkEnd::Retired`, so the read completes `NetErrorCode::Cancelled` ("the read was retired by cancelRead") even though nobody cancelled it. A caller that treats `Cancelled` as the end of the connection drops a healthy connection.
- **Fix:** in the catch, while `!lifetime.expired()`, also do `_readRetired = false`. Alternatively, have the frame clear the flag on every way out, as `_readWaiter` already is. Add a case: stop the token, call `cancelRead` before the turn drains, then do a second read that must return data. Only WFMO is affected. It is not the default backend, but it ships in v0.1.0.
- **Related nit:** the header says `_readWaiter` is "cleared on every way out of the park" (`WindowsSocket.hpp:186`). That is untrue when the frame is destroyed rather than resumed, because neither the catch nor the normal path runs. I found no reachable path that destroys a parked read while the socket and its loop both live, so this is only an overclaim in the comment.

The d6912e4 fix is otherwise correct, which was the question asked:
- The `weak_ptr` is taken while the socket is alive.
- The unwinding path reads only the control block, which the frame keeps alive.
- `_lifetime` expires with the socket's members, after `close(Cancel)` has run in the destructor body.
- The normal path touches `this` only when the resumption is not an `Abandoned` or stop throw, as `latchNetworkEvents()` already did.

### S2. CHANGELOG `### Imported` does not list every import, and two new texts say it does

- **Where:** `CHANGELOG.md:17` ("see *Imported* below for each import and what changed on the way in") and `docs/provenance.md:23` ("The `Imported` section of the changelog lists every import by commit"), against `CHANGELOG.md:2067-2091`.
- **Missing, counted from `.agent/reference/provenance.md` at 5686d6b:**
  - endo `src/tui` at `f774a210`: 151 rows. The largest import has no row at all, and no Imported row mentions `src/tui` or `core::tui`.
  - fastcached `src/FastCache/Net` at `0708dd54`: 70 rows. The table covers only the datagram, blocking, TcpClient and HealthProbe files. The reactor and `EventLoop` merge, the socket contract, the dial, TLS and the IOCP files are absent.
  - fastcached `5389e29a`: `TuiRuntime.hpp` and its test.
- **Failure scenario:** a consumer migrating on the release notes, as the brief intends ("imports with SHAs"), finds no record that `core::tui` or most of `core::net` came from upstream, or at which commit. The SHAs themselves are correct: 212, 110, 103, 4, 2 and 2 rows match the per-file table exactly.

### S3. The per-consumer Breaking summary is incomplete for contour, endo and tuidu

- **Where:** `CHANGELOG.md:39-50`.
- **Status of the detailed list:** every B6-B11 break has a bullet under `### Breaking`. The gap is the per-consumer summary, which is what the brief asked for and what a consumer reads first.
- **contour bullet omits:**
  - `generateSelfSignedCertificate()`/`makeSelfSignedServerContext()` now take `SelfSignedOptions`, and the default CN changed from `"contour-daemon"` to `"localhost"`. contour calls `makeSelfSignedServerContext()` in `src/vthost/Daemon.cpp:127`, so the certificate it serves silently changes CN.
  - A TLS read of a peer that closed without `close_notify` is now `ConnReset`, not `0`.
  - `EPIPE` is now `SystemError`.
  - A stop of the flow's own token on a socket operation throws `OperationCancelled` instead of returning `Cancelled`.
  - `Task`'s rvalue-only `operator co_await` (the awaiter owns the task), and `whenAll`/`whenAny` taking tasks by rvalue. These are the `coro` breaks contour's `src/coro` callers meet first.
- **endo bullet** lists only `src/platform` and `src/tui`. Per `.agent/reference/consumers.md`, endo fetches contour's `src/{crispy,coro,net}` (and `D:/endo/src/net` carries `Tls.cpp`), so endo meets every contour item as well. The bullet should say so.
- **tuidu bullet** says "the `coro` and `tui` changes above", but no coro change appears above; only the namespace rename does. The Task and `whenAll`/`whenAny` breaks belong there.

### S4. LoopAffinityCanary waits without a bound

- **Where:** `src/core/net/LoopAffinityCanary.cpp:183`: `while (!entered.load(...)) std::this_thread::yield();`
- **Failure scenario:** a regression in `post` or `wake` on the default backend means the worker never runs the posted callback. All 12 modes then spin until ctest's `TIMEOUT 60` and report a bare timeout that names nothing. That is exactly what `.agent/rules/testing.md` §"Every wait is bounded, and says what it was waiting for" forbids.
- **Fix:** bound the wait (for example 30 s of steady time), print `loop-affinity-canary: the worker never ran a posted callback`, and exit 1. It is not a false pass, since a timeout is red, but it breaks the rule the canary exists to uphold.

## Nits

- **N1. The "written here" count is stale.** `CHANGELOG.md:22` and `docs/provenance.md:21` say 95 files were written here, but the table has 96 `origin: core-cpp` rows. 8a62570 counted before 4a5a447 added `detail/ParkId.hpp`.
- **N2. `ParkId.hpp` repeats a figure its own commit refutes.** `src/core/net/detail/ParkId.hpp:10` says ParkTable "is 99.4% of what `ISocket.hpp` cost a translation unit". 4a5a447's measurement says the saving is 13% (130,784 to 113,503) and that the 99.4% `-H` figure over-counted.
- **N3. The doc misdescribes how the loop-affinity canary is judged.** `docs/design/coroutines-and-lifetimes.md:140` says the first three canary rows "each print a marker ... registered with `PASS_REGULAR_EXPRESSION`". Row 1, loop-affinity, is judged on the assertion's own text, and the table row above says so.
- **N4. `hostdriven-canary.closedPark` is judged by the weaker marker scheme.** Its pass is the marker printed before `addTimer` (`HostDrivenCanary.cpp:205`, `CMakeLists.txt:425`), so any abort after the marker also passes, for example a different assertion on the `registerPark` path. ae91149 argues, correctly, that the assertion's own text ("armHostWake with a closed park pending") is the discriminating expression. Using it here costs one line.
- **N5. The error-table change has meaning shifts the CHANGELOG does not name.**
  - `EINTR` and `WSAEINTR` now map to `Cancelled`. The one reachable site is a POSIX non-blocking `connect()`: `beginConnect` treats `EINTR` as neither pending nor `SystemError` any more. It now reads as "the resource cancelled", although POSIX says the connect continues asynchronously.
  - `ETIMEDOUT` on a `PosixSocket` read (keepalive or user-timeout death) is now `Timeout`. That makes `isDeadlineExpiry()` true, indistinguishable from `setReceiveDeadline`'s own `Timeout` except by `systemCode`.
  - Both are consistent with IOCP, so they are intended. The CHANGELOG "Changed" entry should name them, because a caller that loops on `isDeadlineExpiry` now re-reads a dead connection once more before EOF.
- **N6. `ci-retry.sh` retries every non-zero exit, not only transient ones.** It is bounded at three attempts, keeps the last status, and warns on each retry, so a real failure is delayed by 45 s, not masked. That is acceptable. `choco install ninja` and `choco upgrade llvm` in the windows job are not wrapped, but they fetch from a third-party server too.
- **N7. check-layering holds sub-targets to their module's row.** `tui_output` files are held to `tui`'s row, not their own, so a `tui_output` file including `<core/net/...>` passes. That is documented in the header, but "`core::tui_output` depends on base only" is the one include edge `rules/tui.md` singles out, and it is the one this scan cannot see.
- **N8. `check-text-encoding`'s `double` pattern will flag legitimate text.** A Latin-1 letter followed by a typographic quote or dash (for example `cafe` with an accent followed by a right single quote) matches `DOUBLE_ENCODED` (`scripts/check-text-encoding.py:69`). The ALLOW list is the escape hatch. The limitation is worth one sentence in the docstring.
- **N9. The new canaries are registered with a bare `add_test`.** They sit inside `if(CORE_CPP_TESTING AND NOT EMSCRIPTEN)` rather than going through `core_cpp_add_test` as the brief asked. The gating the brief cared about (a040878) holds, and this matches the existing hostdriven canary.

## Verified, no finding

**c5ffc8f (#41).**
- Every park-to-ready path goes through `queueParkedWaiter`: `takeWaiter` has one call site, and that site now passes the park.
- `unregisterPark(source)` is a no-op for an invalid or retired id, because ids are never reused. That covers the finished-frame branch, which unregisters first, and a deadline park already taken.
- Loop affinity is asserted before the call.
- The test is red without the fix on `parkedWaiterCount() == 1` and is bounded (500 turns, with an `INFO` saying what it waited for).

**bcdea27.**
- The three states are handled, and the flows' only suspension is a direct `co_await _loop.waitReadable`, so `cancelPending(outer handle)` matches.
- Destroying the frames runs only awaiter destructors, while the members are alive.
- The new case is bounded, and its commit records that it is red with #41 reverted.

**670a7ed.**
- `PublishSelf` records `parkUntilReady`'s own handle, which is the handle the park holds.
- `cancelRead` touches nothing after `resume()`.
- `CancelRead_test` hung without the fix.
- See S1 for the defect.

**b3aa435.**
- The POSIX dial loses no row: `EPERM`, `EAFNOSUPPORT` and `EPROTONOSUPPORT` were added to the table.
- `EPIPE` is still `SystemError` on POSIX. On Windows, `WSAECONNABORTED` and `WSAENETRESET` are still `SystemError`.
- The row tests assert every row, and their commit records that the POSIX test is red on errno 1, 97 and 93 against the old table.
- Meaning changes: for sockets, codes that were `SystemError` gain categories, and that is in the CHANGELOG (see N5 for two it omits).

**4a5a447.**
- `ParkId` and its `std::hash` moved intact.
- `detail/ParkId.hpp` is in the module's `FILE_SET` and in the wasm subset.
- The CHANGELOG notes the transitive-include break.

**Gates.**
- Each new check has a self-test (or, for check-layering, an in-script planted-violation fixture) that proves it can fail.
- The file-reading checks are labelled `tree-level`, and each has a `style` step with a `covers:` marker. `tidy-record.py` reads a build tree, so it is not tree-level; it runs as the clang-tidy job's last step, and its self-test is tree-level.
- The canaries use `PASS_REGULAR_EXPRESSION` plus `FAIL_REGULAR_EXPRESSION` and `SKIP_RETURN_CODE`, with no `WILL_FAIL`.
- The 12 loop-affinity PASS strings each match the first literal of that member's assertion (EventLoop.cpp lines 53, 241, 620, 718, 741, 767, 789, 813, 840, 939, 962 and 1079).
- Exit 77 is used honestly:
  - `check-text-encoding`: git cannot list the tree, so nothing was scanned. In the `style` job a 77 fails the step.
  - The canaries: `NDEBUG`.
- `check-tree-level-coverage` now refuses a missing `ci-ok` or one that does not need `style`, with self-tests that are red on mutation.

**7e2a0dc.** Spot-checked across `net/windows`, `platform` and `Utils.hpp`. The changes are real fixes, with no suppression:
- The `InvalidSocket` constant.
- `InvalidHandle`'s type is unchanged (`void* const`).
- A member init moved into the initializer list.
- `ranges` algorithms.
- `sizeof(sockaddr_in)`, which equals `sizeof(out)`.
- A `const` dropped to allow the implicit move.
- The `.clang-tidy` addition is a naming option, not a disabled check.

**General.**
- No `NOLINT`, diagnostic pragma, `/wd` or `SUCCEED(` is added.
- No file in the range contains a CR byte.
- The only stale-looking `WILL_FAIL` mentions left are the ones that explain the replacement.
- The family table in `HostDrivenLoop_test.cpp` has the 13 members that threading.md names.
