# Task B5 — re-review of fix round 1

Scope: the two commits in `rereview-B5.diff` — `fb3fe97..a02031c` (the fix round, 15 files,
+358/−64) and `27b8b43..8d7b8b2` (M8's `TokenDelayAwaiter` guard, +26) — against
`task-B5-review.md` (1 Critical, 2 Important, 8 Minor, 2 opinions, 2 rulings),
`task-B5-report.md` and `task-B5-fixround1.md`.

This re-review was owed after the fix round and was not performed. No builds were run; both
commits are reported CI-green at 25/25 and that was not re-checked. Everything below is read from
the diff and from the tree at `master`.

**Verdict: the fix round is sound. Ten of eleven findings are addressed correctly, one is
addressed with a concern, and the Critical's fix is correct but its FAMILY is not closed.**

**Counts: 9 ADDRESSED, 2 ADDRESSED-WITH-CONCERN, 0 NOT ADDRESSED.**

---

## Per-finding

| # | Finding | Verdict |
|---|---|---|
| **C1** | `addTimer` never asked a host-driven loop for a turn | **ADDRESSED-WITH-CONCERN** |
| **I1** | the kind check making `TimerId` safe had no case | **ADDRESSED** |
| **I2** | `cancelTimer` carried no serialisation assertion | **ADDRESSED** |
| **M1** | `consumer-wasm`'s `FAIL_REGULAR_EXPRESSION` inert | **ADDRESSED** |
| **M2** | the new meaning of `resumed` reached one of three doc sites | **ADDRESSED** |
| **M3** | `DeadlineTimer` documented a non-null callback, did not check it | **ADDRESSED** |
| **M4** | `pendingTimerSlotCount()` public with no CHANGELOG entry | **ADDRESSED** |
| **M5** | `.front()` guarded only by a sibling count | **ADDRESSED** |
| **M6** | a case name claiming half a behaviour | **ADDRESSED** |
| **M7** | an assertion that cannot come out the other way | **ADDRESSED** |
| **M8** | a park filed before the stop registrations that can throw | **ADDRESSED-WITH-CONCERN** |

Both labelled opinions were taken (the sibling-kill case; `[[maybe_unused]]` in both wasm
programs). Both rulings were applied (`InterruptibleSleep.hpp:66-70` and `CHANGELOG.md` now give
the real reason for no `[[deprecated]]`; `resumed` → `drained` at all three doc sites plus the
five in-tree references).

---

### C1 — ADDRESSED-WITH-CONCERN

**The fix itself is right.** `EventLoop.cpp:688-689`:

```cpp
if (!isOnWorkerThread())
    armHostWake();
```

`isOnWorkerThread()` is `running() && this thread is the driver`
(`detail/WorkerIdentity.hpp:85-89`), so the predicate reads exactly "not inside a turn", which is
what the comment claims. `armHostWake()` rather than `wake()` is the correct choice and the case
proves the difference: `soonestDelayMs(host) == 50` would fail against a `wake()`, which
`HostDrivenBackend::wake()` turns into `scheduleAt(now)` → a 0 ms `callAfter`.

**The non-quiescent paths, which the new case does not exercise, are correct by composition and
are separately covered.** `HostDrivenBackend::scheduleAt` (`HostDrivenBackend.cpp:43-59`) returns
early only when a pump is already out *no later than* the new request, and
`HostDrivenBackend_test.cpp:170-188` asserts both directions — a later deadline does not displace
a scheduled pump, an earlier one is scheduled beside it. So arming a timer off-turn on a loop that
already holds a deadline works, in both orderings, without a case of its own. I traced this rather
than assuming it, because the new case asserts `host.pendingCount() == 0` first and therefore says
nothing about it.

**The concern: the fix does not reach every path that files work a turn must run.** The review
asked for exactly this check. The new comment at `EventLoop.cpp:678-680` states an enumeration —

> Every other member that files work a turn must reach -- `post`, `submit`, `schedule`, `spawn`,
> `requestCancel`, `stop` -- wakes for this reason; this one was the exception until it was not.

— and that enumeration is **incomplete in three places**. Enumerated by reading every public
member of `EventLoop` that mutates `_ready` or `_parks`, not by grepping for `wake`:

| Member | Files | Asks the host for a turn? |
|---|---|---|
| `post` (`:595`) | a callback | `_backend.wake()` ✔ |
| `submit` (`:497`) | ready work | on-turn inline / off-turn `wake()` ✔ |
| `schedule` (`:522`) | a deadline | on-turn `registerPark` (turn arms on exit) / off-turn `wake()` ✔ |
| `spawn` (`:634`) | ready work | `if (!isOnWorkerThread()) _backend.wake()` ✔ |
| `requestCancel` (`:833`) | ready work | on-turn inline / off-turn `wake()` ✔ |
| `stop` (`:608`) | the stop flag | `_backend.wake()` ✔ |
| `addTimer` (`:660`) | a deadline | **fixed this round** ✔ |
| **`registerPark` (`:738`)** | a deadline or a readiness park | **nothing** ✘ |
| **`resumeSoon` (`:719`)** | ready work | **nothing** ✘ |
| **`requestStop` (`:623`)** | ready work, via `unparkEverything` (`:924-954`) | **nothing** ✘ |

All three are public, all three assert `teardownIsSerialisedWithDispatch()` — so all three are
*legal* off a turn on the loop's own thread, which is precisely the browser-callback position C1
was about. `addTimer`'s own doc (`EventLoop.hpp:379`) says "Loop thread only, like `registerPark`
and `resumeSoon`", i.e. the header already groups the fixed member with two unfixed ones.

**`registerPark` is the one that matters**, because it is `addTimer`'s own implementation path: the
fix sits one level *above* the primitive, so the primitive's other callers are uncovered.
Concretely reachable today, with no new API:

```cpp
// core::async::DetachedTask has std::suspend_never initial_suspend (DetachedTask.hpp:61),
// so the body runs INLINE at the call — off-turn, in a DOM handler.
core::async::DetachedTask pace(core::net::EventLoop* loop) {
    co_await loop->delay(16ms);   // DelayAwaiter::await_suspend -> registerPark, EventLoop.hpp:811
    …
}
```

On a quiescent host-driven loop the park is filed, correct, and silently never resumed — C1's
exact failure, through the coroutine half instead of the callback half. `requestStop()` off-turn
has the same shape: the flows it unparks queue into `_ready` and nothing pumps, so the
cancellation stalls until something else drives the loop or the loop is destroyed.

This is **not a regression** — all three predate B5 and none is in its diff — and natively none is
reachable (the only legal off-turn caller on another thread is caught by the assert, and a
`runOnce`-driven consumer recomputes its timeout in step 3). But it is what the review asked to be
verified, the complete fix is the same two lines placed in `registerPark` plus a `wake()` in
`resumeSoon`/`requestStop`, and the comment as written will be read by the next author as an audit
that has been done.

**For B12 specifically:** the report's §11 hands B12 `addTimer` for the redraw pacer, and that is
now safe. A `co_await loop->delay()` from a TUI input callback on a host-driven loop is not.

**The case is sound.** Mutation: remove the two lines → `REQUIRE(host.pendingCount() == 1)` at
`HostDrivenLoop_test.cpp:155` fails, as B5 measured. Second mutation, not run but worth recording:
replace `armHostWake()` with `_backend.wake()` → `CHECK(soonestDelayMs(host) == 50)` fails on 0.
The case distinguishes both the presence of the arming and its value.

### The WebAssembly cases — the isolation is real

I checked the specific thing the lead asked about, because B5's own account is that its *first*
fix was still insufficient.

`tests/wasm/HostDrivenTimer_smoke.cpp`: phase 1 constructs the `DeadlineTimer` with nothing else
on the loop, `waitForFlag(&timerFired)` yields for up to `Bound` = 2000 ms in `Step` = 10 ms
slices, then `auto const timerFiredAlone = timerFired;` snapshots the verdict **before** phase 2's
`spawn`. `status` reads the snapshot. With C1 reverted, nothing is ever scheduled with the host
during phase 1 — `PlatformLoop`'s constructor schedules nothing (`PlatformLoop.hpp:66-70`), and
`emscripten_sleep` yields to a host with an empty queue — so `timerFiredAlone` is false and the
`FAIL` line prints. The mechanism is isolated.

`tests/consumer-wasm/main.cpp` uses no snapshot, and does not need one: `checks.expect(timerFired,
…)` at `:145` is evaluated **before** `loop.spawn(…)` at `:147`, which is the same observation
point by a different route. `checkHostDrivenLoop` is the last of four checks (`:167-170`) and the
three before it construct no loop, so nothing else has a pump outstanding.

**The mutation that would leave both green if the fix were reverted: moving any `spawn`, `post`,
`submit` or `requestCancel` above the `DeadlineTimer` construction in phase 1.** That wake buys a
turn whose `armHostWake()` picks up every deadline in the heap. Both files carry a comment saying
so in as many words (`HostDrivenTimer_smoke.cpp:57-58`, `main.cpp:140-141`), which is the best
available defence — the isolation is a property of the program's *order*, and no assertion in
either program can observe its own ordering. Nothing structural enforces it. That is a known and
documented fragility rather than a defect, and I could not find a cheap way to close it: an
assertion on the backend's pump count is not reachable from a `PlatformLoop`'s public surface.

### I1 — ADDRESSED

`Timers_test.cpp:274-291`. The case parks a real coroutine handle through the public
`registerPark` + `ParkEntry::onDeadline`, wraps its `ParkId` in `TimerId { park }`, and asserts
three things: the refusal, that the park survives (`pendingTimerCount() == 1`, i.e. the check runs
*before* `_parks.take`), and that the flow still resumes. Deleting `|| entry->onExpired ==
nullptr` from `EventLoop.cpp:709` reds all three, which is what B5's M9 measured. Lifetimes are
right: `flow` is declared after `loop`, so the `Task` frame goes before the loop, and the park's
waiter has already been taken by the drain.

One residual, reported under new defects below: the *symmetric* direction — `requestCancel` handed
a timer's park — is still uncovered, and the three `static_assert`s do not reach the form that
actually compiles.

### I2 — ADDRESSED

`EventLoop.cpp:699-701` carries the same `teardownIsSerialisedWithDispatch()` assert as `addTimer`
with a message naming `post()`. It is a `noexcept` function and the predicate is two atomic loads,
so nothing about the assert can throw. The new sibling-kill case exercises the *passing* direction
on the worker thread (`~DeadlineTimer` from inside the drain), which would have caught an inverted
predicate. The failing direction has no canary — see the "no case" verdicts.

### M1 — ADDRESSED

`tests/consumer-wasm/CMakeLists.txt:138` is now `FAIL_REGULAR_EXPRESSION "check[(]s[)] failed"`. A
bracket expression carries no backslashes, so CMake's un-escape pass leaves it intact and CTest
compiles a regex that matches the literal `check(s) failed` the program prints at `main.cpp:174`.
The comment above it records why `\(s\)` did not. Correct.

### M2 — ADDRESSED (via the rename)

All three sites carry the new meaning: `RunOnceResult::drained` (`EventLoop.hpp:406-418`),
`runUntilIdle` (`:440-443`) and `TestLoop::tick()`/`drain()` (`TestLoop.hpp:67-70, 72-73`). A
tree-wide grep for `.resumed`, `::resumed` and `RunOnceResult::resumed` across `src/`, `tests/`
and `docs/` returns nothing but one CHANGELOG line — which is itself a small new defect, below.
(That grep would miss a reference in a comment that writes the field name without a dot or colon;
I read the three doc sites directly for that reason.)

### M3 — ADDRESSED

`DeadlineTimer.cpp:14-19`. The assert is in the constructor, where the mistake is, with a message
that says what would otherwise happen. Observation, not a defect: `addTimer` has a *release*
fallback (`return TimerId::invalid()`) and the constructor does not, so in a Release build the
null callback is still armed and still called through at `DeadlineTimer.cpp:53`. The review asked
for one assert and got it; recording the asymmetry so it is a decision rather than an oversight.

### M4 — ADDRESSED

`CHANGELOG.md:450-452` names `TimerCallback`, `ParkEntry::onCallback` and
`EventLoop::pendingTimerSlotCount()`, which is the full set the review identified.

### M5 — ADDRESSED

`DeadlineTimer_test.cpp:200` adds `REQUIRE(backend.recordedTimeouts().size() == 1)` guarding the
container that `.front()` indexes, beside the `waitCount()` check rather than instead of it.
Matches `Timers_test.cpp:338`.

### M6 — ADDRESSED

`Timers_test.cpp:372-384` now spawns `recordCancellation`, drains to the park, asserts
`pendingTimerCount() == 2`, then asserts both clauses after `requestStop()`: `CHECK(cancelled)`
and `REQUIRE(pendingTimerCount() == 1)`. `cancelled` is declared before the loop, correctly.

### M7 — ADDRESSED

`InterruptibleSleep_test.cpp:143-146` removes `CHECK(loop.pendingTimerCount() == 0)` and leaves a
comment saying why it is not coming back.

### M8 — ADDRESSED-WITH-CONCERN

**The placement asked about is right.** `InterruptibleSleep.cpp:84-93`:

- The guard is declared **before** `registerPark`, so a throw from `registerPark` itself is
  covered — `_park` still holds its default-constructed `ParkId`, and `unregisterPark` on an
  invalid id is a no-op. This is the same shape as `DelayAwaiter` (`EventLoop.hpp:802-813`).
- `registered = true` sits after the **second** `emplace` (`:93`), so a `std::bad_alloc` from
  either `_tokenReg.emplace` or `_flowReg.emplace` unregisters the park. That is the correct
  placement for a two-callback awaiter: a flag after the first emplace would leave a throw from
  the second uncovered, and the guard has no dismiss, so the flag is the only control.
- **The `noexcept` marking is honest, not a silencer.** `unregisterPark` is declared
  `void unregisterPark(ParkId) noexcept` (`EventLoop.hpp:528`), and `ScopeGuard`'s constraint is
  `requires std::is_nothrow_invocable_v<Callable&>` (`detail/ScopeGuard.hpp:31`) — so the marking
  is *required* for the code to compile at all, and the body genuinely cannot throw. It is not
  suppressing a diagnostic; it is satisfying a constraint whose purpose is that the guard runs
  from a destructor.

One benign asymmetry I checked and cleared: on the throw-from-`_flowReg` path the guard
unregisters the park while `_tokenReg` is still registered on `_token`. If that token is stopped
in the window, the callback runs `requestCancel` on an id the generation check resolves to
nothing. Harmless.

**The concern: there is a third awaiter with this exact shape and it was not guarded.**
`WaitHandleAwaiter::await_suspend` (`EventLoop.hpp:871-886`) calls `registerPark` at `:877` and
then `_cancelReg.emplace(…)` at `:884` with nothing between them. A throw from that emplace leaves
the park filed for a frame that then unwinds through its `co_await`, so `await_resume` — the only
other caller of `unregisterPark` — never runs. It is **worse than the two that were fixed**: a
readiness park carries a backend registration (`entry->attached`), so the leak is a kernel
registration that `detach` never reverses, not only a table entry.

The new comment claims otherwise: `InterruptibleSleep.cpp:81-82` says "Matched to `DelayAwaiter`'s
guard in `EventLoop.hpp` **so the two cannot drift**". There are three, and one of them has
already drifted. Same reachability argument applies (a 16-byte closure inside both mainline
`std::function` small buffers), so this is not a live defect — but a comment asserting that a
family is closed when it is not is the thing this project keeps paying for.

---

## The three rows B5 marks "no case"

B5 moved a fourth row out of that column by realising the checker was the compiler rather than the
runtime. The lead asked whether the same move is available for any of the remaining three. It is
available for one, and one of the other two is mislabelled.

### 1. Lazy pruning's counterfactuals — **"no case" is HONEST, and this is the strongest of the three**

The *claim* is covered: `Timers_test.cpp:421-…` asserts three exact equalities with N fixed before
the run, and the instrument cross-checks itself across sections (`pendingTimerSlotCount()` reads
`N` in one section and `0` in another for the same `pendingTimerCount() == 0` state, so a
`pendingTimerSlotCount` that merely forwarded to `pendingTimerCount` would red). What has no
mutation is the *arithmetic about designs that do not exist in the tree* — what eager pruning or
no pruning would have cost. No runtime case can execute an implementation that was not written,
and mutating `pruneTimers` is `detail/ParkTable.hpp`, B4's file. The row is honest and the reason
it gives is the real one.

### 2. The loop-thread-only assertions — **"no case" is NOT honest as phrased; the right word is "declined"**

The row says an `assert` "cannot be observed from inside a Catch case — it aborts the binary",
then names the tree's own answer in the next sentence: a `WILL_FAIL` canary process
(`src/core/net/HostDrivenCanary.cpp`), which is how B4 covers `run()` and `blockOn()`. So the
mechanism exists, is in this module, and is already wired into CMake with the `canary` label. That
makes this "testable, and declined for cost", which is a different claim from "no case", and the
column heading is what a later reader will act on.

The cost of leaving it rose this round rather than falling: I2 **added a second such assertion**,
and the review's stated reason for asking was B6's socket deadlines and B12's TUI, i.e. the two
tasks about to build on it. The predicate both asserts share —
`teardownIsSerialisedWithDispatch()` = `!running() || isOnWorkerThread()` (`EventLoop.hpp:499-502`)
— is one expression with two atomic loads, and nothing currently reds if it inverts. The passing
direction is exercised incidentally by the new sibling-kill case; the failing direction is not
exercised anywhere.

### 3. `DeadlineTimer` allocates nothing and holds no coroutine frame — **TESTABLE after all, by B5's own move**

This is the row where the fourth row's lesson applies and was not applied.

B5's stated obstacle is that the only assertion it can think of is fragile (`sizeof`). That is true
of an **equality**; it is not true of an **upper bound**. `static_assert(sizeof(DeadlineTimer) <=
5 * sizeof(void*))` does not break on padding, on member reordering, or on a platform with a
different pointer size — it breaks on exactly one thing, which is a member being added. That is
the claim. The same file can add `static_assert(!std::is_polymorphic_v<DeadlineTimer>)` and, for
the "no coroutine frame" half, the type has no `promise_type` and is not a coroutine handle
wrapper — `static_assert(std::is_nothrow_move_constructible_v<…>)` says nothing useful, but the
size bound does, because a coroutine frame would arrive as an owning member or a `shared_ptr` and
both grow the type.

The row's own prose is additional evidence that a compile-time check is owed: it says
"**four** members, none owning". There are **five** — `_loop`, `_onExpired`, `_state`, `_timer`,
`_settled` (`DeadlineTimer.hpp:81-85`). A claim stated in prose and miscounted in the same
sentence is the exact failure mode the claims table was built to prevent.

---

## New defects introduced or left by the fix round, severity-ordered

### Important — C1's family is not closed, and a comment now asserts that it is

`EventLoop.cpp:678-680` (the comment), `EventLoop.cpp:738` (`registerPark`), `:719`
(`resumeSoon`), `:623` (`requestStop`).

Full argument under C1 above. Three public members file work that only a turn can reach and ask a
host-driven backend for nothing; `registerPark` is `addTimer`'s own implementation path, so the
fix is placed above the primitive rather than in it. Reachable today through
`core::async::DetachedTask`, which is eagerly started (`DetachedTask.hpp:61`), so a
`co_await loop->delay()` in a handler parks off-turn. Not a regression — all three predate B5 —
but it is the check the review asked for and the new comment reads as that check having been done.

Minimal complete fix: move the two lines into `registerPark` (a no-op for non-host-driven
backends, and `scheduleAt`'s coalescing absorbs the extra calls) and give `resumeSoon` and
`requestStop` the `if (!isOnWorkerThread()) _backend.wake();` that `spawn` has.

### Minor — `WaitHandleAwaiter::await_suspend` is the unguarded third instance of M8's shape

`EventLoop.hpp:877` then `:884`, with nothing between. Worse consequence than the two guarded
awaiters because the leaked park carries a backend attach. The comment at
`InterruptibleSleep.cpp:81-82` says "the two cannot drift"; there are three.

### Minor — the symmetric half of I1 is still uncovered, and the `static_assert`s do not reach it

The three `static_assert`s (`Timers_test.cpp:47-53`) are **correct for what they literally
assert**: `TimerId` and `ParkId` are both aggregates over a single member
(`detail/ParkTable.hpp:47-59`, `:108-120`) with no converting constructor and no conversion
operator, so neither is implicitly convertible to the other and both negative assertions hold for
the right reason, not vacuously.

But the claim row they were written for is "the two id types cannot be handed to each other's
cancellation", and `TimerId::park` is a **public** member — `loop.requestCancel(timer.park)`
compiles. That form is caught at runtime by `!entry->parked` in `resolveCancel`
(`EventLoop.cpp:865`), which has no case; the mirror of I1, in the direction the fix round did not
go. Worth stating plainly: that guard's mutation would come back **green**, because
`queueParkedWaiter` → `_parks.takeWaiter` short-circuits on an empty waiter anyway
(`EventLoop.cpp:884-888`). So the defence is real but doubled, and the honest claim is narrower
than the row's wording: *implicit conversion* goes neither way, and the explicit form is refused
at runtime by a check nothing exercises.

### Minor — the CHANGELOG narrates the rename of a name that never shipped

`CHANGELOG.md:445-450` still introduces the field as `RunOnceResult::resumed`, describes what it
counts, and then says "**That field is renamed `resumed` → `drained`** while `RunOnceResult` is
still unreleased". `RunOnceResult` has never been in a release, so a reader of the eventual notes
is told about a rename from a name they were never given. The review's own argument for renaming
now — "today it is a word in a CHANGELOG entry" — is undercut by turning one word into a clause.
The entry should simply say `RunOnceResult::drained`.

### Observation — `DeadlineTimer` with a null callback still calls through null in Release

Recorded under M3 above. The review asked for one assert and one assert is what landed; the
asymmetry with `addTimer`'s release fallback is a decision worth being explicit about rather than
a defect.

---

## What I checked and found correct, stated because it is a review result

- `isOnWorkerThread()` is the right predicate for "inside a turn", and the nesting in
  `WorkerIdentity::Scope` (`detail/WorkerIdentity.hpp:41-73`) means a `runOnce` inside a `run`
  does not release the claim early, so `addTimer` from a timer callback is correctly skipped.
- `cancelTimer` needs **no** wake or arm, and I verified rather than assumed it: removing a
  deadline can only make the next one later, so an already-armed host pump fires early, runs a
  turn, finds nothing due and re-arms from the same heap. `armWakeAt(std::nullopt)` is a no-op, so
  a loop that goes quiescent stops asking. The same argument covers `unregisterPark` and
  `cancelPending`.
- `HostDrivenBackend::scheduleAt`'s coalescing is correct in both directions for an off-turn
  `addTimer`, and both directions are covered by `HostDrivenBackend_test.cpp:170-188`.
- The new sibling-kill case (`DeadlineTimer_test.cpp:207-236`) tests the path the cancellation
  window exists for, and its declaration order is right: `pair` after `loop`, so the victim is
  destroyed before the loop it would otherwise reach through freed storage.
- `ScopeGuard`'s `requires std::is_nothrow_invocable_v<Callable&>` makes the `noexcept` lambda
  marking load-bearing rather than cosmetic, and `unregisterPark` is genuinely `noexcept`.
