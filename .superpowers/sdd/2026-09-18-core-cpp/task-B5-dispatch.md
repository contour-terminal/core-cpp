# Task B5: timers that do not poll, and a loop that can be driven by a browser

Read the plan's Task B5 (`docs/superpowers/plans/2026-09-18-core-cpp.md`) and the design spec's
§2 item 5 and the `EventLoop` interface block. **Read B4's report first** — it owns the turn, the
park table and the teardown order you are building on, and its contract is what the plan's prose
only approximates.

## What already exists, checked at `origin/master` rather than taken from the plan

B4 landed the coroutine-facing half. **You are adding the callback-facing half, and the hard part
is not adding a second scheduler beside the one that is there.**

| Thing | Where | State |
|---|---|---|
| `fireExpiredTimers()` | `EventLoop.cpp:395`, turn step 5 | exists |
| `ParkEntry::onDeadline(work, deadline)` | park table | exists |
| `registerPark` / `unregisterPark` / `cancelPending` | `EventLoop` | exists |
| `DelayAwaiter`, `delay()`, `sleepUntil()` | `EventLoop.hpp:326,330,617` | exists |
| `schedule(deadline, ParkedWork)` | `EventLoop.cpp:452,457` | exists |
| **`addTimer` / `cancelTimer` / `TimerId` / `TimerCallback`** | — | **yours** |
| **`DeadlineTimer`, `interruptibleSleepUntil`, `nextWakeStep`** | — | **yours** |

**The deadline mechanism that landed is the PARK TABLE, not a heap of its own.** `DelayAwaiter`
parks with `registerPark(ParkEntry::onDeadline(parkedWorkFor(awaiting), deadline))`, and step 5
fires whatever has expired, FIFO by sequence. A `ParkEntry` today carries `ParkedWork` — a
coroutine to resume.

**The design question this task decides, and it is yours to decide and to report:** `addTimer`
fires a **callback**, not a coroutine — "no frame", in the plan's words. Does a callback timer
become a second kind of `ParkEntry`, or a separate list that step 5 also drains? And is `TimerId`
a `ParkId`, a distinct type over the same table, or its own? **Say which you chose and what it
costs**, because the wrong answer here is a second scheduler that drifts from the first, and the
symptom arrives in B8 or B12 rather than in your tests.

`ParkId` is already generation-checked and never reused — read how before inventing a second
identity scheme.

## What to build

- `addTimer(SteadyTimePoint, TimerCallback, void* state) -> TimerId` and
  `cancelTimer(TimerId) noexcept -> bool`, both on `EventLoop`, neither allocating a frame.
- `DeadlineTimer(EventLoop&, SteadyTimePoint, callback, void* state)` over them, **destroyable
  from inside its own callback** — that is a test, not a remark.
- `interruptibleSleepUntil(EventLoop*, StopToken, SteadyTimePoint) -> Task<WakeReason>`.
- `nextWakeStep`.
- `sleepUntil(EventLoop* loopOrNull, SteadyTimePoint)`: a null loop or an elapsed deadline
  resolves **inline, without suspending**.
- Keep the deprecated `wakeBound` overload for one release, marked as such.

**Nothing here polls.** fastcached's `DeadlineTimer` had a poll interval; the whole point of this
task is that it does not, because the loop already knows the next deadline.

## Sources

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as blobs:

```
git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:src/FastCache/Async/<file>
```

`SleepUntil.{hpp,cpp}`, `DeadlineTimer.{hpp,cpp}`, `InterruptibleSleep.{hpp,cpp}` and their tests.
**Take the reasoning, not the shape** — their `IReactor` is B4's `EventLoop`, and their poll
interval is the thing you are removing.

Record the pin and a row per imported file in `.agent/reference/provenance.md`, and every renamed
public symbol in `tools/migrate/renames.json`, **in the same commit as the code**. Base
`renames.json` on `git show HEAD:<path>`, never the worktree: it is generated as well as
hand-edited.

## Tests, first

- `SleepUntil_test`: null loop and elapsed deadline resolve inline — assert **no suspension**, not
  merely a prompt return.
- `DeadlineTimer_test`, **without** the poll-interval cases.
- `InterruptibleSleep_test`, rewritten: a cancel wakes promptly **with `ManualClock` frozen** — so
  the case cannot pass by real time elapsing — and **leaves nothing parked**. Assert the park table
  is empty afterwards; that is what distinguishes a cancel from a race you got away with.
- `DeadlineTimer` destroyed from inside its own callback.
- `addTimer`/`cancelTimer` over `testing::TestLoop`: FIFO by sequence among equal deadlines,
  `cancelTimer` on an already-fired id returns false rather than cancelling a reused one.

Write the case, run it, **capture the RED verbatim**, then implement, then the GREEN. Say how many
failures you expect and from which cases **before** you run.

## WebAssembly — this is the task the browser story depends on

`tests/wasm/HostDrivenTimer_smoke.cpp` (Emscripten only, `-sASYNCIFY`): a `PlatformLoop` spawns a
coroutine that awaits `delay(20ms)` and sets a flag; `main` calls `emscripten_sleep(10)` in a loop,
**bounded to 2 s**, until the flag is set; the test fails on timeout. Run it under node in the
`emscripten` job on **both** emsdk versions, and extend `tests/consumer-wasm` to the same scenario
— Task A8 left it building base and async only, and the plan says the task that adds B5 extends it.

No `std::thread`, no blocking wait, no `Threads::Threads`, nothing newer than libc++ 17 without a
`__cpp_lib_*` guard.

## What this session has learned that bears on you

- **`std::jthread` does not exist on AppleClang or FreeBSD libc++.** `__cpp_lib_jthread` is the
  feature-test and `StopToken.hpp` has a fallback for exactly that. Use `std::thread` with an
  explicit join. This cost two required macOS jobs tonight.
- **`ctest -L hygiene` is in your gate list and it is the one that gets skipped.** A lane landed a
  `for (;;)` tonight after being told the line number, because its checklist had six toolchains and
  not that. No C-style `for`.
- **Check the artifact, not the exit code.** `cmake -P` prints `CMake Error: Not a file` and exits
  **0**. A build that "succeeded" may have produced nothing; a test run that passes may have run
  stale binaries off a failed re-configure.
- **A measurement that cannot come out the other way is not a measurement.** Before reporting a
  number, say what input would have made it different. If you measure a rate, fix `n` and the
  expected-hit arithmetic **before** the runs.
- **Prefer the form that returns the evidence over the form that returns a count.**
- **One commit per push** — GitHub builds only a push's tip, so an intermediate commit is built by
  nothing. `unbuilt-commits` will now name them, which is a report, not a remedy.
- **The shared tree is not a buildable state.** Build in a throwaway `git worktree` at
  `origin/master` plus your own files. Never `git pull --rebase`; `git fetch origin`, then push.
- **Never a bare `git commit`, `git commit -a` or `git add`.** `git commit --only -- <paths>`; a
  private `GIT_INDEX_FILE` only for hunks of a file whose other hunks are not yours, then snapshot
  `git status --porcelain` before, `git reset -- <your paths>` after, and **diff the snapshots**.
  `git show --stat` after every commit.
- **Report a defect in another lane's file; do not fix it.**

## What B4 left you, in its own words — read this before you design

B4's report raises a concern aimed at you by name:

> **`ParkTable::pruneTimers` is lazy, and a cancelled deadline leaves a heap slot until the heap
> root reaches it.** Bounded by the number of deadlines ever armed rather than by the number live,
> so a loop that arms and cancels a deadline per request grows the heap until one of them is due.
> The alternative — erase-and-reheap per cancellation — is O(n) per cancel, which is what upstream
> did and what makes a loop with many deadlines quadratic. **Stated because B5 arms far more
> deadlines than B4 does, and it is B5 that will find out whether lazy is enough.**

**You are the task that finds out.** `DeadlineTimer` and `interruptibleSleepUntil` arm and cancel
deadlines at a rate `EventLoop` alone never did — a socket with a receive deadline cancels one per
successful read. **Write the case that grows the heap**: arm and cancel N deadlines without letting
any fire, and assert something about the heap's size or the turn's cost. If lazy pruning is enough,
that case documents why; if it is not, you have found it with a measurement rather than inheriting
a quadratic loop in B8.

**Do not "fix" it pre-emptively.** B4's reasoning for lazy is sound and erase-and-reheap is the
known-bad alternative. Measure first, and say what you measured.

Two more from B4's report that touch you:

- **`spawn` wakes the backend when called off the turn** — needed so a flow spawned before anything
  drives a host-driven loop still starts. Your WebAssembly smoke depends on that behaviour.
- **`_inRun` decides whether an idle turn blocks**, and it is a member rather than a parameter: a
  consumer driving `runOnce()` in its own loop gets a returning turn rather than a blocking one.
  Relevant if any of your cases drive the loop by hand.

## Then

- `python scripts/clang-format.py --check <paths>` (it now refuses a path that is not a C++
  source — if it refuses one of yours, that is the guard working), the `clang-tidy` preset,
  `ctest -L hygiene`, `mkdocs build --strict`.
- WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`; Windows `cl-debug` and
  `clangcl-release`; the `emscripten` job green.
- `CHANGELOG.md` under `[Unreleased]`.
- `.agent/rules/async-and-net.md` gains the rule this task proves — *a timer does not poll; the
  loop already knows its next deadline* — citing its origin as a full URL.

## Report

`D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B5-report.md`: RED/GREEN per test, **the
callback-timer design decision and what it costs**, whether `TimerId` is a `ParkId`, which cases
could not run here and which CI run covered them, and what you are leaving for B6 and B12 named
explicitly. Return only status, the commit range, a one-line test summary, and concerns.

Commit as the plan names it:
`net: callback timers; DeadlineTimer and interruptible sleep no longer poll`.
