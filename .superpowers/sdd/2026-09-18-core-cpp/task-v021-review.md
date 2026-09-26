# Review: core-cpp v0.2.1 (e443af4..bf139f8)

Reviewer: review lane, read-only. Tree: D:/core-cpp-wt-v021 at bf139f8. Nothing was built or run;
each finding below was traced in the source at the cited lines.

## Verdict: CHANGES

Item 6 does what it says for the case contour hit, and the core of it is sound. `complete()`
settles at once, and `await_resume` reads only the awaitable's own state (a string-literal
`NetError`), so the settled result never refers to the resource. Data-wins and the Cancelled
VALUE are unchanged. A stop that lands between settle and drain throws only where no value
was produced, which matches `await_resume`'s documented order. Deferral does open two new
use-after-free windows, though, and one of them is in core-cpp's own code. There is also a
CHANGELOG/SemVer question the lead should rule on.

## Blockers

### B1. WindowsSocket::cancelRead, then destroying the socket before a turn, writes into the freed socket
- `src/core/net/windows/WindowsSocket.cpp:141-148` (`cancelRead`) and `:175-196` (`parkUntilReady`).
- `cancelRead` takes the `parkUntilReady` frame back with `cancelPending`, which also unregisters
  its park (`EventLoop.cpp:699-710`), and queues it with `resumeSoon`. When the socket is
  destroyed before the next drain, `~WindowsSocket` -> `close(FdWakePolicy::Cancel)` ->
  `notifyHandleClosing(_event, Cancel)` finds no park for that frame, so nothing marks it
  abandoned. The drain then resumes it. `WaitHandleAwaiter::await_resume` (`EventLoop.hpp:1122-1135`)
  gets `FdWakeReason::Ready` from `wakeReasonOf` because the id is not in `_abandoned`, the token is
  not stopped, and it returns normally. Then `parkUntilReady` runs `_readWaiter = {}` and
  `std::exchange(_readRetired, false)` on the freed `WindowsSocket`. The `_lifetime` weak_ptr guard
  exists (`:183`), but only the `catch` path checks it.
- Scenario (WFMO loop): a reader flow is parked in `sock->read(...)`, and the owner runs
  `sock->cancelRead(); sock.reset();` (or `cancelRead()` followed by destroying the connection
  object) inside one turn, which is a heap-use-after-free. Before this range the victim was resumed
  inside `cancelRead()` while the socket was alive.
- The same hole already existed for `close()` then destroy on WFMO, because `close(Resume)`
  queued the waiter through the closed-park list and `~WindowsSocket`'s `close(Cancel)` returns
  early on `_closed`. The range extends it to `cancelRead`, and one fix covers both: check
  `lifetime.expired()` on the normal path after `co_await _loop.waitReadable(_event)` too, and
  throw `OperationCancelled` before touching a member.
- Severity: blocker. The release exists to close memory-safety windows of exactly this shape, and
  this one is in core-cpp's own code. WFMO is not the default backend, and the fix is two lines.

## Should-fix

### S1. A borrowed frame whose waiter is queued during `~EventLoop` step 5 later calls `cancelPending` on a destroyed loop
- `src/core/net/IoAwaitable.hpp:195-204` (the new destructor branch) and `:355-360`, together with
  `src/core/net/EventLoop.cpp:53-160` (teardown).
- A callback park (every `PosixSocket` and `IocpSocket` read or write) is neither queued by
  `unparkEverything` (`EventLoop.cpp:1391-1398`) nor called. Step 4 drops it. In step 5,
  `_roots.clear()` destroys a spawned root that owns socket S. `~PosixSocket` -> `close(Cancel)`
  -> `abandon()` on the operation of a flow T parked on S -> `complete()` -> `resumeSoonOn`
  pushes T into `_ready`. Nothing drains after step 5, so T is never resumed, and T's
  `ResultAwaitable` keeps `_queued` set and `_loop` pointing at the loop. If T is a borrowed frame
  (a `Task` the caller holds) that outlives the loop, its destruction runs `cancelPendingOn(*_loop)`
  on freed storage. Before this range, `abandon()` resumed T inline in step 5, T threw
  `OperationCancelled` and unwound, and it never touched the loop again.
- Scenario: a test fixture declares a `Task` before the loop, which `EventLoop_test.cpp:1245` does
  deliberately. That task reads a socket owned by a `loop.spawn`ed handler, and the fixture is torn
  down in reverse order.
- The same applies to `CompletionWait` (`IocpOperation.hpp:215-221`).
- Fix: after step 5, drain the borrowed entries once more (each unwinds, because `_abandoned`
  throws). Alternatively, have `resumeSoon` after step 4 of teardown resume inline.

### S2. Destroying a socket after `close()` / `cancelRead()` and before a turn resumes its flow on the NORMAL path against a dead socket, and this is not documented
- `src/core/net/posix/PosixSocket.cpp:96-124`, `src/core/net/windows/IocpSocket.cpp:262-292`,
  `src/core/net/ISocket.hpp` (the `close()` doc).
- Before this range, `close()` ran the flow inside the call, while the socket was alive. A
  destructor that found the operation still parked abandoned it, so the flow threw. Now `close()`
  settles the operation with a Cancelled VALUE and forgets it (the slot is empty). A destructor that
  runs before the drain cannot reach it, so the flow resumes normally and receives `Cancelled`
  after its socket is gone. core-cpp's own code is safe here because the settled result does not
  reference the socket. A consumer flow that touches its socket after a failed read, such as
  `sock->isClosed()`, `sock->close()` in cleanup, or a retry, now reads freed memory where before
  it did not.
- Scenario: `conn->close(); connections.erase(id);` in one turn, with the reader doing
  `auto r = co_await conn.read(buf); if (!r) conn.close();`.
- Either keep the operation reachable until `await_resume`, so that the destructor can flip it to
  abandoned (for example, leave it in the slot and let `await_resume`'s retire hook clear it), or
  document in `ISocket::close` and the CHANGELOG *Changed* entry that the socket must outlive the
  next turn after `close()` / `cancelRead()`. The rule as written ("the socket is still alive when
  close() returns, and gone after one turn", SocketDecorator_test header) invites the unsafe order.

### S3. The new queued-while-destroyed paths have no test
- `IoAwaitable.hpp:199-203` and `IocpOperation.hpp:218-221`: `cancelPending` on a frame destroyed
  while its waiter is queued. `CloseResumesThroughLoop_test.cpp` proves two things: the waiter has
  not run when `close()`/`cancelRead()` returns and has run after at most two turns, and the owner
  destroyed by the resumed flow case (contour's). Nothing destroys the awaiting frame, or the socket
  (S2), between the verb and the drain. `grep cancelPending src/core/net/*_test.cpp` finds nothing
  for these branches.
- A case per branch belongs in `CloseResumesThroughLoop_test`: settle, destroy the frame, run a
  turn, and assert with ASan/value. It should also cover the socket being destroyed before the
  turn (S2, and B1 on WFMO).

### S4. The CHANGELOG files caller-visible behaviour breaks under *Changed* in a patch release
- `CHANGELOG.md` `[Unreleased]` *Changed*. Three entries change documented behaviour in ways the
  entries themselves say callers must adapt to:
  - "a caller that asserted a parked flow's outcome right after close() ... now runs one loop
    turn". The 0.2.0 `ISocket::cancelRead` doc promised inline resumption and the
    second-call-retires-the-re-armed-read behaviour, and this reverses both.
  - SyncGuard writes no markers to a non-terminal.
  - `nextEvent*` throws after `inputClosed()`.
- `.agent/rules/library-hygiene.md:169-181`: a breaking change goes under **Breaking**, and a patch
  release never breaks.
- The defensible reading is that each one fixes a violated guarantee (G2 for item 6, the
  #49 spin for item 3). If the lead rules that way, the CHANGELOG should say so explicitly ("fixes
  G2; code that depended on the violation must ...") instead of presenting a migration note as a
  *Changed* bullet. If not, these belong in 0.3.0. This needs an explicit ruling in the PR, not
  silence.

### S5. Consumer impact: endo's REPL spins after a hangup once it is on core-cpp
- `src/core/tui/runtime/TuiRuntime.hpp` (`nextEventFor` now throws without parking after
  `inputClosed()`).
- `endo/src/shell/ui/Prompt.cpp:178-186` catches `OperationCancelled` and `co_return {}`, and
  `endo/src/shell/Shell.cpp:1395-1418` loops `while (!_quit && prompt.ready())` and calls
  `prompt.read()` again. On a hung-up terminal every `read` now throws at once, so the 100% CPU spin
  from #49 moves from the runtime into endo's REPL. That REPL also redisplays the prompt to a dead
  terminal on each pass. The CHANGELOG notes the obligation in general terms. The PR's "Consumer
  impact" section should name endo `Prompt::read` / `Shell` REPL (check `inputClosed()` and set
  `_quit`) and tuidu's `Modal` caller.

### S6. `ResultAwaitable::resumeThrough` is new public API with no caller and no test
- `IoAwaitable.hpp:402-408`, and the *Added* entry in the CHANGELOG. `grep resumeThrough src`
  finds only the declaration.
- A patch release that adds an untested public hook commits to it. Either give it a test (an owner
  with no park that settles, then assert the deferral), or leave it out of 0.2.1.

## Nits

- **N1** `src/core/net/windows/WindowsSocket.cpp:148` and `IoAwaitable.cpp:25-28`: `resumeSoon`
  with a bare handle files the entry as borrowed (`ownedByLoop = false`), whatever the chain is.
  `WindowsSocket::cancelRead` has just disarmed the `parkedWorkFor` claim in `cancelPending`. A
  loop-owned chain queued this way and still pending at teardown is resumed rather than freed,
  which the `~EventLoop` comment calls wrong for a `DetachedTask`. `CompletionWait::finish`
  preserves the claim (`std::move(waiter)`). Consider storing `ParkedWork` in `ResultAwaitable`
  from `await_suspend`, which has the `Promise` type.
- **N2** `.agent/rules/async-and-net.md` (the new bullet): "Nothing but the loop's drain step calls
  a waiter's `resume()`" overclaims. `IoAwaitable.hpp:361` (a loop-less owner),
  `TuiRuntime.cpp:107,116` (the destructor's deliberate unwind) and the testing doubles all
  resume elsewhere. Say "no resource with a loop".
- **N3** `src/core/net/testing/InMemorySocket.hpp:166-181`: the "What it does NOT model" list does
  not name the new divergence. `close()`/`cancelRead()` resume inline here and through the loop on
  every real transport, so a consumer test of contour's detach shape on this double still crashes.
  `ParkingReadableSocket` behaves the same way. Keeping them inline is sound (there is no loop to
  defer to, and ISocket and the CHANGELOG say so), but the class doc is where a test author looks.
- **N4** `src/core/tui/posix/TerminalInput.cpp:41-52`: EINTR is handled by `safeRead`, and
  EAGAIN/EWOULDBLOCK are excluded, so neither is misread. A tty EOF counts only when `poll`
  confirms POLLHUP/POLLERR/POLLNVAL, which is right. EIO is also what a background-process-group
  read returns when SIGTTIN is ignored, and that would latch `inputClosed` permanently.
  endo leaves SIGTTIN at its default, so no current consumer is affected. It is worth a sentence in
  the `inputHasEnded` comment.
- **N5** `src/core/tui/posix/TerminalInput.cpp:134-138` and `windows/TerminalInput.cpp:110-116`:
  `poll()` records `_inputClosed`, but no `poll()` caller outside the runtime reads it, so a
  `poll()`-driven loop still spins. That is harmless, but the CHANGELOG's "records ... the same way"
  suggests it helps there.
- **N6** `src/core/net/windows/IocpSocket.cpp:1259-1298` (`bindUnix`): if `prepareListening` fails
  after `bind`, the socket file stays on disk, because `_path` is only set on success. The test
  (`windows/UnixListener_test.cpp`) checks neither that `IocpListener::close` deletes the file
  (new in this range) nor the stale-file reclaim through the IOCP listener.
- **N7** `cmake/CoreCppOptions.cmake:23`: `CORE_CPP_WITH_TUI_OUTPUT`'s default follows
  `CORE_CPP_WITH_TUI` only on the first configure. Turning TUI off in an existing cache leaves the
  leaf on. That is harmless (no libunicode), but the options table's "default `CORE_CPP_WITH_TUI`"
  reads as live.

## Checked and found sound

- **Every `.resume()` outside tests** (`grep -rn "\.resume()" src/core`):
  - Executors and combinators (`ParkedWork`, `ThreadPoolExecutor`, `SyncRun`, `Join`, `Generator`)
    are schedulers, not resources.
  - `EventLoop.cpp:463` is the drain.
  - `IoAwaitable.hpp:361` is only reached by a loop-less owner.
  - `ReadinessDial.cpp:102` `settleDial` is reached only from `onDialWake` and the dial's timer
    callback, both of which the loop invokes in its drain (`EventLoop.cpp:438-445`).
  - `TuiRuntime.cpp:107,116` is the destructor's documented deliberate unwind.
  - `Terminal.cpp:266` is `TerminalInput::resume`, not a coroutine.
- **Resource by resource** (`resumeSoon`/`submit`):
  - `PosixSocket` and `IocpSocket` name the loop through `cancelThrough` in every arm hook.
  - `CompletionWait::finish` goes through `resumeSoon` and keeps the chain claim.
  - TLS `SerialGate` goes through the executor (since B11).
  - `WriteQueue`, `SplitSocket` and `AsyncBufferedReader` are coroutines over the socket verbs.
  - POSIX and Windows UDP `close()` only sets a flag and waits through `loop.waitReadable`.
  - The listeners and `waitReadable` go through the closed-park list.
- **The stop callback that stays armed while queued** can only `requestCancel` a callback park or
  a park already unregistered. It can never queue the frame a second time (the BackendParity
  #760 shape), because these waiters are not park-table waiters.
- **Items 1/8 CMake:** `core_cpp_add_modules` enters `tui` for `tui_output` alone and checks
  only that row's module DEPS. `core_cpp_add_module`/`core_cpp_add_test` gate on each row
  (`CoreCppTargets.cmake:288,380`). The force-on is a normal variable in core-cpp's scope, and so is
  the Emscripten force-off (after it). Nothing touches the parent's cache. `tests/` is added only
  when it exists, the vendored CI leg asserts both the status line and >=10 registered suites, and
  the tui-output leg exists and is allowlisted in the hygiene check.
- **Item 10:** `AcceptAddressSize` is `sizeof(sockaddr_storage)+16`, which is enough for
  `sockaddr_un`. `accept()` uses protocol 0 for AF_UNIX. `connectUnix` goes through `adoptSocket`,
  which closes the socket on failure on both backends. The test runs over `BackendMatrix`, with a
  bounded wait and a SKIP for Unsupported.
- **Items 5, 7, 9:**
  - `nativeHandle` is additive.
  - The `resetStyle` / `buildSgrReset` / `SgrReset` names collide with nothing in the six consumers
    (grepped).
  - The char-literal regex bounds a literal to one char or one escape. `1'000'000` and `'"'` are
    covered.
