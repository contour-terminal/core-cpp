# Task B5 — fix round 1

Against `.superpowers/sdd/2026-09-18-core-cpp/task-B5-review.md` (1 Critical, 2 Important, 8 Minor,
2 labelled opinions, 2 escalated rulings), on top of `fe48143`.

**Everything in the review is addressed.** The Critical has a case that fails without the fix, both
Important items are fixed, seven of the eight Minors are fixed and the eighth is answered, both
opinions are taken, and both rulings are applied — including the one that says my *reasoning* was
wrong while my conclusion was right.

**CI on `fe48143` completed green** (Build `35566311842` and Docs `35566311863`, no failed jobs)
while the review was being written. The review says it was not consulted in either direction.

---

## C1 — `addTimer` never asked a host-driven loop for a turn

**The review is right and the defect is mine.** `addTimer` filed the park and returned, calling
neither `wake()` nor `armHostWake()`. `armHostWake()` runs only at the end of a turn, and a
quiescent host-driven loop has nothing scheduled with the host — so nothing would ever start the
turn that looks at the deadline just filed. A timer armed from the loop's own thread but outside a
turn, which is legal by the documented contract, **was filed, correct, and silently never fired.**

The fix is `spawn`'s shape, with one deliberate difference:

```cpp
if (!isOnWorkerThread())
    armHostWake();
```

`armHostWake()` rather than `wake()`, which is what `spawn` uses: a spawned flow means "as soon as
you can" and has no deadline, while a timer has one, so the host is told *when* instead of being
asked for a pump it would spend finding nothing due. Inside a turn it is skipped, because the turn
arms the host on its way out. A backend that is not host-driven ignores it, and correctly — nothing
is driving such a loop off-turn (the assertion above it says so) and the next turn computes its
timeout from the same heap in step 3.

**The case**, in `HostDrivenLoop_test.cpp` over `ManualHostScheduler`, which is where the
host-driven contract is held on every platform rather than only under node:

*A timer armed on a quiescent host-driven loop asks the host for the turn that runs it.* It asserts
`host.pendingCount() == 0` **first** — because a pump pending from anything else is exactly what
masked this — then arms, then asserts the host was asked and at the right delay (50 ms, not "as
soon as you can"), then pumps twice and sees the callback run.

**Mutation M8**, predicted in writing before the run: remove the two lines, expect one case to fail
on `pendingCount()`. Exactly one did:

```
/wt-b5/src/core/net/HostDrivenLoop_test.cpp:155: FAILED:
  REQUIRE( host.pendingCount() == 1 )
with expansion:
  0 == 1
```

**Making the two WebAssembly programs discriminate took two attempts, and the second one is the
lesson.** The controller asked for the timer to be armed with no `spawn` before it. Reordering is
not enough and I said so: whichever is armed first, the spawn's `wake()` still buys a turn whose
`armHostWake()` arms the host for **every** deadline in the heap. So both programs now have a
**phase 1 with nothing else on the loop at all** — arm the frameless timer, wait, judge — before
phase 2 spawns the coroutine.

That still was not enough, and only running the mutation showed it. With C1 reverted the smoke
printed:

```
ok host-driven-timer: a coroutine delay and a DeadlineTimer both fired within 2030 ms
```

Phase 1 timed out at 2000 ms, then phase 2's `spawn` woke the loop and the still-filed timer fired
**late** — and my verdict read `timerFired` at the end of the program, by which time it was true.
The fix is one line, `auto const timerFiredAlone = timerFired;` taken at the end of phase 1, and
the verdict reads that. **The structure was right and the observation was still taken at the wrong
moment**, which is the same error as the one below at a smaller scale. Both directions are now
measured rather than argued:

| | smoke | `consumer-wasm` |
|---|---|---|
| C1 fixed | `ok … both fired within 60 ms` | `ok a DeadlineTimer alone on a host-driven loop fired…` |
| C1 reverted | `FAIL … the DeadlineTimer did NOT fire after 2000 ms (armed alone, with nothing else on the loop) and the coroutine delay fired after 30 ms` | `FAIL a DeadlineTimer alone…`, `1 check(s) failed`, ctest red |

The failure message names which of the two mechanisms broke, which the single combined wait could
not: with one shared bound, a phase-1 timeout starved phase 2 and the message blamed both.

**What I should have caught and did not.** The review's masking explanation is the part that stings:
both WebAssembly programs call `loop.spawn(...)` before constructing the `DeadlineTimer`, and
*spawn's* `wake()` buys the turn that picks the timer's deadline up. **My two integration tests were
green because of an unrelated call's side effect**, and I wrote both of them. I had also checked
that the smoke could fail — but I checked it by moving the *deadline* past the bound, which tests
the bound, not the arming. The mutation I did not think to run is the one that deletes the `spawn`.

## I1 — the kind check had no case, and it is the arm the design argument rests on

`cancelTimer` refuses a `TimerId` naming a coroutine park via `entry->onExpired == nullptr`, checked
before the park is taken. My §1 rested the whole "why `TimerId` is a distinct struct" argument on
it — and the review is right that **deleting it would have reddened nothing**: every `cancelTimer`
call in the suite passed an id that `addTimer` had returned. Seven mutations were run and predicted;
this was the eighth and it got away.

New case, *cancelTimer refuses a TimerId that names a coroutine park*: park a flow's handle by hand
through the public `registerPark`, wrap the `ParkId` in a `TimerId { park }` (one expression, since
`TimerId` is an aggregate over a public member), and assert the refusal, that the park was **not**
taken, and that the flow still resumes at its deadline.

**Mutation M9**, predicted as one case: it failed on three assertions, which is the whole
consequence chain rather than just the return value —

```
CHECK_FALSE( loop.cancelTimer(TimerId { park } ) )   !true
CHECK( loop.pendingTimerCount() == 1 )               0 == 1
CHECK( ran )                                         false
```

— the park taken, the count dropped, and the flow never resumed.

## I2 — `cancelTimer` now carries the serialisation assertion

`addTimer` asserted `teardownIsSerialisedWithDispatch()`; `cancelTimer` asserted nothing, and it is
the half reached from `~DeadlineTimer` → `disarm()` — from wherever the owning object happens to be
destroyed, which is not a place the timer's author chose. Added, with a message that says what to do
instead. My §9.5 claimed the API was "loop-thread only (asserted)"; that was true of arming and not
of retiring, and the claim is now true of both.

## The Minors

| | Fix |
|---|---|
| **M1** `consumer-wasm`'s `FAIL_REGULAR_EXPRESSION` inert | `check[(]s[)] failed`. **Proved with the same instrument the review used** rather than reasoned: a `cmake -P` probe shows the old value matching `checks failed` (never printed) and *not* `2 check(s) failed` (what the program prints), and the new one doing the reverse. |
| **M2** the new meaning reached one of three doc sites | Subsumed by the rename below — which is the review's own argument for renaming, and it proved itself: the compiler named every stale site the moment the field changed, including two my sync script had not been copying. |
| **M3** `DeadlineTimer` documented a non-null callback and did not check it | `assert` in the constructor, where the mistake is, rather than a null call one turn later from inside the drain. |
| **M4** `pendingTimerSlotCount()` public with no CHANGELOG entry | Added, with `TimerCallback` and `ParkEntry::onCallback`, which were also public and also missing. |
| **M5** `.front()` guarded by a sibling count | `REQUIRE(recordedTimeouts().size() == 1)` beside the `waitCount()` check, matching the equivalent case in `Timers_test.cpp`. |
| **M6** a case name claiming half a behaviour | The case now spawns a flow and asserts it unwound, so both clauses of *requestStop() cancels flows and leaves callback timers armed* are exercised. |
| **M7** an assertion that cannot come out the other way | Removed, with a comment saying why it is not coming back. It was `pendingTimerCount() == 0` in the null-loop case, which no implementation could fail. **The dispatch's own rule, applied to me.** |
| **M8** a park filed before two `emplace`s that can throw | **Not fixed in this round, and agreed as stated at the time.** It is `DelayAwaiter`'s shape from B4 rather than something B5 introduced, the closure is 16 bytes inside every `std::function` small-buffer, and the review notes it for the record rather than asking. Fixing it properly means a scope guard around the park in both awaiters, which is B4's file and a change I would rather see argued than slipped into a fix round. **Subsequently closed** — see the note below. |

### M8, closed after the fact

Routed to B4 rather than fixed across lanes. B4 guarded `DelayAwaiter` in **`27b8b43`**, and the
matching guard for `TokenDelayAwaiter` landed as **`8d7b8b2`** on top of it — same `detail::ScopeGuard`
with a flag, the flag after the **second** `emplace` because this awaiter registers two stop
callbacks, which is the only difference between the two. B4 also put the `noexcept` requirement in
`ScopeGuard.hpp`'s own doc comment, so the constraint is now stated at the type rather than only at
its two call sites.

**It closes an unreachable defect and does not fix a live one**, and the commit message says so
rather than implying a bug was found: sixteen bytes of closure sit inside the small-buffer
optimisation of both libstdc++'s and libc++'s `std::function`, so no allocation occurs on either
toolchain this project builds. No test — provoking the throw would exercise `std::function`'s
allocator, not this code.

**Re-gated at the moved base, not carried over from `a02031c`.** The base change touched
`ScopeGuard.hpp`, the header this change includes, so the earlier green was not evidence about it.

## The two opinions, both taken

- **The cancellation window is now tested through the shape it exists for.** New case, *A timer
  callback may retire another timer already queued by the same turn*: two deadlines due together,
  step 5 queues both, and the first callback destroys the second's `DeadlineTimer` — so
  `~DeadlineTimer` reaches `cancelTimer` from inside the very drain about to reach the entry it
  retires. The old case called `cancelTimer` by hand between two ticks, which exercises the branch
  but not the path.
- `[[maybe_unused]]` in place of `std::ignore = timer` in both WebAssembly programs.

## The two rulings

**1. `[[deprecated]]` — upheld, and my reasoning was wrong.** I argued the attribute was
*impossible* here. The review found a third option I did not consider: a PRIVATE per-source
`COMPILE_OPTIONS` on the one test translation unit is neither a diagnostic pragma nor a PUBLIC flag,
so it breaks no rule in this tree. The conclusion survives on better grounds — the overload exists
so a fastcached caller *compiles unchanged*, and `[[deprecated]]` under that consumer's `-Werror` is
exactly what would stop it; `renames.json` and the codemods are what report a migration here.
**Corrected in `InterruptibleSleep.hpp`, in `CHANGELOG.md` and in §9.1 of the report.** The test case
stays.

**2. `RunOnceResult::resumed` → `drained`.** The merged count is kept — `idle` derives from it, and
without the merge *A timer callback may arm another timer* would have `drain()` stop early. The
field is renamed, because `resumed` names a verb that is false for half of what it holds, and
because the window closes at the release: `RunOnceResult` is still under `[Unreleased]`, so today it
is a CHANGELOG word and a handful of references rather than a 0.x break grepped across six
consumers. All three doc sites are updated — `RunOnceResult::drained`, `runUntilIdle`, and
`TestLoop::tick()`/`drain()`, the one a test author actually reads.

## Proving the clang-tidy instrument, not just reading its output

I reported "0 findings" off a log at **179 of 526 files** and caught it only because I looked at the
artifact instead of the count. B4 hit the same gate two other ways in the same hour — never starting
it, and running it with the analyser absent. **A null result is indistinguishable from a non-result
unless the instrument proves itself:** "0 findings" is the same string whether the analyser ran
clean, ran on nothing, or never ran.

Four facts recorded beside the result, rather than one:

| | |
|---|---|
| Binary CMake configured | `CORE_CPP_CLANG_TIDY_EXE:FILEPATH=/home/christianparpart/.local/bin/clang-tidy` |
| Its `--version` | `LLVM version 22.1.8` |
| The pin in `.clang-tidy-version` | `version: 22.1.8` |
| The build actually invokes it | `ninja -t commands <object>` shows `--tidy="/home/christianparpart/.local/bin/clang-tidy;--extra-arg-before=--driver-mode=g++"` |

The fourth is the one that matters and is the one a `command -v` cannot give you: it is the build's
own command line, so it says which binary *this build* runs rather than which one *my shell* can
find. That distinction is live here — a login shell finds the pinned binary on this machine and a
non-login shell does not.

**And a fifth way the gate could go quiet, which is specific to this tree:** the compile launcher is
`fastcache-cc`. A cache hit that skipped the analyser would produce a clean run over an unanalysed
tree. CMake's `__run_co_compile` runs tidy as its own process rather than as part of the compile, so
a cache hit does not skip it — but that is reasoning, and the point of this section is not to
reason, so it is confirmed by feeding the analyser a deliberate violation and watching it report.

## Gates, all re-run against the fix round

Every row measured on **`a02031c`** — the fix round rebased onto B4's `fb3fe97` — checked out in a
throwaway worktree, not on the working copy the fixes were written in.

| | Result |
|---|---|
| `clang-debug` | **31/31** |
| `gcc-release` | **31/31** |
| `clang-asan-ubsan` | **31/31** |
| `clang-tsan` | **31/31** |
| `clang-tidy` | **509 statements, 0 findings**, from a DELETED tree — see below |
| `emscripten` | **13/13** |
| `consumer-wasm` | **1/1**, and red with the fix reverted |
| `cl-debug` | **33/33** |
| `clangcl-release` | **33/33** |
| `ctest -L hygiene` | **16/16** |
| `mkdocs build --strict` | **green** |
| pinned `clang-format` 22.1.8 | clean |

**The tidy row is 509 and not 25, and that difference is the point.** The first post-rebase run
reported `steps=25 findings=0`: the tree was already built, so Ninja re-ran only what the rebase
had changed and inherited the rest. Under the finding above that is a stale verdict, and reporting
it as "clang-tidy clean" would have been the same error as reading a count off an unfinished log —
an hour after I wrote that error down. The tree was deleted and rebuilt, which is what forces every
statement to run.

**ASan and TSan first came back 30/31, and the failure was mine rather than the code's.**
`core-cpp.cmake-hygiene` reported `src/core/Base64.cpp: no row in provenance.md` — a zero-byte file
my own instrument probe had created, because one probe command ended `touch <path>` on a path that
does not exist (Base64 is header-only) and **`touch` creates**. The provenance check did exactly
what it is for. Removed, both presets 31/31 on re-run, and the shared checkout was never touched.
`touch -c` refuses to create, and is what a timestamp probe should use.

`core-cpp-net_backend-test`: **2254 assertions in 60 cases**, up from 2236 in 57 — the three new
cases are C1's, I1's and the cancellation window's real shape.

## Every load-bearing claim in the report, with its case or an explicit "no case"

Asked for because prose does not fail, and because both of this round's misses were claims that
were load-bearing in a report before they were load-bearing in a test. **A list where three rows
say "no case" is a better artifact than a list where the reader cannot tell which rows those are.**

**And one row moved out of "no case" while the list was being written, which is the point of making
it.** *The two id types cannot be handed to each other's cancellation* had no case and looked as
though it could not have one. It can: **a runtime case cannot express it — the program that would
prove it by failing is the one that does not compile — and the compiler can.** Three
`static_assert`s now hold it. That is the general answer to "this claim is untestable", which is
usually "untestable *at runtime*"; the remaining three rows below are the ones where it genuinely
does not apply, and each says why.

| Claim | Case | Mutation |
|---|---|---|
| One answer to "when is the next deadline" — an armed timer bounds the wait | *An armed timer is what bounds the turn's wait*; *An armed DeadlineTimer bounds the turn's wait to its own deadline* | — |
| One firing order across callbacks and coroutine deadlines | *A callback timer and a coroutine deadline share one firing order* | M1, M4 |
| FIFO by arming sequence among equal deadlines | *Timers due at the same instant fire in the order they were armed* | M4 |
| A spent id resolves to nothing; ids are never reused | *cancelTimer after the callback has run reports false* | — |
| `cancelTimer` reaches the queued-but-not-run window | *cancelTimer still prevents a callback whose deadline has fired but not yet run*; *A timer callback may retire another timer already queued by the same turn* | M1 |
| A `TimerId` naming a coroutine park is refused | *cancelTimer refuses a TimerId that names a coroutine park* | **M9** (new this round) |
| The two id types cannot be handed to each other's cancellation | three `static_assert`s in `Timers_test.cpp` | compile-time |
| Teardown drops a queued callback rather than running it | *A loop destroyed with a timer armed never runs it* | M3 |
| A timer is settled before its callback runs, so it may destroy itself | *A DeadlineTimer may be destroyed from inside its own callback* (asserts `settled()` from inside) | M2 |
| Nothing polls | *An armed DeadlineTimer bounds the turn's wait to its own deadline* (asserts the 500 ms value) | — |
| `interruptibleSleepUntil` parks once rather than per step | *The wakeBound overload forwards* (asserts `pendingTimerCount() == 1`) | — |
| A cancel wakes it promptly and leaves nothing parked | *A stopped token wakes an interruptible sleep with the clock frozen* (asserts `tick() == 1`) | M6 |
| The supplied token is reported; the flow's own unwinds | *A cancel of the awaiting flow's own token unwinds*; *The supplied token is reported even when it is also the flow's own* | M7 |
| The free `sleepUntil` never suspends on a null or elapsed deadline | *sleepUntil with no loop never suspends*; *…with a deadline already gone…* (both assert `await_ready()`) | M5 |
| `nextWakeStep` is usable in a constant expression | two namespace-scope `static_assert`s | compile-time |
| Arming outside a turn asks a host-driven backend for one | *A timer armed on a quiescent host-driven loop asks the host…*; both WebAssembly programs | **M8** (new this round) |
| A WebAssembly program's exit status does not survive the host | both directions run by hand, and the reasoning recorded beside the ctest property | — |
| **Lazy pruning is bounded by what was armed behind the live root** | *Lazy timer pruning is bounded by the deadlines armed behind the live root* — three exact equalities with a stated counterfactual each | **no mutation run.** The counterfactuals (1 for eager, 1000/1001 for none) are arithmetic I reasoned rather than executed. Mutating `pruneTimers` is a change to B4's file, which is why I did not; it is the one measurement here resting on argument for its alternatives. |
| **`addTimer` and `cancelTimer` are loop-thread only, asserted** | **no case.** An `assert` cannot be observed from inside a Catch case — it aborts the binary. The tree's shape for this is a `WILL_FAIL` canary process (`HostDrivenCanary.cpp`), which is how B4 covers `run()`/`blockOn()`. Adding a third canary mode for two more assertions was more than this round warranted; naming it here is the alternative to implying it is covered. |
| **`DeadlineTimer` allocates nothing and holds no coroutine frame** | **no case.** Structural, and visible in the header: four members, none owning. I know of no assertion that would fail if it stopped being true without also being fragile (`sizeof`), so it is stated rather than checked. |

## The methodological point, which is the review's and I accept it

> *Seven mutations were run and predicted in writing, and the eighth — the arm the central design
> argument rests on — was not among them.*

Both gaps have the same signature: **I mutated the things I had written a case for, rather than
deriving the mutations from the argument.** The kind check and the host wake were each load-bearing
in prose before they were load-bearing in a test, and prose does not fail. The rule I would carry
forward is to enumerate mutations from the *claims*, not from the cases — every "without X this
would break" in a report is a mutation owed, and if running it comes back green that is the finding.
