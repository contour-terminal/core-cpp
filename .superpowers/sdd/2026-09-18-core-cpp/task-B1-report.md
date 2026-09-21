# Task B1 report — `core::async` grafts

**Status:** DONE_WITH_CONCERNS, fix round 1 included (the concerns are other agents' in-flight work
in `src/core/net/`, not this task's code). See **Fix round 1** at the end for C1, I2, I3, I4 and the
six Minors, with their REDs.

**Commits:** `b21229d`, `f72cabc`, `7838b53` on `master`, pushed. (They are not contiguous:
two other agents pushed between them.)

- `b21229d async: teardown-safe ownership, detached tasks, executors and AsyncQueue from fastcached`
- `f72cabc async: one runner and one join behind whenAll and whenAny`
- `7838b53 docs(rules): the #1041 hiding shape is refused by two tools, not one`
- `78bf537 docs(modules): the async row names what the module now holds`
- `66dfc9a async: the rename table, the drift row and the link smoke's bound`
- `d7a7bee docs(async): an empty task's refusal is a precondition, not an error to catch`

**Fix round 1 commits:** `ac0ff76..c99ca38`, pushed, four of them and contiguous:

- `ac0ff76 fix(async): the parks of one fan-out free their shared chain root once`
- `0a77bfa fix(async): a join counts atomically, because its children may finish anywhere`
- `4d20406 fix(async): AsyncQueue::push() is [[nodiscard]], and says what it cost`
- `c99ca38 docs(async): what closes the #1041 hazard is the pure virtual, not the using`
- `ceb471d docs(async): the example of what a deleted move forbids was one that compiles`
  (a fifth, pushed after the other four: re-reading my own diff I found the movability comment
  claimed `auto h() { return whenAll(…); }` was ill-formed against a deleted move. It is not —
  that operand is a prvalue of the return type, so copy-elision constructs it in place. Compiled
  both forms against the deleted move to settle it; only the lvalue return is rejected.)

**Upstream pin:** fastcached `0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21` (`origin/master`, fetched
2026-09-20 — the SHA the dispatch names). Every source read as a git blob with
`git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:<path>`; `grep -c $'\r'`
reported 0 on every one. Recorded in `CHANGELOG.md` (Imported table), `NOTICE` and
`.agent/reference/provenance.md` (one row per file).

---

## What landed

New public headers in `src/core/async/`, all header-only, all in the WebAssembly subset but the
last:

| Header | Holds |
|---|---|
| `ParkedWork.hpp` | `ParkedWork`, `detail::Parked`, `detail::unownedRootOf`, `detail::parkedWorkFor` |
| `DetachedTask.hpp` | `DetachedTask` (R66) |
| `SyncRun.hpp` | `syncRun`, `syncRunWith` (R66) |
| `IExecutor.hpp` | `IExecutor` with both `submit` overloads |
| `ResumeOn.hpp` | `ResumeOn` |
| `ThreadPoolExecutor.hpp` | `ThreadPoolExecutor`, header-only (R65); not in the WebAssembly `FILE_SET` |
| `AsyncQueue.hpp` | `AsyncQueue<T>`, `AsyncQueueOptions`, `AsyncQueueOverflow`, `AsyncQueueAdmission`, `AsyncQueuePush` |
| `Join.hpp` | the one runner, join state and awaiter behind `whenAll` and `whenAny` (commit 2) |

Modified: `Task.hpp` (the ownership graft), `UniqueCoroHandle.hpp` (`release()`), `Awaitable.hpp`
(`CarriesUnownedRoot`), `WhenAll.hpp`, `WhenAny.hpp`.

### The graft

`Task::operator co_await() &&` moves the frame out of the `Task` into the awaiter (a
`detail::UniqueCoroHandle`, so the RAII is single-sourced), which holds it across the suspension
and destroys it at the end of the `co_await` expression. Ownership in a chain therefore runs
strictly downward, which is what makes an abandoned chain freeable from its root.
`unownedRoot` is a member of every promise in the module, set at each `await_suspend` from
`detail::unownedRootOf(awaiting)`, and non-empty exactly where the chain bottoms out in a
`DetachedTask`.

Two departures from upstream's layout, both deliberate:

- `unownedRootOf` / `parkedWorkFor` live in `ParkedWork.hpp`, not `Task.hpp`. The file that defines
  `ParkedWork::abandon` is the one that decides it, and `ResumeOn.hpp` and `AsyncQueue.hpp` then
  need no `Task.hpp`.
- The promise probe is the `CarriesUnownedRoot` concept rather than upstream's
  `std::is_base_of_v<TaskPromiseBase, Promise>`. That is what lets the `whenAll`/`whenAny` runners
  — a coroutine type of their own, not derived from `TaskPromiseBase` — carry the answer too. A
  runner that did not would make every park underneath a combinator read as *somebody owns this*,
  and a detached chain would leak whole. It has a test.
- `SyncRun`'s `Retrieve` overload is a separate name, `syncRunWith`, as the dispatch's file table
  spells it, rather than a second overload of `syncRun`.

---

## RED/GREEN per test

Every case below was seen to fail before the code that makes it pass. Where the old code could not
express the case at all, the RED is the compiler refusing it, captured against `HEAD`'s headers in
a scratch tree (`git show HEAD:src/core/async/*.hpp` into `-I.`), which is the honest form of "this
did not exist".

### 1. The awaiter owns what it awaits, and a task with no frame has no result

`Task_test.cpp`: *Awaiting a task takes its frame, leaving the name that held it empty* and *A task
owning no frame has no result to give*.

RED (`core-cpp-async-test`, before `Task.hpp` changed):

```
/mnt/d/core-cpp/src/core/async/Task_test.cpp:203: FAILED:
  CHECK_FALSE( stillOwnsAfter )
with expansion:
  !true

/mnt/d/core-cpp/src/core/async/Task_test.cpp:205: FAILED:
  CHECK( destroyed == 1 )
with expansion:
  0 == 1

/mnt/d/core-cpp/src/core/async/Task_test.cpp:219: FAILED:
  CHECK_THROWS_AS( empty.result(), std::logic_error )
because no exception was thrown where one was expected:

/mnt/d/core-cpp/src/core/async/Task_test.cpp:223: FAILED:
  CHECK_THROWS_AS( emptyVoid.result(), std::logic_error )
because no exception was thrown where one was expected:

===============================================================================
test cases: 11 |  9 passed | 2 failed
assertions: 34 | 30 passed | 4 failed
```

GREEN: `All tests passed (169 assertions in 42 test cases)` after the graft.

### 2. A non-default-constructible result

RED, compiled against `HEAD`'s `Task.hpp`:

```
./core/async/Task.hpp:158:24: error: call to deleted constructor of 'Measurement'
  158 |                 return T {};
      |                        ^ ~~
probe3.cpp:30:27: note: in instantiation of member function
      'core::async::Task<Measurement>::Awaiter::await_resume' requested here
```

GREEN: *A task produces a value whose type cannot be default-constructed*.

### 3. `release()`

RED, same probe: `error: no member named 'release' in 'core::async::Task<Measurement>'`.
GREEN: *release() hands the frame to the caller, and the task keeps nothing* — which also caught a
leak of my own on the first run (see §ASan below).

### 4. `syncRun` / `syncRunWith`

RED: `fatal error: 'core/async/SyncRun.hpp' file not found`.
GREEN: four cases — the value, the refusal of a still-suspended task (value and void), the park
taken back before the refusal (the retriever is called exactly once and the body reaches its end),
and the retriever untouched where nothing parked. Plus a case for a task owning no frame, so
`syncRun` refuses rather than resuming a null handle.

### 5. `unownedRoot` through the `whenAll` and `whenAny` runners

`ParkedWork_test.cpp`, with a `QueuedExecutor` that never runs anything by itself.

RED (after the rest of the graft was green — so the two cases failed *alone*, which is the
asymmetry that makes them worth something):

```
-------------------------------------------------------------------------------
unownedRoot reaches a coroutine parked underneath a whenAll runner
-------------------------------------------------------------------------------
/mnt/d/core-cpp/src/core/async/ParkedWork_test.cpp:284: FAILED:
  CHECK( counters.destroyed == 2 )
with expansion:
  0 == 2

-------------------------------------------------------------------------------
unownedRoot reaches a coroutine parked underneath a whenAny runner
-------------------------------------------------------------------------------
/mnt/d/core-cpp/src/core/async/ParkedWork_test.cpp:299: FAILED:
  CHECK( counters.destroyed == 2 )
with expansion:
  0 == 2

===============================================================================
test cases: 13 | 11 passed | 2 failed
assertions: 48 | 46 passed | 2 failed
```

GREEN after adding `unownedRoot` to both runner promises and setting it in their awaiters.

### 6. `ParkedWork` primitives

All green on the first run, with the rest of `ParkedWork_test.cpp` (the abandoned chain freed from
its root, the resumed chain not freed twice, borrowed work left alone, `Parked::take()`, and
`unownedRootOf` over each shape of chain). These are ported properties rather than new behaviour,
and the `whenAll`/`whenAny` cases above are the ones that were red.

### 7. `ThreadPoolExecutor`

Ported from upstream; green on the first run on every preset. The value it adds here is that R65's
header-only body is exercised (four cases: work moves off the caller's thread, N threads run N
blocking pieces at once, nothing is abandoned on either route out, zero threads is clamped to one).

### 8. `AsyncQueue`, and the stop-aware `pop`

RED with the stop handling removed from `AsyncQueue.hpp` — i.e. the queue fastcached ships:

```
/mnt/d/core-cpp/src/core/async/AsyncQueue_test.cpp:368: FAILED:
  CHECK( executor.pending() == 1 )
with expansion:
  0 == 1
/mnt/d/core-cpp/src/core/async/AsyncQueue_test.cpp:369: FAILED:
  CHECK_FALSE( queue.hasWaiter() )
with expansion:
  !true
/mnt/d/core-cpp/src/core/async/AsyncQueue_test.cpp:373: FAILED:
  CHECK( out.end == ConsumerEnd::Cancelled )
with expansion:
  0 == 2
core-cpp-async-test: AsyncQueue.hpp:144: ~AsyncQueue(): Assertion
  `!_waiter.resume && "AsyncQueue destroyed with a consumer still parked on it"' failed.
due to a fatal error condition:
  SIGABRT - Abort (abnormal termination) signal
```

— the cancelled consumer stays parked forever and the queue's own teardown assertion fires.

GREEN: `All tests passed (567 assertions in 77 test cases)`, on `core-cpp.async` and
`core-cpp.async-fallback` alike. Four cancellation cases: a parked consumer cancelled, a flow
already cancelled before its `pop` parks, an item already queued beating a cancellation, and a
`close()` beating one.

### 9. The `#1041` compile-time check

`static_assert`s in `ParkedWork_test.cpp` over `IExecutor`, `QueuedExecutor`, a type with both
overloads and a type with only the borrowing one, plus one line in `ThreadPoolExecutor_test.cpp`
for the real pool. See the concern about the negative control below.

### 10. The runner collapse

RED for the one behaviour the collapse could have changed silently — `WhenAll_test.cpp`,
*whenAll rethrows a child's cancellation like any other escape*, with `whenAny`'s classification
wrongly applied to `whenAll`'s policy:

```
/mnt/d/core-cpp/src/core/async/WhenAll_test.cpp:188: FAILED:
  CHECK( caught )
with expansion:
  false
```

GREEN: `All tests passed (318 assertions in 78 test cases)` on both binaries.

---

## The three deferred defects

### 1. `await_resume()` / `result()` answering a null handle with `T {}`

**Decision: refuse it, with a `std::logic_error` from one place
(`detail::refuseEmptyTask()`), for `Task<T>` and `Task<void>` alike.**

Why a throw rather than an assertion, given that `cpp-guidelines.md` says a precondition violation
is an assertion:

- The empty state is one the type admits **by design** — default-constructed, moved from, released
  — and `done()` answers `true` for it. So the question is reachable through the documented guard,
  not only through undefined behaviour. `std::optional::value()` answers exactly this question
  exactly this way.
- An assertion is unobservable in a release build and a process abort in a debug one, so the case
  would have to be a death test; core-cpp has no death-test facility, and
  `.agent/rules/testing.md` asks a case to assert what distinguishes.
- The module already refuses a sibling precondition this way: `syncRun` throws `std::logic_error`
  for a task still suspended after its resume, which is fastcached's own decision and which the
  dispatch asked to be ported verbatim. Two answers to "you drove a task that has no answer" would
  be worse than one.
- `Task<void>::result()` returned silently for an empty task. It throws now, because a `Task<void>`
  and a `Task<int>` must not disagree about what an empty task means.

The `T {}` is gone from every path, so `T` no longer has to be default-constructible — which is the
requirement the dispatch asked to remove, and is asserted by a `static_assert` beside the case.

Both halves are recorded under **Breaking** in `CHANGELOG.md` with a migration note.

### 2. `makeWhenAllRunner`'s try/catch duplicating its own promise's `unhandled_exception()`

Fixed by the collapse (commit 2): the runner body is `co_await std::move(task);` and the promise's
`unhandled_exception()` is the one place what escaped is recorded and classified
(`detail::isCancellation`). The wrapper no longer wires the state twice either — the awaiter wires
each runner once, in the start loop.

### 3. The ~200 lines of copy-paste between `WhenAll` and `WhenAny`

Collapsed into `Join.hpp`, in its own commit, after the graft was green on every preset. Line
counts: `WhenAll.hpp` 221 → 118, `WhenAny.hpp` 327 → 182, plus `Join.hpp` 292 — 548 → 592 lines
total, but the duplicated *logic* is one copy instead of two, and `Join.hpp` carries the comments
that used to be repeated in both files.

A policy supplies: the extra join state, the parent-cancellation registration and how to arm it,
the token each child observes, and the latch step. `whenAll` latches nothing and hands each child
the awaiting coroutine's own token; `whenAny` latches the first child to *complete*, requests stop
on its own child source and arms the parent bridge.

**`whenAny`'s cancellation-bridge ownership rule is intact.** The state is reference-counted for
every join now, `WhenAnyCancelBridge` still takes a `shared_ptr` copy on its own stack before
`request_stop()`, and the final awaiter still copies the state out of the promise before the latch
step. `WhenAny_test.cpp` passes unchanged on both `StopToken` branches, including the case that
resumes a coroutine from inside a stop callback and the destroyed-mid-`request_stop()` race.

---

## core-cpp#15 (symmetric-transfer depth)

**It cannot be closed or narrowed by this task.** Whether `final_suspend`'s symmetric transfer is a
tail call is a property of the compiler's sibling-call optimisation (GCC, only at `-O2` and better)
and of WebAssembly's tail-call support (`-mtail-call`). The ownership graft changed *who owns a
frame*, not how control is transferred, so the depth at which the 100000-frame chain overflows is
the same before and after. I did not add a second deep-chain case; the existing one, with its
`CORE_ASYNC_SYMMETRIC_TRANSFER_IS_TAIL_CALL` skip, is the only one. `docs/modules/async.md` now
records this answer where it used to say "to be decided in Task B1".

One thing the graft *did* change about teardown, recorded in the same paragraph: a chain destroyed
before it completes is now unwound by the awaiters rather than by the `Task` locals. It is still
plain recursion, one frame per level, so the bound is unchanged.

---

## `.agent/rules/async-and-net.md`

A new section, **Task ownership**, sitting between the event-loop contract and the socket rules,
because these are about coroutine frames rather than sockets. Each rule has a case behind it in
`src/core/async/{Task,ParkedWork,AsyncQueue}_test.cpp`:

- the awaiter owns what it awaits, and ownership runs downward
  ([fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025));
- `unownedRoot` is set at every `await_suspend`, by every coroutine type in the module — including
  the combinator runners, which is the hole this task found;
- a task that owns no frame has no result, and `syncRun`/`syncRunWith`
  ([fastcached#178](https://github.com/LASTRADA-Software/fastcached/issues/178));
- an executor resumes what it is handed or frees it, never neither;
- both `IExecutor::submit` overloads are pure, which is what closes the hiding hazard, with
  `using IExecutor::submit;` and two diagnostics behind it — see the fix round, where this bullet
  was corrected ([fastcached#1041](https://github.com/LASTRADA-Software/fastcached/issues/1041));
- a queue never resumes its consumer inline;
- a stop callback is registered before the park is published and never under the lock it takes;
- where a cancel and an answer arrive together, the answer wins;
- a pool that is stopping drains rather than drops.

The Status paragraph now says the section is live code rather than a forecast.

**A finding worth keeping:** `-Woverloaded-virtual` — on **GCC and clang alike**, gated by
`CORE_CPP_GCC_OR_CLANG` in `cmake/CoreCppToolchain.cmake`, not GCC alone as I first wrote — and
clang-tidy's `bugprone-derived-method-shadowing-base-method` each refuse the `#1041` hiding shape
outright, and both are errors under core-cpp's warning set. That is two diagnostics ahead of the
compile-time check, and it is now written down. **Corrected in the fix round:** neither is the
real guard. Both `submit` overloads are pure virtual, so the hiding class never becomes concrete.

---

## Verification

| Gate | Result |
|---|---|
| `python scripts/clang-format.py` (22.1.8) over every file touched | clean |
| clang-tidy 22.1.8 over every `core::async` TU (headers included via `HeaderFilterRegex`) | clean |
| WSL `clang-debug` (own tree) | `core-cpp.async`, `async-fallback`, `async-link-smoke` pass; 318 assertions × 2 binaries |
| WSL `gcc-release` | pass |
| WSL `clang-asan-ubsan` (LeakSanitizer on) | pass |
| WSL `clang-tsan` | pass |
| Windows `cl-debug` | pass |
| Windows `clangcl-release` | pass |
| Full suite, WSL gcc-release and asan | 19/19 (excluding `core-cpp.tui`, `cmake-hygiene` and `vendor-selftest`, see Concerns) |
| `ctest -L hygiene` provenance rule over my files | clean (the failures left are `src/core/net/`, not mine) |
| `mkdocs build --strict` | clean |
| CI | see below |

Two things the sanitizers and GCC caught that clang-debug did not, both fixed before the commit:

- **LeakSanitizer**, one 72-byte allocation: my own `release()` case released a frame and never
  destroyed it. The case now destroys the released handle and asserts the sentinel count on both
  sides, which is a better statement of what `release()` means anyway.
- **`-Woverloaded-virtual`** refused the `#1041` negative control, which derived from
  `IExecutor` and re-declared one overload — the defect, on purpose. Rewritten without inheritance
  (see Concerns). I met it on GCC first and wrote it down as GCC's; clang has it too, under the
  same `CORE_CPP_GCC_OR_CLANG` row.

The Emscripten leg is CI's: no emsdk is installed on this machine. **(Wrong, and corrected in the
fix round: one is, at `~/emsdk`. The leg now runs locally, 3/3.)** What was done for it:
`ThreadPoolExecutor.hpp` carries the `#error`, is appended to the header list only when
`CORE_CPP_USE_THREADS`, and its test likewise; `AsyncQueue_test.cpp`'s cross-thread case is
compiled only where a second thread exists and uses `std::thread`, not `std::jthread` (which needs
`__cpp_lib_jthread` — exactly what `StopToken.hpp` has a fallback for). Nothing else in the module
names a thread.

---

## Concerns

1. **The `#1041` negative control is no longer the defect's own shape.** I wrote it as an
   intermediate interface deriving from `IExecutor` that re-declares `submit(handle)`, and both GCC
   and clang-tidy refuse that outright under core-cpp's warning set. There is no way to express it
   without a `NOLINT` or a diagnostic pragma, both forbidden. It is now a pair of plain types — one
   with both overloads, one with only the borrowing half — which still proves the concept
   distinguishes, with a comment recording that two tools refuse the inheriting form. I judged two
   real diagnostics worth more than a faithful reproduction; say so if you want it the other way.

2. **`syncRunWith`'s deliberate-leak branch is untested**, as it is upstream: the branch exists for
   a task whose retriever did *not* wake it, and exercising it would leak a frame under ASan by
   design. The woken branch is tested.

3. **`whenAll` now allocates one `shared_ptr` control block per join**, where its state used to be
   a member of the awaiter. `whenAny` already did. It buys one spelling of the rule that keeps a
   stop state alive across its own `request_stop()`, and it is small beside the one coroutine frame
   per child that `whenAll` already allocates — but it is a real change and worth a second opinion.

4. **`AsyncQueue` uses `std::mutex` and `std::atomic` in the WebAssembly subset.** The Global
   Constraints forbid `std::thread`, `std::jthread`, blocking waits and `Threads::Threads` there,
   and none of those appear; a mutex is uncontended and lock-free under single-threaded Emscripten,
   and `core::async` links `Threads::Threads` only when `CORE_CPP_USE_THREADS`, so
   `tests/consumer-wasm`'s "nothing links threads" scan is unaffected. The `emscripten` CI leg is
   what proves it.

5. **Another agent's work in `src/core/net/` is mid-refactor and does not build.** Reported, not
   touched, as the dispatch asks:
   - `core-cpp.cmake-hygiene` fails on `src/core/net/{IoBackend.hpp,detail/ReadyBatch.hpp,
     detail/WaitTimeout.hpp,detail/WakeupChannel.hpp,posix/PollBackend.*,linux/EpollBackend.*,
     bsd/KqueueBackend.*}` having no provenance row, and on a C-style `for` at
     `src/core/net/detail/ReadyBatch.hpp:121`. Every `src/core/async/` row of mine is accepted.
   - On Windows, `src/core/net/EventLoop_test.cpp` includes a deleted `core/net/PollEventSource.hpp`,
     and `BackendParity_test.cpp` / `AsyncBufferedReader_test.cpp` do not compile against the
     half-landed `ScriptedBackend`. The async targets build and pass on both Windows presets.
   - `core-cpp.tui` fails in `GenericSyntaxHighlighter_test.cpp:955`
     (`REQUIRE(shipped.size() == golden.size())`) with uncommitted edits in that file in the tree.
   - `core-cpp.migrate-renames` and `core-cpp.migrate-codemods` fail; they come from an uncommitted
     `tools/` directory and an uncommitted `tests/CMakeLists.txt`.

   None of this is reachable from `core::async`, and my two commits touch none of those files. The
   pushed CI run will show whichever of them had landed on `master` when it started.

6. **A push from another agent cancelled the Build run for my own commit.** `35541946544`, pushed
   for `f72cabc`, was cancelled by the workflow's concurrency group a minute later when
   `a8e5212 tools(migrate): ...` landed. Its Docs run, `35541946451`, completed green. The Build
   evidence is therefore run `35542004684`, which is `a8e5212` — my two commits plus that one — and
   run `35542238141` for `7838b53`. That is a property of a shared branch rather than of this
   task, but it means no Build run exists whose head is exactly one of my commits.

---

## CI

| Run | Head | Result |
|---|---|---|
| Docs `35541946451` | `f72cabc` (mine) | success |
| Build `35541946544` | `f72cabc` (mine) | **cancelled** by the concurrency group when another agent pushed a minute later |
| Docs `35542004676` | `a8e5212` (mine + one other) | success |
| Build `35542004684` | `a8e5212` (**both my code commits** + one other) | **success**, all 23 jobs |
| Docs `35542564701` | `78bf537` (mine, docs only) | success |
| Build `35542238141`, `35542564691` | `7838b53`, `78bf537` (mine, docs only) | cancelled by later pushes from other agents |
| Build `35542633374` | `fa7b569` (**all four of my commits** + three others) | **success**, all 23 jobs |

**Two full greens cover this task.** `35542004684`'s head `a8e5212` contains `b21229d` and
`f72cabc`, which are every line of code this task wrote; `35542633374`'s head `fa7b569` contains
all four of my commits. The branch is contested — four other agents pushed during this task, and
the workflow's concurrency group cancelled two of my own runs — so neither green has a head that is
exactly one of my commits, and that is worth knowing when reading them.

Run `35542004684` job by job, all green:

```
consumer-smoke (cpm)        consumer-smoke (vendored)   consumer-smoke (wasm)
coverage                    emscripten (emsdk 3.1.56)   emscripten (emsdk latest)
linux (clang-22)            linux (clang-22-arm64)      linux (clang-22-cxx26)
linux (clang-22-tracy)      linux (gcc-14)              linux (gcc-15)
macos (appleclang)          macos (llvm-22)             sanitizers (clang-asan-ubsan)
sanitizers (clang-tsan)     style                       windows (cl-debug)
windows (cl-release)        windows (cl-release-tls)    windows (clangcl-release)
compile-cache
```

The three legs this task most needed are among them: **both `emscripten` jobs** (the
`ThreadPoolExecutor` exclusion, and `AsyncQueue`'s mutex and atomics in the subset),
**`macos (appleclang)`** (the `StopToken` fallback branch, which is why `ThreadPoolExecutor` uses
`std::thread` rather than `std::jthread`), and **`consumer-smoke (wasm)`**, which is the
"nothing links threads" scan a new header could have broken.

---

## Follow-ups after the graft landed (`66dfc9a`)

Three gates arrived on `master` while this task ran, each finding something real:

- **`tools/migrate/renames.json`** (`ddddc67`) had a *pending* `FastCache::SyncRun` row with no
  target, so its pending arm checked nothing — the R74 shape. It is delivered now, with 31 rows
  behind it for the rest of B1's map. Two facts the table cannot carry, each recorded as a note on
  its type's row instead: **`AsyncQueue::Close`**, because a member row keys on `(profile, name)`
  and `ISocket::Close` already holds that name; and **`AsyncQueuePush::accepted`**, whose type
  changed with its name (`AsyncQueueAdmission`), so every use site needs a human — the same reason
  `core::tui::LanguageId::Endo` has no rewrite row. `syncRunWith` likewise has no row: the table
  keys on the source symbol and `FastCache::SyncRun` is already spoken for, so a two-argument call
  is left to fail to compile, which names the site. **That keying is worth knowing for B4–B11**: a
  member name shared by two classes can be recorded once per profile, whatever its scopes.
- **`scripts/check-upstream-drift.py`** could not read this module's `ThreadPoolExecutor` row: its
  upstream path was the pattern `ThreadPoolExecutor.{hpp,cpp}`, which `git log -- <path>` cannot
  take. The row was not drifting — it had **stopped being checked**. It names the header alone now,
  with the `.cpp` as a merge partner in the notes, and the gate is green: *350 rows checked, 350 up
  to date, 0 drifted.*
- **The header list is literal again** inside `core_cpp_add_module()`, with only the threads-only
  header behind a `set()` variable of its own. `tools/migrate/check-renames.py` resolves a `${...}`
  in `HEADERS` through a `set()` beside it but not through a `list(APPEND)`, so my earlier form made
  every `core::async` rename row read as naming a private header.
- **`core-cpp.async-link-smoke` is bounded at 60 seconds.** `core_cpp_add_test()`'s 300-second
  default (`a13070c`) does not reach a bare `add_test()`, and ctest's own 1500 names neither the
  test nor what it waited for. 60 rather than 300 because the program links and runs a handful of
  `StopToken` operations: it finishes in milliseconds or it is broken.

Verified after the change: `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan` and
`cl-debug` all pass `ctest -L async`; the link smoke carries `TIMEOUT 60.0` in ctest's own listing;
`check-renames`, `check-upstream-drift` and both their self-tests pass, as do `rewrite_test.py` and
`semantic_rename_test.py`; `core-cpp.cmake-hygiene` now passes outright. The only `-L hygiene`
failure left is `core-cpp.migrate-renames` on **ten `src/core/net/` rows** that belong to Task B3's
in-flight work (seven deleted `*EventSource.hpp` includes, and `core::net::{IoBackend,Interest}` and
`makeDefaultBackend` now existing against a row still marked pending).

Three of the messages that prompted this round were stale by the time they arrived — the pinned
`clang-format --check` is clean over all 391 files, the `-Woverloaded-virtual` break was fixed
before the first push (the lead's own later message confirms `gcc-release` green on it), and the
missing provenance rows landed with the graft. `gcc-release` was in my loop from the first build
round; it is what caught that warning here rather than in CI.

---

## The controller's rulings, answered (`d7a7bee`)

- **Concern 2's requirement is met.** `detail::refuseEmptyTask()` now opens by saying it reports a
  **precondition violation, not a recoverable error**: an assertion that survives a Release build,
  where `assert` would hand back the silently wrong value this change removed, and not something to
  catch — a `catch` around `result()` would make the empty state a supported path rather than a call
  to fix. It also says why it is not `OperationCancelled`, which *is* written to be unwound through.
  Each of the four `@throws` lines points at it, and `docs/modules/async.md` and
  `.agent/rules/async-and-net.md` say the same, those being the other two places a caller looks.
- **Concerns 1 and 3** needed no code; the rulings match what shipped.

### On the brace row: it was already fixed, and the failing run predates the fix

The controller asked twice for `.agent/reference/provenance.md`'s `ThreadPoolExecutor.{hpp,cpp}` to
be committed. It was committed, in `66dfc9a`, and pushed. Verified against the remote rather than
the worktree:

```
$ git show origin/master:.agent/reference/provenance.md | grep -c 'ThreadPoolExecutor\.{hpp,cpp}'
0
$ git merge-base --is-ancestor 66dfc9a origin/master && echo YES
YES
```

and no row anywhere in the table still carries a brace or a glob in its upstream cell. The cited
red run, `35544422449`, has head `18ee360`, which `git merge-base --is-ancestor 18ee360 66dfc9a`
confirms is an **ancestor** of the fix — so that run's tree genuinely did still hold the pattern.
`scripts/check-upstream-drift.py` against the current tree: *351 rows checked, 351 up to date, 0
drifted* (351 rather than the earlier 350, because `093d3a4` fixed a checker bug that skipped a row
whose notes quote a header — mine is such a row, and it passes).

What the worktree copy of that file holds beyond `origin/master` is the **net lane's** 25
uncommitted rows (`HostDrivenBackend`, `IHostScheduler`, `emscripten/`, `ManualHostScheduler`), none
of them mine. Which is R87's point exactly: the worktree is not a base.

### R87 and the index rules, as applied here

Every commit of this task was made with `git commit --only -- <pathspecs>`, and every one was
checked with `git show --stat` before pushing; none took a file it did not name. For this last one
the diff was taken against `HEAD` rather than the index (`git diff HEAD -- <files>`), which is the
R87-correct comparison, and every changed line in it was mine. No file of mine contains a CR byte.

---

# Fix round 1

Rulings R97 and R98 taken as written; 1 Critical, 3 Important and 6 Minor addressed, one of the
Minors argued rather than changed because it was already delivered. **R97 holds** — refcounting did
not move the contract, and the one thing it did move is recorded below as a finding of its own.

**One thing I could not resolve: R99.** `task-B1-fixround1.md`'s title reads *"Rulings R97–R99"*,
but only R97 and R98 are numbered in its body, and `progress.md:539-540` defines only those two —
`git grep R99` across the session's files finds it in that title and nowhere else. I3 and I4 carry
no ruling number. I have taken both as ordinary findings and done them in full; if R99 says
something I have not read, it is the one item of this round I have not answered.

## C1 — a fan-out's parks free their shared chain root exactly once

**Reproduced.** The review's reading was right, and its reproducer is now four cases in the suite:
two combinator ones at arity two (`whenAll`, `whenAny`), a raced variant, and one over the
primitives.

RED, `out/build/b1-asan`, with `AbandonState::release()` reverted to the pre-R97 behaviour of every
park destroying the root it was handed:

```
=================================================================
==555383==ERROR: AddressSanitizer: heap-use-after-free on address 0x7cd584de0188 at pc 0x5cc9aaba6142 bp 0x7ffe7efc03c0 sp 0x7ffe7efc03b8
READ of size 8 at 0x7cd584de0188 thread T0
...
0x7cd584de0188 is located 72 bytes inside of 184-byte region [0x7cd584de0140,0x7cd584de01f8)
freed by thread T0 here:
```

`addr2line` over the reading frame, the freeing frame and the free itself:

```
std::vector<core::async::detail::JoinRunner<core::async::detail::WhenAllPolicy>, ...>::size() const
  /usr/include/c++/15/bits/stl_vector.h:1119
(anonymous namespace)::parkTwoUnderWhenAll(IExecutor*, FrameSentinel, Counters*) [clone .destroy]
  ././../../../src/core/async/ParkedWork_test.cpp:229
std::__n4861::coroutine_handle<void>::destroy() const
  /usr/include/c++/15/coroutine:144
```

— the second park destroying a frame the first had already destroyed, and the read landing in the
dead awaiter's runner vector. Exactly the shape the finding names.

**Fixed by refcounting**, per R97. `ParkedWork::abandon` is no longer a raw
`std::coroutine_handle<>` but a `detail::AbandonClaim`: a copyable RAII reference into a
`detail::AbandonState` that holds the root, counts live parks, and destroys the root when the last
claim goes. `detail::claimOn(root)` builds that state once per chain, under a `std::once_flag` in
the `DetachedTask` promise, so N parks of one chain share one state; `detail::Parked::resume()`
disarms it first, because a chain being resumed is no longer abandoned. The invariant is stated
where `ParkedWork` documents `abandon`.

GREEN: `All tests passed (343 assertions in 83 test cases)` on `core-cpp.async` and
`core-cpp.async-fallback` alike, in `b1-clang-debug`, `b1-asan` (LeakSanitizer on), `b1-tsan`,
`b1-gcc-release`, `b1-cl-debug` and `b1-clangcl-release`.

### Two things the fix turned up that the finding did not name

1. **`ThreadPoolExecutor::submit(ParkedWork)` dropped the claim.** It delegated to
   `submit(work.resume)`, which discards `work` — harmless while `abandon` was a raw handle nobody
   released, and a **free of a live chain** the moment the claim refcounts. Every ASan tag failed
   with SIGSEGV until the pool's queue became `std::deque<detail::Parked>`, holding the claim until
   the work is resumed. This is the shape R97 implies but does not spell out: an executor that
   takes a `ParkedWork` and keeps only half of it.
2. **`disarm()` alone would have been permanent.** A chain resumed off one executor and later
   parked on a second could then never be freed — a leak traded for the double free.
   `AbandonState::rearm()` is called from `claimOn()`, so each fresh park re-arms the chain.

## I2 (R98) — the join's counters are atomic, and a cross-thread join is a case now

**Reproduced.** `ThreadPoolExecutor_test.cpp` gained *"A join whose children finish on a pool
completes exactly once"*: eight `Task<void>` children that each `co_await ResumeOn { pool }` before
finishing, joined by `whenAll` under a `DetachedTask`, on a four-thread pool. The file did not
mention `whenAll` before.

RED, `out/build/b1-tsan`, with `JoinState::remaining` and `latched` reverted to a plain
`std::size_t` and `bool` and the decrements to `--remaining`:

```
WARNING: ThreadSanitizer: data race (pid=507549)
  Write of size 8 at 0x720c00001360 by main thread:
  ...
  Previous write of size 8 at 0x720c00001360 by thread T4:
  ...
  Location is heap block of size 48 at 0x720c00001350 allocated by main thread:
SUMMARY: ThreadSanitizer: data race (.../core-cpp-async-test+0x14988a)
```

`addr2line` over the two writing frames names exactly the two sites the finding named:

```
core::async::detail::JoinRunner<WhenAllPolicy>::PromiseType::FinalAwaiter::await_suspend(...)
  ./../../../src/core/async/Join.hpp:168
core::async::detail::JoinAwaiter<WhenAllPolicy>::await_suspend<DetachedTask::promise_type>(...)
  ./../../../src/core/async/Join.hpp:292
```

— the child's final-suspend decrement on pool thread T4, against the start-phase guard's release on
the main thread, over the 48-byte join state. **And ctest reports it**: `core-cpp.async (Failed)`,
not a warning printed under a green run.

Fixed: `remaining` is `std::atomic<std::size_t>` with `acq_rel` on both decrements, `latched` is
`std::atomic<bool>` claimed through a new `JoinState::claimLatch()` built on `exchange`, and both
policies' `onChildFinished` go through it. The ordering argument is written above the struct: the
decrement returning 1 acquires what every other child released; `continuation` is written before
any child starts and read only by that last thread, which the start-phase `+1` guarantees;
`exception` is written only by whoever claims the latch.

GREEN: the same case under TSan, `All tests passed (343 assertions in 83 test cases)`, no warning.

**Q4 is settled as R98 says**: the `shared_ptr` stays.

## I3 — what actually closes the `#1041` hazard, and my propagated error

Taken in full, and the claim is now the stronger, true one: **both `IExecutor::submit` overloads
are pure** (`IExecutor.hpp:43,56`), so a concrete executor declaring only the borrowing half hides
the owning one, fails to override it, and stays abstract — unusable rather than silently leaky.
`using IExecutor::submit;` is belt and braces, and every one of them is a no-op today. The residual
shape is an **intermediate abstract** class declaring one half, which `-Woverloaded-virtual` and
clang-tidy's `bugprone-derived-method-shadowing-base-method` each refuse.

Corrected in four places in the tree: `ThreadPoolExecutor_test.cpp`'s comment, which had claimed
the `static_assert` was *"the one line that holds this executor to it"* — it is a reachability
check, and now says so; `IExecutor.hpp`'s class documentation, which said *"nothing would diagnose
it"*, true of the upstream defect and false here; and both `#1041` bullets in
`.agent/rules/async-and-net.md`.

The **"GCC is the only one of our three toolchains"** line is corrected wherever I wrote it down —
three places in this report — to `-Woverloaded-virtual` being gated by `CORE_CPP_GCC_OR_CLANG`
(`cmake/CoreCppToolchain.cmake:84`), so clang diagnoses it too. The negative control stays: it
proves the concept discriminates rather than accepting everything.

## I4 — `[[nodiscard]]` on `AsyncQueue::push()`

RED, a translation unit that discards the result, compiled with the module's own warning set
(`clang++ -std=c++23 -fsyntax-only -Wall -Wextra -Werror -Isrc`):

```
$ clang++ -std=c++23 -fsyntax-only -Wall -Wextra -Werror -Isrc out/b1-nodiscard-probe.cpp
$ echo $?
0
```

No diagnostic, exit 0: the discarded `push()` raised nothing.

GREEN, after the attribute, same command:

```
out/b1-nodiscard-probe.cpp:6:5: error: ignoring return value of function declared with 'nodiscard' attribute [-Werror,-Wunused-result]
    6 |     q.push(1); // a silent drop, discarded
      |     ^~~~~~~~~
1 error generated.
```

Nothing in the tree discarded it, so no call site changed. The CHANGELOG entry is amended in place
rather than given a Breaking row: the API is unreleased.

## The six Minors

1. **The empty-task throw must not be caught** — already delivered, in `d7a7bee`, which predates
   the review's read. `Task.hpp:121-127`: *"Do not catch it: a `try`/`catch` around `result()` turns
   'this task owns no frame' into a supported path"*, and *"Nothing in `core::async` is declared to
   throw it, and no caller should handle it"*. Argued, not changed.
2. **`displaced = 1` under `DropNewest`** — the field's documentation was the wrong half of the
   story. It now says what the number is under each policy: the items evicted to make room
   (`Accepted`), or **this** item (`Refused`). Both are one item lost, which is what a caller adding
   it to a loss counter is asking; the admission says which end went. The cumulative accessor's
   wording follows.
3. **The brace list** now reads `{Task,ParkedWork,AsyncQueue,ThreadPoolExecutor}_test.cpp`, and
   says the last is compiled only where `CORE_CPP_USE_THREADS` is on — so in the WebAssembly leg
   the resume-or-free rule and the cross-thread join are stated and not exercised.
4. **`AGENT.md`** now says the async merge landed in Task B1, names what it brought, and scopes
   what is left of Phase B to the networking half and B12.
5. **`docs/modules/async.md`** — both sentences cut back to what is known. The overflow depth is
   *not* measured here, and "unchanged" is now labelled an argument from what was edited rather
   than a number anyone took. The pre-completion teardown is stated **with** the caveat its sibling
   paragraph carries: it is plain recursion, one stack frame per level, no tail call collapses it,
   its depth is likewise unmeasured, and `Task_test.cpp` says in as many words that its case "never
   has one".
6. **Movability** — restored, and the asymmetry pinned in both suites. `JoinAwaiter`'s move is
   defaulted, so each awaiter gets what its own members allow: `whenAll`'s moves, and `whenAny`'s
   is **immovable by the standard**, because it owns a registered `StopCallback` whose stop state
   holds its address. RED for the fix was
   `static assertion failed ... 'std::is_move_constructible_v<core::async::detail::WhenAllAwaiter>'`.
   I then asserted the same of `whenAny` and it failed to compile — which is how I learned the
   asymmetry is a property rather than an oversight, so that suite asserts the true one, with the
   reason. Copies stay deleted on both.

## One more defect found while verifying, not in the review

**clang-tidy `performance-unnecessary-value-param`** now fires on the `#1041` negative control's
`submit(ParkedWork)`, and **`modernize-pass-by-value`** on `detail::Parked`'s constructor. Neither
was reachable before this round: `ParkedWork` stopped being trivially copyable the moment `abandon`
began refcounting, so a by-value parameter merely read became a real copy. `Parked` takes the
`std::move`. The control keeps its by-value signature — it is a control *for* a by-value signature
— and gains a body that sinks the parameter, with the reason recorded beside it. Worth noting for
B3/B4/B5: any executor written against `ParkedWork` from here on wants to move it, not copy it.

## Verification

| Gate | Result |
|---|---|
| `scripts/clang-format.py --check` over `src/core/async/` (22.1.8) | 23 files clean |
| clang-tidy 22.1.8 over every `core::async` source and test | clean, after the two fixes above |
| WSL `b1-clang-debug` | `core-cpp.async`, `async-fallback`, `async-link-smoke` — 3/3 |
| WSL `b1-gcc-release` | 3/3 |
| WSL `b1-asan` (LeakSanitizer on) | 3/3 |
| WSL `b1-tsan`, including the new cross-thread join | 3/3 |
| Windows `b1-cl-debug` | 3/3 |
| Windows `b1-clangcl-release` | 3/3 |
| **WebAssembly `b1-emscripten` under node** (emsdk at `~/emsdk`, which the first round did not find) | 3/3 |
| `ctest -L hygiene` | **14/14** at the time I ran it, with my commits in (see below) |
| `mkdocs build --strict` | clean |

**CI, under R73.** My own head's runs were superseded twice and reported `cancelled` with **zero
jobs**, which is the signature to distrust — so this is the run that actually covers the work:
**`35548899124`, head `455b21b`, completed `success`, 23/23 jobs, 0 failed.** `455b21b` is a
descendant of `c99ca38`, so it carries all four of the main commits, and its green includes
`style`, `clang-tidy`, `linux (clang-22 / clang-22-cxx26 / clang-22-arm64 / gcc-14 / gcc-15 /
clang-22-tracy)`, `sanitizers (clang-asan-ubsan, clang-tsan)`, `windows (cl-debug, cl-release,
cl-release-tls, clangcl-release)`, `macos (llvm-22, appleclang)`, **`emscripten (emsdk latest)` and
`emscripten (emsdk 3.1.56)`**, `coverage`, `compile-cache` and all three `consumer-smoke` legs.
The fifth commit `ceb471d` is a comment-only correction, covered by the next run on `870d12b`
(`35549323301`), which at the time of writing stands at 19 jobs green, 0 failed, with
`compile-cache`, `clang-tidy`, `coverage` and `windows (cl-release)` still running — every one of
them already green on `455b21b` with this code in. I am not claiming that run's result, only
saying where it stood.

**`ctest -L hygiene` after the fact:** re-run once the net/B4 lane pushed on top of me, it is
12/14 — `cmake-hygiene` on provenance rows for `src/core/net/{PlatformLoop.hpp,
detail/ParkTable.hpp, detail/WorkerIdentity.hpp, testing/TestLoop.hpp}`, and `migrate-renames` on
three rows (`core::net::EventLoop::submit`, `core::net::PlatformLoop`, `core::net::testing::
TestLoop`) whose targets now exist and want marking delivered. Every one of those is that lane's
in-flight work; none is mine, and I have not touched them. Reported, not fixed.

**The WebAssembly leg was worth running rather than leaving to CI.** This round put `<mutex>` and
`std::call_once` into `ParkedWork.hpp` and a `std::once_flag` into `DetachedTask`'s promise, and
that module's rule is that the wasm subset is single-thread safe. It compiles and runs there:
`core-cpp.async`, `async-fallback` and `async-link-smoke` all pass under node, with `StopToken is
core-cpp's fallback`. The first round reported the Emscripten leg as CI's because no emsdk was
installed on this machine; one is, at `~/emsdk`, and `source ~/emsdk/emsdk_env.sh` is all it needs.

**One environment note, not a code defect:** `b1-cl-debug` first failed to link with
`Task_test.cpp.obj : fatal error LNK1163: invalid selection for COMDAT section 0xAF5`, against
objects predating the two layout changes in this round. `--clean-first` cleared it and it has not
recurred. Same family as `AGENT.md`'s stale-object rule for `clangcl` trees, on a `cl` tree.

## The two gaps that are not mine

Left alone as instructed, and both confirmed by grep: nothing in `tests/cmake/` compiles a header
standalone, and `no-tsan` appears in `.github/workflows/build.yml:487` (`-LE no-tsan`) and in no
`set_tests_properties(... LABELS ...)` anywhere.

## R83, R84 and R87 in this round

Five commits, all `git commit --only -- <pathspecs>` except one, all followed by `git show --stat`,
none taking a file it did not name. No amend, no reset of HEAD or the worktree, no rebase, no
cherry-pick; the one integration was `git merge --ff-only origin/master`, which is not a rebase and
moved nothing.

The exception is `4d20406`, which needed `CHANGELOG.md`: that file also carried the B12b lane's
uncommitted Imported-table split. Built per R87 from `HEAD`'s blob
(`git show HEAD:CHANGELOG.md`) with my one substitution applied to *that*, never from the worktree
copy, hashed with `git hash-object -w` into a private `GIT_INDEX_FILE`, and committed from there.
Their hunk is still uncommitted in the worktree, untouched. Afterwards the shared index held stale
entries for the two paths that commit moved, so `git reset -- CHANGELOG.md
src/core/async/AsyncQueue.hpp` refreshed them from HEAD — the repair R84 permits, worktree
untouched. Verified: `git status` shows nothing staged. No file of mine contains a CR byte
(scanned with `grep -U $'\r'` over every changed path).

## Fix round 1, addendum: the three `AGENT.md` lines, and a red `origin/master`

`ba48a50 docs(agent): two lines that claimed completeness and nothing checked them` — the two
further lines the controller sent after the round closed, plus the `:26-27` one already in
`c99ca38`. Prose only, `AGENT.md` alone; `.agent/rules/testing.md` has the same defect in a worse
form and is A12's, untouched here.

- **`:130`** said *"one binary per module linked to `core::testing_main` (async has a second, over
  the StopToken fallback)"*. I counted rather than took the list: `ctest -N -L core-cpp` and the
  `core_cpp_add_test` calls agree — async 2, **net 4** (`net_types`, `net_backend`, `net`,
  `net_tls`), tui 2 (`tui_output`, `tui`), the other five 1 each. And there is a **sixth** binary
  the sentence does not cover at all: `core-cpp-async-link-smoke`, which is a bare `add_test` and
  deliberately does *not* link `core::testing_main` — that is the whole point of it, and it is
  mine. The line now says to count and points at the canonical rule.
- **Steps 4 and 5** took the controller's text verbatim. Worth recording why it was wrong: step 4's
  justification was *"step 5 means every change edits `CHANGELOG.md`"*, and `c99ca38` — the commit
  that wrote those words — has no CHANGELOG entry. Neither do three other commits of the same day.
  Step 4 keeps its other leg, which stands alone and is the better argument. **Its own new rule
  applied to itself:** `ba48a50` is a prose edit and earns no CHANGELOG entry; `mkdocs build
  --strict` was run anyway, per the step as rewritten, clean in 0.42 s.

### The verification that matters: a private worktree, and what it found

On the controller's advice I built in a throwaway worktree at `origin/master` rather than the
shared checkout. My own result is the clean one I wanted: **0 warnings, 0 errors, `core-cpp.async`
/ `async-fallback` / `async-link-smoke` 3/3, `All tests passed (343 assertions in 83 test cases)`
on both binaries** — uncontaminated by any lane's partial edits.

**But the premise was not quite right, and the difference is the finding.** The controller's note
said the shared *working tree* does not build because B4's `EventLoop.hpp` is ahead of its `.cpp` —
an uncommitted partial edit, which is expected and blameless. That is not what I hit. A clean
worktree at **`origin/master`**, configured from scratch, fails to compile:

```
src/core/net/testing/ScriptedBackend_test.cpp:40:48: error: cannot initialize a member subobject
of type 'platform::NativeHandle' (aka 'int') with an rvalue of type 'std::nullptr_t'
        handler = ReadinessHandler { .handle = nullptr,
```

`platform::NativeHandle` is `void*` on Windows (`Types.hpp:26`) and `int` on POSIX (`:48`), so
`.handle = nullptr` compiles on one platform and on no other. Introduced by
**`7e2e3ae fix(net): the scripted backend stops saying things no kernel would say`**, which is
**not** an ancestor of `870d12b` — the last completed green run — so no green covers it.

**And the reason nobody has seen it is a supersession, mine.** `7e2e3ae`'s own run
(`35549980333`) reports `cancelled`, superseded when I pushed `ba48a50` on top of it. That is the
zero-job cancellation pattern working exactly as documented, doing exactly the harm it is supposed
to warn about: the one head that would have caught this never ran, and three commits have landed
since. Not mine to fix — `src/core/net/` is another lane's — and reported rather than touched.

The general form is worth keeping, because it inverts the note I was given: **a red build in a
shared checkout is not evidence about your change — but it is not automatically someone's
uncommitted mess either.** Both times I have now looked, the answer came from `git log -- <the file
in the error>`, which costs one command and distinguishes "a lane is mid-edit" from "master is
broken and CI missed it". The second is the one that needs saying out loud.

### CI, finally settled

Both runs that cover this round completed green: **`35548899124` / `455b21b` — `success`, 23/23,
0 failed** (the four main commits), and **`35549323301` / `870d12b` — `success`, 24/24, 0 failed**
(all five, `ceb471d` included). `ba48a50`'s own run was cancelled by the next push; it is a
markdown-only commit, and `origin/master` is red for the unrelated reason above, so no run after
`870d12b` can be green on Linux until `7e2e3ae` is repaired.

**Repaired, by its own lane, within the hour:**
`8d05bb7 fix(net): NativeHandle is an int on POSIX, so the scripted probe cannot be nullptr`.
`origin/master` no longer contains `handle = nullptr`. Reporting it rather than fixing it cost
nothing and kept the file with the lane that owns it.

## The rule R97 earned, in the rulebook

`c5e2db6 docs(rules): a queue of submitted work holds detail::Parked, not a bare handle`. It was
not already in `.agent/rules/async-and-net.md` — line 98's *"resumes what it is handed, or frees
it"* names `detail::Parked` but says nothing about what a queue's **element type** must be, which
is the half that was a bug. Stated mechanically rather than as the incident, because
`EventLoop::submit`, `resumeSoon` and `schedule(timePoint, ParkedWork)` are being written against
`ParkedWork` now and can all take the same shape, and with the tell spelled out: **a `submit`
overload whose body names `work.resume` and nothing else has taken half of a two-part value.**
Placed directly under the resume-or-free rule, since it is that rule's mechanical corollary.

Cited to [fastcached#1025](https://github.com/LASTRADA-Software/fastcached/issues/1025) for the
origin of the question and to this task's own `ac0ff76` for where it was a bug — there is no
core-cpp issue for it, and I did not invent one.

Its own step 5 applied: a rulebook edit earns no CHANGELOG entry. `mkdocs build --strict` run per
step 4 as rewritten, clean in 0.43 s.

## Fix round 1, addendum 2: the residual risk I named, now tested

I closed the round saying `AbandonState::claim()`'s relaxed increment and the disarm/release
interleaving in `Parked::resume()` were *reasoned about rather than tested*, and handed those to
the re-review as where to push. Standing by was the moment to settle them instead.

`04472c5 test(async): two threads take claims on one chain, which nothing else did`.

**The gap was real.** Every claim in the suite was taken on the thread that started the join: a
child awaiting `ResumeOn` suspends on its starter's thread, so `claimOn` never ran concurrently
with itself. The new case has each of sixteen children park **twice**, so the second `claimOn` runs
on whichever pool thread resumed it — concurrently with a sibling's `claimOn`, with
`Parked::resume()`'s `disarm()`, and with a third claim's `release()`.

**Proven to reach that code rather than merely to pass**, with the prediction committed first:
make `AbandonState::_parks` a plain `std::size_t`, and

| Case | TSan races, symbolized |
|---|---|
| the new probe | `AbandonState::claim()`, `detail::parkedWorkFor`, `AbandonState::release()` |
| the existing single-park join | `AbandonState::release()` only |

**One half of my prediction was wrong, and it is the interesting half.** I expected the existing
case to show *no* race on `_parks`. It shows fourteen — because `Parked::resume()`'s local
`ParkedWork` is destroyed on the pool thread, so the **decrement** was already concurrent and
already covered. What was never covered is the **increment**, and the new case is the only thing
that reaches it. I had the right conclusion ("untested") for a reason that was half wrong.

With the real code: 21 runs under ThreadSanitizer and 21 under AddressSanitizer, clean, plus
`clang-debug`, `gcc-release`, `cl-debug` and `clangcl-release` — `All tests passed (346 assertions
in 84 test cases)`, up from 343/83. clang-format and clang-tidy clean. Test-only, so no CHANGELOG
entry, per the step 5 this round rewrote.

### One pre-existing thing noticed on the way, not mine and not touched

The Windows async binary reports **81** cases where Linux reports **84**, and nothing says so.
The three are `Task_test.cpp`'s *"Task captures and rethrows a body exception"* and
`WhenAny_test.cpp`'s two exception cases, all inside `#ifndef _WIN32` with a documented reason —
exception propagation through a coroutine frame crashes the Catch2 harness on MSVC. The reason is
sound and recorded; what is missing is that a **compiled-out** case reports nothing at all, where
`.agent/rules/testing.md` asks for `SKIP` precisely so a case that could not run says so. A
`TEST_CASE` whose Windows body is one `SKIP(...)` would cost three lines and make the counts agree.
Two of those files are under re-review right now, so I have reported it rather than reached for it.

### Correction: the concurrent decrement is not pre-existing, and nothing shipped

The controller drew a further inference from the half-wrong prediction above — *"if the decrement
was already concurrent under the old code, the ordering question is not something R97 introduced;
it has been live in shipped behaviour longer than either of us thought"* — and routed it to the
re-review. **It does not hold, on three counts, each checked rather than reasoned:**

1. **There was no counter before R97.** `git show ac0ff76^:src/core/async/ParkedWork.hpp` matches
   `_parks|fetch_add|fetch_sub|AbandonState|claim\(\)|release\(\)` **zero** times.
   `ParkedWork::abandon` was a plain `std::coroutine_handle<>`. There was nothing to decrement, so
   there was no ordering question about a decrement.
2. **The "existing case" is not old.** It is *"A join whose children finish on a pool"*, written in
   this same fix round for I2, minutes before the probe — and the mutation I measured (plain
   `std::size_t _parks`) mutates post-R97 code. Both sides of that comparison are hours old.
3. **Nothing has shipped.** `git tag` is empty and `CHANGELOG.md`'s only section is
   `[Unreleased]`. `ParkedWork.hpp` itself first appears in `b21229d` — **this task**.

So the relaxed `fetch_add` question is genuinely new with R97 and is not archaeology. What survives
of the finding is what it actually was: a **test-coverage** fact, not a shipped-hazard one.
Concurrent `release()` was already reached by the single-park case; concurrent `claim()` was reached
by nothing until `04472c5`.

There *was* a concurrency hazard in the pre-R97 code, and it is not this one: N parks of a chain
each destroying the same root from whichever thread dequeued them. That is C1, and R97 is its fix.
Calling the counter's ordering "pre-existing" would merge the fix with the defect it removed.

### The Windows `SKIP` change: held, but verified and ready

The controller approved it on substance and held it for sequencing — an unrelated housekeeping
change has none of the justification `04472c5` had for landing mid-review. Rather than leave it as
an intention, it is **written, verified on four toolchains and parked as a patch**, so landing it
when the re-review reports is one commit and no build cycle.

The `#ifndef _WIN32` stays — its reason is sound — and gains an `#else` whose body is a
`TEST_CASE` of the same name containing one `SKIP`. Only the silence goes.

Prediction committed before running: Windows goes from 81 cases to 84, reporting 81 passed and
3 skipped with exit 0; Linux is unchanged at 84 passed, 346 assertions. Both held exactly:

| Toolchain | Result |
|---|---|
| `cl-debug` (MSVC) | `test cases: 84 | 81 passed | 3 skipped`, `assertions: 334`, exit 0 |
| `clangcl-release` | `assertions: 334 | 334 passed`, exit 0 |
| `clang-debug` (Linux) | `All tests passed (346 assertions in 84 test cases)`, both binaries, 0 warnings |
| clang-format 22.1.8 / clang-tidy 22.1.8 | clean on both files |

The assertion counts differ by design (334 against 346): a skipped case contributes none. The
**case** counts now agree, which is the whole point — a reader comparing the two platforms is no
longer looking at 81 against 84 with nothing to explain it.

Patch parked at the session scratchpad as `windows-skip.patch`, 43 lines, based on `48af4e6`.
Both verification worktrees removed.
