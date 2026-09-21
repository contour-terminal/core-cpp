# Task B1: re-review of fix round 1

Scope note on the commit range: the dispatch named `ac0ff76..ceb471d`. That range, taken literally,
**excludes `ac0ff76` itself** — which is the commit that actually fixes C1 (`fix(async): the parks
of one fan-out free their shared chain root once`). The five commits in the range
(`0a77bfa`, `4d20406`, `c99ca38`, `455b21b`, `ceb471d`) cover I2/I3/I4/minors/self-corrections but
not C1's own diff. I reviewed all of `ac0ff76` through `ceb471d`, and identified `4bde234` and
`455b21b` as another-lane commits (`fix(migrate)`, `test(hygiene)`) interleaved in the shared
history — not B1's, consistent with the dispatch's own note about a shared checkout. B1's actual
fix-round-1 commits are `ac0ff76`, `0a77bfa`, `4d20406`, `c99ca38`, `ceb471d` (five, matching the
dispatch's count, just not the literal range boundary).

Verification ran in a private detached worktree, `git worktree add --detach D:/core-cpp-wt-rereviewB1 ceb471d`,
removed at the end (`git worktree remove D:/core-cpp-wt-rereviewB1`). Built with WSL Ubuntu-26.04,
clang 22, through fastcache-cc. I did not build the Windows presets (`cl-debug`, `clangcl-release`)
or Emscripten; CI's green run (`35548899124`, head `455b21b`, cited in the report) is what covers
those, and I have no reason to doubt it for the parts unrelated to the new finding below.

## Verdicts on the fix-round-1 findings

**C1 (refcounted abandon root) — ADDRESSED**, and it is a real, careful fix. I read
`ParkedWork.hpp`'s new `detail::AbandonState`/`detail::AbandonClaim`, `DetachedTask.hpp`'s
`abandonState`/`abandonOnce`, and `ThreadPoolExecutor.hpp`'s `std::deque<detail::Parked>` queue in
full, traced the refcount/disarm/rearm lifecycle by hand, and could not find a double-free or leak
in it. The invariant is stated on `ParkedWork::abandon`'s doc comment as required. I built the
promoted arity-2 tests (`"Two parks of one chain free it once between them"`,
`"Two parks of one raced chain free it once between them"`, plus the two follow-on primitive
cases) under ASan+UBSan (`clang-asan-ubsan`) and ran them individually and as part of the full
suite: clean, no ASan report, `343 assertions in 83 test cases` on both `core-cpp-async-test` and
`core-cpp-async-fallback-test`, matching the report's claimed count.

I tried to break it as instructed, at higher arity and with real OS threads rather than the
single-threaded `QueuedExecutor` (see the new Critical finding below, which the stress run
surfaced downstream of C1's own machinery but is not a defect in `AbandonClaim`/`AbandonState`
itself — see "What C1 is and is not responsible for"). I did not find a defect in the refcount
mechanism in isolation: I could not construct a case, at any arity, using only `ParkedWork_test.cpp`'s
`QueuedExecutor`, that free the same chain root twice or leaked it. The claim/rearm asymmetry
(disarm is chain-wide, rearm happens once per `claimOn`) held under manual tracing of every
resume/decline path I could enumerate.

**The "second half" claim (`ThreadPoolExecutor::submit(ParkedWork)` used to drop the claim) —
VERIFIED, by reading `ac0ff76`'s diff directly**: before the fix, `submit(ParkedWork work) override { submit(work.resume); }`
discarded `work.abandon`; after, the queue holds `detail::Parked` and `worker()` calls
`entry.resume()`, which owns the claim until the coroutine is resumed or declined. This is a real,
correctly-described defect the implementer found beyond the review's own text.

**I searched the rest of `src/core/async` for the same shape** (a `ParkedWork` stored or forwarded
as a bare `std::coroutine_handle<>`): `grep -rn "ParkedWork" src/core/async` and read every
non-test hit. `AsyncQueue.hpp` stores a `ParkedWork _waiter` member (not a bare handle) and moves
it whole on `pop()`/`push()`. `ResumeOn.hpp` constructs one and hands it straight to
`IExecutor::submit(ParkedWork)`. `ParkedWork.hpp`'s own `detail::Parked` is the type C1 introduced.
I found no other executor or queue in this module that narrows a `ParkedWork` to `.resume` before
storing it. `clang-tidy`'s new findings in the report (`performance-unnecessary-value-param` on the
`#1041` negative control, `modernize-pass-by-value` on `Parked`'s constructor) are consistent with
this — the type stopped being trivially copyable, and the tidy warnings are the kind of thing that
would fire on a *new* executor written against this interface with the old copy-by-value habit. I
did not build a fresh executor to test this affirmatively; noting it as residual risk for B3/B4/B5
as the report itself does.

**I2 (atomic join counters) — ADDRESSED for the `remaining`/`latched` counters as reviewed, but
see the new Critical finding: the atomics are correct, and a data race they previously masked is
gone, but the fix does not close every race a cross-thread join can hit.** The ordering is stated
above `JoinState` (`acq_rel` on every decrement, the "cannot be read early" argument for
`continuation` resting on the `+1` start-phase guard). I checked this argument by hand: every
decrement (both children's and the start-phase release) uses `acq_rel`, so whichever one returns
old-value 1 has, via the atomic's total modification order and the release-sequence rule,
synchronized-with every earlier decrement's release — including the guard's own release of
`continuation`'s write. That argument is sound as far as it goes for the counter itself. `latched`'s
`claimLatch()` (an `exchange`) correctly serializes the two combinators' one-time work
(`WhenAllPolicy`'s first-escape latch, `WhenAny`'s winner) — I confirmed this is a *new* fix beyond
what R98 named: the pre-round code was `if (!state.exception) state.exception = outcome.escaped;`
and `if (state.winner.has_value() || outcome.cancelled) return; state.winner = ...`, both
check-then-act races on two threads. `claimLatch()` closes both. Good catch, correctly described.

I built and ran `ThreadPoolExecutor_test.cpp`'s new case (*"A join whose children finish on a pool
completes exactly once"*) under `clang-tsan`: it passes on a clean run, matching the report. Run
once, it is silent. Run repeatedly under load, it is not — see below.

**I3 (`#1041` comment) — ADDRESSED.** I read `c99ca38`'s diff to `.agent/rules/async-and-net.md`,
`IExecutor.hpp`, `docs/modules/async.md`, and `0a77bfa`'s replacement comment in
`ThreadPoolExecutor_test.cpp:144`. The new sentence is accurate: both `IExecutor::submit` overloads
are declared pure (`IExecutor.hpp:43,56`, confirmed by reading the header), so a derived class
hiding one stays abstract; the `static_assert` is a reachability check over the real pool, not the
guard itself. The `CORE_CPP_GCC_OR_CLANG` correction (`cmake/CoreCppToolchain.cmake:84`) is
accurate — I read that line and it does gate `-Woverloaded-virtual` for both compilers, not GCC
alone. The negative control (`BothSubmitOverloads`, a plain non-inheriting type with only
`submit(handle)`) is still present in `ParkedWork_test.cpp`, now with a body that sinks its
`ParkedWork` parameter per the new clang-tidy finding — the control's *purpose* (distinguishing
"hiding is possible" from "hiding is refused") is unchanged.

**I4 (`[[nodiscard]]` on `AsyncQueue::push()`) — ADDRESSED.** Read `AsyncQueue.hpp:157`: the
attribute is present, the CHANGELOG entry is amended (not a new Breaking row, correctly, since the
API is unreleased), and the `displaced` documentation fix (see Minor 2) landed in the same commit.

## The six Minors

1. **Empty-task throw must not be caught — ARGUED, and the argument is accurate.** I read
   `Task.hpp:118-125` directly: "Do not catch it... Nothing in `core::async` is declared to throw
   it, and no caller should handle it" is present, word for word as claimed, and predates this fix
   round (`d7a7bee`, before `task-B1-fixround1.md` was even written against `b528631`-era code, per
   the report's own timeline claim, which I did not independently re-verify commit-by-commit but
   which is consistent with `d7a7bee` sitting before `ac0ff76` in `git log`).
2. **`displaced=1` under `DropNewest` — ADDRESSED.** `AsyncQueue.hpp:66-72`'s doc for
   `AsyncQueuePush::displaced` now states both policies' meaning; I read it, it says what the report
   claims.
3. **Brace list omitting `ThreadPoolExecutor_test.cpp` — ADDRESSED.** `.agent/rules/async-and-net.md`'s
   file list now includes it, with the `CORE_CPP_USE_THREADS`/wasm caveat.
4. **`AGENT.md` reading as future work — ADDRESSED.** Read the diff in `c99ca38`; the module table
   text now says Task B1 already merged the async design in, and names what.
5. **Two overstated sentences in `docs/modules/async.md` — ADDRESSED.** Both call-outs (the
   "unchanged" depth claim, and the missing pre-completion-teardown caveat) are corrected as
   described; I read the before/after text.
6. **`whenAll`'s awaiter losing movability — ADDRESSED**, and see the self-correction below.

Six for six: five straightforwardly addressed, one (Minor 1) correctly argued as already delivered
rather than silently dropped.

## The two self-corrections

**"`whenAny`'s awaiter is immovable by the standard" — VERIFIED by compiling, not by reading.**
`WhenAny_test.cpp`'s `static_assert(!std::is_move_constructible_v<decltype(whenAny(...))>)` already
encodes this claim; I additionally confirmed by reasoning through the type (a live, registered
`StopCallback` inside the awaiter, and a registered stop callback holds its own address, so the
standard forbids moving it) rather than re-deriving it from scratch — this is a correct application
of a well-known constraint (`std::stop_callback` and this module's fallback both pin the callback
object), not something I found reason to doubt.

**"The restored-move justification's example was one copy elision keeps legal" — VERIFIED by
compiling both forms, in my own scratch file, against a deliberately move-deleted `JoinAwaiter`.**
I temporarily edited `Join.hpp` in the private worktree (reverted before finishing) to change
`JoinAwaiter(JoinAwaiter&&) noexcept = default;` / the move-assignment to `= delete;`, and compiled
two forms with `clang++-22 -std=c++23 -fsyntax-only`:

- `auto formA() { return whenAll(std::vector<Task<void>>{}); }` — **compiles** with the move
  deleted (guaranteed copy elision constructs the return value in place; RC=0).
- `auto formB() { auto a = whenAll(std::vector<Task<void>>{}); return a; }` — **fails** with the
  move deleted: `error: call to implicitly-deleted copy constructor of 'detail::WhenAllAwaiter'`.

This exactly matches `ceb471d`'s commit message and the corrected comment in `Join.hpp`. The
original (fix-round, pre-`ceb471d`) comment's claim — that deleting the move broke
`auto h() { return whenAll(...); }` — is confirmed false, and the replacement text is confirmed
true. Both self-corrections verified.

## New finding: C2 (Critical) — a completed `whenAll`/`whenAny` join over a `ThreadPoolExecutor` can free a sibling child's frame while another worker thread is still resuming it

**Not introduced by this fix round's refcounting** (`AbandonClaim`/`AbandonState` is not implicated
— the crash is in `Join.hpp`/`JoinRunner`'s teardown, which predates B1's R97 fix and is exactly
what I2's own new test case exercises). I is a genuine, reproducible race, found by doing what the
dispatch asked — stressing the fan-out/pool interaction at higher concurrency than a single test
run — and it is real on completely unmodified `ceb471d` code, not an artifact of instrumentation
(I added and then removed diagnostic prints partway through; the *first* reproduction, and the
*confirming* reproduction afterward, are both against the untouched checked-out source).

**Reproduction**: build `clang-asan-ubsan` (or `clang-tsan`) at `ceb471d`, then run
`ThreadPoolExecutor_test.cpp`'s new case in a loop:

```
for i in $(seq 1 60); do
  ./core-cpp-async-test "A join whose children finish on a pool completes exactly once"
done
```

**Observed rate**: 1 failure in 60 (ASan, clean rebuild after reverting my diagnostics), 1 in 30
(an earlier ASan sample), 1 in 40 (TSan, clean). All three are independent runs against
unmodified `ceb471d` object code (same test, same binary, no source changes), so the true rate is
in the low single-digit percent per invocation of this one test case — rare enough that a single
`ctest` pass, or even several, is unlikely to catch it, and consistent with the report's own CI
run (23/23 green) not having seen it. 0 failures in 40 runs of `core-cpp-async-fallback-test` is
not evidence the fallback binary is clean — at this rate, a 40-run sample has a substantial chance
of seeing zero regardless (same source, same `Join.hpp`/`ThreadPoolExecutor.hpp`, so I have no
reason to think it is unaffected).

**What the two clean (non-instrumented) crashes show, addresses resolved with `addr2line -e <bin> -f -C -i`:**

*Crash 1 (ASan, heap-use-after-free, WRITE)*: thread T0, inside
`JoinAwaiter<WhenAllPolicy>::await_suspend<DetachedTask::promise_type>`, executing
`Join.hpp`'s start-phase guard release (`_state->remaining.fetch_sub(1, acq_rel)`) — writes to a
`WhenAllPolicy::State` that a **different thread (T3)** had already freed, via
`~JoinAwaiter()` → `~vector<JoinRunner>()` → `~JoinRunner()` → `.destroy()` on a runner's frame →
`~shared_ptr<State>()` reaching zero.

*Crash 2 (TSan, heap-use-after-free/data race)*: thread T1, inside `detail::Parked::resume()`
(`ParkedWork.hpp:306`, the `!work.resume.done()` check on an entry it just popped off the pool's
queue) — reads a coroutine frame that **thread T4** is concurrently freeing, via the identical
`~JoinAwaiter()` cascade, this time bottoming out in `Task<void>::Awaiter::~Awaiter()`
(`Task.hpp:316`) destroying the **child `Task<void>`'s own frame** inside `makeJoinRunner`'s
suspended `co_await std::move(task)`.

**Mechanism, as far as I traced it with instrumentation** (added and removed in the private
worktree, not committed): `remaining` genuinely does need all 8 children's decrements plus the
start-phase guard's own release to reach zero — the `+1` guard's *arithmetic* is not violated, and
its release does not have to be the temporally-last decrement (a diagnostic run showed the guard
release firing with `old_remaining=4`, i.e., after only 5 of 9 total decrements — completely
ordinary, since submission is fast and the pool's four workers are still churning). The bug is
that **whichever thread's decrement is the one that reaches zero unconditionally destroys every
runner in `_runners`** (`~JoinAwaiter()`'s `std::vector<JoinRunner<Policy>>` destructor is
unconditional) **the moment it returns `join->continuation`, with no guarantee that every other
runner's own `FinalAwaiter::await_suspend` invocation — running concurrently on other worker
threads for children that already decremented — has fully unwound back out of the coroutine
machinery that touches that runner's frame.** `FinalAwaiter::await_suspend` copies `promise.state`
into a local `join` specifically so the *state* outlives the decrement (the comment says so), but
nothing protects the **runner's own frame** (`self`) or a **still-queued sibling child's frame**
from a *concurrent* teardown triggered by a *different* child's completion. This is a shape TSan's
own crash (Crash 2) shows directly: T1 has already dequeued a child's `ParkedWork` and is about to
resume it; T4, processing a different, unrelated child, happens to be the one whose decrement hits
zero, and cascades through freeing every runner including the one T1 is holding a live handle to.

**What C1 is and is not responsible for**: this is not a resurgence of the double-free C1 fixed —
`AbandonClaim`/`AbandonState` are not on either stack trace, and the freed objects are the
`WhenAllPolicy::State` and a child `Task<void>` frame, not an `unownedRoot` abandon claim. It is,
however, exactly the class of hazard R98/I2 was supposed to close (*"a join may span threads"*),
and I2's own new test is the one that surfaces it. I read I2's fix carefully and believe the
atomics themselves are correct for the counter they protect; the gap is one level up, in what
`~JoinAwaiter()` is allowed to assume about the runners it owns once the counter reaches zero.

**What I could not do**: pin the exact minimal interleaving with certainty (I have a plausible
mechanism, evidenced by two independently-symbolized clean crashes plus one instrumented run
showing the guard release firing well before all children complete, but I did not construct a
forced/deterministic repro, e.g. by injecting a sleep at a specific point, given the time this
review had). I also did not check whether `WhenAnyPolicy` (cancellation path) has the identical
exposure — the crashing case is `whenAll`-only; I'd expect the same shape to apply to `whenAny`
since `~JoinAwaiter()`'s teardown is shared code, but I have not reproduced it there.

**Recommendation**: this should be a Critical for a fix round 2, scoped to `Join.hpp`'s
`~JoinAwaiter()`/`_runners` teardown and the `ThreadPoolExecutor` cross-thread case I2 introduced
as a first-class case — not to C1's `ParkedWork`/`AbandonClaim` machinery, which held up under
everything I could throw at it.

## `e80d1a1` — `release()`'s CAS retry reshaped to a `do`/`while` (style fix over `50aed76`), reverified

`e80d1a1` rewrites `release()`'s retry loop from `for (;;) { ...; if (CAS(...)) { ...; return; } }`
to `do { ...compute takesTheRoot/desired... } while (!CAS(...)); if (takesTheRoot) ... destroy...`,
solely to satisfy the `[c-style-for]` hygiene rule (`for (;;)` is banned; `50aed76` had introduced
one). `takesTheRoot`/`desired` move from loop-scoped `const` to function-scoped mutable, and
`root.destroy()` moves from inside the loop to after it. Read it by hand: semantically identical —
both variables are recomputed at the top of every iteration from the current `expected` (including
the one whose CAS succeeds), and the loop only exits on that success, so the values used after the
loop always belong to the winning iteration. Not a change to the fix's logic.

**Re-verified anyway, since the diff touches the body of a use-after-free fix and this task has
specifically punished trusting "obviously equivalent."** Fresh worktree at `origin/master`
(`e80d1a1`), grepped the built tree for the `do`/`while` form before compiling (confirmed present,
`ParkedWork.hpp:92-102`), copied the byte-identical probe (checksum matched), deleted any prior
binary, built fresh, same 3×2000 TSan protocol against my original baseline:

| Run | Original baseline | `50aed76` | `e80d1a1` |
|---|---|---|---|
| TSan, 2000 iters, run 1 | 4 | 0 | **0** |
| TSan, 2000 iters, run 2 | 9 | 0 | **0** |
| TSan, 2000 iters, run 3 | 10 | 0 | **0** |

All three clean, all three reporting full arrival, exit 0. The restructuring did not reopen the
window. Same caveat as before: this confirms the refactor preserved the fix for the mechanism I
characterized; it is not a proof that no other path exists.

### What the original probe does, mechanically (left as a description, not a classification)

The sixteen `Prober`s each park twice, and those two parks are made two different ways within one
run. The **first** claim on each `Prober` comes from `rootTask`'s own spawn loop: sixteen
`claimOn()` calls in a tight sequential loop on one thread, resuming each `Prober` in turn. The
**second** claim on each `Prober` comes from *inside* `Parked::resume()`, on whichever pool worker
thread resumed that `Prober`'s first park — i.e. from the `ResumeOn` inside the `Prober`'s own body
running a second time, not from the spawn loop. My original symbolized crash had the racing,
about-to-be-destroyed `claimOn()` on the spawn-loop side (main thread, resuming a later `Prober` for
the first time) and the releasing claim on the second-park side (a worker thread finishing an
earlier `Prober`'s second `ResumeOn`). I am describing this rather than naming what about it made it
find the defect — the person who tried to name it once already got the answer they'd have been
confident in wrong.

### A second probe, built to differ from the first on a further mechanical point

Built `probe_multispawn.cpp` (not committed): four `std::thread`s, each constructing and resuming
four `Prober`s directly from its own thread body, all four started together with no ordering
between them and no single loop anywhere the first claims are made. Second parks still come from
`Parked::resume()` on a pool worker thread, as in the first probe.

**Checked it could reach the defect at all before trusting a clean run from it**: built against the
pre-fix tree (`D:/core-cpp-wt-b1-probe`, `04472c5`), ran the same 3×2000 TSan protocol — **9, 9, 10
heap-use-after-free reports**, matching the first probe's 4/9/10 and the implementer's own rate.

**Then ran against `e80d1a1`** (fresh worktree, `do`/`while` form confirmed present, fresh build):
3×2000 TSan, **0, 0, 0**, full arrival every run.

Two probes, mechanically different from each other and from the implementer's three harnesses in
where and how the first claim on a chain is made, both clean against `e80d1a1` after both were
shown able to reproduce the defect against `04472c5`. I have not built a third variant; I don't have
a specific further mechanical difference in mind that isn't already covered by one of these two,
and I'm saying that plainly rather than continuing to look for one.

## C3 fixed by `50aed76` — verified independently with the same Join-free probe

`50aed76` ("a chain that parks again is armed and counted in one step") replaces the two atomics
(`_parks`, `_armed`) with one 64-bit word (bit 63 armed, the rest the count), makes `claimAndArm()`
a single compare-exchange doing `(expected + 1) | ArmedBit`, and makes `release()` another
compare-exchange that clears the armed bit **as part of** taking the root — so *armed* is never
observable without the claim that accompanies it, and the right to destroy is claimed rather than
merely read. This closes exactly the gap described below (between a `rearm()` store and a `claim()`
store that used to be two separate operations). Nothing in `Join.hpp` changed, consistent with the
correction above.

**Re-ran my Join-free probe (`D:/core-cpp-wt-b1-probe/probe.cpp`, byte-identical, checksum
verified) against `origin/master` (`ebae689`, a descendant of `50aed76`) in a fresh worktree
(`D:/core-cpp-wt-probe2`), built from scratch** (binaries removed before rebuilding, freshly
compiled, checked against the caution about a broken configure serving stale binaries): same
protocol as before, three runs of 2000 iterations under `-fsanitize=thread`, plus a fourth at
12000 to match the implementer's own post-fix validation scale, plus ASan for parity.

| Run | Before (this report) | After (`50aed76`) |
|---|---|---|
| TSan, 2000 iterations, run 1 | 4 | **0** |
| TSan, 2000 iterations, run 2 | 9 | **0** |
| TSan, 2000 iterations, run 3 | 10 | **0** |
| TSan, 12000 iterations | not run before | **0** |
| ASan+UBSan, 2000 iterations | 0 (already clean) | **0** |

**18000 TSan iterations, 0 failures, all with full arrival (no case timed out) each run.** This is
not a new baseline — it is the same test, same binary construction, same machine, rerun after the
one-line-diff-away fix, and it goes from a reproducible ~0.2-0.5%/iteration failure rate to zero
across triple the iteration count I originally used to find it.

**What I am claiming, precisely, per the open item I flagged**: this proves `50aed76` closes the
mechanism I characterized (the `rearm`-then-`claim` gap) — a clean 18000 does not, and cannot,
prove there is no unrelated second path to the same wrong conclusion, since absence of a symptom
is never proof of absence of an unrelated cause. I have no evidence of a second path: I re-read the
fixed `release()`/`claimAndArm()` and could not find another way to observe `armed && count==0`
without the CAS having already claimed the destroy right atomically with that observation, and
`disarm()`'s bare `fetch_and` is the only other writer of the word and cannot, on its own, fabricate
a false "armed with zero count" reading since it never touches the count bits. If it fires under
either my harness or the implementer's while this fix is in place, that is a new, second defect,
not a regression of this one — a reading the implementer has already agreed to in advance.

## New finding: C3 (Critical) — `claimOn()`'s `rearm()`-then-`claim()` is not atomic as a pair, and a concurrent `release()` can free the root in the gap

Found after the report above was filed, in response to follow-up questions pushing on exactly this
mechanism (the implementer's own named residual risk: "`AbandonState::claim()`'s relaxed
`fetch_add` and the disarm/release interleaving"). **Confirmed, reproducible, and distinct from
both C1 (fixed by R97) and C2 (the `~JoinAwaiter()` teardown race reported above).**

**The code** (`ParkedWork.hpp:225-236`):

```cpp
[[nodiscard]] inline AbandonClaim claimOn(std::coroutine_handle<> root)
{
    if (!root) return {};
    auto& promise = std::coroutine_handle<DetachedTask::promise_type>::from_address(root.address()).promise();
    std::call_once(promise.abandonOnce, [&promise, root] { promise.abandonState = std::make_shared<AbandonState>(root); });
    promise.abandonState->rearm();              // (1) _armed = true
    return AbandonClaim { promise.abandonState }; // (2) _parks += 1, inside AbandonClaim's ctor
}
```

Steps (1) and (2) are two separate, non-atomic operations on two different atomics
(`_armed`, `_parks`). Between them, `_armed` already reads `true` but `_parks` has **not yet** been
incremented for this new claim.

**The race**: a *concurrent* `release()` on a *different, pre-existing* claim on the same
`AbandonState`, if it happens to be the one bringing `_parks` to exactly 0 (i.e., no other live
claims besides the one this new `claimOn()` is about to add), reads `_armed` in that same window —
sees `true` (from step 1, which already ran) — and concludes the chain is abandoned, calling
`root.destroy()`. The concurrent `claimOn()` then completes step (2) and returns an `AbandonClaim`
referencing a **freed coroutine frame**: the very next access to `promise.abandonState` (which lives
in that frame) is a use-after-free.

**This requires fan-out** (arity ≥ 2, or at minimum two temporally-overlapping claim lifetimes on
one root) — a single linear chain's claims never overlap enough for the "old==0, but a new claim is
mid-flight" gap to open, so it did not exist before R97's refcounting, and it is not reachable
through `ParkedWork_test.cpp`'s single-threaded `QueuedExecutor`. It **is** reachable through the
exact shape both `04472c5`'s new case and the pre-existing "join whose children finish on a pool"
case use: several children sharing one root, parked on a real `ThreadPoolExecutor`, where children
are spawned/resumed one at a time (in `JoinAwaiter::await_suspend`'s for-loop or my probe's spawn
loop) while pool worker threads are concurrently racing them to completion — so an early child can
fully finish (bringing `_parks` transiently to 0) before a later sibling has even made its first
claim.

**Reproduction, isolated from C2**: built a probe (`D:/core-cpp-wt-b1-probe/probe.cpp`, not
committed) that exercises only `AbandonState`/`AbandonClaim`/`ThreadPoolExecutor`/`ResumeOn` —
sixteen self-freeing "Prober" coroutines (`final_suspend = suspend_never`, no owning vector, so no
`~JoinAwaiter()` in the picture at all) sharing one root, each parking twice on a 4-thread pool.
Under `clang++-22 -fsanitize=thread`, three independent runs of 2000 iterations: **4, 9, and 10
ThreadSanitizer heap-use-after-free reports respectively** (roughly 0.2-0.5% per iteration).
Symbolized with `addr2line` against the probe's own (fully debug-built) binary:

- *Read*, main thread: `AbandonState`'s `shared_ptr::operator->()` → `claimOn` (`ParkedWork.hpp:236`,
  the `rearm()`/`AbandonClaim` construction line) → `parkedWorkFor` (250) → `ResumeOn::await_suspend`
  (40) — i.e., exactly the read of `promise.abandonState` inside a fresh `claimOn()` call, spawning
  the next prober.
- *Previous write* (the free), a pool worker thread: `operator delete` ← the **root task's own
  `.destroy()` ramp** ← `AbandonState::release()` (`ParkedWork.hpp:71`) ← `~AbandonClaim()` (130) ←
  `~ParkedWork()` (179) ← `Parked::resume()` (319) ← `ThreadPoolExecutor::worker()` (172).

This is the root frame being destroyed by one claim's `release()` while a *different* thread is
mid-`claimOn()` establishing a new one — precisely the mechanism above, not a guess from the stack
alone: I traced the exact interleaving by hand (one prober's second-park release racing a sibling's
first `claimOn()`) before looking for it, and the trace predicts exactly these two call chains.

Ran the same iteration count under `-fsanitize=address,undefined` (`ASAN_OPTIONS=detect_leaks=0`):
**0 failures in 2000 iterations.** This is expected, not exculpatory — the race window is a couple
of instructions wide (between one atomic store and the next), and ASan's instrumentation doesn't
perturb thread scheduling the way TSan's does; TSan is the sanitizer that widens narrow windows
enough to hit them, which is exactly why the dispatch asked for it specifically.

**Correction, superseding the "distinct defect" claim below and the C2 write-up above: C2 and C3
are almost certainly the same defect, and C2's attribution to Join.hpp was wrong.**

The implementer instrumented the C2 scenario directly (a magic word on `JoinAwaiter`, checked after
every `resume()` in the start loop) and found `JoinAwaiter` already destroyed at `i=2` of 8 — at
which point at most 3 of 9 required decrements can have happened, so `remaining >= 6`. **The join
cannot have completed.** A separate underflow detector on `remaining` never fired — the counter
itself is sound. A flag raised for the start loop's duration and checked inside
`AbandonState::release()` immediately before `root.destroy()` hit 8 times in 1920 runs.

This means my C2 stack traces were accurate but my *interpretation* of them was not. I read
`~JoinAwaiter()` → `~vector<JoinRunner>` → a runner or child frame freed under a concurrent
`Parked::resume()` as "the join legitimately completes (`remaining` reaches 0) and its teardown
races a sibling still in flight." It is not that: `joinOnPool` (the `DetachedTask` root) is itself
the target of every child's `unownedRoot` via `ResumeOn`, so every child's `ResumeOn` hop
claims/releases the *same* `AbandonState` as the one this section's mechanism describes. What
actually happens is `AbandonState::release()`, racing exactly as described above, incorrectly
concludes the chain is abandoned and calls `root.destroy()` on `joinOnPool` **while the start-phase
loop (or a later part of `JoinAwaiter::await_suspend`) is still running inside that same frame** —
which is exactly where my own ASan trace was (the start-phase guard release, `Join.hpp:302`). The
premature `root.destroy()` then cascades through `~JoinAwaiter()` and `~vector<JoinRunner>` exactly
as I traced; a completed join and an abandoned chain tear down the same objects through the same
destructor, so one reads as the other from a stack alone, and I read it as the wrong one.

**My isolated probe is direct, independent evidence for this correction, built and run before I
knew there was a dispute to settle.** It contains no `Join.hpp`, no `whenAll`, no `JoinRunner` —
only `AbandonState`/`AbandonClaim`/`ThreadPoolExecutor`/`ResumeOn` — and it reproduces the same
`root.destroy()`-via-`release()` use-after-free (4, 9, 10 hits per 2000 TSan iterations across
three runs, ~0.2-0.5%, closely matching the implementer's own 8/1920 ≈ 0.4%). A defect that fires
with `Join.hpp` entirely absent cannot be a `Join.hpp` defect. **There is one defect here, not two;
call it C3, and treat the "C2" section above as superseded by this one** — I am leaving it in place
rather than deleting it because the stack traces in it are real and useful for anyone tracing this
later, but its conclusion ("whichever thread's decrement brings remaining to 0 unconditionally
destroys every runner") is wrong and should not be acted on.

**On "location proven, mechanism not"**: the rearm-then-claim gap above *is* the mechanism, not
just the location. It answers the specific question left open: `release()` concludes "abandoned"
because it observes `_parks == 0` (true, transiently, because a new claim is mid-flight and hasn't
incremented yet) *and* `_armed == true` (also true, because that same new claim's `rearm()` already
ran) — both individually correct reads of stale-but-real state, combined across a window where they
do not describe a moment that ever existed as a whole. This is also consistent with the
implementer's own finding that disabling `rearm()` suppresses the crash: with `rearm()` a no-op,
`_armed` never becomes `true` again after the first `disarm()`, so `release()` never takes the
`root.destroy()` branch at all — that is what "proves the destroy comes through `release()`"
without yet explaining why, and the explanation is the gap between steps (1) and (2) in `claimOn()`.

I do not think `PROBE-AWAITER-DEAD at i=2 of 8` admits another reading. It is a positive assertion
that a specific, still-referenced object was destroyed at a point in the start loop where the
join's own arithmetic forbids it — that is not something a scheduling artifact or a compiler
reordering can produce; it requires an actual `destroy()` call on that frame from somewhere outside
the loop, and `AbandonState::release()` is the only thing in this codebase that ever calls
`root.destroy()`.

**On the field SEGFAULT** (`d44e2d6`, four concurrent builds): now attributable to this one defect
rather than "C2 or C3, don't need to pick" — there is only one defect to attribute it to.

**Fix shape, not prescribed but worth naming**: the ordering bug is that `_parks` can be observed at
0 by a concurrent `release()` while a `rearm()` for a *new* claim has already been published. Making
`claim()`'s increment happen (and be visible) before or atomically with `rearm()`'s write — or
collapsing the two into one operation `release()` cannot observe half of — closes the window;
whether that is best done by reordering, by a single combined atomic, or by a different protocol is
an implementation decision, not this review's to make.

## Anything else new

No other new Critical or Important defects. I looked for, and did not find, problems in: the
`claimLatch()` mechanism itself (traced by hand, looks correct for both policies); the destructor
member-order note in `JoinAwaiter` (`_parentRegistration`, `_parentToken`, `_state`, `_runners`,
`_tasks`, destroyed in that reverse order) — I confirmed `_state`'s own copy is released before
`_runners`, which is fine on its own (the last real reference is what frees the state, wherever it
sits), and is not what causes C2 above (C2 is about runner/child *frames*, not the shared state's
own lifetime, which I never saw double-freed or leaked in any run).

## Summary

| Finding | Verdict |
|---|---|
| C1 — refcounted abandon root | ADDRESSED |
| C1's undisclosed second half (`ThreadPoolExecutor::submit`) | VERIFIED |
| I2 — atomic join counters, ordering | ADDRESSED (counters); see C2 |
| I3 — `#1041` comment correction | ADDRESSED |
| I4 — `[[nodiscard]]` on `push()` | ADDRESSED |
| Minor 1 (empty-task throw doc) | ARGUED, correctly |
| Minor 2 (`displaced` doc) | ADDRESSED |
| Minor 3 (brace list) | ADDRESSED |
| Minor 4 (`AGENT.md`) | ADDRESSED |
| Minor 5 (`docs/modules/async.md`) | ADDRESSED |
| Minor 6 (movability) | ADDRESSED |
| Self-correction: `whenAny` immovable by the standard | VERIFIED |
| Self-correction: move-deletion example was legal under elision | VERIFIED by compiling |
| ~~New: C2, cross-thread join teardown races a sibling's frame~~ | **SUPERSEDED — same defect as C3, wrongly attributed to `Join.hpp`; see the correction under C3** |
| **New: C3, `claimOn()`'s rearm-then-claim races a concurrent `release()`** | **NEW CRITICAL, not addressed (introduced by R97). The one real defect behind both C2's traces and this section — mechanism identified, fix belongs in `ParkedWork.hpp`, not `Join.hpp`.** |

Everything the fix round claims to have fixed, I could confirm as fixed. The round is not
complete: `ThreadPoolExecutor_test.cpp`'s own new cross-thread join case is intermittently a real
use-after-free under both ASan and TSan, at a rate too low for one CI pass to reliably catch, and
the fix round's report cannot be read as having closed R98's "a join may span threads" concern —
only the counter half of it.
