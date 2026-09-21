# Task B1: `core::async` grafts — ownership, detached tasks, executors, AsyncQueue

Phase A is done: `core::async` today is contour's `Task`, `UniqueCoroHandle`, `Cancellation`,
`Awaitable`, `WhenAll`, `WhenAny` and our own `StopToken` (std or hand-rolled fallback), all
repaired by two gate passes. Phase B merges fastcached's async design into it. **You are the
first Phase B task**, and every later one builds on what you leave: B3's `IoBackend` and B4's
`EventLoop` are written against `ParkedWork` and `IExecutor`; B5's timers and B6's sockets await
through the ownership rules you set here.

Read the plan's Task B1 (`docs/superpowers/plans/2026-09-18-core-cpp.md:776-798`) and the design
spec's §2 items 1–6 (`docs/superpowers/specs/2026-09-18-core-cpp-design.md`) before you start.
Those two are your requirements; this file adds what they cannot know.

## Sources

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`** (`origin/master`, fetched
2026-09-20). Read every one as a blob, never from the working tree:

```
git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:src/FastCache/Async/<file>
```

| File | Lines | Holds |
|---|---|---|
| `Task.hpp` | 537 | `DetachedTask`, `TaskPromiseBase`/`FinalAwaiter`, `TaskAwaiterBase`, `Task<T>`/`Task<void>`, `SyncRun`, `SyncRunWith` |
| `Task_test.cpp` | 181 | |
| `ParkedWork.hpp` | 142 | `ParkedWork`, `Detail::Parked` |
| `ParkedWork_test.cpp` | 655 | |
| `IExecutor.hpp` | 54 | |
| `ResumeOn.hpp` | 51 | |
| `ThreadPoolExecutor.{hpp,cpp}` | 95 + 92 | |
| `ThreadPoolExecutor_test.cpp` | 182 | |
| `AsyncQueue.hpp` | 328 | |
| `AsyncQueue_test.cpp` | 365 | |

`DetachedTask` and `SyncRun` have no files of their own upstream — they live in `Task.hpp`.

Record the pin in `CHANGELOG.md` and `NOTICE` (both already carry the contour and endo pins;
fastcached's row is yours to add), and a row per imported file in
`.agent/reference/provenance.md` with this SHA.

## The merge: contour's Task is the base

Our `Task` stays the base — its `UniqueCoroHandle`, its stop token in the promise, its
`[[nodiscard]]`, its Doxygen. From fastcached it **gains**, per spec §2 item 6:

- `unownedRoot` propagation, set at every `await_suspend`;
- an awaiter that takes ownership from the rvalue `Task`;
- `release()`;
- no default-constructible-`T` requirement;
- `DetachedTask`, `syncRun`, `ParkedWork`/`detail::Parked`, `IExecutor` (with
  `using IExecutor::submit;` in every derived class — LASTRADA-Software/fastcached#1041),
  `ResumeOn`, `ThreadPoolExecutor`, `AsyncQueue` (stop-aware `pop`).

Apply the rename map: `IsReady`/`Native`/`Release` → `done`/`handle`/`release`, `SyncRun` →
`syncRun`, `Submit`/`Schedule` → camelBack, `CancellationSource`/`CancellationToken` →
our `StopSource`/`StopToken`, `Detail::` → `detail::`, `FastCache::` → `core::async::`,
`<FastCache/Async/X.hpp>` → `<core/async/X.hpp>`.

## Two rulings before you start

**R65 — `core::async` stays `KIND INTERFACE`; `ThreadPoolExecutor` is header-only.**
fastcached's `ThreadPoolExecutor.cpp` is 92 lines of worker loop and start/stop. Our module table
(spec §1, `AGENT.md`, `cmake/CoreCppModules.cmake`, `docs/modules/`) says this module is INTERFACE
and depends on "std only"; making it STATIC changes what every consumer links and what the
WebAssembly subset builds. Inline the source into the header. If you find a reason the body
cannot be header-only, stop and report it rather than changing the module's kind.

**R66 — `DetachedTask.hpp` and `SyncRun.hpp` are their own headers**, as the plan names them,
even though upstream keeps them in `Task.hpp`. Every other piece of this module's vocabulary is a
header of its own (`Awaitable.hpp`, `Cancellation.hpp`, `UniqueCoroHandle.hpp`), each header is
self-contained, and the public API is the `FILE_SET`.

## The three defects Task A11's gate pass deferred here

They are deferred to B1 precisely because this task rewrites the code that holds them. Each needs
a test.

1. **`Task.hpp` — `await_resume()` and `result()` return a default-constructed `T` for a null
   handle**, inventing a value and forcing every `T` to be default-constructible. B1 removes that
   requirement, so decide what a null handle yields instead and say why in the report. A moved-from
   or released `Task` is the ordinary way to reach it.
2. **`WhenAll.hpp` — `makeWhenAllRunner` duplicates its own promise's `unhandled_exception()`**
   with a try/catch and wires the state twice; `WhenAnyRunner` shows the single-source form.
3. **`WhenAll.hpp` / `WhenAny.hpp` — the two runners, states and awaiters are ~200 lines of
   near-identical copy-paste**, differing only in the latch step. One runner parameterised by an
   on-finish policy collapses both. Do this **after** the ownership graft lands and is green, in
   its own commit, so a regression has one suspect.

`WhenAny`'s cancellation-bridge ownership rule (A11's Critical finding 1) is load-bearing and
already tested — do not lose it in the collapse. Its test resumes a coroutine from inside a stop
callback, which is what the real awaitables do; the collapsed runner must still pass it on both
`StopToken` branches.

## Tests first, as always

Write the case, run it, capture the RED, then implement. The plan lists the set:

- fastcached's `syncRun` `logic_error` (a task that never completes);
- symmetric-transfer depth — **reconcile with what is already there**: `Task_test.cpp` has a
  100000-frame deep-chain case whose skip is driven by
  `CORE_ASYNC_SYMMETRIC_TRANSFER_IS_TAIL_CALL` from `src/core/async/CMakeLists.txt`, filed as
  core-cpp#15. Do not add a second one. Say in the report whether your ownership change lets #15
  be closed or narrowed;
- a non-default-constructible `T`;
- awaiting an rvalue `Task` empties it, and contour's "named local keeps owning after `co_await`"
  case updated to the new rule;
- `unownedRoot` propagates through the `whenAll`/`whenAny` runners;
- `ParkedWork` primitives;
- `ThreadPoolExecutor_test`;
- `AsyncQueue_test`, plus a stop-aware `pop` that throws `core::async::OperationCancelled`;
- a compile-time check that `executor.submit(ParkedWork{})` selects the owning overload (#1041).

`src/core/async/CMakeLists.txt` builds every StopToken-naming test twice — once with
`std::stop_token`, once with `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK`. Anything you add that names
a `StopToken` belongs in `_coreCppAsyncStopTokenTests` so both binaries get it. Read that file's
comments before editing it; they explain why the fallback is a separate binary (ODR) and why the
link smoke exists.

## WebAssembly

`core::async` is in the subset. `ThreadPoolExecutor.hpp` is the one exception:

```cpp
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    #error "core::async::ThreadPoolExecutor needs threads; single-threaded Emscripten has none"
#endif
```

Exclude it from the WebAssembly `FILE_SET` and from the header self-check, and keep its test out
of the subset's test sources. Everything else in the module stays in. Nothing may use a library
feature newer than libc++ 17 (emsdk 3.1.56) without a `__cpp_lib_*` guard. The `emscripten` CI job
must be green before this task is done.

## Then

- `python scripts/clang-format.py --check` (files you touched), the `clang-tidy` preset clean,
  `ctest -L hygiene`, `mkdocs build --strict` if a public header's comments or `docs/` changed.
- Local presets: WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`
  (`ThreadPoolExecutor` and `AsyncQueue` are why TSan matters here); Windows `cl-debug` and
  `clangcl-release`. Build `clangcl-release` before every push, not only `cl-debug` — clang-cl
  catches signedness and conversion warnings MSVC does not.
- `CHANGELOG.md` under `[Unreleased]`, with anything a consumer would notice under `Breaking`
  with a migration (the file's preamble says every break goes there — not `Changed`).
  `docs/modules/async.md` and `AGENT.md`'s module table if the module's dependency story changes.
- `.agent/rules/async-and-net.md` gains the lifetime rules you carry over from fastcached's
  `.agent/rules/wire-and-protocol.md` (§Socket and coroutine lifetime) and `AGENT.md`, each citing
  its origin as a full URL (`LASTRADA-Software/fastcached#NNNN`). The plan requires the rule to be
  written in the same task that implements it.

## Concurrency

**One other agent is working this checkout right now**: impl-A12, in `src/core/net/`. Yours is
`src/core/async/`. You share one working tree, one index and one local `master`.

- **Never `git pull --rebase`** — it refuses outright with another session's unstaged work, and
  stashing would take their edits. `git fetch origin`, then push; it is a fast-forward.
- Stage with explicit pathspecs, and for `CHANGELOG.md`, `NOTICE`, `.agent/reference/provenance.md`
  and `docs/` read **every hunk** with `git diff -- <file>` first and stage only your own with
  `git apply --cached` from a trimmed patch (`git add -p` is interactive and unavailable). Confirm
  with `git diff --cached -- <file>` and check `git show --stat` before pushing. Pathspecs protect
  against the wrong file, not the wrong hunk.
- Never run a formatter over a file another session is editing. Format what you touched.
- Report anything of theirs that looks broken; do not fix it.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B1-report.md`: RED/GREEN per test,
the decision for deferred defect 1 and why, whether core-cpp#15 can close, what you carried into
`.agent/rules/async-and-net.md`, and the CI run IDs. Return only status, the commit range, a
one-line test summary, and concerns.

Push, watch CI (`Build`, and `Portability` if you touched anything platform-shaped) to green.
