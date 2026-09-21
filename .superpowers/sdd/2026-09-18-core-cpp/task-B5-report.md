# Task B5 — callback timers; `DeadlineTimer` and interruptible sleep no longer poll

**Status: DONE. Pushed as `fe48143`** (range `4049954..fe48143`), 24 files, +2229/-44, a clean
fast-forward onto `4049954`. `origin/master` is `fe481436a49f9bba6b9b1d40db1b0d2463d21dd5`.

**Every gate is green** -- including the five the machine failure in section 10 had left unrun, and
every one of them re-run against the *committed* tree rather than the working copy they were first
measured on. Section 6 has the table and the one failure that was not the code.

Written first to `Z:/core-cpp-b5-rescue/task-B5-report.md` during the outage, when this path was on
a detached volume. **That copy no longer exists and nothing needs tidying**: `Z:` was the user's
second Dev Drive and has since been dropped, so the staging directory went with it.

One thing worth keeping from how that was discovered. When I tried to delete the copy, the tool
answered *"Remove-Item on system path 'Z:\core-cpp-b5-rescue' is blocked. This path is protected
from removal."* I carried it as an open cleanup item on that basis for the rest of the task, and the
real cause was that the volume was gone -- `Get-PSDrive` lists `C D Temp X`, and `Test-Path Z:\` is
false. **A refusal is not evidence that the thing refused exists**, and an error message naming a
plausible cause is the same trap as a probe answering a neighbouring question: confident, specific,
and about something else.

---

## 1. The design decision, and what it costs

**A callback timer is a park in the same table as a coroutine deadline. `TimerId` is a distinct
type over that table, not a `ParkId` and not an identity scheme of its own.**

`detail::Park` gains two members:

```cpp
TimerCallback onExpired = nullptr;   ///< set for a callback park; its `parked` is empty
void* callbackState = nullptr;
```

A callback park is a `Park` whose `parked` is empty and whose `onExpired` is set. It is filed by
the same `registerPark`, sits in the same deadline heap with the same sequence number, is named by
an id from the same never-reused counter, is fired by the same `fireExpiredTimers` in turn step 5,
and is run by the same `drainReadyQueue` in turn step 2 of the next turn.

**What it costs, honestly:**

| Cost | Detail |
|---|---|
| `ParkEntry` grows two fields and a third factory | `ParkEntry::onCallback(cb, state, deadline)` beside `onDeadline` and `onReadiness`. It is public API, so this is API surface a reader has to understand. |
| `ReadyEntry` becomes a two-shape entry | It carries a `ParkId callbackPark` beside `Parked parked`. `drainReadyQueue` branches once per entry. |
| `RunOnceResult::resumed` changes meaning | It now counts what step 2 took off the ready queue — coroutines resumed **plus** timer callbacks run. It had to: `idle` is derived from it, and a turn that ran a callback and resumed nothing is not idle, so `runUntilIdle()` would otherwise stop having just handed control to a callback that queued more work. Documented in the header and in `CHANGELOG.md`. |
| `registerPark`'s refusal widens | From `!entry.work.resume` to `!entry.work.resume && entry.onExpired == nullptr`. |
| One turn of latency | A callback runs in step 2 of the turn AFTER the step 5 that found it due — exactly like `co_await delay()`, and for the same reason (G2). A caller who expected step-5-immediate gets one more turn. |
| A cancellation window that has to be closed | Between step 5 queueing a due timer and step 2 running it, the timer is neither run nor, naively, cancellable. See below. |

**What it buys, which is why I chose it over a separate list:**

- **One answer to "when is the next deadline".** `computeTimeout` (step 3) and `armHostWake` both
  read `_parks.nextDeadline()`, and a callback deadline is in it for free. The brief's stated
  danger — two things computing the next deadline independently — cannot arise, because there is
  only one heap. With a separate list I would have had to merge two minima in two places, and the
  symptom of getting that wrong is a wait that is too long, which is a hang rather than a failure,
  arriving in B8 or B12 as the brief predicted.
- **One firing order across the two kinds**, by deadline then by arming sequence, asserted by
  *A callback timer and a coroutine deadline share one firing order*. Two mechanisms would order
  each kind among itself and nothing across.
- **The generation check for free.** Ids come from the one never-reused counter, so `cancelTimer`
  on a spent id resolves to nothing without a second scheme. This is also what makes the
  cancellation window O(1) — see §2.
- **Teardown already knew what to do.** `unparkEverything` skips a park with no waiter, and
  `abandonParkedWork` empties the whole table, so a callback park needed no new teardown step; only
  the queued-but-not-yet-run entries needed a decision (they are dropped, §2).

**`TimerId` is a distinct struct holding a `ParkId`.** Cost: ~12 lines, and `DeadlineTimer` stores
a `TimerId` rather than a `ParkId`. Benefit: `cancelTimer(TimerId)` and `requestCancel(ParkId)`
cannot be handed each other's arguments. They are both public, both take an opaque id, and only one
of them is meaningful for any given id — `requestCancel` on a timer's id would resolve to nothing
silently, and `cancelTimer` on a coroutine park's id would, without the kind check I added, unpark
a flow and leave it waiting forever. The strong type makes the first impossible; the kind check in
`cancelTimer` (`entry->onExpired == nullptr` → `false`, checked **before** the park is taken) makes
the second impossible even if somebody constructs a `TimerId` by hand.

## 2. Three decisions inside that one, each with a case

**(a) A timer callback runs in turn step 2, not in step 5 where it is found due.** Step 5 could
call it immediately. Then user code would run at a second point in the turn: outside
`dispatchBatch`, outside the assertion that no backend dispatch is in flight, and after the drain
rather than in it. One place that hands control outside the loop is worth one queue hop.
Mutation M1 (§4) fires callbacks inline in step 5 and reds three cases.

**(b) `cancelTimer`'s `true` means "this call prevented the callback", including in the window
between due and run.** The alternative — "already fired ⇒ false" — leaves a window in which a
`DeadlineTimer`'s owner can be destroyed while its callback is still queued, and the callback then
runs against storage that is gone. It costs **no scan of the ready queue**: the ready entry names
the `ParkId`, so taking the park out of the table is what makes the entry resolve to nothing when
the drain reaches it. That is the generation check doing the work rather than a second mechanism.
Case: *cancelTimer still prevents a callback whose deadline has fired but not yet run*.

**(c) Teardown drops a queued timer callback rather than running it.** It is not work to unwind and
not a frame to free; it is a call into a `DeadlineTimer`'s owner, which is being destroyed with the
loop or is already gone. `~EventLoop` step 2 skips callback entries when it splits the ready queue.
Mutation M3 reds *A loop destroyed with a timer armed never runs it*.

## 3. RED then GREEN

**The first RED is the compile failure**, and I wrote the prediction down before running it: *the
build fails in all four new test files (no `addTimer`/`cancelTimer`/`TimerId`/
`pendingTimerSlotCount` on `EventLoop`, no `DeadlineTimer`, no `WakeReason`/
`interruptibleSleepUntil`, no free `sleepUntil`/`nextWakeStep`); zero test cases run.* That is what
happened. Verbatim, first and last of ~40 unique diagnostics:

```
src/core/net/DeadlineTimer_test.cpp:25:18: error: no member named 'DeadlineTimer' in namespace 'core::net'
src/core/net/InterruptibleSleep_test.cpp:30:18: error: no member named 'WakeReason' in namespace 'core::net'; did you mean 'FdWakeReason'?
src/core/net/InterruptibleSleep_test.cpp:31:18: error: no member named 'interruptibleSleepUntil' in namespace 'core::net'
src/core/net/SleepUntil_test.cpp:24:18: error: no member named 'nextWakeStep' in namespace 'core::net'
src/core/net/SleepUntil_test.cpp:25:7: error: no member named 'sleepUntil' in namespace 'core::net'; did you mean 'core::net::testing::TestLoop::sleepUntil'?
src/core/net/Timers_test.cpp:108:29: error: no member named 'addTimer' in 'core::net::testing::TestLoop'
```

**The first GREEN run found a defect in my own case, not in the code.** 55 of 56 passed:

```
/wt-b5/src/core/net/Timers_test.cpp:184: FAILED:
  CHECK( trace.fired == std::vector<std::string> { "callback@5", "coroutine@10", "callback@15" } )
with expansion:
  { "callback@5", "callback@15" }
  ==
  { "callback@5", "coroutine@10", "callback@15" }
```

`delay()` measures from the instant the flow **runs**, not from the `spawn`: a spawned coroutine has
not parked on anything until a turn has resumed it, so advancing the clock before the first drain
put its deadline 10 ms past the advance. The case now drains first and asserts
`pendingTimerCount() == 3` before the clock moves, and the comment says why.

**GREEN:** `core-cpp-net_backend-test` — **2236 assertions in 57 cases** (it was 126 in 27 at B4;
my four files added 30 cases). Full suites in §6.

## 4. Arm-removal mutations

Each applied in a throwaway worktree, built, run, restored. **Every prediction was written down
before the run.** Two came out stronger than predicted, and both misses are recorded.

| # | Arm removed | Predicted | Actual | RED, verbatim |
|---|---|---|---|---|
| M1 | Step 5 calls the callback inline instead of queueing it | 2 cases | **3** | `REQUIRE( calls == 0 )` → `1 == 0` in *cancelTimer still prevents…* and in *A loop destroyed with a timer armed…*; plus `{ "callback@5", "callback@15", "coroutine@10" }` in *…share one firing order* — which I had not predicted, and which is the sharper red: firing inline makes a later callback run before an earlier coroutine |
| M2 | `DeadlineTimer::fire` sets `_settled` **after** the callback | 1 | 1 | `CHECK( owner.settledWhenCalled )` → `false` |
| M3 | `~EventLoop` keeps queued callback entries in the borrowed queue | 1 | 1 | `CHECK( calls == 0 )` → `1 == 0` |
| M4 | Due callbacks pushed to the **front** of the ready queue (a separate-list-drained-first design) | 1 | **2** | `{ "callback@15", "callback@5", "coroutine@10" }`, and `{ "third", "second", "first" }` in the FIFO tie-break case, which I had not predicted |
| M5 | `DelayAwaiter::await_ready` drops the null-loop test | 1, likely a crash | 1, a crash | `SIGSEGV - Segmentation violation signal` in *sleepUntil with no loop never suspends*; the binary died at 39 of 57 cases |
| M6 | `TokenDelayAwaiter` drops the supplied token's stop registration | 2 | 2 (6 assertions) | `CHECK( loop.tick() == 1 )` → `0 == 1`; `CHECK( loop.pendingTimerCount() == 0 )` → `1 == 0`; `CHECK( done )` → `false`; `CHECK( reason == WakeReason::Cancelled )` → `0 == 1` |
| M7 | `await_resume` checks the flow token **before** the supplied one | 1 | 1 | `CHECK( outcome == Outcome::Cancelled )` → `3 == 2` (i.e. `Unwound`) |

**M2 is the one worth a reviewer's attention.** As first written, the destroy-from-inside-the-
callback case asserted only `calls == 1`, `timer == nullptr` and `pendingTimerCount() == 0` — and
it **passed** against the mutation, because settling after the call is a use-after-free that no
value in the case could see. It was an ASan-only red, which is a red once in N runs. The case now
records `DeadlineTimer::settled()` from **inside** the callback (`SelfDestroying::settledWhenCalled`)
and asserts it, which turns it into a deterministic value failure on every platform. I would have
shipped the weaker case believing it worked.

## 5. The measurement B4 asked for: is lazy pruning enough?

B4's concern 4 says a cancelled deadline leaves a heap slot until the root reaches it, and that B5
would find out whether that is enough. **It is**, and here is the number rather than the argument.

`n = 1000` arm/cancel pairs, and the expected counts were fixed before the runs. `n` is 1000 and
not more because every assertion is an exact equality — a larger `n` buys nothing but Debug-CRT
allocator time on Windows, which is what cost commit `1562018`. I added
`ParkTable::timerSlotCount()` and `EventLoop::pendingTimerSlotCount()` so the difference from
`pendingTimerCount()` — which is exactly what lazy pruning is carrying — can be read at all.

| Case | Predicted | Measured |
|---|---|---|
| 1000 armed and cancelled, **no live deadline**, one turn | 0 slots | **0** |
| 1000 armed and cancelled **behind a live root**, one turn | 1001 slots (the root plus every stale slot) | **1001** |
| then advance past the root and drain | 0 slots | **0** |

**What input would have made these different:** eager erase-and-reheap per cancellation would
report **1** in the middle row (and be O(n) per cancel, which is the quadratic loop upstream had);
no pruning at all would report **1000** in the first row and **1001** in the third.

**The conclusion is sharper than B4's statement of the problem.** The bound is not "deadlines ever
armed". Pruning walks from the root while the root is stale, so what accumulates is only what was
armed and cancelled **behind the current live root** — and the root is by definition the *soonest
live* deadline, so every stale slot behind it is due later than the root, and the turn that fires
the root reclaims them all. A loop with one receive deadline per request and a one-second stats
tick carries at most one second of cancelled deadlines, reclaimed each second. I did not change the
pruning, as the brief asked.

## 6. Gates and platforms

| | Result |
|---|---|
| `clang-debug` (WSL) | **31/31** |
| `gcc-release` (WSL) | **31/31** — and it is the toolchain that found a real defect: `-Wshadow` on `DeadlineTimer`'s constructor parameter `onExpired` shadowing the static member `onExpired`. clang-debug and clang-tidy were both silent. The static is now `DeadlineTimer::fire`. |
| `clang-asan-ubsan` (WSL) | **31/31**, no sanitizer report |
| `clang-tsan` (WSL) | **31/31** |
| `clang-tidy` preset (WSL) | **clean**, no finding |
| `emscripten` (emsdk 3.1.56, WSL) | **13/13** with `-LE tree-level`, including `core-cpp.hostdriven-timer-smoke` |
| `consumer-wasm` (emsdk 3.1.56) | **1/1**, six `ok` lines including the two this task added |
| `cl-debug` (VS dev shell) | **33/33** |
| `clangcl-release` (VS dev shell) | **33/33** |
| `ctest -L hygiene` | **16/16** |
| `mkdocs build --strict` | **green** |
| pinned `clang-format` 22.1.8 | 14 files formatted, clean |

Every row was re-run against the committed tree (`fe48143` checked out in a throwaway worktree),
not against the working copy they were first measured on. That mattered: `clang-tidy`, `emscripten`
and `consumer-wasm` had last run *before* the `-Wshadow` rename, which touches `DeadlineTimer.cpp`
— a file in the WebAssembly subset.

### The one thing that failed, and why it was not the code

`cl-debug` failed its first build after the volume came back, in libunicode's table generator:

```
unicode_tablegen: invalid map<K, T> key
```

Not my change — `libunicode` is `core::tui`'s dependency and nothing in this commit reaches it —
but "not mine" is a claim, so I measured it. The generator's *inputs* looked identical to a tree
where it worked: same 48 files, same 41,500,790 bytes. **The checksums were not.** My
`ucd-17.0.0.zip` and B4's had the same size and different MD5s, and so did nearly every extracted
file. Mine was downloaded and unpacked at **07:24** — the minute the volume ran out of space and
detached — so the outage wrote same-length, wrong-content bytes into a fetched dependency.

Running B4's known-good `unicode_tablegen.exe` against **my** data reproduced the failure, which is
what ruled out a corrupt binary and pinned it on the data. Deleting my build tree and re-fetching
fixed it: `cl-debug` then built clean and ran 33/33.

**This is worth a rule.** A disk that fills mid-write does not truncate a file, it leaves it the
right length and the wrong content — so a fetched dependency stays *plausible* and fails much later
as a bug in somebody else's code. Comparing sizes says nothing; comparing checksums says everything.

## 7. The WebAssembly leg, and the defect it nearly hid

`tests/wasm/HostDrivenTimer_smoke.cpp` is the dispatch's scenario plus one: a `PlatformLoop` on the
real host, a spawned coroutine awaiting `delay(20ms)`, **and** a frameless `DeadlineTimer` on the
same heap, with `main` calling `emscripten_sleep(10)` in a bounded loop until both fire. It runs
under node in the `emscripten` job, which the matrix runs on both emsdk versions.
`tests/consumer-wasm` runs the same scenario as a consumer and now links `core::net`; `-sASYNCIFY`
is added by the consumer, never by core-cpp.

**Then I checked the artifact rather than the exit code, and the gate was a decoration.** Moving the
deadline past the program's own bound, it printed

```
FAIL host-driven-timer: after 2000 ms the coroutine delay did NOT fire and the
DeadlineTimer did NOT fire -- the host never pumped the loop to its deadline
```

and **node exited 0**. ctest reported it *Passed*. A loop with an armed deadline always leaves a
pending `emscripten_async_call` behind — that call **is** how the host is asked for its next turn —
and Emscripten's `safeSetTimeout` takes a runtime keepalive per pending timer, so `main` returning
is an *implicit* exit while the runtime is kept alive: the status is recorded and never published.

Two fixes were tried and both rejected, measured rather than reasoned:

- `-sEXIT_RUNTIME=1` changes nothing. The exit path reads the keepalive **counter**, not
  `noExitRuntime`. I confirmed the flag was on the link line and the run still exited 0.
- `emscripten_force_exit()` does publish the status (the same run then exited 1), but it zeroes
  that counter, so the pending timer's later `runtimeKeepalivePop` trips an assertion: the consumer
  program died with `Aborted(Assertion failed)`, a JavaScript stack and **exit 7**. That turns a
  legible failure into an abort, and would do the same to a run that had already succeeded.

So both programs say what happened on their last line and ctest reads it
(`PASS_REGULAR_EXPRESSION` + `FAIL_REGULAR_EXPRESSION`). **Both directions are proven**: the smoke
passes green and, with the deadline moved past its bound, `core-cpp.hostdriven-timer-smoke
(Failed)`; `consumer-wasm` likewise. The reasoning is written out in `tests/wasm/CMakeLists.txt`
and carried into `.agent/rules/async-and-net.md`.

This also means the pre-existing `consumer-smoke (wasm)` leg was *about* to become blind — it was
sound only because that program had no loop, and I was the change that gave it one.

## 8. What else is in the commit

- **`.agent/rules/async-and-net.md`** gains *Timers: one mechanism, and it does not poll* — eight
  rules, each citing its origin as a full URL, including the lazy-pruning measurement and the
  WebAssembly exit-status finding.
- **`.agent/reference/provenance.md`**: 9 new rows (8 from fastcached `0708dd54`, 1 `origin:
  core-cpp` for `Timers_test.cpp`, which has no upstream — fastcached had no frameless timer).
- **`tools/migrate/renames.json`**: 495 → 505 rows, based on `git show HEAD:` and verified
  byte-identical on round-trip before editing. Three include rows, four symbol rows
  (`WakeReason`, `NextWakeStep`, `SleepFor`, `DeadlineTimer`), two member rows (`Disarm`,
  `IsSettled`), one `removed` row (`DefaultPollInterval`), and the two pre-existing shape-change
  rows (`SleepUntil`, `InterruptibleSleepUntil`) gain `target`s so the drift gate validates them.
- **`CHANGELOG.md`**: five **Added** entries and a new **Deprecated** section.
- **`docs/modules/net.md`** and **`docs/design/threading.md`**: the new headers, and the "one
  deadline mechanism" paragraph. `net.md` also said *the loop, its timers and the sockets do not
  yet [build under Emscripten]*, which B4 left stale and my change makes doubly so.

## 9. Judgement calls a reviewer should overturn if they disagree

0. **One commit, not four — and the four-way split was my own idea, which I then argued myself out
   of.** After my work was swept into another lane's commit and then stranded on a detached volume,
   I proposed splitting it: `addTimer`/`cancelTimer`/`TimerId` plus `Timers_test.cpp`, then
   `DeadlineTimer`, then the sleep pair, then the wasm leg. The controller echoed that plan back
   to me as an instruction, so what looked like a disagreement with the controller was a
   disagreement with an earlier version of myself. I committed it as one, which is what the plan
   and the dispatch both name
   (`net: callback timers; DeadlineTimer and interruptible sleep no longer poll`). The reason is
   that the urgency was *losing* the work, and a single local commit ends that risk completely —
   whereas splitting would multiply the bookkeeping the gates require (a provenance row must land
   in the same commit as the file it describes, and `renames.json` and `CHANGELOG.md` likewise) and
   turn one push and one CI run into four, each of which has to be green on its own to mean
   anything. Splitting one local commit afterwards is easy; un-splitting four is not, so this is
   the reversible direction. **If a reviewer wants it split, say so and it will be.**

1. **The deprecated `wakeBound` overload carries no `[[deprecated]]` attribute.** UPHELD at review,
   **and my stated reason was wrong** — corrected in the header, the CHANGELOG and here in fix
   round 1. I argued the attribute was *impossible*: warnings fatal, the pragma forbidden, and
   MSVC's C4996 firing even inside a deprecated caller. The last part is true, the conclusion was
   not, because a third option exists that breaks no rule here — a PRIVATE per-source
   `COMPILE_OPTIONS` (`/wd4996`, `-Wno-deprecated-declarations`) on the one test translation unit
   is neither a pragma nor a PUBLIC flag. **The real reason is the one that should have been
   written down first:** the overload exists so a fastcached caller *compiles unchanged*, and
   `[[deprecated]]` under that consumer's own `-Werror` is exactly what stops it doing so. What
   reports a migration in this project is `tools/migrate/renames.json` and the codemods, not the
   compiler. **An argument that is false but reaches the right answer is worse than no argument**,
   because the first person to check it overturns the conclusion with it.
2. **The overload ignores `wakeBound` rather than honouring it.** Honouring it would keep a polling
   path alive inside the one function whose purpose is removing polling, so a caller who migrates
   and keeps the argument would not get the fix.
3. **`nextWakeStep` is public and core-cpp itself never calls it.** It is the arithmetic a consumer
   still carrying a bounded wait of its own needs (fastcached's `RaftDriver`, `ExpiryReaper`). Its
   doc says so and says it goes when they do. It is tested directly, including `static_assert`s
   that it is usable in a constant expression.
4. **`interruptibleSleepUntil` watches two tokens, and the supplied one wins.** Where the caller
   hands in its own flow's token, it gets `WakeReason::Cancelled` rather than an exception, because
   a caller that asked to be told asked to be told. M7 is the mutation; the case is *The supplied
   token is reported even when it is also the flow's own*.
5. **`addTimer` is loop-thread only** (asserted with `teardownIsSerialisedWithDispatch()`), not
   cross-thread like `schedule()`. The plan's own consumer note says `schedule → loop.post(...
   addTimer ...)`, so posting is the intended cross-thread path and a second inbound queue would
   have been a second mechanism.
6. **`DelayAwaiter` gained an `EventLoop*` constructor and its member became a pointer.** Additive;
   the `EventLoop&` constructor is unchanged and every existing call site compiles.

## 10. The outage, kept for the record (RESOLVED)

**This is history, not a blocker.** `C:` was freed, `C:\DevDriveX.vhdx` re-attached, `D:` came
back intact and WSL restarted. Nothing had to be rebuilt. It is kept because the failure mode
is worth knowing and because §6 shows it left one silent trap behind.


At 07:24:19 the Windows system volume `C:` reached **0 bytes free**. `D:` was a
dynamically-expanding Dev Drive backed by `C:\DevDriveX.vhdx` (751 GB on disk); it could not grow,
ReFS logged event 50, the `disk` provider logged fifteen paging-I/O errors on
`\Device\Harddisk1\DR1`, and Windows detached the image, which then reported `Attached: False`.
WSL's `ext4.vhdx` is also on `C:`; it went read-only in the same minute -- which is what killed the
`clang-tsan` build mid-way -- and `wsl.exe` then failed with `Wsl/Service/E_UNEXPECTED`.

Two VHDX files alone were 1007 GB of an 1862 GB volume (`C:\Users` a further 741 GB), so this was a
matter of when rather than whether. The last allocation before it went was mine: a `cl-debug` build
tree in a worktree I had just created at `D:\core-cpp-wt-b5`.

**While it was down I deleted nothing and re-attached nothing**, because both are irreversible on
somebody else's machine and neither was mine to decide. What I did instead was write this report to
`Z:` -- the *other* Dev Drive, `C:\DevDrive.vhdx`, fixed-size and so unable to fail the same way --
and send a rebuild-grade description of every unpushed change to the controller, so the work was
recoverable from the conversation whether or not the image came back. It came back once the
controller freed space on `C:`, with `D:\core-cpp` intact; nothing had to be rebuilt.

**Three lessons the outage is worth keeping for:**

1. **A full disk corrupts rather than truncates, but only on the volume that stayed writable.**
   Section 6's `libunicode` failure is the trap it left behind: a dependency fetched at 07:24 had
   the right file names and the right byte counts and the wrong bytes, and failed hours later as
   what looked like a bug in somebody else's code generator. Sizes prove nothing there; checksums
   prove it.

   **The two volumes failed differently, and that difference is the test.** WSL's image went
   READ-ONLY, so writes failed outright and nothing was silently mis-written -- which is why the
   `clang-tsan` build died loudly with `Read-only file system` and why all five Linux trees
   checksum clean (`aa35d214bbc24b3b8d07b96bb9db7dca`, five times). The Windows Dev Drive stayed
   WRITABLE while the VHDX behind it could not grow, so writes "succeeded" with the wrong bytes.
   So "do not trust a build directory that was open during a detach" is not a superstition and not
   a blanket rule: **ask which way that filesystem failed.** One that refuses writes is safe; one
   that accepts writes it cannot honour is not, and only the second kind needs checksumming.
2. **Work that is not committed has no copy.** Mine was swept into another lane's commit earlier in
   the same hour and then stranded here, and both times it survived by luck rather than by design.
3. **Build output is what belongs on the volume you can afford to lose.** My build trees are
   deleted again now that the gates have run.

## 11. What I am leaving for B6 and B12, named

- **B6 (sockets).** A receive deadline per read is the workload that arms and cancels deadlines
  fastest, and §5 is the measurement it should be held to: if `pendingTimerSlotCount()` ever grows
  without a live root ahead of it, the reasoning in §5 is wrong and not the pruning. `DeadlineTimer`
  is the shape for a dial's timeout — it tears the operation down; `withTimeout` only stops waiting.
- **B12 (TUI runtime on `EventLoop`).** `addTimer` is the frameless timer the TUI's redraw pacer
  and debounce want, and it is loop-thread only: a TUI thread that arms from elsewhere must
  `post()`. The plan's `PlatformLoop`-per-thread note (`schedule → loop.post(...addTimer...)`,
  `cancel` → `post(cancelTimer)`) is exactly this API and needs no addition.
  **B12 does NOT have to route every `addTimer` through `post()`.** It would have had to before fix
  round 1: `addTimer` filed a park and asked a host-driven backend for nothing, so a timer armed
  from an input callback on a quiescent browser loop was filed, correct, and silently never fired.
  That is C1, it is fixed, and *A timer armed on a quiescent host-driven loop asks the host for the
  turn that runs it* (`HostDrivenLoop_test.cpp`) is what keeps it fixed. Arming from a TUI input
  handler on the loop's own thread is now enough.
- **B12 / any WebAssembly consumer.** A program that owns a loop cannot be judged by its exit
  status (§7). Any new wasm test or consumer needs `PASS_REGULAR_EXPRESSION`.
- **Whoever removes the deprecated overload** (next minor): `interruptibleSleepUntil`'s four-argument
  form, its case, its `renames.json` note, and `nextWakeStep` with it if no consumer still has a
  bounded wait.

## 12. Defects in other lanes' files — reported, not fixed

1. **`tools/migrate/renames.json` has no include row for `FastCache/Async/IReactor.hpp`.** Eleven
   other `FastCache/Async/*.hpp` headers have one; the one that became `core/net/EventLoop.hpp` —
   the single most-included header a fastcached consumer has to rewrite — does not. Task B4's.
   I added my three and left that one alone.
2. **`docs/modules/net.md` said the loop and its timers do not build under Emscripten.** Stale
   since B4 landed `EventLoop.cpp` in the WebAssembly subset. I rewrote that paragraph because my
   change makes it doubly wrong, so this is a note rather than a hand-off.
