# Task B4: `EventLoop` absorbs fastcached's reactor contract

B3 replaced the backends. This task replaces what drives them. contour's `EventLoop` is a thin
pump over a token registry; fastcached's `IReactor` is a full contract — turn structure, park
table, generation-checked cancellation, an ordered teardown, and thread-affinity guarantees
asserted on every platform. The merged `EventLoop` is the second, and B5's timers, B6's sockets,
B7's IOCP and B12's TUI runtime are all written against it.

Read the plan's Task B4 (`docs/superpowers/plans/2026-09-18-core-cpp.md`, the "Task B4" section)
and the design spec's §2 — the `EventLoop` declaration, the **`runOnce` turn**, the **teardown**
order, and Rules 1 to 5 (`docs/superpowers/specs/2026-09-18-core-cpp-design.md`). The spec's
declaration is a contract: implement it exactly, including the `[[nodiscard]]`s, the `noexcept`s
and `using coro::IExecutor::submit;`.

**Read B3's report first** (`task-B3-report.md`). It says what `EventLoop` adaptation it made to
keep the tree compiling and what it deliberately left for you. Anything it marked as
"failing by design, B4's answer" is yours to decide, not to inherit.

## The two orders that are the whole task

**`runOnce`, per turn:**

1. Swap the inbound queue. Run posts; resolve cancel requests by live `ParkId`.
2. Drain the ready queue.
3. `clock.refresh()`, then compute the timeout.
4. `backend.wait(timeout)`.
5. `clock.refresh()`, then fire expired timers FIFO by sequence.

**`~EventLoop`, in order:**

1. Assert that teardown is serialised with dispatch.
2. `requestStop()` on the root stop source, and move all parks to the ready queue.
3. Run bounded drain passes.
4. Abandon to a fixpoint: free the `abandon` roots of remaining `Parked` work
   (LASTRADA-Software/fastcached#1025).
5. Destroy the spawned roots.
6. Unregister wake.

Both orders are load-bearing, and both are the kind of thing a reader will "simplify". Write the
reason for each step beside it, and make at least the abandon fixpoint and the
resolve-cancels-before-drain ordering provable by a test that fails if the steps are swapped.

`teardownIsSerialisedWithDispatch() == !running() || isOnWorkerThread()`.

## Sources

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as blobs:

```
git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:src/FastCache/Async/<file>
```

`IReactor.hpp`, `IReactor_test.cpp`, `TestReactor.{hpp,cpp}`, `TestReactor_test.cpp`,
`ReactorClockRefresh_test.cpp`, `ParkedWork_test.cpp`, `ReactorTeardown.hpp`,
`ReactorWorkerIdentity.hpp`, `PlatformReactor.hpp`. Plus contour's `EventLoop_test.cpp`, already
in the tree.

Apply the rename map: `IReactor` → `EventLoop`, `PlatformReactor` → `PlatformLoop`,
`TestReactor` → `testing::TestLoop`, `Submit`/`Schedule`/`CancelPending`/`Run`/`Stop`/`Clock` →
camelBack, `SleepUntil{&r,tp}` → `loop.sleepUntil(tp)`. Every renamed public symbol gets a row in
`tools/migrate/renames.json` **in the same commit** — `PlatformLoop` and `TestLoop` already have
**pending rows waiting for you**, and `ctest -L hygiene` goes red until you fill them in. That is
the drift gate working as designed.

Record the pin and a row per imported file in `.agent/reference/provenance.md`.

## Tests, first

- contour's `EventLoop_test` keeps passing, adapted to the new contract.
- fastcached's `IReactor_test`; `TestReactor_test` → `TestLoop_test`: FIFO order, thread-safe
  `submit`, and **`cancelPending` ownership transfer on every backend, including submissions**.
- `ReactorClockRefresh_test` → `ClockRefresh_test`: the two `clock.refresh()` calls in the turn
  are not decoration. A test that passes with either one removed is not testing them.
- `ParkedWork_test` → `LoopTeardown_test`, over B3's `BackendMatrix`, carrying the **six #1025
  proofs**. This is the most valuable file in the task: teardown is where a loop frees something
  another object still owns, and it is the bug class that is invisible until a consumer's process
  exits.
- `spawn` of 100000 tasks is O(1) per completion — assert `spawnedCount()` drops without a sweep,
  not that it merely ends up at zero.
- `runOnce` bounds its turn; `IdlePolicy::Return` returns instead of blocking.
- **Host-driven mode**, over B3's `testing::ManualHostScheduler`:
  - `loop.post(f)` runs `f` only once the host pumps;
  - a spawned coroutine awaiting `loop.delay(50ms)` resumes after the host pumps at the armed
    deadline, with `ManualClock` driving time;
  - `run()` and `blockOn()` are precondition violations on a host-driven loop — a Debug death
    test, label `canary`, `WILL_FAIL`.
- After each turn on a host-driven backend, the loop calls `backend.armWakeAt(nextDeadline)`.
  Assert the deadline it passes, not merely that it called.

Thread affinity, asserted on every backend: **G1** exactly one thread dequeues a loop;
**G2** every resumption happens in turn step 2; **G3** helper threads only post. G2 is the one
worth the most: it is Rule 1 seen from the loop's side, and a test that a resumption never
happens inside `backend.wait()` catches the whole class.

Write the case, run it, capture the RED verbatim, then implement, then the GREEN.

## WebAssembly

`EventLoop`, `PlatformLoop`, `TestLoop` and the host-driven path join the WebAssembly `FILE_SET`.
No `std::thread`, no blocking wait, no `Threads::Threads`, nothing newer than libc++ 17 without a
`__cpp_lib_*` guard. `run()` and `blockOn()` must compile there and assert at runtime rather than
being compiled out — the browser reaches them through a consumer's mistake, not through ours. The
`emscripten` CI job must be green before this task is done.

## Then

- `python scripts/clang-format.py --check` on what you touched, the `clang-tidy` preset clean,
  `ctest -L hygiene` (including the rename gate), `mkdocs build --strict`.
- Local presets: WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`; Windows
  `cl-debug` and `clangcl-release`. **Run `gcc-release` yourself, not only clang** — it is the
  only one of the three toolchains that diagnoses `-Woverloaded-virtual`, which is exactly how
  `using coro::IExecutor::submit;` goes missing (LASTRADA-Software/fastcached#1041), and this task
  implements that interface. TSan matters here too: the park table and the inbound queue are the
  cross-thread surface.
- `CHANGELOG.md` under **`Breaking`** with migrations. contour, endo and tuidu all have callers.
- `.agent/rules/async-and-net.md` gains the teardown order, the turn order and G1 to G3, each
  citing its origin as a full URL. The plan requires the rule to be written in the task that
  implements it.
- `docs/design/threading.md` (G1 to G5 and the teardown order) is listed in B13, but if you write
  the rules here, write that page here too and let B13 check it rather than author it.

## Concurrency

Other lanes share this checkout and branch; I will name them in the dispatch message. One working
tree, **one `.git/index`**, one local `master`.

- **Never a bare `git commit`, `git commit -a` or `git add`.** Files only you touched:
  `git commit --only -- <pathspecs>`. A shared file where you need some hunks: a private index
  (`export GIT_INDEX_FILE=$(mktemp)`, `git read-tree HEAD`, `git apply --cached` a trimmed patch,
  `git commit`, unset). `git show --stat` after every commit; an unexpected file means stop.
- **Never `git pull --rebase`** — it refuses with another lane's unstaged work. `git fetch origin`,
  then push; it is a fast-forward.
- Your commit will usually get no CI run of its own: GitHub supersedes a **pending** run in a
  concurrency group regardless of `cancel-in-progress: false`. Watch the newest head, not your own
  SHA, and say so plainly if you close with no completed green run covering your commits.
- Never run a formatter over a file another lane is editing. Report anything of theirs that looks
  broken; do not fix it.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B4-report.md`: RED/GREEN per test,
how you proved the two orders are load-bearing rather than incidental, which of B3's deferrals you
took and what you decided, the six #1025 proofs and what each one would catch, and the CI run IDs.
Return only status, the commit range, a one-line test summary, and concerns.

Push, watch the newest head to green.
