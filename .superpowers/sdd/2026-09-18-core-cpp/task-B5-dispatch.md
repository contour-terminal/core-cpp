# Task B5: timers — callback-based, and nothing polls any more

B4 gave the loop its turn structure and its park table. This task puts time on it. The headline
is in the commit message the plan names: **`DeadlineTimer` and interruptible sleep no longer
poll**. In **fastcached at the pin** both wake on an interval and re-check -- none of
`DeadlineTimer`, `interruptibleSleepUntil`, `addTimer`, `WakeReason` or `wakeBound` exists in
`src/` yet, and you import them; after this they arm a deadline and are
resumed by the loop's timer step. A cancel has to wake them *promptly* with the clock frozen,
which is the property that proves polling is gone — an interval-based implementation passes every
test that lets real time advance.

Read the plan's Task B5 (`docs/superpowers/plans/2026-09-18-core-cpp.md`, the "Task B5" section)
and the design spec's §2 — the `EventLoop` timer members, the `runOnce` turn's step 5 (expired
timers fire FIFO by sequence), and `sleepUntil(EventLoop*, SteadyTimePoint)`
(`docs/superpowers/specs/2026-09-18-core-cpp-design.md`).

**Read B4's report first** (`task-B4-report.md`) for what it left you, and B3's for the
`HostDrivenBackend` contract — `armWakeAt` is how a timer fires in a browser, and this task is
the first real consumer of it.

## Sources

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as blobs:

```
git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:src/FastCache/Async/<file>
```

`SleepUntil.hpp`, `SleepUntil_test.cpp`, `DeadlineTimer.{hpp,cpp}`, `DeadlineTimer_test.cpp`,
`InterruptibleSleep.{hpp,cpp}`, `InterruptibleSleep_test.cpp`.

Rename map: `SleepUntil{&r, tp}` → `loop.sleepUntil(tp)`, `InterruptibleSleepUntil` →
`interruptibleSleepUntil(&loop, token, tp)`, `CancellationToken` → `StopToken`. Every renamed
public symbol gets a row in `tools/migrate/renames.json` **in the same commit**; the drift gate
runs under `ctest -L hygiene`.

Record the pin and a row per imported file in `.agent/reference/provenance.md`.

## What to build

- **`EventLoop::addTimer(SteadyTimePoint, TimerCallback, void* state) -> TimerId`** and
  **`cancelTimer(TimerId) noexcept -> bool`**, frame-free: a timer is a callback and a state
  pointer, not a coroutine. B4 built the turn that fires them; this is the registration side.
- **`DeadlineTimer(EventLoop&, SteadyTimePoint, callback, state)`** over those. Its destructor
  cancels.
- **`interruptibleSleepUntil(EventLoop*, StopToken, SteadyTimePoint) -> Task<WakeReason>`**.
- **`nextWakeStep`**, and `sleepUntil(EventLoop* loopOrNull, SteadyTimePoint)` where a null loop
  or an already-elapsed deadline resolves **without suspending**.
- Keep the deprecated `wakeBound` overload for one release, marked and recorded.

## Tests, first — and the three that carry the task

1. **A cancel wakes promptly with `ManualClock` frozen, and leaves nothing parked.** Freeze the
   clock, start an interruptible sleep with a deadline far away, request the stop, and assert the
   coroutine resumes without the clock ever moving *and* that the loop's park table and timer heap
   are both empty afterwards. An interval-based implementation cannot pass this: it has nothing to
   wake it while time stands still. Assert the emptiness, not just the resumption — a timer left
   in the heap after cancellation is core-cpp#16's shape, a use-after-free waiting for the heap to
   pop it.
2. **`DeadlineTimer` is destroyable from inside its own callback.** The callback runs, destroys
   the timer that is running it, and the loop continues to the next turn without touching freed
   memory. Run it under ASan; a pass without ASan proves much less.
3. **A null loop and an already-elapsed deadline resolve inline** — `await_ready()` true, no
   suspension, no timer registered. fastcached's `SleepUntil_test` has these; they are the
   difference between a delay you can call unconditionally and one that needs a guard at every
   site.

Plus: `DeadlineTimer_test` without its poll-interval cases (they are testing the mechanism this
task deletes), and timers firing **FIFO by sequence** when several expire in the same turn — equal
deadlines must fire in registration order, which is B4's step 5 seen from here.

Write the case, run it, capture the RED verbatim, then implement, then the GREEN.

## The browser leg

`tests/wasm/HostDrivenTimer_smoke.cpp`, Emscripten only, built `-sASYNCIFY`: a `PlatformLoop`
spawns a coroutine that awaits `delay(20ms)` and sets a flag; `main` calls `emscripten_sleep(10)`
in a loop, **bounded to 2 s**, until the flag is set, and fails on timeout. Run it under node in
the `emscripten` job on **both** emsdk versions (3.1.56 and latest), and extend
`tests/consumer-wasm` to the same scenario — the plan says the task that adds B5 extends it, and
that is this task.

This is the first end-to-end proof that a core-cpp event loop can run inside a host's, which is
morph's whole reason for adopting us. If it cannot be made to work, that is a report, not a
workaround.

No `std::thread`, no blocking wait, no `Threads::Threads` in the subset; nothing newer than
libc++ 17 without a `__cpp_lib_*` guard.

## Then

- `python scripts/clang-format.py --check <paths>` (a bare run is an error, exit 2),
  `python scripts/python-style.py --all --check` if any Python changed (likewise refuses a bare
  run), the
  `clang-tidy` preset clean, `ctest -L hygiene` (including the rename gate),
  `mkdocs build --strict`.
- Local presets: WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`; Windows
  `cl-debug` and `clangcl-release`. **Run `gcc-release` yourself** — it is the only one of the
  three toolchains that diagnoses `-Woverloaded-virtual`, and this module is full of interfaces.
  ASan matters for test 2; TSan for the cross-thread cancel path.
- `CHANGELOG.md` under **`Breaking`** with migrations for the poll-interval parameters that
  disappear. contour and fastcached both have callers.
- `.agent/rules/async-and-net.md`: **CITE, do not add** -- the cancellation rule is already there
  at `:53-57`, and what is missing is only its origin URL. Duplicating it would make *a reason
  recorded twice, which is a reason that can disagree with itself*. The rule this implements — *a cancel from the
  flow's own token throws `OperationCancelled`; a cancel from the resource returns
  `NetErrorCode::Cancelled` as a value* — with its origin as a full URL.
- `docs/modules/net.md`: the timer surface.

## Concurrency

Other lanes share this checkout; I will name them in the dispatch message. One working tree, **one
`.git/index`**, one local `master`.

- **Never a bare `git commit`, `git commit -a` or `git add`.** Files only you touched:
  `git commit --only -- <pathspecs>`. Some hunks of a shared file: a private index
  (`export GIT_INDEX_FILE=$(mktemp)`, `git read-tree HEAD`, `git apply --cached` a trimmed patch,
  `git commit`, unset). `git show --stat` after every commit.
- **Never `git pull --rebase`** — it refuses with another lane's unstaged work. `git fetch origin`,
  then push.
- Your commit will usually get no CI run of its own; GitHub supersedes a **pending** run in a
  concurrency group. Watch the newest head, and say so plainly if you close with no completed
  green run covering your commits.
- Never run a formatter over a file another lane is editing. Report anything of theirs that looks
  broken; do not fix it.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B5-report.md`: RED/GREEN per test,
how you proved polling is gone rather than merely faster, what the browser smoke actually
demonstrated on each emsdk version, and the CI run IDs. Return only status, the commit range, a
one-line test summary, and concerns.

Commit `net: callback timers; DeadlineTimer and interruptible sleep no longer poll`.
