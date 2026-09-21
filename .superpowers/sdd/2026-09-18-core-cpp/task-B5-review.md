# Task B5 review — callback timers; `DeadlineTimer` and interruptible sleep no longer poll

Reviewed: `fe48143` (`4049954..fe48143`), 24 files, +2229/-44, against
`.superpowers/sdd/2026-09-18-core-cpp/task-B5-dispatch.md`, the implementer's report, and
`docs/superpowers/specs/2026-09-18-core-cpp-design.md` §2 item 5 and its `EventLoop` interface
block.

CI on `fe48143` was still running when this review was written and was not consulted in either
direction.

## Verdicts

**Spec compliance: PASS.** Every symbol the spec's `EventLoop` interface block names is present
with a matching signature — `addTimer(SteadyTimePoint, TimerCallback, void*) -> TimerId`,
`cancelTimer(TimerId) noexcept -> bool`, `delay`, `sleepUntil`, and the free
`sleepUntil(EventLoop* loopOrNull, SteadyTimePoint)` with the spec's own annotation
(`null/elapsed ⇒ no suspend`) actually implemented as `await_ready() == true` rather than as a
prompt return. §2 item 5's "`interruptibleSleepUntil` and `DeadlineTimer` stop polling" is
delivered and proved: both park once, `DefaultPollInterval` is gone, and
`DeadlineTimer_test.cpp:184` asserts the *value* the backend was handed (500 ms) rather than a
wake-up count. The dispatch's extras — `nextWakeStep`, the deprecated four-argument overload, the
lazy-pruning measurement, the two WebAssembly programs, the provenance rows, `renames.json`, the
rulebook section with full-URL origins — are all present.

**Code quality: PASS WITH ONE REQUIRED CHANGE.** The central design decision is right and the
reasoning for it holds up under reading, not just under argument. The cancellation-window logic,
`DeadlineTimer::fire`'s ordering, the `~EventLoop` drop, and the `DelayAwaiter` null paths are all
correct — I traced each one and they are covered below. One Critical defect must land before Task
B6 or B12 builds on this.

**Counts: 1 Critical, 2 Important, 8 Minor, 2 labelled opinions.**

---

## What I verified and found correct

These are stated because the lead asked for them specifically, and because "I checked it and it
holds" is a review result.

**The cancellation window.** `cancelTimer` returns `true` exactly when it prevented the callback,
across all four positions:

| When | Path | Answer |
|---|---|---|
| Before the deadline | `_parks.find` hits, `onExpired` set, `take` | `true` |
| Queued by step 5, not yet run | `takeExpired` leaves the park in the table with `deadline` reset; `find` still hits | `true`, and the `ReadyEntry` resolves to nothing at `EventLoop.cpp:445` |
| From inside the callback | `runDueCallback` took the park at `EventLoop.cpp:445` **before** the call | `false` |
| After it ran | same | `false` |

`ParkTable::dropIndices` guards `_liveTimers` on `park.deadline.has_value()`
(`ParkTable.hpp:480`), so a park cancelled in the queued-but-not-run window does not
double-decrement. `requestCancel` handed a timer's `ParkId` falls out at
`EventLoop.cpp:777` (`!entry->parked`). The inverse — `cancelTimer` handed a coroutine park's id —
is refused at `EventLoop.cpp:652` **before** the park is taken, which is the right order; see I1
for the fact that nothing tests it.

**`DeadlineTimer::fire` ordering** (`DeadlineTimer.cpp:37-54`). `callback` and `callbackState` are
read into locals, `_settled` and `_timer` are set, then the call — and the function ends on the
call. Nothing touches `*self` afterwards. `~DeadlineTimer` → `disarm()` returns at
`if (_settled) return;` without ever reaching `_loop`, so a loop pointer is not dereferenced from
inside its own callback either. The case at `DeadlineTimer_test.cpp:163` now records
`settled()` from inside the callback, which is what turns the M2 mutation into a value failure
instead of an ASan-only one.

**`~EventLoop` dropping queued callback entries is right.** `EventLoop.cpp:81-87` splits the ready
queue and `continue`s past callback entries, which `_ready = std::move(borrowedQueue)` then
discards. Nothing leaks: the `Park` itself is still in `_parks` and is freed by step 4's
`takeAll()` + `parks.clear()`; a callback park carries no frame, no claim and no owned chain, and
`callbackState` is borrowed. `unparkEverything` skips it for the same reason
(`EventLoop.cpp:858`, `!entry->parked`). Running it would be the one path on which a callback
reaches an object whose loop has stopped existing. Correct as built.

**`DelayAwaiter`'s null paths are all covered.** `await_ready` (`EventLoop.hpp:722`) guards;
`await_suspend` is unreachable with a null loop and says so; `await_resume`
(`EventLoop.hpp:750`) guards; the implicit destructor touches nothing. Every construction site in
the tree was checked: `EventLoop::delay`/`sleepUntil` (`EventLoop.cpp:882,887`) use the reference
constructor, and the free `sleepUntil` is the only pointer site. `core::tui::DelayAwaiter` is an
unrelated type and is untouched.

**`interruptibleSleepUntil`'s two registrations and the `await_resume` order.** The order
(supplied token, then flow token, then `Deadline`) is what M7 exercises and it is load-bearing.
The double registration on one token is safe: both call `requestCancel(park)`, and the second
resolves a park whose waiter the first already took. A stop arriving between the
`stop_requested()` check and `_tokenReg.emplace` is not lost — `StopCallback`'s constructor
invokes immediately when stop was already requested, with a valid `_park`. Member declaration
order looked like a hazard (the two `StopCallback`s are destroyed *after* the tokens they are
registered on) but is not: `detail::StopCallbackFallback` holds its own
`std::shared_ptr<StopState>` (`StopToken.hpp:513`), as `std::stop_callback` does.

**A timer callback arming another timer is safe under the drain.** `runDueCallback` holds the
`Park` in a local `unique_ptr` after `_parks.take`, so a re-arm that rehashes `_parks` cannot
invalidate it. `Timers_test.cpp:267` covers it.

**The WebAssembly pass/fail regexes discriminate — for `tests/wasm`.** `tests/wasm/CMakeLists.txt:58`
requires the literal `ok host-driven-timer: a coroutine delay and a DeadlineTimer both fired`,
which `HostDrivenTimer_smoke.cpp:94` prints only on the success branch; the failure branch
(`:88`) shares no substring with it, and `FAIL_REGULAR_EXPRESSION "FAIL host-driven-timer:"`
matches only it. A crash before either line matches neither, and ctest fails on "Required regular
expression not found". Correct. The consumer's copy is not — see M1.

**No banned constructs.** No `NOLINT`, no diagnostic pragma, no C-style `for(;;)`, no raw owning
pointer, no `new`/`delete`, SPDX on line one of all nine new sources, coroutine parameters by
value in `interruptibleSleepUntil` (both overloads) and in every test coroutine, `[[nodiscard]]`
on the fallible/ignorable returns, Doxygen on every public entity, names conforming.

---

## Critical

### C1 — `addTimer` does not ask a host-driven loop for a turn, so a timer armed outside a turn never fires

`src/core/net/EventLoop.cpp:628-641`

`addTimer` files the park and returns. It calls neither `_backend.wake()` nor `armHostWake()`.
`armHostWake()` runs only at the end of a turn (`EventLoop.cpp:294`), and
`HostDrivenBackend::armWakeAt(std::nullopt)` is a deliberate no-op
(`HostDrivenBackend.cpp:71`) — so a quiescent host-driven loop has **no** pending
`emscripten_async_call` and nothing will ever call `armHostWake()` again.

**What breaks.** A host-driven loop (`PlatformLoop` under Emscripten; any `HostDrivenBackend`
loop) that is quiescent — nothing ready, nothing inbound, no armed deadline — has scheduled
nothing with the host. The program then calls `loop.addTimer(...)`, or constructs a
`DeadlineTimer`, from the loop's own thread but **outside a turn**: a DOM event handler, a
`requestAnimationFrame` callback, the TUI input path B12 will add. `teardownIsSerialisedWithDispatch()`
is `!running() || isOnWorkerThread()` (`EventLoop.hpp:451`), which is `true` there, so the assert
passes and the call is legal by the documented contract. The park is filed, the host is never
asked for a pump, no turn ever runs, `armHostWake()` never sees the deadline. **The timer never
fires and nothing reports it.**

`spawn` handles exactly this case and its comment names the reason
(`EventLoop.cpp:619-625`: "a host-driven backend has no wait at all to notice -- there, `wake()`
IS how the host is asked for the turn"). `post`, `submit`, `schedule`, `requestCancel` and `stop`
all wake. `addTimer` is the one member that files work reachable only by a turn and does not.

**Why neither wasm program catches it.** Both call `loop.spawn(...)` *before* constructing the
`DeadlineTimer` — `HostDrivenTimer_smoke.cpp:71` then `:75`, and
`tests/consumer-wasm/main.cpp:118` then `:119`. The spawn's `wake()` schedules a pump; that
pump's turn runs `armHostWake()`, which picks up the already-filed timer deadline. Delete the
`spawn` line from `HostDrivenTimer_smoke.cpp` — leaving the frameless timer alone, which is the
shape this task exists to add — and the program prints `FAIL host-driven-timer: after 2000 ms ...
the DeadlineTimer did NOT fire`. That is the reproduction.

`src/core/net/HostDrivenLoop_test.cpp` has no `addTimer` or `DeadlineTimer` case at all; its only
deadline arrives through `spawn(delayThenFlag(...))` (`:95`). So the callback-timer × host-driven
combination is untested at the unit level and masked at the integration level.

**Say this to B6 and B12 now.** The report's §11 hands B12 `addTimer` as "the frameless timer the
TUI's redraw pacer and debounce want" and B6 a receive deadline per read. Both arm from event
callbacks. Natively the gap is unreachable (the only legal off-turn caller is another thread,
which the assert catches, and a `runOnce`-driven consumer recomputes the timeout in step 3), so
this will present as a browser-only, B12-only silent hang.

The shape of the fix is `spawn`'s — `if (!isOnWorkerThread()) armHostWake();` at the end of
`addTimer` — but that is the implementer's call, not mine.

---

## Important

### I1 — the kind check that makes `TimerId` safe is not covered by any case

`src/core/net/EventLoop.cpp:652`, and the absence is in `src/core/net/Timers_test.cpp`

`cancelTimer` refuses a `TimerId` naming a coroutine park via `entry->onExpired == nullptr`,
checked before the park is taken. The report's §1 rests the entire "why `TimerId` is a distinct
struct" argument on it: *"`cancelTimer` on a coroutine park's id would, without the kind check I
added, unpark a flow and leave it waiting forever. ... the kind check makes the second impossible
even if somebody constructs a `TimerId` by hand."*

`TimerId` is an aggregate with a public `ParkId park` member (`ParkTable.hpp:108-120`), so
`TimerId { someParkId }` is one expression. Nothing in the tree writes it. Every `cancelTimer`
call in the suite passes an id that `addTimer` returned, so **deleting `|| entry->onExpired ==
nullptr` from line 652 reds nothing** — an eighth arm-removal mutation that would have come back
green. Given that the seven that were run are the strongest part of this work, this is the one
that got away.

**What breaks if the arm goes.** `_parks.take(id)` on a coroutine park drops the park and its
indices and destroys the `Park`, releasing the claim its `parked` holds — freeing the frame of a
suspended flow that still believes it is parked, or at best leaving it parked forever with nothing
able to resume it. The case is two lines: park a flow on `delay()`, take its `ParkId` (via
`registerPark`, which is public), wrap it, `CHECK_FALSE(loop.cancelTimer(TimerId { park }))`, then
advance the clock and assert the flow still resumes.

### I2 — `cancelTimer` carries no serialisation assertion, and it is the half reached from a destructor

`src/core/net/EventLoop.cpp:643`

`addTimer` asserts `teardownIsSerialisedWithDispatch()` (`:632`) with a message telling the caller
to `post()` instead. `cancelTimer`, whose Doxygen says "Idempotent, and loop thread only"
(`EventLoop.hpp:352`), asserts nothing — and it is the half reached from `~DeadlineTimer` →
`disarm()` (`DeadlineTimer.cpp:34`), i.e. from wherever the owning object happens to be
destroyed, which is not a place the author of the timer chose.

**What breaks.** A `DeadlineTimer` held as a member of an object destroyed on a worker thread
while the loop is turning: `_parks.find` and `_parks.take` mutate an `unordered_map`, a
`multimap` and a heap that the turn is reading. Unsynchronised, so heap corruption or a lost
park, non-deterministic, and the tree's own Debug guard that would have named it at the call site
is present on one half of the pair and absent on the other. The report's §9.5 claims the API is
"loop-thread only (asserted with `teardownIsSerialisedWithDispatch()`)"; that is true of arming
and not of retiring.

This is a missing guardrail rather than a live defect in core-cpp's own code — nothing here calls
`cancelTimer` off-thread — but B6's socket deadlines and B12's TUI are where a `DeadlineTimer`
starts being a member of objects with less predictable destruction sites.

---

## Minor

### M1 — `consumer-wasm`'s `FAIL_REGULAR_EXPRESSION` is inert

`tests/consumer-wasm/CMakeLists.txt:138`

`FAIL_REGULAR_EXPRESSION "check\(s\) failed"`. CMake un-escapes `\(` to a literal `(` in a quoted
argument, so the property value is `check(s) failed`; CTest then compiles that as a regex, where
`(` and `)` are grouping, giving a pattern that matches `checks failed`. The program prints
`2 check(s) failed` (`main.cpp:155`). Verified with the same engine CTest uses:

```
$ cmake -P /tmp/re.cmake
-- property value is: [check(s) failed]
-- A: literal-paren output NO MATCH -> FAIL regex is inert
-- B: checks-failed MATCHES
```

The test still fails correctly on a failing run, because `PASS_REGULAR_EXPRESSION
"consumer-wasm: every check passed"` is only printed when `status == 0` and CTest reports
"Required regular expression not found" otherwise. So this manufactures no green today — it
removes the backstop the comment above it says is there, and the report's "**Both directions are
proven** ... `consumer-wasm` likewise" was proven by the PASS regex alone. `\\(s\\)` or
`[(]s[)]` would fix it. The `tests/wasm` copy has no metacharacters and is fine.

### M2 — the new meaning of `resumed` reached one of the three places that state it

`src/core/net/EventLoop.hpp:218`, `src/core/net/testing/TestLoop.hpp:68-69`, `:73`

`RunOnceResult::resumed` (`EventLoop.hpp:145-150`) is documented correctly. The two members that
forward it are not: `runUntilIdle` still says "@return How many coroutines were resumed in all",
`TestLoop::tick()` says "How many coroutines it resumed. Zero means the loop had nothing to do",
and `TestLoop::drain()` repeats the first. `tick()` is the one a test author reads, and
`InterruptibleSleep_test.cpp:107` already relies on the new meaning. See the ruling on decision 2.

### M3 — `DeadlineTimer` documents a non-null callback and does not assert it

`src/core/net/DeadlineTimer.cpp:10-17`; the contract is at `DeadlineTimer.hpp:46`

`addTimer` asserts its own callback (`EventLoop.cpp:635`) and returns `TimerId::invalid()` in
release. `DeadlineTimer`'s constructor passes `&DeadlineTimer::fire`, which is never null, and
stores the user's `onExpired` unchecked. `DeadlineTimer { loop, deadline, nullptr, nullptr }`
therefore constructs fine and calls a null function pointer at `DeadlineTimer.cpp:53`, one turn
later, inside `runOnce` — with no diagnostic naming the construction site. One `assert` in the
constructor puts the failure where the mistake is.

### M4 — `pendingTimerSlotCount()` is new public API with no CHANGELOG entry

`src/core/net/EventLoop.hpp:368`

`EventLoop.hpp` is in the module's `FILE_SET HEADERS`, and `AGENT.md` says "Public API = the
module's `FILE_SET HEADERS`, and a change to it is a CHANGELOG entry". The five **Added** entries
cover `addTimer`/`cancelTimer`/`TimerId`, `DeadlineTimer`, `interruptibleSleepUntil`/`WakeReason`,
`sleepUntil`/`nextWakeStep` and the wasm tests; none mentions `pendingTimerSlotCount`, nor
`TimerCallback` or `ParkEntry::onCallback`, which are also public and also new.

### M5 — `.front()` on a container guarded only by a sibling count

`src/core/net/DeadlineTimer_test.cpp:200-201`

`REQUIRE(backend.waitCount() == 1);` then `backend.recordedTimeouts().front()`. The equivalent
case in `Timers_test.cpp` guards the container it actually indexes
(`REQUIRE(backend.recordedTimeouts().size() == 1)` at `:338`). If the two ever diverge, this one
is UB — a crash mid-binary rather than a red naming the case.

### M6 — a case name claims half a behaviour it does not exercise

`src/core/net/Timers_test.cpp:302`

`TEST_CASE("requestStop() cancels flows and leaves callback timers armed")` spawns no flow. Only
the second clause is asserted. The first clause is covered elsewhere
(`InterruptibleSleep_test.cpp:166`), so nothing is unproven — but a reader counting coverage from
case names gets the wrong number.

### M7 — an assertion that cannot come out the other way

`src/core/net/InterruptibleSleep_test.cpp:145`

`CHECK(loop.pendingTimerCount() == 0)` in *A null loop reports Deadline without parking*. The
sleep was handed `nullptr` and has no way to reach `loop`, so this is true for every possible
implementation. The real assertion is `outcome == Outcome::Deadline` on the line above, and it
does the work. (The dispatch's own rule: "A measurement that cannot come out the other way is not
a measurement.")

### M8 — a park is filed before the stop registrations that can throw

`src/core/net/InterruptibleSleep.cpp:64-68`

`registerPark` runs, then two `std::optional<async::StopCallback<std::function<void()>>>::emplace`
calls. A `std::bad_alloc` from either leaves the park filed for a frame that then unwinds through
the `co_await`, so `await_resume` never runs and `unregisterPark` never happens: a permanently
parked entry whose waiter is a destroyed frame, which teardown would then try to resume. This is
B4's `DelayAwaiter` shape (`EventLoop.hpp:741-742`) rather than something B5 introduced, but B5
widens the window from one emplace to two. Practically unreachable — the closure is `EventLoop*`
plus `ParkId`, 16 bytes, inside libstdc++'s and libc++'s `std::function` small-buffer — so this is
noted for the record, not asked for.

---

## Opinions (labelled as such — no failure scenario)

- **The cancellation window is tested through `cancelTimer` directly, not through the shape it
  exists for.** `Timers_test.cpp:243` calls `cancelTimer` by hand between `tick()` and `drain()`.
  The scenario the design argument names is two timers due in the same batch where the first
  callback destroys the second's `DeadlineTimer` — the `~DeadlineTimer` → `disarm()` →
  `cancelTimer` path reaching into the window from inside the drain. I traced it and it works.
  A case would make it stay working.
- `std::ignore = timer;` at `HostDrivenTimer_smoke.cpp:77` and `consumer-wasm/main.cpp:121` where
  `[[maybe_unused]]` is the tree's idiom for an object held only for its lifetime.

---

## Rulings on the two escalated decisions

### 1. The deprecated `wakeBound` overload carries no `[[deprecated]]` — **UPHELD, on different grounds. The test case stays.**

The decision is right. Its stated reason is not, and the header comment
(`InterruptibleSleep.hpp:66-70`) and the CHANGELOG entry should be corrected rather than left
asserting something false.

The stated reason is that the attribute is *impossible* here: warnings fatal, pragma forbidden,
and the GCC/Clang "deprecated inside deprecated" suppression not working on MSVC. The last part is
correct — MSVC's C4996 fires regardless of whether the caller is itself deprecated. But the
conclusion does not follow, because a third option exists that breaks no rule in
`.agent/rules/cpp-guidelines.md`: a **PRIVATE per-source compile option** on the one test
translation unit (`set_source_files_properties(InterruptibleSleep_test.cpp PROPERTIES
COMPILE_OPTIONS ...)` with `/wd4996` or `-Wno-deprecated-declarations`). The rulebook forbids the
diagnostic *pragma* in source and forbids PUBLIC/INTERFACE flags; a private per-file option on a
test binary is neither. So the choice was between three options, not two.

On the merits, it still comes out the same way. The CHANGELOG says the overload is "kept for one
release so a fastcached caller compiles unchanged". `[[deprecated]]` inside a consumer's `-Werror`
build means it does **not** compile unchanged — the attribute defeats the one thing the shim
exists to do. And the channel that actually reports a rename to a consumer in this project is
`tools/migrate/renames.json` and the codemods, not the compiler; the row is already there and the
migration is mechanical (drop an argument).

So: no attribute, and the case *The `wakeBound` overload forwards, and the bound changes nothing*
(`InterruptibleSleep_test.cpp:209`) stays. Change the header and CHANGELOG wording from "the
attribute would make the only call site that can test the overload a build failure" to the real
reason — a `[[deprecated]]` under a consumer's `-Werror` is the opposite of "compiles unchanged",
and `renames.json` is what reports this migration.

### 2. `RunOnceResult::resumed` counting timer callbacks — **the counting change is correct and required. The documentation change is NOT sufficient: rename the field.**

**Keep the merged count.** I verified the derivation the report gives: `result.idle = !hadInbound
&& result.resumed == 0 && result.dispatched == 0 && fired == 0` (`EventLoop.cpp:290`), and
`runUntilIdle` stops on `idle` (`:194`). Without the merge, a turn that ran only a timer callback
would report idle. Concretely, `Timers_test.cpp:267` (*A timer callback may arm another timer*)
would break: `drain()` would stop on the turn that ran the first callback, before the re-armed
second one. Splitting the field into two counters would be worse — `idle` would then have to sum
them, which is the same number with an extra chance to forget one.

**Rename it.** Three reasons, and the third is the deciding one:

1. `resumed` names a verb that is false for half the values it now holds. A timer callback is
   called, not resumed. The tree's naming rules are about exactly this kind of precision.
2. A rename would have caught M2 and the doc edit did not. The meaning is stated in three places;
   one was updated. `EventLoop.hpp:218` and `TestLoop.hpp:68,73` still say "coroutines", and
   `tick()`'s doc is the one a test author reads. A compiler-enforced rename cannot leave two of
   three behind.
3. **The window is open and closes at the next release.** `RunOnceResult` landed in Task B4 and
   sits under `[Unreleased]` in `CHANGELOG.md` — no release has shipped it. Today the rename is a
   word in a CHANGELOG entry plus five in-tree references (`EventLoop.cpp:223,193,290`,
   `TestLoop.hpp:70`, the `EventLoop_test.cpp`/`ClockRefresh_test.cpp` assertions). After the
   release it is a 0.x break that has to be recorded under Breaking and grepped across six
   consumers.

Suggested name: `drained` — "what step 2 took off the ready queue", which is the phrasing the
header already uses, and which sits correctly beside `dispatched` (step 4's count). `ran` also
works. Whatever the name, update all three doc sites with it.

---

## For Task B6 and Task B12, immediately

- **C1 changes nothing about the shape B6 builds on** — a socket receive deadline is armed from
  inside a turn (from a resumed flow), so the gap does not reach it natively. **It does change
  what B12 can rely on**: a TUI redraw pacer or debounce arming from an input callback on a
  host-driven loop is precisely the reachable case. If C1 is not fixed first, B12 must route every
  `addTimer` through `post()`, which is a heavier contract than the report's §11 hands it.
- Everything else in this review is additive. `addTimer`/`cancelTimer`/`TimerId`, the park-table
  merge, `DeadlineTimer`'s settle-before-call and the `pendingTimerSlotCount` diagnostic are the
  right primitives and B6/B12 can build on them as they stand.

## On the report

§5's measurement is sound and the conclusion is genuinely sharper than the concern it answers:
the bound is what was armed and cancelled behind the live root, not deadlines ever armed, and the
three-row table has a stated "what input would have made it different" for each. §7's finding —
that the wasm gate was a decoration, found by checking the artifact rather than the exit code — is
the most valuable thing in the commit, and the two rejected fixes are measured rather than
reasoned. §6's libunicode diagnosis (same sizes, different checksums) is correct and is worth the
rule it asks for.

The one methodological gap is I1: seven mutations were run and predicted in writing, and the
eighth — the arm the central design argument rests on — was not among them.
