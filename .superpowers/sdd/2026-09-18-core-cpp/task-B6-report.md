# Task B6 — one frame-free, stop-aware socket contract and one POSIX socket

**Status: DONE_WITH_CONCERNS.** One commit, **`770f2dc`**, on `9924358`. 33 files, +4787/−206.

`origin/master` moved **five times** while I worked, and I rebased onto each: from my dispatched
base `fe48143` through `5d7a5ae` (B7a's IOCP backend, landing on `detail/ParkTable.hpp` and
`EventLoop` — the two files I extend), `29e9b24` (B5's `registerPark` arming), `bd7bcb5`, `e8c6574`
(B12 putting the TUI runtime on `core::net::EventLoop`, which gives my loop changes a second
consumer) and `9924358`. §6 says which gate ran against which SHA; none of them ran against a
working copy.

Three things a reviewer should weigh first:

1. **§3 — I extended `EventLoop` with a frameless readiness park.** That is B4's and B5's file, it is
   beyond what the dispatch scoped, and it is the design decision the whole task rests on.
2. **§7 — my own canary found a real defect of mine**, reachable in Release where the guard that
   catches its usual spelling is compiled out.
4. **§7 — I corrupted 72 em-dashes in two other lanes' files and nearly shipped it.** Caught by the
   rebase, not by any gate.

Two stale claims in the dispatch are in §9; one of them cost nothing and the other is worth fixing
before the next lane inherits the list.

---

## 1. What landed

**Created.** `src/core/async/AsTask.{hpp}` + `AsTask_test.cpp`; `src/core/net/IoAwaitable.hpp`,
`SocketContract.{hpp,cpp}`, `ISocket.cpp`, `SocketContractCanary.cpp`, and the suites
`IoAwaitable_test.cpp`, `WaitReadable_test.cpp`, `CancelRead_test.cpp`, `ReactorSocket_test.cpp`,
`SocketDecorator_test.cpp`, `AcceptedHalfClose_test.cpp`.

**Rewritten.** `ISocket.hpp` (every operation is now a `ResultAwaitable`), `posix/PosixSocket.{hpp,cpp}`
(frame-free; +672/−… on the pair).

**Changed.** `EventLoop.{hpp,cpp}` and `detail/ParkTable.hpp` (§3), `SplitSocket.hpp`, `Tls.cpp`,
`windows/WindowsSocket.{hpp,cpp}`, the two test doubles in `AsyncBufferedReader_test.cpp` and
`WriteQueue_test.cpp`, `posix/FdPassing_test.cpp`, both `CMakeLists.txt`.

**Bookkeeping.** `provenance.md` (13 new rows, 3 amended), `renames.json` (+18 rows, 514 → 532
against the final base),
`CHANGELOG.md` (two **Added**, three **Breaking** with migrations), `.agent/rules/async-and-net.md`
(a new section, twelve rules, each citing its origin as a full URL), `docs/modules/net.md`.

---

## 2. The contract, and what each rule cost

| Rule | Where it lives | What proves it |
|---|---|---|
| Frame-free operations | `ResultAwaitable<R>`, `IoAwaitable.hpp` | `ReactorSocket_test`'s parked arms; nothing allocates per operation |
| Flow cancel throws, resource cancel is a VALUE | `ResultAwaitable::await_resume` | `IoAwaitable_test` ×2, `CancelRead_test`, `ReactorSocket_test` ×2 |
| A value beats a stop (#884) | the order of two tests in `await_resume` | `IoAwaitable_test`, mutation-proved (§5) |
| A destructor ABANDONS, `close()` resolves | `ResultAwaitable::abandon` | `ReactorSocket_test`, mutation-proved |
| Detach first, complete last | `PosixSocket::close` takes both ops into locals | `SocketDecorator_test` (the resumption destroys the socket) |
| One read op, one write op | `contract::claimReadSlot` / `claimWriteSlot` | the canaries, which drive a REAL socket |
| `waitReadable`'s count is the contract (#677) | `PosixSocket::tryProbe`, `WindowsSocket::waitReadableTask` | `WaitReadable_test`, mutation-proved |
| One deadline mechanism | `setReceiveDeadline` → `EventLoop::addTimer` | `ReactorSocket_test` asserts `pendingTimerCount() == 1` while parked |
| `shutdownWrite` reaches the peer AND closes our own write half | `PosixSocket::shutdownWrite`, `WindowsSocket::shutdownWrite` | `AcceptedHalfClose_test`, on `poll`/`epoll` and on `iocp`/`wfmo`; both halves mutation-proved separately (§5, M7 and M8) |

---

## 3. The deviation that matters: a frameless readiness park in `EventLoop`

**This is the one decision to overturn if you disagree with it**, because everything else follows.

`ISocket::write` writes ALL of its buffer, so it is inherently multi-step: send → partial → wait
writable → send more. `read` has the same shape whenever a level-triggered poller reports a spurious
readable and `recv` answers `EAGAIN`. **A `co_await` expression suspends exactly once**, so
`await_resume` cannot re-park — which means a frame-free operation's retry loop CANNOT live in the
awaiting coroutine. It has to run wherever the readiness is delivered. That is why fastcached's
socket owns its own `ReadinessHandler` and retries inside the reactor callback.

core-cpp had no such thing: every readiness park in `ParkEntry` carries an `async::ParkedWork`, i.e. a
coroutine to resume.

So `ParkEntry` gains `onReadyCallback(callback, state, handle, kind, interest)`, `Park` gains
`onReady`, and there is a `ParkWake` reason (`Ready`, `Cancelled`, `Abandoned`) because a frameless
park has no frame to observe the difference. It is dispatched through the existing ready queue in
turn step 2 — **modelled directly on B5's frameless timer**, which is a park in the same table for
the same reason. Unlike a timer park it SURVIVES its own dispatch, because its owner runs a retry
loop across many wakes and only `unregisterPark` retires it.

**What it buys, and it is the whole argument.** The socket's registration is in the one park table,
so it inherits four loop-wide properties rather than being invisible to all four:
`notifyHandleClosing` (which at `29e9b24` asserts rather than hopes), `requestCancel`'s generation
check — what makes `cancelRead` retire the operation it was called on and not a later one that
reused the slot — the turn's decision to enter the backend wait, and `registerPark`'s
`armHostWake()`, which is inert for a readiness park today for the reason set out below, and already
in place if a host-driven backend ever gains readiness.

**It does NOT inherit `~EventLoop`'s teardown, and I claimed in fix round 0 that it did — in this
section, in the CHANGELOG and in the commit message.** Round 1's review was right and I checked it
rather than taking it: teardown step 2 skips every `_ready` entry with `callbackPark` set, and
`unparkEverything` excludes callback parks by its `!entry->parked` test, each with a comment saying
why — there is no frame to unwind and calling the callback would reach an owner being destroyed. So
a socket operation still parked when its loop is destroyed is **neither completed nor abandoned**,
and the awaiting coroutine is never resumed and never unwinds. That is a real limit of this design
and it now says so in the CHANGELOG. It is bounded by an ordering rule the library already has:
destroy loop-owned objects before the loop.

**The alternative I rejected** was exposing `EventLoop::backend()` and letting each socket attach its
own handler — one line instead of about sixty. It is the second mechanism B5's timer rule exists to
forbid, and two costs are concrete rather than stylistic.

*The turn would not wait for the readiness it registered.* Step 4 enters the backend wait on
`driven && (_parks.size() != 0 || !closed.empty() || !_ready.empty() || idleWait)`. A registration
the table cannot see contributes to none of the first three. Under `blockOn` the `idleWait` term
covers for it, because the default `IdlePolicy::Block` makes an idle turn wait anyway — but
`runOnce()` sets neither `_inRun` nor `until`, so `idleWait` is false unconditionally there, and a
`run()` on an `IdlePolicy::Return` loop is false too. On both, a socket read outstanding only in the
backend polls and returns instead of waiting for its own readiness. Those are exactly the drives an
embedded consumer uses.

*The registration would sit outside the one function that arms.* `29e9b24` put `armHostWake()` in
`registerPark` rather than in `addTimer`, precisely because an arming placed at one call site covered
one of the seven that reach it. A socket attaching its own handler to the backend would be the eighth
and the one outside it. I weight this below the first cost, for the reason in the next paragraph.

**On the controller's question — when a readiness park is armed from off the loop thread, what asks
the loop for a turn?** I went looking for the case to answer this with, and the honest answer is that
there is nothing to arm and the reason is structural rather than lucky. Three parts:

1. **The one place that arms is `registerPark`, and my park reaches it by the same call as every
   other member of the family.** `PosixSocket::armRead` is
   `_loop.registerPark(ParkEntry::onReadyCallback(&PosixSocket::onReadWake, this, _fd,
   DefaultHandleKind, interest))` — no second arming, and none to add. That is `787d8cf`'s rule as
   stated: **ready work wakes, a park arms**, and mine is a park.
2. **For a readiness park that arming is inert today, and provably rather than incidentally.**
   `armHostWake` returns immediately unless the backend is host-driven, and `HostDrivenBackend`
   refuses every handle — `attach` answers `NetErrorCode::Unsupported`, "this backend has no
   readiness at all". So a readiness park on a host-driven loop cannot be filed in the first place,
   and there is no case to write because there is no state to construct. B5 already placed the
   tripwire for the day that changes: the `assert(_closedParks.empty())` in `armHostWake`, whose
   message names `notifyHandleClosing` as the other thing that would then need arming. My park is on
   the same side of that assertion as every other readiness park, so it inherits the answer instead
   of needing its own.
3. **"Off the loop thread" is out of contract for a socket anyway.** `registerPark` opens with
   `assert(teardownIsSerialisedWithDispatch())`, so filing a park is legal only on the loop's own
   thread or with nothing driving the loop — and a socket verb touches a loop-owned object, which the
   module's own rule already confines to that thread.

I had written the stronger claim — that the socket would be "filed, correct, and silently never
reached" — and checked it before shipping it. It is wrong: the backend that needs the wake is the one
that cannot hold the park. The cost of the rejected alternative is the step-4 one above, which is
live today; this one is a property my park inherits for free and would have had to re-earn.

**Cost, honestly.** `ParkEntry` and `Park` each grow one field; `ReadyEntry` grows a `ParkWake`;
`queueParkedWaiter` gains a two-argument form and `runDueCallback` a branch;
`registerPark`'s refusal and `resolveCancel`'s early return each widen by one term. Nothing existing
changes shape — a `ParkEntry` built through `onDeadline`, `onCallback` or `onReadiness` behaves
exactly as before, which is why B7a's work rebased over it cleanly.

I flagged this to the controller before implementing it rather than after.

---

## 4. Other deliberate deviations — please review these

**(a) `ResultAwaitable::await_suspend` returns a `std::coroutine_handle<>`, not a `bool.`** A handle,
so a coroutine-backed transport starts its task by SYMMETRIC TRANSFER rather than by a nested
`resume()`. A nested resume grows the stack one frame per decorator per operation, and a TLS pump
over a buffered reader is three deep before any application code appears.

**(b) `ResultAwaitable` has a move constructor, valid only before the operation has been awaited.**
`asTask` has to move the operation into a coroutine frame, and a coroutine parameter cannot be an
immovable type. After `await_suspend` the object is pointed at from three places (the owner's slot,
the stop callback's capture, the park's cancellation route), so a move then would be a
completion written into storage that has moved away. Debug refuses that case with an assertion
rather than documenting it.

**(c) A coroutine-backed mode, `ResultAwaitable(Task<Result>)`.** A raw socket's read is a syscall and
a retry, so it needs no frame. A DECORATOR's is not: a TLS read decrypts, may drive a handshake and
may park on a raw read of its own. Writing that as a hand-rolled state machine to save a frame trades
away the one thing coroutines are for. `Tls.cpp`'s bodies are unchanged; only their signatures moved.
Same for the two test doubles.

**(d) `readWithFd` and `writeVectored` have working defaults, and they cost a frame.** A base class
has nowhere frame-free to keep an inner operation. Every transport here overrides them or does better,
so nothing in this tree reaches those defaults; what does is a transport from outside the library,
and giving it a working default is better than making it abstract. **This keeps contour's behaviour:
`readWithFd` still reads and reports `fd = -1`** — an earlier draft of mine answered `Unsupported`,
which would have been a silent behaviour change for a Windows caller.

**(e) `PosixSocket` has no `Impl` pimpl**, unlike both upstreams. They had one to hide epoll and
kqueue types; the multiplexer is `IoBackend`'s now and there is nothing platform-shaped left to hide.

**(f) `contract::*` is public, not `detail`.** The spec asks for it (§2 item 7) and the reason is in
the header: every rule there was stated somewhere unreachable from the place it must be obeyed, so
the seventh transport got no warning. A transport outside this library is under the same rules.

**(g) `WindowsSocket` keeps its coroutine bodies, gains `waitReadable` and `shutdownWrite`, and does
NOT gain `cancelRead`.** The plan says "adapt `windows/WindowsSocket` to the new interface over
Wfmo"; the signature change had to happen everywhere at once, the bodies did not. I implemented
`waitReadable` there rather than inheriting the default, because the default answers a flat `1` and
this socket can tell — inheriting it would have re-created the exact divergence #677 was filed on.
`cancelRead` I did not: retiring its park needs a handle on it that a socket whose read is an ordinary
coroutine awaiting the loop does not have, and giving it one is a redesign of a class Task B7 replaces
outright. **The gap is stated on the class**, and it costs coverage: `CancelRead_test` and
`SocketDecorator_test` are registered POSIX-only and the CMakeLists says why. §8 concern 1.

---

## 5. RED then GREEN, and every mutation

Each mutation was applied to the implementation, the suite re-run, and the arm restored. **Every
prediction below was written down before the run.** One was wrong, and that is the interesting one.

| # | Arm removed | Predicted | Actual | RED, verbatim |
|---|---|---|---|---|
| M1 | `asTask` takes `Aw&&` instead of `Aw` | 1 case | 1 | `CHECK( Counted::live == 1 )` → `0 == 1` |
| M2 | `await_resume` tests the token BEFORE the value | **wrong — see below** | 0 | nothing failed |
| M2′ | the same, against the corrected case | 1 case, 2 assertions | 1 case, 2 assertions | `CHECK( outcome == Outcome::Value )` → `3 == 1`; `CHECK( bytes == 7 )` → `0 == 7` |
| M3 | `tryProbe` answers a flat `1` (the pre-#677 behaviour) | 3 cases, 6 assertions | 3 cases, 6 assertions | `CHECK( observed.count == 0 )` → `1 == 0`, ×6 (3 cases × 2 backends) |
| M4 | the destructor RESOLVES instead of abandoning | 1 case, 2 assertions | 1 case, 2 assertions | `CHECK( outcome.threw )` → `false`, ×2 |
| M5 | `takeRead` leaves the receive deadline armed | 1 case, 2 assertions | 1 case, 2 assertions | `CHECK( loop.pendingTimerCount() == 0 )` → `1 == 0`, ×2 |
| M6 | the null-awaitable guard in `settleRead`/`abandonRead` | 1 case | 1 case **and exit 139** | `SIGSEGV`, plus `1 failed` |

**M2 is the one worth a reviewer's time, because my case was worthless and I nearly kept it.** The
#884 case as I first wrote it completed the operation and THEN requested the stop. That asserts
nothing at all: completing resumes the flow synchronously, so `await_resume` has already run and
returned before the stop exists — and the case passed against an implementation that tests the token
first. I predicted 1 case / 2 assertions and got zero, which is what made me look.

The corrected case requests the stop while the operation is still parked and completes it with bytes
afterwards, which puts a stopped token and an arrived value in front of `await_resume` at the same
instant. That is also the realistic ordering: a `whenAny` sibling stops the token while the read is
parked, and a readiness dispatch in the same turn may already have put bytes in the caller's buffer.
The case now carries that reasoning, and the measurement that the obvious spelling is vacuous.

**The canaries, both directions, and the registration rewritten under them.**

Each of the three drives a REAL socket into its guard and dies (exit 1, naming its own assertion:
`claimReadSlot` … "a read operation was armed over a parked one"). Each guard was then removed and
the canary **rebuilt** — the first attempt at this measured a stale binary, because the neutered
header did not compile and I read the exit code of the previous build.

| Guard removed | ctest verdict, verbatim |
|---|---|
| `contract::claimReadSlot` (the plain `read` verb) | `read-slot ***Failed  Error regular expression found in output. Regex=[SURVIVED]`, and **only that mode** — `write-slot` and `empty-read-buffer` stayed green |
| `contract::requireReadBuffer` + `contract::claimWriteSlot` | `write-slot` and `empty-read-buffer` both `***Failed … Regex=[SURVIVED]`, `read-slot` green |

So each mode is tied to its own guard rather than to the binary, which was not true of the version I
first wrote: the PASS marker was one shared string, so a mode that fell into the wrong branch would
have passed on another guard's marker. It now prints its mode
(`socket-contract-canary: read-slot: reached the guarded call (…)`) and the registration matches
`"${guard}: reached the guarded call"`.

**`WILL_FAIL` is gone from this registration**, which was the controller's correction and not my
finding. **Every marker goes to `stderr`**, which was also the controller's — `_Exit` flushes
nothing, so a marker on `stdout` is lost on precisely the abort path it exists to prove. Checked
rather than assumed: all fifteen output calls in `SocketContractCanary.cpp` pass `stderr`, including
the two that clang-format wrapped onto a continuation line, where the stream is not on the same line
as the string and a grep for `stderr` beside the text would have missed them.

What I did measure, because CMake documents none of it:

- A Release run exits **77** with no marker printed at all, and ctest reports `***Skipped`, not a
  failure for the missing PASS expression. So `SKIP_RETURN_CODE` really is evaluated ahead of
  `PASS_REGULAR_EXPRESSION`.
- ctest then folds those three into `100% tests passed, 0 tests failed out of 3` — the skip is
  invisible in the total. That is the concrete reason the skip count belongs beside every number in
  §6 rather than being left implied.

**The new half-close case, and why it carries two assertions.**

| # | Arm removed | Predicted | Actual |
|---|---|---|---|
| M7 | `PosixSocket::shutdownWrite` made a no-op | 1 case, 4 assertions of 18 | 1 case, 4 of 18 — `CHECK( **exchange.peerRead == 0 )` → `1 == 0` and `CHECK_FALSE( …writeAfterHalfClose->has_value() )` → `!true`, in each of the two Linux backend sections |
| M8 | `trySendFlat` reports a refused write (`EPIPE`) as a full success, FIN still sent | 1 case, 2 assertions of 18 | 1 case, 2 of 18 — only `CHECK_FALSE( …writeAfterHalfClose->has_value() )` |

M8 is the one that justifies the second assertion existing: the peer still sees its clean EOF, so a
port that carried only the first assertion is green against a socket whose own writes lie about
having been refused. Note also what M7 did **not** do — it did not hang. A no-op half-close leaves
the write half working, so the stray byte arrives and the peer's read returns 1 rather than parking
for a FIN that never comes. The case reports rather than times out.

**GREEN.** `core-cpp-net-test`: **1409 assertions in 198 test cases**. Per suite:
`[ioawaitable]` 23/8, `[waitreadable]` 58/5, `[cancelread]` 42/3, `[reactor]` 110/9, `[decorator]`
34/2; `[astask]` 10/6 in the async binary.

**Honest caveat on ordering.** `asTask` is the only unit here where the first RED was a genuine run
against nothing (`fatal error: 'core/async/AsTask.hpp' file not found`, captured verbatim). Everywhere
else the contract had to exist before a case could name it, so the REDs are arm-removal. What that
does not prove is that a case would fail against an empty implementation; what it does prove is that
every rule has a case that dies without it — and, in M2, that one of mine did not.

---

## 6. Gates

**Every number below was measured against a committed tree, never a working copy.** The full matrix
ran against `f277331`; `origin/master` then moved once more (`9924358`, a TUI destructor fix) and the
rebase onto it produced the pushed commit `770f2dc`, which I re-verified with a full `clang-debug`
build and `ctest` — **37/37, 0 skipped, 99 build steps** — before pushing. The delta between the two
trees is upstream TUI work and documentation; my commit rebased onto it with no conflict. **I am
stating that rather than restating the whole table as if it had been measured twice**, because it
was not, and which legs ran on which SHA is exactly the kind of thing that decays into a false
claim.

**The matrix ran three times, and the first two runs are why this paragraph exists.** I had a batch
in flight and killed it when I noticed I had edited two files *after* committing — the numbers it
would have produced were about a tree that does not exist. The second run found a real clang-tidy
defect in my new test file, which moved the commit again. The third followed `origin/master` moving
to `e8c6574`, B12 putting the TUI runtime on `core::net::EventLoop` — which gives my loop changes a
second consumer, so it was not a rebase I could wave through. **The table is that third run.**

**CI on the pushed commit: green, 28 of 28 jobs, 0 failed** (run `35596646379`). That includes the
three legs no local preset of mine covers and the one I had a written hypothesis about:

- **`macos (appleclang)`, `macos (appleclang-debug)` and `macos (llvm-22)` all pass — and they are
  the only thing in this record that witnesses `kqueue`.** No local preset of mine runs it; §6's
  table is `poll`, `epoll`, `iocp` and `wfmo`. The commit message states the hypothesis — *nothing
  here is kqueue-shaped or macOS-shaped; the socket asks `IoBackend` to watch a descriptor and
  never asks which mechanism did it* — and these legs are the measurement of it. They also compile
  the two branches nothing else does: `MSG_CMSG_CLOEXEC`'s fallback and the `fcntl(FD_CLOEXEC)`
  arm in `tryReadWithFd`. **FreeBSD (`portability.yml`) has not been dispatched**, so the BSD half
  of that hypothesis is still untested.
- **`consumer-smoke` on all four legs** (`vendored`, `cpm`, `wasm`, and the shared fixture), which
  is what B3 shipped broken and which builds nowhere locally.
- **`style`**, the single leg that runs the `tree-level` checks once, plus `linux
  (clang-22-arm64)`, `linux (clang-22-cxx26)`, `linux (clang-22-tracy)`, `coverage`,
  `compile-cache` and both `emscripten` pins.

I had been told to expect the macOS legs red, inherited from B12's `std::jthread` in
`TuiRuntime_test.cpp` which AppleClang's libc++ does not have. **That was already fixed by the time
I rebased**: at `770f2dc` that file uses `std::thread` with an explicit join, and the only surviving
`std::jthread` in the tree is `windows/IocpCanary.cpp`, which is inside `if(CORE_CPP_TESTING AND
WIN32)` and never reaches macOS. So I did not inherit the failure and did not have to discount it.

**Skip counts are beside every total, because ctest hides them.** `gcc-release` and
`clangcl-release` each report `100% tests passed, 0 tests failed` while six of their tests never
ran. A green total is not a count of what executed.

| Configuration | Tests | Skipped | Notes |
|---|---|---|---|
| `clang-debug` (WSL) | **37/37** | 0 | |
| `gcc-release` (WSL) | **37/37** | **6** | the 3 `hostdriven-canary` and the 3 `socket-contract-canary` modes: `NDEBUG` removes the refusals they observe |
| `clang-asan-ubsan` (WSL) | **37/37** | 0 | |
| `clang-tsan` (WSL) | **37/37** | 0 | |
| `emscripten` (emsdk, WSL) | **28/28** | 0 | the leg that proves `EventLoop.cpp` and `detail/ParkTable.hpp`, the two WebAssembly-subset files this task changes, still build and run single-threaded |
| `cl-debug` (VS dev shell, `--clean-first`) | **38/38** | 0 | **537 ninja steps** |
| `clangcl-release` (VS dev shell, `--clean-first`) | **38/38** | **6** | **537 ninja steps**; skips are 3 `hostdriven-canary`, 2 `iocp-canary`, 1 `windows-dialog-canary` |
| `clang-tidy` preset (WSL) | **37/37** | 0 | tree deleted before the run; **539 build steps**, 0 findings. See below |
| pinned `clang-format` 22.1.8 | 435 files clean | | `--all` cannot run here: the script shells out to `git ls-files`, and git inside WSL cannot resolve this worktree's Windows gitdir pointer. The file list therefore comes from git on the Windows side and is passed explicitly — same 435 files, same pinned binary |
| `mkdocs build --strict` | exit 0 | | run through Windows Python; `mkdocs` is not installed in this WSL |
| `ctest -L hygiene` | green | | inside the runs above (provenance, renames, codemods, drift, layering, platform-sources, tree-level coverage) |
| `python-style.py` | not run, not owed | | no Python in the repository changed |
| `consumer-smoke` (CI only) | **green**, both legs | | `tests/consumer-shared/ConsumerSmoke.hpp` builds only in CI's `consumer-smoke` jobs, and it is what B3 shipped broken. I checked it locally with `-fsyntax-only` against the new headers first; CI's `vendored` and `wasm` legs then confirmed it on `770f2dc`. Its `co_await connection->read(...)` needed no change — a `co_await` does not care whether it is handed a task or an awaitable, which is exactly why the signature change is invisible to the common caller |
| `tests/consumer-wasm`, `tests/wasm` | not run, not owed | | neither fixture names `ISocket`; both drive `PlatformLoop`, `DeadlineTimer` and `NetError`, which the passing `net_backend` binary covers |

**The ninja step count is the point of the `clangcl-*` row, not decoration.** The installed
fastcache-cc predates fastcached `ca8dfc32`, so a `clangcl-*` incremental build after a header edit
can link stale objects and report green. 537 steps is a full rebuild of the tree; the failure mode
looks like single digits. (It was 541 before the last rebase: B12 deleted six TUI files and added
three, which is the whole of the difference.)

**What would have made these numbers different, and what they therefore do NOT witness.** Adding a
case to an existing Catch2 binary cannot move a ctest total: `AcceptedHalfClose_test.cpp` lives in
`core-cpp-net-test`, which is one ctest test either way. So `37/37` is evidence that nothing broke
and is *no* evidence that my new case ran. The evidence that it ran is the binary's own output, and
it is here:

- `core-cpp-net-test`: **1430 assertions in 200 test cases** on `clang-debug` (1409 in 198 before
  this work), and **2189 assertions** on `cl-debug`, which has more backend sections per case.
- The new case alone: **18 assertions**, on `poll` and `epoll` under Linux and on **`iocp` and
  `wfmo`** under Windows — 9 per backend section. The Windows run is the one that matters, because
  the `WindowsSocket::shutdownWrite` it exercises is new in this commit and reaches its peer through
  the Windows accept path.
- Per suite: `[ioawaitable]` 26/9, `[waitreadable]` 58/5, `[cancelread]` 42/3, `[reactor]` 110/9,
  `[decorator]` 34/2, `[socket]` 262/20; `[astask]` 10/6 in the async binary.

### clang-tidy: the instrument, and what it caught

**Binary** `/home/christianparpart/.local/bin/clang-tidy`; `--version` reports `LLVM version
22.1.8`; the pin in `.clang-tidy-version` is `version: 22.1.8`. They match. It is reached through
`bash -lc` and not `bash -c`: a non-login shell does not put `~/.local/bin` on `PATH`, and a
configure that cannot find clang-tidy does not fail — it produces a tree that compiles without
analysing anything and reports green.

**The tidy tree was deleted before the run** (`rm -rf out/build/clang-tidy`, confirmed gone), because
`.clang-tidy` is not an input of any object (core-cpp#36), so an incremental tree can answer about
the rules it was configured with rather than the ones on disk.

**Proof it analysed rather than passed through — two live findings, both in files I wrote this
session, neither planted:**

1. `SocketContractCanary.cpp:94`, `:102` — `modernize-use-std-print`: "use `std::println` instead of
   `fprintf`". Fixed by emitting literal pieces through `std::fputs`, which every other canary in
   this module already does; `<print>` is past the libc++ 17 floor the WebAssembly subset builds
   against, so the check's own suggestion is not available here.
2. `AcceptedHalfClose_test.cpp:44` — `misc-unused-using-decls`: `using core::net::ISocket;` left
   behind when I rewrote the case from the `whenAll` shape to the sequential one, which dropped the
   parameter that named the type. Fixed by deleting the declaration.

The second is the better evidence: the file is **created in this commit**, so a configure that had
silently not run clang-tidy could not have produced that diagnostic. The build reporting it was at
ninja step 211 of 543, so the analysed surface is the tree rather than a corner of it.

**And the deliberate proof, because a green leg proves nothing about a tool that did not run.** A
clean tidy build prints no command lines — ninja echoes one only on failure — so the clean run's own
log cannot name the analyser. Two things do:

- **`build.ninja`, which is the build contract** (`CMakeCache.txt` is not):
  `--tidy="/home/christianparpart/.local/bin/clang-tidy;--extra-arg-before=--driver-mode=g++"`,
  carried by **210 objects**, under the `fastcache-cc` launcher.
- **A planted violation.** I re-inserted `using core::net::ISocket;` into
  `AcceptedHalfClose_test.cpp` and rebuilt that one target on the already-green tree: exit 1,
  `error: using decl 'ISocket' is unused [misc-unused-using-decls,-warnings-as-errors]`,
  `1 warning treated as error`. Removing it again rebuilt clean, exit 0, zero `error:` lines, and
  `git status` empty — so the tree still matched its commit.

**The earlier round found six things, all fixed**: a static data member under the wrong case rule
(`ClassMemberCase: CamelCase` — a static data member is a class member, not a private instance
member, so it takes no `_`), two `bugprone-use-after-move` reports from `std::move` inside a Catch
macro (hoisted out), a `tryProbe` that could be `const`, three implicit-widening multiplications,
one missing-parentheses report, a dead store, and **`trySend` at a cognitive complexity of 57
against a threshold of 50** — which I took as the machine noticing what a reader would, and split
into `trySendFlat`, `trySendSegments` and `advanceSegmentCursor`.

---

## 7. What I got wrong

**My own canary found a real defect of mine, and it is a Release defect.** The verb records the
operation's kind and buffer when it is CALLED; the awaitable records itself at the owner when it is
AWAITED. `[[nodiscard]]` makes dropping one in between a warning rather than an impossibility — and a
consumer building with different flags does not even get the warning. The slot was then left naming an
operation with no frame behind it, and `close()` or the destructor dereferenced null. Reachable
without anything exotic: `auto op = sock->read(buf);` in a scope that returns early. I found it only
because the `empty-read-buffer` canary, with its guard removed, exited 139 instead of 0 — and I
stopped to ask why a benign path would crash rather than recording the exit code and moving on.
`ResultAwaitable`'s destructor now retires, every slot consumer tolerates a null awaitable, and
`ReactorSocket_test` has a four-section regression case whose own mutation (M6) reproduces the
segfault.

**I corrupted 72 em-dashes in two other lanes' files and no gate noticed.** A Python edit of mine
wrote `ParkTable.hpp` and `EventLoop.hpp` with Windows' default encoding, the compiler refused them as
invalid UTF-8, and my "repair" decoded the whole file as cp1252 and re-encoded it as UTF-8. But the
files were MIXED by then: my new text was cp1252 and every pre-existing em-dash was already valid
UTF-8, so the round-trip fixed mine and double-encoded 72 of theirs into `â€”`. Everything stayed
green — it is inside comments, it is still valid UTF-8, and neither clang-format nor clang-tidy nor
any hygiene check reads for it. **The rebase is what caught it**, as a conflict on lines neither lane
had semantically touched. Reversed, and verified by diffing against the base: 6 changed lines contain
non-ASCII and all 6 are additions of mine; no pre-existing line differs. I also re-scanned all 32
committed files for mojibake and for non-UTF-8 bytes — 0.

The rule I would keep: **a whole-file encoding round-trip cannot repair a file that is already mixed**,
and "the compiler now accepts it" does not mean the bytes are right. The compiler only told me *some*
bytes were wrong.

**Three measurements I read wrong before reading them right, all the same shape.** A `clang-format --check` whose
`FORMAT_CHECK_EXIT=0` was `tail`'s status while the script was reporting violations three lines
above; and a canary run whose exit codes were the previous build's because the build had failed and I
piped past it. And a gate log I read that was an hour stale: I piped a
four-preset batch through `tail`, which cannot emit until its input closes, so the task's output
file stayed empty and I went to the per-preset logs instead — where `clang-tsan.log` and
`clang-tidy.log` still held the PREVIOUS, pre-rebase run and reported a plausible `34/34` for a
batch that had not reached them yet. The tell was the file timestamps, not the contents: 10:30 and
10:56 against a batch whose first preset finished at 11:20. **A log file that exists is not a log
file from this run**, and a number that looks right is the easiest kind to keep.

All three are the same shape as the rulings already in the brief, and all three were caught by
asking for the thing itself — the exit code of the command rather than the pipeline, and the
mtime of the log rather than its last line.

**I argued for my own design over a function that had been deleted.** §3's case for filing the
readiness park in the park table cited `hasPendingWork()` twice — in the report, in the commit
message and in the CHANGELOG — as the concrete cost of the alternative. B5 deleted that function
from `EventLoop.cpp` the same day, in the commit landing beneath mine, and the controller caught it,
not me. Re-deriving the argument from what the code now does made it better and narrower: the live
cost is the turn's step-4 predicate, which decides whether to enter the backend wait from
`_parks.size()`. And when I went to make the *second* half of the argument — that a registration
outside the table would not get the host-wake arming — I found I could not: the only host-driven
backend refuses every handle, so a readiness park cannot exist on one. **I had been about to
strengthen a claim I could not have tested.** What I keep from this is narrower than "check your
citations": an argument stated over a named function decays when that function does; an argument
stated over a property of the code survives, and B5's own rulebook edit that week says the same
thing about coordinates versus properties.

**I first gated the socket suites nowhere and let Windows find it.** `WindowsSocket` overrode neither
`waitReadable` nor `cancelRead`, so five of my cases failed on `cl-debug` against a socket the suites
were never written for. No Linux preset could have told me: the suites are POSIX-shaped and
`makeSocketPair` hands back whatever the platform's socket is.

---

## 8. Concerns

1. **`WindowsSocket` has no `cancelRead`, and `ISocket::cancelRead`'s own documentation says the
   inherited no-op is unsafe for a transport whose reads park — which its do.** No caller in this
   library reaches it on Windows today, so it is latent rather than live, and Task B7 replaces the
   class. But it is a documented rule that one shipped transport does not keep, and the cost is
   visible: `CancelRead_test` and `SocketDecorator_test` do not run on Windows at all.
2. **`TlsSocket` does not keep three rules `ISocket`'s docs attach to it, and round 1's review
   found all three where my concern list named only `WindowsSocket::cancelRead`.** `Tls.cpp` is
   B11's file and mid-merge, so I have reported rather than fixed — the same call I made for
   `WindowsSocket`. What I DID fix is the part that is mine: the interface documentation, which
   promised behaviour the shipped decorator does not have.

   | Verb | What the shipped `TlsSocket` does | Why it matters |
   |---|---|---|
   | `waitReadable` | not overridden, so it inherits the base's flat `1` | the default never SUSPENDS, so a TLS watchdog loop is a spin, not a wait. This is live, not latent |
   | `shutdownWrite` | not overridden, so the no-op the header calls *"for FAKES"* | a response framed "ends at EOF" over TLS never reaches its peer until the full `close()` |
   | `handshakeIfNeeded` | not overridden by anything in the tree | **B8 is about to write the accept loop this verb exists for.** No bytes are lost today because the handshake happens lazily inside `readPlain`/`writePlain`, but an accept loop awaiting it gets an immediate success and begins autodetection mid-handshake |

   **On `shutdownWrite` I want to flag a signature problem rather than just a missing override.** A
   clean TLS half-close is a `close_notify` record that has to be written and flushed; the verb is
   synchronous and `void`, so it cannot await that. Forwarding to the inner socket — the obvious
   one-line "fix" — sends a FIN with no `close_notify`, which a strict peer reads as a truncation
   attack rather than an orderly end. So this one is not a missing line in B11's file; it is a verb
   that cannot be implemented correctly by a decorator as declared, and that is B6's to own. I have
   documented it on the verb rather than shipped a plausible-looking forward.

3. **No Windows socket is frame-free yet**, so the allocation saving this task exists for is currently
   POSIX-only. That is what "one POSIX socket" in the task title scopes, and B7's IOCP socket is where
   Windows gets it.
4. **`ReactorSocket_test` moves 1 MiB twice per backend.** It runs in about 1.1s for the whole net
   binary on `clang-debug`, and the `TIMEOUT 120` backstop is untouched — but it is the first case
   here that is sized rather than scripted, and a slow CI runner is where that would show.
5. **A socket operation parked when its loop is destroyed is never resumed and never unwound**, and
   the reason I first gave for tolerating this was wrong. I wrote that a socket outliving its loop
   keeps a `ParkId` naming a dropped park and that "`unregisterPark` on a stale id is a no-op, so
   this is safe". It is not a stale-id no-op: `~PosixSocket` would be calling `cancelTimer`,
   `unregisterPark` and `notifyHandleClosing` **on a destroyed `EventLoop` object**. The conclusion
   — do not let a socket outlive its loop — is unchanged; the reason recorded for it was, and a
   later lane would have read the reason. The actual position: callback parks are excluded from
   teardown by design (step 2's `callbackPark` skip, `unparkEverything`'s `!entry->parked` test),
   so the awaiting coroutine of a still-parked operation simply never resumes. The ordering rule is
   what bounds it, and it is now stated in the CHANGELOG rather than implied.
6. **`readWithFd`'s default now costs a coroutine frame** where contour's cost one too (it was a
   `Task`), so this is not a regression — but it is the one place where the "frame-free" claim on the
   interface has an asterisk, and the header says so.
7. **The LISTENER side is untouched, and that is a scope call a reviewer should confirm.**
   `IListener::accept()` still returns `async::Task<AcceptResult>`, and `posix/PosixListener`,
   `UnixListener` and `AcceptLoop` are unchanged. The plan's B6 "Files:" line names `IListener.hpp`
   and those three, but its contract section and the dispatch's both list only the ISocket verbs, and
   Task B8 re-implements listeners outright (`listen(loop, ListenOptions)`, `adoptListener`,
   `IConnector`). Converting accept to an awaitable now would be work B8 redoes, and an accept parks
   once per connection rather than once per read, so the frame it costs is not the one this task
   exists to remove. What I did take from the spec is `core::net::SocketResult`, which B8's
   `AcceptResult` should become an alias of. **The spec's `IListener::localPort` → `boundPort`
   rename is likewise not done**, for the same reason: it is a listener rename in a header B8 rewrites.
8. **Reported, and mostly fixed under me while I was writing it up.** Sweeping my OWN files for the
   `WILL_FAIL` claim after the registration changed (I found one, `SocketContract.hpp:19`, and fixed
   it) turned up the same stale claim in nine other places across six files that `a48e727` had not
   edited. Before I could report it, `fb2615f` landed and fixed the rulebook
   (`.agent/rules/async-and-net.md`), the two provenance rows and the 0.1.0 CHANGELOG notes, and
   routed the remaining source comments to Task B13 with the reason stated: two lanes hold
   uncommitted work across those files. So there is nothing here for the controller to action that
   is not already routed — **except one item I would ask B13 not to treat as a comment sweep**:

   `src/core/net/EventLoop.cpp:536` is not a description of a registration. It is B5's stated
   *justification for leaving an invariant without a canary* — "a third canary mode for an
   invariant that is unreachable today is more than it is worth" — and the thing it prices that
   against is "the tree's shape for that is a `WILL_FAIL` canary process". The shape changed, so
   the cost that was weighed is no longer the cost. Whoever rewrites that comment should re-decide
   the question rather than re-word the sentence.

9. **Not mine, observed:** the shared checkout had uncommitted edits to `EventLoop.{hpp,cpp}`,
   `CHANGELOG.md` and `.agent/rules/async-and-net.md` throughout. I did not touch the shared tree at
   all — the whole task was committed from a detached worktree at `origin/master` plus my own files,
   which is also why the `--only`/private-index dance in the dispatch's Concurrency section did not
   arise.

---

## 10. Fix round 1

Nine findings, all addressed. Ordered as the controller asked: the two signature-level items first,
because B8 and B9 are writing against `ISocket` now.

| # | Severity | What changed |
|---|---|---|
| 4 | MEDIUM, signature | `setReceiveDeadline(d<=0)` now REMOVES the bound. The citation said `SO_RCVTIMEO` reads zero as "leave it alone"; `man 7 socket` says the opposite — *"If the timeout is set to zero (the default), then the operation will never timeout"* — which I read on the man page rather than taking from the review. New case, mutation-proved |
| 9 | LOW | `ISocket.hpp` no longer reaches `EventLoop.hpp`: a one-line out-of-line `requestCancelOn(EventLoop&, ParkId)` in a new `IoAwaitable.cpp` restores the forward declaration. **Measured**: 138718 → 130777 preprocessed lines, and 0 occurrences of `EventLoop.hpp` under `clang++ -H` |
| 1 | HIGH | `write`/`writeVectored` claim the slot as their FIRST statement and attempt on a temporary `WriteOperation`, installed only once the operation is known to park. The inline branch can no longer touch `_write` |
| 2 | HIGH | The same guard now runs at all five arm hooks, which is where the slot is actually taken — the site the `asTask` gap goes through |
| 3 | MEDIUM | The owner-lifetime requirement is stated correctly: the owner must outlive the AWAITABLE, not the await, because the destructor retires an unsettled operation |
| 5 | MEDIUM | `tryProbe` is no longer `const` and no longer guesses: `FIONREAD` + `POLLHUP` on a plain fd, and an `ENOTSOCK` branch that discovers `_plainFd` here rather than assuming a read preceded it. Two new cases |
| 6 | MEDIUM | Reported, not fixed — `Tls.cpp` is B11's and mid-merge. The three interface docs that promised what the decorator does not do are corrected, and §8 concern 2 carries the detail |
| 7 | LOW/MED | "It inherits the teardown" was false in three places. Corrected in the CHANGELOG and in §3, and concern 5 now records the real reason rather than the wrong one |
| 8 | LOW | The turn's step-5 idempotence comment no longer claims an invariant my park kind breaks |

**Mutations, each predicted before it ran.**

| Arm removed | Predicted | Actual |
|---|---|---|
| `setReceiveDeadline` back to "non-positive is a no-op" | 1 case, 8 of 20 | 1 case, **8 of 20** |
| `write()` back to the reviewed shape | new canary mode red, old one green | `write-slot-inline` **`SURVIVED`**, `write-slot` **passed** |
| `tryProbe` back to the flat `1` and no `ENOTSOCK` | 2 cases | 2 cases, 6 assertions — `parked == false` and `hasValue == false`, which is scenarios A and B exactly |

**Two things the mutations taught me that the fixes did not.**

*The first deadline case was decorative in one half.* With the writer sleeping 5ms against a 20ms
bound, the byte assertion passed against the very behaviour it existed to catch — only the timer
count failed. Moving the write past the bound made it load-bearing; and then the count had to move
BEFORE the bound, because taken afterwards it reads 0 against the broken code too. The wait is split
for that reason, and each half now carries one assertion.

*A `REQUIRE` inside a loop inside a section silently costs coverage.* Both spellings of "no bound"
shared one `DYNAMIC_SECTION`, so the `REQUIRE` aborted it and the `-5ms` case ran only when
everything already passed — it was covered exactly when it could not matter. The mutation showed 4
failures where I predicted 8; giving each value its own section produced the 8. That is the same
family as the rule about a `REQUIRE` above a stop, in a shape I had not seen.

**`gcc-release` caught what `clang-debug` could not, and it reported as green.** My new lambdas
took an `EventLoop* loop` parameter inside a scope that already had a `loop` local, and a
`bool* sawPark` inside one that already had that parameter. GCC's `-Wshadow` is an error in this
tree; clang did not diagnose it, so `clang-debug` built and passed. **The gcc leg read
`BUILD_EXIT=1` with `CTEST_EXIT=0` and `100% tests passed, 0 tests failed out of 38`** — the tests
ran against the previous binary, because a failed target leaves the last one in place. Four
compile errors, fixed by renaming to `pump`/`seenPark`, and the whole matrix re-run against the
amended commit.

That is the third time in this task that a green total came from a binary that was not built from
the tree under test, and the tell was the same each time: **the build's own exit code, read
separately from the test run's.** A batch that reports only `tests passed` cannot distinguish them.

**And then I caused a fourth, by running two gate batches at once.** I launched the re-run while the
first batch was still working, and both drove the SAME build trees. The re-run's `clang-tidy` leg
reported `CONFIGURE_EXIT=0 BUILD_EXIT=0 CTEST_EXIT=0`, `100% tests passed out of 38` — and its build
log says **`ninja: no work to do.`** Its `rm -rf out/build/clang-tidy` had run, but the other batch
recreated the tree afterwards, so the leg analysed nothing and tested binaries it had not built. Four
of the six legs were the same shape: 7 "steps", all of them the `stb` sub-build.

The objects turned out to be current — newest object 15:58 against newest source 15:38, so the
analysis WAS of the amended code — but I could not have known that from the summary I was reading,
and "it happened to be fine" is not a gate result.

**The rule I first wrote down for this was wrong, and worth correcting rather than keeping because
it sounded strict.** I wrote that a leg counts only if the build exits 0, the step count is
non-zero, AND `no work to do` is absent. The last clause is not a defect signal: `ninja` reaches it
by comparing every input timestamp against every output, so **`no work to do` after a SUCCESSFUL
build is a positive statement that the tree matches the sources** — I confirmed it on the
`clang-debug` tree with `ninja -n` plus newest-source (15:38) against newest-object (16:14). What
actually burned me twice was different and simpler: **a build that exited NON-ZERO while the test
run went ahead on the previous binary.** `gcc-release` did exactly that — `BUILD_EXIT=1`,
`CTEST_EXIT=0`, `100% tests passed`. So the tell is the build's exit code read separately, and a
zero step count is only suspicious when the tree was supposed to have been deleted.

The final matrix was re-run **serially** — the overlap was mine, from launching a second batch
before the first had finished — with the tidy tree deleted and the deletion VERIFIED by testing for
the directory, rather than inferred from `rm`'s exit code.

**Gates, against the committed tree `c8264a0`.** WSL legs run serially; Windows legs with
`--clean-first`. Build exit code beside every total, because that is what distinguishes a green run
from a green reading.

| Configuration | Tests | Skipped | Build |
|---|---|---|---|
| `clang-debug` | **38/38** | 0 | exit 0, tree already current (`ninja -n` agrees) |
| `gcc-release` | **38/38** | **7** | exit 0 — this is the leg that caught the `-Wshadow` errors clang did not |
| `clang-asan-ubsan` | **38/38** | 0 | exit 0 |
| `clang-tsan` | **38/38** | 0 | exit 0 |
| `emscripten` | **28/28** | 0 | exit 0 |
| `clang-tidy` | **38/38** | 0 | exit 0, **548 ninja steps from a verified-empty tree, 0 findings** |
| `cl-debug` (`--clean-first`) | **38/38** | 0 | exit 0, **538 ninja steps**, no `no work to do` |
| `clangcl-release` (`--clean-first`) | **38/38** | **6** | exit 0, **538 ninja steps**, no `no work to do` |
| pinned `clang-format` 22.1.8 | 436 files clean | | |
| `mkdocs build --strict` | exit 0 | | |

The 38th test is the new `write-slot-inline` canary mode; `gcc-release` skips 7 rather than 6 for
the same reason, and `clangcl-release` still skips 6 because the socket-contract canaries are POSIX
only.

**CI on `c8264a0`: green, 28 of 28 jobs, 0 failed** (run `35614400394`). All three macOS legs, all
five Windows legs, both `gcc-14` legs and **`linux (gcc-15)`**.

That last one was expected to be red. Master had been failing on it because Canonical's apt archive
was returning 503 and the compiler never installed, which `ci-ok` requires (**core-cpp#42**, open).
I verified the diagnosis instead of accepting it — on `d2860dc`, the commit before mine, the failing
step is **`Install GCC 15`**, not a configure, compile or test step — so the reading rule was the
step rather than the job. By the time my run reached it the archive had recovered and it passed, so
nothing had to be discounted.

**Worth stating anyway, because it was true while I was pushing:** my local `gcc-release` preset
uses **g++ 14.3.0**, so gcc-15 is witnessed by CI alone. Had the outage still been on, this commit
would have shipped with that toolchain unwitnessed — and the `-Wshadow` errors this round found were
GCC-only, which clang never diagnosed. Same shape as the kqueue point above: the leg I cannot run
locally is the only witness.

**Worktree.** Mine had been de-registered and its directory left behind. I verified the leftover was
byte-identical to `770f2dc` (which is on master) through a temporary-index `git status` before
touching it, then recreated the worktree at the remote head and moved the build trees back rather
than rebuilding them.

---

## 9. Two stale claims in the dispatch

**(a) The Sources list names two files that do not exist at the pin.** It says to read fastcached's
`Net/IoAwaitable.hpp` and `Net/SocketContract.hpp` as blobs at `0708dd54`.
`git ls-tree -r --name-only 0708dd54` has neither. Both are SECTIONS of `Net/ISocket.hpp` (the
`IoAwaitable` class, and `namespace Detail` for `RequireReadBuffer`), and the slot guards are in
`Net/ReadSlot.hpp` and `Net/WriteSlot.hpp`. The plan's B6 file list is about the DESTINATION layout in
core-cpp and is fine; it is only the "read as blobs" line that is wrong. Cost: one `ls-tree`.

**(b) The canary list is right but its upstream is elsewhere.** The three canaries exist upstream under
`src/tests/`, not `src/FastCache/`. Recorded in the provenance row.

Everything the brief said I would CONSUME was present and everything it said I would BUILD was
genuinely absent — verified, not assumed.

