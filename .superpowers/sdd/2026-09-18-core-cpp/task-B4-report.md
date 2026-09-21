# Task B4 — `EventLoop` absorbs fastcached's reactor contract

**Status: DONE_WITH_CONCERNS.** My commits: `cc1b237`, `0ae632e`, `8f1d0d0`, `45fc58a`, `d4168b0`,
`1205045`, `216d1d1`, `a040878`, `1562018`, `a9ea52b`, `4049954` (range `cc1b237^..4049954`;
other lanes' commits are interleaved). **CI Build `35560895366` on `1562018` is completed and
green — 25 jobs, 0 non-success — and Portability (FreeBSD) `35562217834` on the same head is green
too.** The two commits after it are an assertion string and the repair in §7's last entry.

Seven of those eleven are fixes to my own work, each found by a check the last one did not run —
gcc-release, clang-tidy, ASan, the Windows dev shell, and CI's macOS, consumer and Windows-Debug
legs. Five are worth a reviewer's time and are in §7: **the fixpoint case did not discriminate a
fixpoint** (`d4168b0`), **nine of my cases wrote to dead stack** (`1205045`), **the canary's
`add_test` landed in consumers' suites** (`a040878`), **the scale case ate the net binary's hang
backstop** (`1562018`), and **I committed another lane's in-flight work and broke master**
(`a9ea52b`, repaired by `4049954`).

---

## 1. What landed

**Created.** `PlatformLoop.hpp`, `testing/TestLoop.hpp`, `detail/ParkTable.hpp`,
`detail/WorkerIdentity.hpp`, `HostDrivenCanary.cpp`, and the tests `ClockRefresh_test.cpp`,
`LoopTeardown_test.cpp`, `HostDrivenLoop_test.cpp`, `testing/TestLoop_test.cpp`.

**Rewritten.** `EventLoop.{hpp,cpp}` (+969/−734 on the pair). `EventLoop_test.cpp` gains the turn's
order and bound, G2, G3, the worker-identity query and `spawn`'s O(1) unlink; its clock cases moved
out to `ClockRefresh_test.cpp`.

**Changed elsewhere.** `IoBackend` gains `virtual void setPump(HostCallback, void*) noexcept {}`
beside `isHostDriven()` and `armWakeAt()` (§6a). `src/core/net/CMakeLists.txt` puts `EventLoop.cpp`
and the two new `detail/` headers in the WebAssembly subset and registers the canary.

The API, in the spec's order: `run`, `runOnce`, `runUntilIdle`, `blockOn`, `stop`, `requestStop`,
`using async::IExecutor::submit;`, `submit` ×2, `schedule` ×2, `cancelPending`, `post`, `spawn`,
`delay`, `sleepUntil`, `waitReadable`/`waitWritable` (now taking a `HandleKind`),
`notifyHandleClosing`, `running`, `isOnWorkerThread`, `teardownIsSerialisedWithDispatch`, `clock`,
`rootStopSource`, `resumeSoon`, `registerPark`, `unregisterPark`, `requestCancel`, plus
`IdlePolicy`, `EventLoopOptions`, `RunOnceResult`, `ParkEntry`, `PlatformLoop`, `testing::TestLoop`.
What I did **not** implement is in §6f.

---

## 2. RED then GREEN, per rule

Every new API's first RED was the compile failure of a case naming a member that did not exist;
those are not interesting and are not listed. What is listed is the **behavioural** RED: each
load-bearing arm removed one at a time in a private detached worktree
(`git worktree add --detach … d4168b0`, its own build directory, removed afterwards), the case run,
the output captured, the arm restored. **Every prediction below was written down before the run**
— the count of failing cases and which assertions — and every one matched.

| Arm removed | Predicted | Case that failed | RED, verbatim |
|---|---|---|---|
| Step 1 resolves cancels **before** step 2 drains (moved after) | 1 case, `settled.resumed` and `outcome` among the failures | *A cross-thread cancel is resolved before the turn drains, so it unwinds in that turn* | `settled.resumed == 1 for: 0 == 1`; `outcome == 2 for: 0 == 2`; `loop.spawnedCount() == 0 for: 1 == 0` |
| Step 3's `clock.refresh()` | 1 case, the timeout reads 500 not 400 | *The turn refreshes its clock before it computes the wait's timeout* | `timeoutMs(backend.recordedTimeouts().back()) == 400 for: 500 (0x1f4) == 400 (0x190)` |
| Step 5's `clock.refresh()` | 1 case, a different one | *The turn refreshes its clock after the wait, so a deadline it reached fires in that turn* | `loop.pendingTimerCount() == 0 for: 1 == 0`; `loop.readyCount() == 1 for: 0 == 1`; `fired for: false`; `backend.waitCount() == 1 for: 2 == 1` |
| Teardown step 4's **fixpoint** (one pass instead) | 1 case, and I predicted it might need ASan | *A loop frees parked chains while all of its containers are still alive* | `counters.parkedAtFree == 0 for: 1 == 0` — deterministic, no sanitizer needed |
| Teardown frees what it owns (queued everything instead) | ≥2 cases | four teardown cases | `counters.completed == 0 for: 1 == 0` ×6, `detached.completed == 0 for: 1 == 0` ×2 — 4 of 8 cases |
| A backend callback resumes inline (Rule 1 / G2) | SIGABRT on the Rule 1 assertion | *G2: a flow is never resumed from inside the backend's wait* | `Assertion '!detail::readinessDispatchInFlight() && "EventLoop::drainReadyQueue reached from inside a backend dispatch: " "backend callbacks may only enqueue"' failed.` then `SIGABRT` |
| `spawn`'s O(1) unlink (a sweep instead) | 3 cases | the three `[spawn]` cases | `loop.spawnedCount() == 0 for: 2 == 0`; `== 1 for: 2 == 1`; `== 0 for: 100000 (0x186a0) == 0` (the scale case was 100000 then; `1562018` makes it 10000 and asserts the per-turn drop — §7) |
| The host-driven refusal in `run()` | `hostdriven-canary.run` FAILS, `.blockOn` still passes | `core-cpp.hostdriven-canary.run` | `The following tests FAILED: 13 - core-cpp.hostdriven-canary.run (Failed)` |

**GREEN.** `core-cpp-net-test`: 1123 assertions in 167 cases. `core-cpp-net_backend-test`: 126
assertions in 27 cases (and under Emscripten, where it is the binary that proves the loop builds
there). Full `ctest -L core-cpp`: **31/31 on clang-debug, gcc-release, clang-asan-ubsan and
clang-tsan; 25/25 on emscripten; 33/33 on cl-debug and clangcl-release.**

Honest caveat on ordering: the implementation and the cases were written together, because the
contract had to exist for a case to name it. So the REDs above are arm-removal rather than a first
run against nothing. What arm-removal does not prove is that a case would fail against an *empty*
implementation; what it does prove is that every rule in the contract has a case that dies without
it — and, in two places, that a case I had written did **not** (§7).

---

## 3. How the two orders were proved load-bearing rather than incidental

**`runOnce`, step 1 before step 2.** The case parks a spawned flow on a deadline an hour out,
stops the root source **from another thread** (which is the real path: a stop callback hands the
loop a `ParkId` and touches nothing else), and then asserts that **one** `runOnce()` both resolves
the request and unwinds the flow. Resolving after the drain leaves the flow queued while steps 3
and 4 compute a timeout and block — and this loop's only deadline is an hour out, so the effect is
not "a turn later", it is "an hour later, or never". The mutation shows exactly that:
`settled.resumed` drops to 0.

**`~EventLoop` step 4, the fixpoint.** This one I got wrong first (§7). Carried over from
fastcached, the case parked a deadline chain and a submission whose frame re-enters the loop as it
is freed — and a single-pass mutation **passed** it, because one pass frees both and nothing parks
again. The loop never reaches the state the step exists for. So the submission's frame now carries
a member that parks a NEW chain as it dies, once. With the fixpoint, the pass that starts it is
followed by another that frees it while the park table is whole; with one pass it is left to
member destruction, and its own re-entrant disarm then reads a table whose destructor is running.
That is a deterministic value failure (`parkedAtFree == 0 for: 1 == 0`) *and* the ASan read — I
expected only the second and got both.

**The two clock refreshes** are the third order, and they now have one case each for the reason the
dispatch gives: the single case that preceded them passed with *either* removed. The mutations
above show them failing in different cases, on different assertions.

---

## 4. The six #1025 proofs, and what each catches

`LoopTeardown_test.cpp`, over `testing::BackendMatrix` (every backend this platform builds) plus the
deterministic double. Each counts frame destructions with a sentinel and asserts the number, so it
fails for its own reason on every platform rather than waiting for LeakSanitizer.

| Proof | What it catches |
|---|---|
| A coroutine parked on a loop's deadline is freed exactly once | The defect itself: a loop destroyed with work parked freeing none of it. The leak is *indirect only* under LSan, which is a red once in N runs. |
| A loop deadline that arrives is resumed rather than freed | The over-correction. A teardown that freed everything it holds would pass every other case here. |
| An abandoned chain is freed from its ROOT | Freeing the frame the loop HOLDS gives one destruction out of three: the two above it are reachable only through each other, which is the indirect-only signature. |
| A coroutine parked on a loop **submission** is freed exactly once | The other container. In core-cpp it is also the *inbound* queue, because work handed over from any thread but the loop's waits there and nowhere else — a teardown sweeping only the loop's own containers leaks exactly the shape #1025 was reported on. |
| A loop submission that is dequeued is resumed rather than freed | The reconciliation path: a chain given back at the one place a queued resumption is taken out. A suite that only ever abandons never reaches that line. |
| A loop leaves parked work whose frame something else owns alone | The double free. A loop that freed what it merely borrows would free a `Task` the caller still holds — and the next line of the case would be a use-after-free rather than a failed check. |

Plus two the port added: **a readiness park at teardown** — the kind fastcached's reactors had and
its `ParkedWork_test` did not reach, and the only kind that also holds a registration the backend
knows by address, so teardown must detach before it frees (both halves asserted: the detached chain
freed, the spawned one unwound through `OperationCancelled`); and **the fixpoint case** above.
Upstream's two primitive-level cases stayed where the type lives, in
`src/core/async/ParkedWork_test.cpp`, which already has them.

---

## 5. B3's deferrals: what I took and what I decided

| B3 left | Decision |
|---|---|
| `runOnce`, `IdlePolicy`, `EventLoopOptions`, `resumeSoon`, `registerPark`/`unregisterPark`, `cancelPending`, `ParkedWork`, `IExecutor` | All implemented. `pumpOnce` did have the five-step shape, so this was a rename plus the park table, plus the two ownership rules in §6b. |
| `ParkId` is narrow — it names an fd wait alone | Widened to every kind of parked work, and moved to `detail/ParkTable.hpp` with `ParkEntry`. **The id IS the generation check**: ids come from one never-reused counter, so a cancel for a park that has gone resolves to nothing without a separate generation field. |
| `HostDrivenBackend::setPump` is the seam B4 must use | Taken, but **promoted to a defaulted virtual on `IoBackend`** (§6a). `EventLoop` holds an `IoBackend&` and nothing else; a loop that downcast to reach `setPump` would be a loop that knows one concrete backend by name, and a Qt or frame-callback host-driven backend could then never be given a pump. |
| **B4 owes the `run()`/`blockOn()` refusal** | Taken. Both assert; both are **compiled** under WebAssembly rather than removed, because a symbol simply absent there fails at link time in a consumer's build with nothing to say why. `core-cpp.hostdriven-canary` is a process per mode, `WILL_FAIL`. |
| §6b: `DefaultHandleKind`, not `defaultHandleKind` | Used as B3 spelled it. |
| §6c: `ParkId` is a struct, not an `enum class` | Kept, and the reason restated where it now lives. |
| §6a: `ReadinessHandler::slot` is not implemented | Left out; B7's, as B3 argued. Nothing in B4 wanted it. |

---

## 6. Deliberate deviations — please review these

**(a) `IoBackend` gains `setPump`.** A defaulted virtual no-op beside `isHostDriven()` and
`armWakeAt()`, which are the other two members of the same contract and defaulted for the same
reason. B3 named `HostDrivenBackend::setPump` as the seam but left it concrete, and `EventLoop`
cannot reach a concrete backend without a `dynamic_cast` to a type it should not know. **This is a
change to B3's interface**, and the alternative I rejected is in the header's own comment.

**(b) Teardown frees what the loop OWNS and resumes what it BORROWS — in both containers.** The
spec says "abandon to a fixpoint: free the `abandon` roots of remaining `Parked` work", and it says
drain first; it does not say which work each step takes. I had to decide, and the answer is
`ParkedWork::abandon`: a chain the loop owns is a `DetachedTask`, which carries **no stop token** —
a detached flow has no awaiting coroutine to inherit one from — so resuming it at teardown would
not cancel it, it would run the rest of its body on a loop that is going away. A chain the loop
borrows belongs to a `Task` somebody holds, that owner set a stop token, and resuming it is what
makes the frame unwind and run its cleanup. Without this split, `anAbandonedDeadlineIsFreedExactlyOnce`
reports `completed == 1`: fastcached's answer, reached from contour's draining teardown.
The flag is recorded where the work is queued (`Park::ownedByLoop`, `ReadyEntry::ownedByLoop`)
rather than read back out of `detail::Parked`, which deliberately exposes no accessor for it.

**(c) `cancelPending` searches the inbound queue too, and DISARMS rather than releases.** Upstream's
`Take()` returned a plain handle, so dropping it did nothing; here the claim is refcounted, so
dropping the last one would free the very frame the caller has just been handed — the opposite of
an ownership transfer. And it searches the inbound queue because otherwise the answer depends on
which thread submitted, which is not a transfer anybody can rely on. `TestLoop_test`'s
*cancelPending hands the work back, on every backend* found the first half by failing.

**(d) `requestCancel` resolves inline only on the loop's own thread.** The predicate is
`isOnWorkerThread()`, not `!running() || isOnWorkerThread()`: "nobody is driving right now" is not
"nobody else can start". Inline is the common path, because `whenAny` stops its losers from inside
the drain that ran the winner — and a park left live until the next turn is one whose frame its
owner destroys in between.

**(e) `blockOn` stops when nothing can advance its flow.** It drives through a private `turn(maxWait,
until)`; once `until` is done the turn skips step 4, because a wait entered after the root has
finished can only end on a deadline or a wake belonging to work nobody is waiting for — and on a
loop with a background flow parked on a socket, on nothing at all. This also closes
[core-cpp#17](https://github.com/contour-terminal/core-cpp/issues/17) (`blockOn` spinning at full
CPU), which Task B12 had been carrying. `blockOn(trivialTask())` therefore no longer performs a
turn's wait, and two existing cases that used it as "pump once" now use `runOnce()`.

**(f) What I did NOT implement, and why.** `addTimer`/`cancelTimer`/`TimerId`/`TimerCallback` and
the free `sleepUntil(EventLoop*, tp)` are in the spec's `EventLoop` block but are **Task B5's**
checklist by name ("Implement `addTimer`/`cancelTimer` (no frame) and `DeadlineTimer` over them").
Implementing them here would have taken B5's work without its tests. The park table already carries
the deadline heap they will sit on. Flagging it because the dispatch says the declaration is a
contract to implement exactly.

**(g) `WaitFdAwaiter` → `WaitHandleAwaiter`, and `delay` takes a `SteadyDuration`.** Both are the
spec's spelling. A row for the first is in `renames.json`; `delay`'s argument converts implicitly
from `std::chrono::milliseconds`, so no call site changes.

---

## 7. What I got wrong

**The fixpoint case did not discriminate a fixpoint** (`d4168b0`). Ported from fastcached and
believed. Measured against a single-pass mutation, it passed — one pass frees what the loop holds,
nothing parks again, and the state the step exists for is never reached. The fix is a frame member
that re-parks once as it dies. **This is the one I would have shipped believing it worked**, and
the only reason I did not is that the dispatch asked for the mutation.

**Nine of my cases wrote to dead stack, and four green presets said nothing** (`1205045`). ASan:
`stack-use-after-scope`, `[1440, 1444) 'outcome' (line 965)`. `~EventLoop` resumes every borrowed
park so the flow unwinds, and the unwinding runs on whatever the case gave it — so a counter
declared *after* the loop is destroyed first. This file's older cases already say "declared BEFORE
the loop, so it outlives it"; mine did not. A second report of the same shape one layer in: the
fixpoint case's `armed` flag lived inside the driver's scope, and `ReparkOnce` reads it from inside
`~EventLoop`, which IS that driver's destructor.

**The canary's `add_test()` landed in consumers' suites** (`a040878`). Registered with a bare
`add_test()`, which core-cpp's `CORE_CPP_TESTING` gate never sees — every other test here goes
through `core_cpp_add_test`, which returns early without it, which is how a CPM or vendored
consumer builds core-cpp and registers none of core-cpp's tests. Mine went straight past into the
*consumer's* ctest, naming an executable their build never made: `consumer-smoke (cpm)` red on
`core-cpp.hostdriven-canary.run (Not Run)`. Exactly the shape those legs exist to catch, and B3's
report has the same lesson one file over.

**Two cases read as green in Debug and were not there in Release** (`8f1d0d0`), found by running
gcc-release rather than only clang-debug: a canary helper that is an unused function under `NDEBUG`,
and a clock case that waited for a real 1ms deadline through a bounded number of turns — in a
Release build two thousand turns take under a millisecond.

**The scale case measured the Debug CRT's heap and ate the binary's hang backstop** (`1562018`).
`core-cpp.net` on `windows (cl-debug)` took 69.97s on one head and hit its 120s `TIMEOUT` on the
next. `--durations yes` locally: *spawn of one hundred thousand flows* was **38.556 of a
39.532-second** MSVC Debug run — a hundred thousand live coroutine frames, a list node and a map
node each, in a Debug heap that validates its block list on every allocation. Superlinear, and none
of it inside the loop: ten thousand of the same flows cost **0.211s**, 183× less for 10× fewer.
That backstop exists to report a HANG in two minutes rather than in ctest's default twenty-five,
and a case that eats it leaves every other case in the binary without one; raising it would have
been the wrong trade. The case is now ten thousand and asserts something stronger than it did — one
turn, and the spawned count drops by exactly that turn's batch, which is what discriminates a sweep
and holds at any scale. The old assertion was only that the count ended at zero, which a sweep also
achieves. `core-cpp.net` on cl-debug is now 1.16s. **This is a deviation from the dispatch's
"`spawn` of 100000 tasks"**, taken deliberately and measured.

**`std::jthread` does not exist on AppleClang** (`a040878`). Its libc++ has no `<stop_token>`. No
local preset can catch this; CI's macOS leg did.

**I committed Task B5's in-flight work and broke master for four minutes** (`a9ea52b`, repaired by
`4049954`). `a9ea52b` changed one assertion string and committed with
`git commit --only -- src/core/net/EventLoop.cpp`. `--only` takes the **worktree** copy of the
named path, and by then the worktree copy held B5's `addTimer`/`runDueCallback` work: 89 of their
lines went out with 4 of mine, and it did not compile — the committed `.cpp` names
`ReadyEntry::callbackPark`, `Park::onExpired`, `TimerId` and `runDueCallback`, all declared in
`EventLoop.hpp` and `detail/ParkTable.hpp`, correctly still uncommitted.

`4049954` restores the file to `a9ea52b^` plus my assertion string, built from
`git show HEAD~1:<path>` and committed through a **private index** so nothing in the working tree
was touched; B5's edits are still exactly where they left them, verified by grep and by their file
showing modified again afterwards. A detached worktree at `4049954` configures, builds and runs
the net suite clean (1125 assertions, 167 cases), and the B5 lane was told directly.

The rule has its own heading in the global constraints: **`--only` is for files only I touched, and
in this checkout that has to be checked with `git diff` before every commit** rather than inferred
from who wrote the file originally. I had been doing exactly that for `CHANGELOG.md`,
`provenance.md` and `renames.json` all evening, and stopped doing it for a file I thought of as
mine. The shared file is not the one with many authors — it is **any file another lane is editing
right now**, and which file that is changes hour by hour.

**On Windows the canary hung instead of failing** (`216d1d1`). A failed `assert()` in an MSVC Debug
build opens a message box and waits, so both canary processes ran into ctest's timeout — reported
as a hang, which reads as the loop deadlocking rather than as the refusal firing exactly as it
should. `core::testing_dialogs` installs the suppression; `tests/WindowsDialogCanary.cpp` does the
same thing for the same reason and I did not look at it first.

---

## 8. Gates and platforms

| | Result |
|---|---|
| `clang-debug` (WSL) | **31/31** |
| `gcc-release` (WSL) | **31/31** — the toolchain that diagnoses `-Woverloaded-virtual`, which is how `using async::IExecutor::submit;` goes missing (fastcached#1041). Clean. |
| `clang-asan-ubsan` | **31/31**, no sanitizer report |
| `clang-tsan` | **31/31** — the run that matters here: the park table and the inbound queue are the cross-thread surface |
| `clang-tidy` preset | clean (three findings, all fixed in `45fc58a`) |
| `emscripten` (emsdk in WSL) | **25/25**, including `core-cpp.net_backend`, which is what proves `EventLoop`, `PlatformLoop`, `TestLoop` and the host-driven path build and run under single-threaded WebAssembly |
| `cl-debug` (VS dev shell) | **33/33** |
| `clangcl-release` (VS dev shell) | **33/33** (the canaries skip: `NDEBUG` removes the refusal they observe) |
| pinned `clang-format` 22.1.8 | clean over all 14 files I touched |
| `ctest -L hygiene`, incl. `core-cpp.migrate-renames` | green — 495 rows, 468 delivered, 1 pending (B6's), 0 failures |
| `mkdocs build --strict` | green |

`renames.json`: the three B4 rows marked delivered (and `PlatformLoop`'s target header corrected to
its own file), plus `net::WaitFdAwaiter`, nine `FastCache::IReactor` members and
`FastCache::ReactorWorkerIdentity`. `provenance.md`: nine new rows and three amended.
`CHANGELOG.md`: one **Breaking** entry with seven migrations. `.agent/rules/async-and-net.md`: three
new sections (the turn, the six-step teardown, G1–G3), each citing fastcached#475, #668, #1025 or
#1054 as a full URL. `docs/design/threading.md` rewritten — it said `run()` and `blockOn()` were
"not available" under WebAssembly, which is now the opposite of true.

**CI.**

| Run | Head | Result |
|---|---|---|
| `35557813690` | `1205045` | failure — `windows (cl-debug)` (the dialog hang), `macos (appleclang)` (`jthread`), `consumer-smoke (cpm)` and `(vendored)` (the ungated `add_test`). Three defects, all mine. |
| `35559386105` | `216d1d1` | failure — the canaries now pass on Windows; `core-cpp.net` timed out there instead (the scale case), and macOS/consumer were still unfixed at that head. |
| `35559759646` | `a040878` | **success, 25 jobs, 0 non-success.** |
| `35560895366` | `1562018` | **success, 25 jobs, 0 non-success** — the head this task closes on. |
| `35562217834` | `1562018` | Portability (FreeBSD), dispatched by me; it is the only exercise kqueue gets outside macOS. |
| `35556460524` | `e80d1a1` | Docs, success — later than my only `docs/` commit, and master is linear. |
| `35562217834` | `1562018` | **Portability (FreeBSD), success** — kqueue's only exercise outside macOS. |

---

## 9. Concerns

1. **The last two commits have no CI run of their own yet.** `a9ea52b` is an assertion string and
   `4049954` backs out what it wrongly took; the tree at `4049954` is byte-identical to `1562018`
   but for that string, and `1562018` is covered by the green `35560895366` and by FreeBSD's
   `35562217834`. I also built and ran the net suite from a clean detached worktree at `4049954`.
2. **`blockOn` on a still-suspended task now throws rather than spins.** `Task::result()` refuses a
   task owning no frame; a task that owns one and has not finished is a different case, and what
   the caller gets is whatever `Task<T>::result()` does with an unfinished frame. I did not add a
   refusal for it, because `core::async` owns that question and `refuseEmptyTask`'s comment is
   explicit that the empty state is the one it names. **A reviewer should decide whether B4 owes a
   second refusal or whether it belongs to `Task`.**
3. **`_inRun` decides whether an idle turn blocks**, and it is a member rather than a parameter.
   A consumer that writes its own `while (…) loop.runOnce();` instead of `run()` gets a turn that
   returns rather than blocking when nothing is parked — which is correct for a loop somebody else
   drives, and surprising if they expected `IdlePolicy::Block` to mean "block anywhere". It is
   documented at the wait and in `threading.md`; it is the one place the turn's behaviour depends
   on which entry point reached it.
4. **`ParkTable::pruneTimers` is lazy, and a cancelled deadline leaves a heap slot until the heap
   root reaches it.** Bounded by the number of deadlines ever armed rather than by the number live,
   so a loop that arms and cancels a deadline per request grows the heap until one of them is due.
   The alternative — erase-and-reheap per cancellation — is O(n) per cancel, which is what upstream
   did and what makes a loop with many deadlines quadratic. Stated because B5 arms far more
   deadlines than B4 does, and it is B5 that will find out whether lazy is enough.
5. **`spawn` now wakes the backend when called off the turn.** Needed, or a flow spawned before
   anything drives the loop never starts on a host-driven backend — which is how a WebAssembly
   consumer sets a loop up. On a native backend it costs one byte on the wakeup channel per spawn
   from outside a turn; inside a turn it is skipped.
6. **Not mine, and live in the tree while I worked:** `cmake/CoreCppHeaderSelfCheck.cmake` was
   uncommitted-broken for about an hour (`set(${` split across a newline), so every fresh
   `cmake --preset` failed while already-configured trees kept building; and
   `src/core/async/ParkedWork.hpp` carried a `for (;;)` that `core-cpp.cmake-hygiene` refuses. Both
   were reported to the controller and fixed by their lanes; I touched neither.
7. **`BackendDriver` in `LoopTeardown_test.cpp` drives on the calling thread**, not on a worker of
   its own as fastcached's `PlatformDriver` did. Deliberate — every assertion there is about what
   the DESTRUCTOR does, and a destructor that has to be serialised with a worker thread is one
   whose teardown rule (G5) the case would be exercising instead of the rule it names. The cost is
   that no teardown case runs against a loop that is genuinely running on another thread;
   `EventLoop_test.cpp`'s *A running loop refuses teardown from any other thread* is the only case
   that does, and it asserts the query rather than the teardown.

---

# Fix round 1

Review: `task-B4-review.md` — 1 Critical, 3 Important, 5 Minor. All nine addressed, plus the
CHANGELOG correction the lead said was owed either way, plus one row B5 reported in a file of mine.

Commit `aa368fe` on `4049954`, rebased onto B5's `fe48143` as **`fb3fe97`**.

## What the round changed

| Finding | Resolution |
|---|---|
| **C1** `blockOn` reads a disengaged `optional` | `throw std::logic_error` where the `break` was; `<stdexcept>` added; two cases, `Task<int>` and `Task<void>`, parked on an `AsyncQueue` the loop does not drive. `core::async` untouched. |
| **I1** readiness park leaves `_byHandle` too early | `dropIndices` split into `dropWaiterIndices` and `dropHandleIndex`; only `take()` calls the second. Case: a park dispatched in step 4 and closed before its resuming turn. |
| **I2** teardown strands borrowed inbound work | **Reverted to the drop branch**, documented in three places. See below — this is the part I got wrong. |
| **I3** `spawn` mutates three containers unguarded | `assert(teardownIsSerialisedWithDispatch())` on `spawn`, `resumeSoon`, `requestStop`, `registerPark`, `unregisterPark`, `wakeReasonOf`; Doxygen on `spawn` and a paragraph in `docs/design/threading.md`. |
| **M1** `blockOn` never instantiated under Emscripten | `STATIC_REQUIRE` replaced by taking `&EventLoop::blockOn<void>`, which odr-uses the definition. |
| **M2** `pruneTimers` doc | States it prunes only from the heap root, and what that buys. |
| **M3** `registerPark` discards the `NetError` | `FdRegistrationFailed` gains `NetError reason`; `registerPark` takes an optional out-parameter, so B5's one-argument calls are unchanged. |
| **M4** `blockOn` polls on an `IdlePolicy::Return` loop | Documented on `blockOn`, naming `testing::TestLoop` and what to use instead. |
| **M5** three consistency items | `blockOn` queues through `queueReady`; the dead branch in `TestLoop_test.cpp` replaced by the assertion it was hiding; `_abandoned` pruned in `unregisterPark`, `cancelPending` and `abandonParkedWork`. |

Two things beyond the review: the CHANGELOG's Breaking entry no longer claims `Task::result()`
refuses the still-suspended task — it never did — and `renames.json` gained the include row for
`FastCache/Async/IReactor.hpp`, which B5 found missing while eleven sibling Async headers had one.

## I2: I chose the wrong branch, and the tree already knew

The review offered two branches — move borrowed inbound work into `_ready`, or state that it is
dropped — and asked for "the case that pins whichever you choose." I implemented the move. **Two
cases that would have chosen for me were already in the tree, and I wrote the implementation
before running them:**

```
LoopTeardown_test.cpp:531   CHECK( counters.completed == 0 )   1 == 0   (test double, poll, epoll)
TestLoop_test.cpp:336       SIGSEGV
```

`workSomebodyElseOwnsIsLeftAlone` submits a **never-started lazy `Task`'s** handle off-turn;
resuming it at teardown *starts* the coroutine rather than unwinding it. `TestLoop::stop
short-circuits run()` submits a `countYields` handle that never runs; resuming it re-enters a loop
that is being destroyed, which is the segfault.

The general fact, which is now a constraint rather than a B4 detail:

> **The loop cannot ask a borrowed `std::coroutine_handle<>` what it names.** A suspended flow that
> would unwind and a never-started lazy `Task` that would run are the same type, and
> `ResumeOn::await_resume()` is `noexcept`, so even a genuine continuation runs its body rather
> than unwinding. There is no safe discriminator.

So the drop stands, and what makes it defensible rather than merely expedient is written where it
binds: not *which container* the work is in, but that **a turn accepted it**. The loop owes a
resumption it took up; a submission still inbound is an offer no turn took. The cost is stated in
the open in `~EventLoop`, `.agent/rules/async-and-net.md` and the CHANGELOG — a cross-thread
`ResumeOn { loop }` whose loop dies before the next turn strands its flow forever, and an owner who
needs that delivery runs one more turn on purpose.

## RED, and two cases that passed while proving nothing

Captured before any implementation:

- **C1, `Task<int>`**: not the failure I predicted. I wrote down "returns garbage"; libstdc++'s
  hardened `optional::operator*` aborted the process instead — `Assertion 'this->_M_is_engaged()'
  failed` — taking the binary down before Catch2 could report. **That makes the Critical worse than
  the review stated, not better**: loud on a hardened standard library, silent on one without it,
  and the silent one is what ships to a consumer.
- **C1, `Task<void>`**: `no exception was thrown where one was expected`, as predicted.
- **I1**: `parkedWaiterCount()` `0 == 1` and `attachedCount()` `1 == 0` — the registration survived
  the close.
- **I2**: `CHECK(resumed)` false.

**Two of my first three new cases passed while proving nothing, and I only found out because I
predicted a failure and got a pass.** The close-window case ran one turn; `spawn` off-turn calls
`_backend.wake()`, and a wake consumes no `ScriptedBackend` script step, so turn 1's wait returned
on the wake and dispatched no readiness at all. And the inbound case asserted
`TestLoop::pendingSubmissions() == 1`, which is `readyCount()` — the ready queue, not the inbound
queue it was about. Both now carry an explicit discriminator (`readyCount() == 1` after the
dispatching turn; `readyCount() == 0` for the inbound case) with a comment saying why the case
needs it.

## The SEGFAULT, and three explanations I was offered

`core-cpp.net` segfaulted during a `ctest` run that also reported five tests "Not Run" with
`No such device`. The D: volume — a Dev Drive VHDX on a system drive with 3.7 MB free — had
detached mid-run. Two explanations were offered to me and I had a third of my own: the dying mount,
a silently-corrupted dependency (a disk that fills mid-write leaves a file the right length with
the wrong bytes, which is what happened to B5's libunicode data), and an actual regression.

I refused all three, deleted the build tree rather than trusting one written during a detachment,
rebuilt from scratch on a live volume, and ran it. **It reproduced, and it was mine** — the I2
branch above. Had I accepted any of the exculpatory stories, a real use-after-free would have been
filed as an environment artefact.

On the dependency question specifically: rather than checksumming, the useful instrument was that
**`unicode_tablegen` — the exact binary that failed for B5 — ran to completion in both my builds.**
The artefact the data produces tests the data.

## Known and not done

- The review notes three cases that declare their counter *after* the loop
  (`EventLoop_test.cpp:622`, `:682`, `HostDrivenLoop_test.cpp:69`). They are safe today, because the
  work they name has completed by then, and they are one edit from not being. I have **not** added
  the guarding comment: it is not one of the nine findings, and the gates below were already
  running against `fb3fe97` when I considered it. Worth a follow-up, and it is the class that
  produced nine dead-stack writes in the original round.
- `a9ea52b` has its own in-flight CI run and **it will go red truthfully** — that commit genuinely
  did not compile, because it took B5's `EventLoop.cpp` without the declarations in
  `EventLoop.hpp` that it names. It was repaired forward in `4049954`. Nobody should chase it.
