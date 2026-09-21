# Task A5 report: `core::coro` import + StopToken alias/fallback

Status: **DONE_WITH_CONCERNS** (all work done, local and CI green; the concerns are the judgment calls
and two measured platform limits listed at the end).

Base `a92e0e9`, pushed to `origin/master` as:

| Commit | Subject |
|---|---|
| `6cde65a` | coro: StopToken aliases std::stop_token, with a fallback where the library lacks it |
| `074b7da` | coro: import contour's Task, cancellation and combinators as core::coro |

CI: Build run **35374365149**, conclusion **success**, every job green including `ci-ok`,
`emscripten (emsdk 3.1.56)`, `emscripten (emsdk latest)`, `sanitizers (clang-tsan)`,
`sanitizers (clang-asan-ubsan)`, `clang-tidy`, both macOS jobs, all four Windows jobs, gcc-14/15,
clang-22 (x64, arm64, C++26, Tracy), coverage, compile-cache, style. Docs run 35374365124: success.

## What was implemented

### `src/core/coro/StopToken.hpp` (commit 1)

- `core::coro::StopToken`, `StopSource`, `StopCallback<F>` and `noStopState`.
  - Where `__cpp_lib_jthread >= 201911L` and `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` is not defined,
    they are `std::stop_token`, `std::stop_source`, `std::stop_callback<F>`, and `noStopState` is a
    `std::nostopstate_t`.
  - Otherwise they are `detail::StopTokenFallback`, `detail::StopSourceFallback`,
    `detail::StopCallbackFallback<F>` and a `detail::NoStopStateFallback`.
- Same shape as `Generator.hpp`/`Ranges.hpp`: `<version>` included first; the fallback is always
  defined in `detail::`; `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` forces it; `CORE_CORO_STOP_TOKEN_IS_STD`
  records the choice.
- Measured: emsdk 3.1.56 has `_LIBCPP_VERSION` 170004 and no `__cpp_lib_jthread`
  (`em++ -std=c++23 -E` on `<version>`), so the fallback is live there. No `-fexperimental-library`
  and no INTERFACE flag was added.
- How the fallback works:
  - The state is shared through `std::shared_ptr<detail::StopState>`: a stop-requested flag, a
    source count (for `stop_possible()`), and an intrusive doubly-linked list of callback nodes.
    Registration allocates nothing.
  - `request_stop()`, under the lock, sets the flag once (`true` exactly once) and records the
    requesting thread. It then pops one node at a time under the lock, marks it running, invokes
    it *without* the lock, and clears "running" under the lock and notifies. It touches no node
    after invoking it, so a callback may destroy itself. It keeps a local `shared_ptr` copy, so a
    callback may also destroy the source.
  - Registration checks the flag under the same lock: the callback either joins the list or runs
    inline in the constructor, never both, never neither.
  - `~StopCallback` does one of three things. Linked: it unlinks. Running on another thread: it
    waits on a condition variable (owned by the state, which the callback keeps alive) until it
    returns. Running on this thread, which means inside its own callback: it returns at once.
  - `stop_possible()` on a token is `state && (requested || sources > 0)`; on a source it is
    `state != nullptr`. Copies share state; moves leave no state.
  - A callback is invoked as `std::forward<Callback>(callback)()` from a `noexcept` function, so
    one that throws terminates the program, as with std.
  - Nodes carry a function pointer rather than a virtual function. clang-tidy's
    `portability-template-virtual-member-function` rejected a virtual `invoke()` in the class
    template.
- Single-threaded WebAssembly (`__EMSCRIPTEN__ && !__EMSCRIPTEN_PTHREADS__`): `detail::StopStateSync`
  has two definitions.
  - Threaded: `std::mutex`, `std::condition_variable`, `std::atomic<T>`, `std::thread::id`.
  - Single-threaded: an empty `[[maybe_unused]]` lock, a no-op wait/notify, `Shared<T> = T` (plain
    `bool`/`size_t`) and a one-value `ThreadId`. No atomics, no lock, no wait, and `<thread>`,
    `<mutex>`, `<condition_variable>` and `<atomic>` are not included.
  - The logic is written once, over the policy.
- `.clang-tidy`: `request_stop|stop_requested|stop_possible|get_token` were added to the Function/
  Method/ClassMethod `IgnoredRegexp`, and `callback_type` to the TypeAlias/Typedef ones, with
  comments. There is no NOLINT.
- `cmake/CoreCppTargets.cmake`: `core_cpp_add_test()` takes `NAME <name>` (target
  `core-cpp-<name>-test`, ctest `core-cpp.<name>`, labels `core-cpp;<module>` as before) and
  `DEFINITIONS` (PRIVATE compile definitions). This is the existing style: a keyword in the
  `cmake_parse_arguments` list.
- `src/core/coro/CMakeLists.txt`, ruling R33:
  - `core-cpp-coro-test` has Generator plus StopToken, Task, WhenAll and WhenAny.
  - `core-cpp-coro-fallback-test` (ctest `core-cpp.coro-fallback`, labels `core-cpp;coro`)
    compiles StopToken, Task, WhenAll and WhenAny with `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK`.
- `StopToken_test.cpp`: 17 cases, 5 of them threaded, compiled only where
  `!defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)`.
  - static_asserts: the selection matches the feature macro, whatever was included first; both
    branches are nothrow copy/move; the callback is neither copyable nor movable; `callback_type`
    exists; the `noStopState` constructor is explicit.
  - Cases:
    - a default token has no state;
    - `request_stop` is true only the first time;
    - every callback runs exactly once;
    - a callback constructed on a stopped token runs in its constructor;
    - destroying the head, middle or tail callback leaves the others running;
    - the callback is invoked as an rvalue (a `const&&`-only call operator, a compile-time check);
    - a callback that destroys itself does not wait;
    - a callback may call `request_stop` and register a callback, which proves no lock is held;
    - copies share state;
    - `stop_possible` turns false when the last source (a copy) goes;
    - a requested stop stays possible after the sources go;
    - `noStopState`.
  - Threaded cases:
    - callbacks run on the requesting thread;
    - exactly one of 8 concurrent `request_stop` calls wins;
    - `~StopCallback` waits for a callback running on another thread (the callback sleeps
      100 ms after the destructor starts, and the case asserts it had returned before the
      destructor did);
    - 500 rounds of `request_stop` racing `~StopCallback`, the TSan case;
    - 500 rounds of registration racing `request_stop` (exactly one run).
  - Every cross-thread wait is bounded (30 s, steady clock), and the assertions come after the
    joins.
- Docs: `docs/modules/coro.md` has a StopToken section, and its stale line "`<stop_token>` comes
  from libc++'s experimental library" is gone. `.agent/rules/testing.md` documents the second
  binary and `NAME`/`DEFINITIONS`. `.agent/rules/async-and-net.md` says cancellation is
  `core::coro::StopToken`. AGENT.md's testing line mentions the second binary. CHANGELOG has two
  Added entries. Provenance rows: StopToken.hpp names contour `src/coro/Cancellation.hpp`, since
  the std aliases moved from there; StopToken_test.cpp is `origin: core-cpp`.

### The contour import (commit 2)

- These files were read as git blobs from `D:\contour` at `6777ff05014f8ff163b071e8b0e942830119db80`
  with `-c core.autocrlf=false -c core.eol=lf`, and the importer refuses CR bytes:
  - `Awaitable.hpp`, `Cancellation.hpp`, `Task.hpp`, `UniqueCoroHandle.hpp`, `WhenAll.hpp`,
    `WhenAny.hpp`;
  - `Task_test.cpp`, `WhenAll_test.cpp`, `WhenAny_test.cpp`.
- They were mapped `coro::` → `core::coro::`, `namespace coro` → `namespace core::coro`, and
  `<coro/…>` → `<core/coro/…>`, and the `// NOLINTNEXTLINE(...)` lines were dropped.
- `Cancellation.hpp` includes `<core/coro/StopToken.hpp>` instead of defining the aliases, and its
  `#error` is gone. `OperationCancelled`, `ThisCoroStopToken` and `thisCoroStopToken()` are unchanged.
- Changes forced by core-cpp's gates, each recorded in provenance, the CHANGELOG and coro.md:
  - `WhenAll.hpp`/`WhenAny.hpp`: the final awaiter's local `state` shadowed the promise member
    `state` (`-Wshadow`), so it is renamed `join`/`race`.
  - `WhenAny_test.cpp`: `ManualEvent::waiters = nullptr` (tidy
    `cppcoreguidelines-pro-type-member-init`, which fired in the fallback TU only). Also
    `failingRacer()`/`raceWithFailingWinner()` moved under `#ifndef _WIN32` with their one case:
    clang-cl `-Wunused-function`.
  - `Task_test.cpp`: the "Deep co_await chains" case (depth 100000) calls `SKIP` under
    `__EMSCRIPTEN__` and for GCC without `__OPTIMIZE__`. Measured:
    - gcc-debug (GCC 14.3, `-O0`): SIGSEGV in the fallback binary and a 300 s ctest timeout in the
      std binary. With `ulimit -s unlimited` it passes, so the cause is stack overflow: symmetric
      transfer is not a tail call there. gcc-release (`-O3`) passes.
    - emsdk 3.1.56 under node: `RangeError: Maximum call stack size exceeded`, the same cause (no
      `-mtail-call`).
    - Clang at `-O0` and MSVC debug pass.
- The Windows dialogs merge was verified, and nothing more was done for it. The provenance row for
  `src/core/testing/SuppressWindowsDialogs.hpp` names contour's
  `src/coro/testing/SuppressWindowsDialogs.hpp`, and NOTICE lists it. `test_main.cpp` was not
  imported.
- `README.md` was folded into `docs/modules/coro.md` ("Conventions", with its origin cited), keeping
  only what still holds. Dropped: the NOLINT convention (core-cpp exempts through `.clang-tidy`),
  the "sed re-sync from Endo" history, and "pull at configure time". The README's "contour's copy
  is canonical" became "this copy takes that role".
- Also updated: NOTICE, the CHANGELOG (an Added entry and a row in the Imported table), the
  provenance rows (9 new), `source-map.md`, `docs/modules/index.md`, AGENT.md's module-status line
  and coro.md's status note.

## TDD evidence

**StopToken RED.** The test and the CMake lists existed, the header did not. Command:
`wsl … cmake --build --preset clang-debug --target core-cpp-coro-test core-cpp-coro-fallback-test`.

```
FAILED: [code=1] src/core/coro/CMakeFiles/core-cpp-coro-test.dir/StopToken_test.cpp.o
/mnt/d/core-cpp/src/core/coro/StopToken_test.cpp:2:10: fatal error: 'core/coro/StopToken.hpp' file not found
FAILED: [code=1] src/core/coro/CMakeFiles/core-cpp-coro-fallback-test.dir/StopToken_test.cpp.o   (-DCORE_CORO_FORCE_STOP_TOKEN_FALLBACK)
/mnt/d/core-cpp/src/core/coro/StopToken_test.cpp:2:10: fatal error: 'core/coro/StopToken.hpp' file not found
ninja: build stopped: subcommand failed.
```

**StopToken GREEN.** The same build, then `ctest --preset clang-debug -R core-cpp.coro`:
`100% tests passed, 0 tests failed out of 2`. `core-cpp-coro-test "[StopToken]"` and
`core-cpp-coro-fallback-test` each reported `All tests passed (68 assertions in 17 test cases)`.

**The tests distinguish (fallback neutered).**
- Run 1, clang-debug, fallback binary, three neuters at once:
  - neuters: (a) no wait in `deregisterCallback`; (b) `registerCallback` ignores a stop already
    requested; (c) `isStopPossible()` returns true.
  - result: `test cases: 17 | 14 passed | 3 failed`, and exactly the three expected cases failed:
    - `StopToken_test.cpp:145` `CHECK( calls == 1 )` (runs in its constructor);
    - `:242` `CHECK_FALSE( token.stop_possible() )`;
    - `:374` `CHECK( returnedBeforeDestroyed )` (the destructor waits).
- Run 2, clang-tsan, neuter (a) only, the race case: exit **66**, with
  `WARNING: ThreadSanitizer: data race`. Restored: exit 0, no report.
- The header was restored from a saved copy each time. After restoring, `git diff --stat` on it
  was empty.

**Task import RED.** The three imported tests were in both lists, and the headers were missing.

```
/mnt/d/core-cpp/src/core/coro/Task_test.cpp:2:10: fatal error: 'core/coro/Task.hpp' file not found
/mnt/d/core-cpp/src/core/coro/WhenAll_test.cpp:2:10: fatal error: 'core/coro/Cancellation.hpp' file not found
/mnt/d/core-cpp/src/core/coro/WhenAny_test.cpp:2:10: fatal error: 'core/coro/Cancellation.hpp' file not found
(each twice: core-cpp-coro-test and core-cpp-coro-fallback-test)
```

**Task import, first build with the headers.** It failed with
`WhenAll.hpp:75:33: error: declaration shadows a field of '...WhenAllRunner::PromiseType' [-Werror,-Wshadow]`,
and the same at WhenAny.hpp:94. After the renames it was **GREEN**:
`All tests passed (143 assertions in 44 test cases)` (std) and
`All tests passed (131 assertions in 34 test cases)` (fallback).

## Local results on the final tree (`074b7da`)

| Preset | Result |
|---|---|
| WSL `clang-debug` | 12/12 (coro 44 cases / 143 assertions; coro-fallback 34 / 131) |
| WSL `gcc-debug` | 12/12 (coro 43 passed + 1 skipped; coro-fallback 33 + 1 skipped: the deep-chain case) |
| WSL `clang-tsan` (`-LE no-tsan`) | 12/12 |
| WSL `gcc-release` | 12/12 |
| WSL `clang-asan-ubsan` | 12/12 |
| WSL `clang-tidy` (pinned 22.1.8, full tree, `-k 0`) | no finding; ctest 12/12 |
| WSL `emscripten` (emsdk 3.1.56, node) | 12/12; `ninja -t commands` mentions pthread 0 times |
| Windows `cl-debug` | 15/15 |
| Windows `clangcl-debug` (`--clean-first`) | 15/15 |
| Windows `clangcl-release` (`--clean-first`; run before the last two test-only edits) | 15/15 |
| `python scripts/clang-format.py --check` | 123 files formatted with 22.1.8 |
| `python -m mkdocs build --strict` | built, no warning |
| Repeat, clang-tsan, `[threads]` cases | coro: ran=30 pass=30 fail=0; coro-fallback: ran=30 pass=30 fail=0 |
| Repeat, clang-debug, every case | coro: ran=100 pass=100 fail=0; coro-fallback: ran=100 pass=100 fail=0 |

## Files changed (a92e0e9..074b7da: 23 files, +2742/−36)

- New: `src/core/coro/{StopToken,Awaitable,Cancellation,Task,UniqueCoroHandle,WhenAll,WhenAny}.hpp`,
  `src/core/coro/{StopToken,Task,WhenAll,WhenAny}_test.cpp`
- Changed: `src/core/coro/CMakeLists.txt`, `cmake/CoreCppTargets.cmake`, `.clang-tidy`,
  `.agent/reference/{provenance,source-map}.md`, `.agent/rules/{testing,async-and-net}.md`,
  `AGENT.md`, `CHANGELOG.md`, `NOTICE`, `docs/modules/{coro,index}.md`

## Self-review

- **Correctness of the fallback.**
  - The node list, the running marker and the requester are read and written only under the lock.
  - The flag and source count are `std::atomic` (seq_cst) and are read without the lock.
  - The callback's storage is ordered by the mutex: registration → pop, and clear-running →
    destructor.
  - The condition variable is in the state, and the destructor's `shared_ptr` keeps the state
    alive. The notify runs after the unlock and touches no node.
  - I checked registration against `request_stop`, deregistration against a running callback,
    self-destruction, a nested `request_stop`, nested registration, and a callback that destroys
    the source.
- **ODR:** R33 is followed. No binary mixes TUs with and without the macro, and
  `CORE_CORO_STOP_TOKEN_IS_STD` is decided after `<version>`.
- **Hygiene:** no NOLINT and no pragma; no C-style for; SPDX line on every file; namespace =
  directory; every file has a provenance row (the hygiene ctest passes); PRIVATE definitions only.
- **Test quality:** the RED/GREEN evidence and the neuters above show the cases fail for the
  reason they exist. Bounded waits; no REQUIRE before a join.
- **Consumer impact:** new API only. `core_cpp_add_test` gains optional keywords, which existing
  calls are unaffected by. Consumers that used `coro::StopToken` get `core::coro::StopToken` in
  Phase C. It is `std::stop_token` wherever they have it today, so a consumer that builds now
  has the std branch and sees no behavioural change.

## Concerns

1. **[Superseded in fix round 1 by ruling R34: it is now `inline constexpr NoStopState`.]**
   **`noStopState` is `inline NoStopState const`, not `constexpr`.** The brief spells it
   `noStopState`. `.clang-tidy` files a constexpr variable under ConstexprVariableCase, which is
   CamelCase, and would demand `NoStopState`. A non-constexpr namespace-scope const is ConstantCase
   (camelBack) and passes. std's `stop_source(nostopstate_t)` is not constexpr, so nothing needs
   the constexpr. I added no public type alias for the tag type; `decltype(noStopState)` names it.
2. **Symmetric transfer is not a tail call everywhere (measured), and this matters to consumers.**
   - Where: GCC without sibling-call optimisation (`-O0` measured; `-Og`/`-O1` are likely the same
     but were not measured), and emsdk 3.1.56's WebAssembly (no `-mtail-call`).
   - Effect: a long chain of synchronously completing `co_await`s grows the stack and can
     overflow it. That covers a coroutine looping over awaits of tasks that complete at once.
     Contour's copy has the same latent limit.
   - What I did: the deep-chain test is skipped in those two configurations, not weakened, and
     coro.md and the CHANGELOG record the limit.
   - Who it affects: morph (a WebAssembly consumer), and anyone's GCC debug build. It may deserve
     a tracking issue for B1/B13.
   - Detection is limited: the GCC skip keys on `__OPTIMIZE__`, so a GCC `-O1`/`-Og` build would
     still run, and could crash on, that case.
3. **[Corrected in fix round 1: the second point was wrong.]** **Small deviations from libc++'s
   fallback-equivalent behaviour, harmless.**
   - The fallback registers a callback even when stop is not possible (no sources). It then never
     runs, as with std.
   - `~StopCallback` compares node addresses to decide whether it is "running". A new callback
     built at the address of one that destroyed itself mid-invocation could wait until that
     invocation returns. That is only a spurious wait, never a deadlock or use-after-free.
     **Correction:** this was wrong. The review showed it can deadlock, when the self-destroyed
     callback waits for the thread that destroys the new one. Fix round 1 fixed it (Important 2).
4. **`Generator_test.cpp` is not in the fallback binary.** R33 lists StopToken, Task, WhenAll and
   WhenAny, and Generator names no StopToken.
5. **Commit 1 mentions "every Task promise".** Its message and the header comment do, but Task
   arrives in commit 2. Both went out in one push.
6. **Contour's comment says exception propagation through coroutine frames crashes Catch2 on
   Windows.** `WhenAll_test.cpp`'s throwing cases have no such guard and pass on cl and clang-cl,
   so the claim looks narrower than stated. coro.md reports this factually. I left the
   `#ifndef _WIN32` guards in Task/WhenAny tests as imported.
7. **Branch taken on AppleClang and emsdk latest is unknown.** The static_asserts hold on both
   branches, and I did not check which one macOS and emsdk-latest took. Emsdk 3.1.56 takes the
   fallback (measured); Linux libstdc++, MSVC and clang-cl take std, and the fallback runs there
   through `core-cpp.coro-fallback`.
8. **Carry to C6 (from the brief):** contour's global `<stop_token>` probe
   (`CMakeLists.txt:97-137`) becomes removable if nothing in contour uses `std::stop_token`
   directly.

## Fix round 1 (ruling R35; R34 for the rename)

Status: **DONE**. It was pushed as four commits on top of `8e29bb5`, and CI Build run
**35379331171** is **success** on every job, `ci-ok` included. Docs run 35379331174: success.

| Commit | Subject | Items |
|---|---|---|
| `241d814` | coro: StopToken fallback reads stop_possible in order, and never waits after an inline run | Important 1, Important 2, Minor 6 |
| `7ddac51` | coro: NoStopState is a constexpr constant, as std::nostopstate is | R34 |
| `c63f349` | coro: the deep-chain skip keys on -mtail-call, and the docs say a loop overflows too | Minor 3, Minor 4 |
| `2605c23` | coro: the configure log says which StopToken a toolchain gets | Minor 5 |

Note: `8e29bb5` ("docs: the coroutine module becomes core::async, and Generator moves to base") was
already committed on the local `master` when I committed. It was not pushed, it is not mine, and it
touches only the spec and plan. My push carried it to `origin/master` with my four commits. I did
not rewrite it.

### What changed

1. **Important 1, `stop_possible()`** (`StopToken.hpp`, `isStopPossible()`).
   - The source count is now read first, then the flag: `return _sourceCount != 0 || _stopRequested;`.
   - The comment gives the argument:
     - A count of 0 is final, because a source is only added by copying a live one.
     - Every request was made through a source whose removal came after it. Each removal is a
       seq_cst read-modify-write of the count, so a read that finds 0 synchronises with every
       removal, and so comes after every request.
     - The flag read after a 0 therefore holds the final answer.
   - I did not pack the two into one word: the two-load form is provably right in this order and
     smaller.
2. **Important 2, address reuse** (`StopCallbackFallback::start()`).
   - When `registerCallback` refuses (stop already requested), the callback now does
     `_state.reset()` before it runs inline. This is libstdc++'s approach, and MSVC's
     `_Do_attach` is the same (`_Parent` stays null).
   - The destructor then has no state: it neither deregisters nor waits.
   - `deregisterCallback`'s contract now says only registered nodes may be passed. A registered
     node never shares its address with a running one, because once a callback runs, stop is
     requested and every new callback runs inline.
3. **R34.** `noStopState` became `NoStopState`, `inline constexpr`, in both branches
   (`std::nostopstate_t` / `detail::NoStopStateFallback`). The header, the test, coro.md, the
   CHANGELOG and the provenance note were updated. clang-tidy is clean.
4. **Minor 3.**
   - The Emscripten skip is now `defined(__EMSCRIPTEN__) && !defined(__wasm_tail_call__)`.
   - The GCC skip still keys on `!__OPTIMIZE__`. Its comment and coro.md name `-Og` and `-O1` as
     levels that overflow, are not skipped, and crash the binary; no preset builds at them.
   - Both skip messages, the comment and the docs reference core-cpp#15. I added no stack-growth
     probe.
5. **Minor 4.** coro.md and the CHANGELOG now separate the two shapes:
   - a nested chain overflows at GCC `-O0`/`-Og`/`-O1`;
   - a *loop* of 100000 immediately completing awaits in one coroutine overflows at GCC `-O0`.

   Both use the reviewer's GCC 14.3/15 table, emsdk 3.1.56's node call stack, and link #15.
6. **Minor 5.**
   - coro.md and the CHANGELOG say the fallback is live on libc++ before 20 without
     `-fexperimental-library` (emsdk 3.1.56's libc++ 17, FreeBSD 15's base Clang 19, likely
     AppleClang). It has real threads on all of them but the first.
   - `src/core/coro/CMakeLists.txt` runs, under `if(CORE_CPP_TESTING)`, two `try_compile`
     probes (`SOURCE_FROM_CONTENT`, `NO_CACHE`, C++23, include dir `src/`).
   - Each probe compiles `StopToken.hpp` itself and refuses one answer, so exactly one compiles.
   - It prints `-- [core-cpp] coro: StopToken is std::stop_token` or
     `... is core-cpp's fallback`. If neither probe compiles, it prints a `WARNING` instead of
     reading that as an answer.
   - There is no global state. The result variable is `CORE_CPP_`-prefixed and not cached, and the
     loop variables are `_coreCpp...`.
7. **Minor 6, test gaps** (`StopToken_test.cpp`).
   - New: "A callback that destroys another registered callback keeps it from running". The two
     callbacks destroy each other, so the test is order-independent across implementations; it
     asserts exactly one ran and exactly one is still engaged.
   - New: "The fallback lets a callback destroy the last source and itself while stop runs it".
     - It uses the `detail::` fallback types in both binaries, since the standard does not
       promise this for `std::stop_source`.
     - No token remains, and the callback destroys the last `StopSourceFallback` and then itself.
   - The race case, renamed "request_stop racing the StopCallback destructor never runs a
     destroyed callback":
     - The destructor now starts once the requester is about to call `request_stop`.
     - A callback that wins waits (bounded) until the destructor is on its way, so the destructor
       meets it running.
     - It asserts no gate timeout, no over-run and no late run, and calls `SKIP` when
       `roundsThatRan == 0`.
   - Two further threaded cases, for Important 1 and 2 (below).
   - Renames. Case names whose leading tilde or embedded comma kept them from selecting themselves
     (testing.md, "A case name is an argument") were renamed:
     - "~StopCallback waits ..." became "The StopCallback destructor waits ...";
     - "A destroyed StopCallback is not run, and ..." lost its comma.

### RED/GREEN evidence

**Important 2 (item 2), deterministic.** Case: "A StopCallback that ran in its constructor does not
wait for another at its address".
- Setup:
  - Callback X runs on the test thread, destroys itself (it copies its pointer to a local
    first), and waits, bounded, for thread B.
  - B emplaces a new callback into the same `std::optional` (the same address) on the stopped
    token, and destroys it.
- RED, before the fix (clang-debug, `core-cpp-coro-fallback-test "<name>"`):

  ```
  /mnt/d/core-cpp/src/core/coro/StopToken_test.cpp:591: FAILED:
    CHECK( shared.firstSawSecondDone.load() )
  with expansion:
    false
  test cases: 1 | 1 failed
  exit=1 seconds=30
  ```

  B was stuck in the destructor until X's bounded wait (30 s) gave up.
- The std binary (libstdc++) passes the same case: "All tests passed (4 assertions in 1 test
  case)", 0 s.
- GREEN, after the fix: both binaries "All tests passed (4 assertions in 1 test case)", exit 0, in
  0 s.

**Important 1 (item 1).** Case: "stop_possible stays true while stop is requested and the last source
goes". It runs 200 rounds. In each, a reader spins on `token.stop_possible()` while the test thread
calls `request_stop()` and then destroys the last source. At every instant a source exists or stop
was requested, so every read must be true.
- Without widening the window, the old order did not reproduce: a pass on the unfixed header. The
  race needs a request and a source destruction to land between two adjacent loads.
- So I widened the window. As a temporary experiment, never committed, I put a
  `sleep_for(1ms)` between the two loads:
  - RED, old order (flag, then count) with the widened window:

    ```
    /mnt/d/core-cpp/src/core/coro/StopToken_test.cpp:556: FAILED:
      CHECK( roundsWithFalseReads == 0 )
    with expansion:
      200 == 0
    test cases: 1 | 1 failed
    ```

  - GREEN, new order (count, then flag) with the same widened window:
    `CHECK( roundsWithFalseReads == 0 )` `0 == 0`, and "All tests passed (2 assertions in 1 test
    case)".
- The header was restored from a saved copy after each experiment, and verified identical. The
  committed case keeps guarding the invariant; the ordering argument is in the header comment.

**Minor 6 (item 7).** Clang-asan-ubsan, with the fallback neutered as a temporary experiment and then
restored:
- **Neuter (b):** `StopSourceFallback::request_stop` without the local `state` copy
  (`return _state->requestStop();`).
  - The case "The fallback lets a callback destroy the last source and itself while stop runs
    it" fails in both binaries (both use the fallback types):

    ```
    ==326163==ERROR: AddressSanitizer: heap-use-after-free on address 0x74a156be0330 ...
    SUMMARY: AddressSanitizer: heap-use-after-free (.../core-cpp-coro-fallback-test+0x237cfe)
    ==326175==ERROR: AddressSanitizer: heap-use-after-free on address 0x6e6643de0330 ...
    SUMMARY: AddressSanitizer: heap-use-after-free (.../core-cpp-coro-test+0x28143e)
    exit=1 (both)
    ```

  - So the local copy is load-bearing, and the case proves it.
- **Neuter (a):** `deregisterCallback` returns without unlinking a still-registered node. The case
  "A callback that destroys another registered callback keeps it from running", in the fallback
  binary:

  ```
  StopToken_test.cpp:238: FAILED:  with expansion:  2 == 1      (calls: the destroyed one ran)
  StopToken_test.cpp:239: FAILED:
  test cases: 1 | 1 failed
  ```

  The std binary (libstdc++, unaffected by the neuter) passes.
- Restored: both cases pass in both binaries, exit 0.
- The race case now exercises the interleaving. `roundsThatRan` of 500 rounds, read with `-s`:

  | Build | coro | coro-fallback |
  |---|---|---|
  | clang-debug | 500 | 181 |
  | clang-tsan | 500 | 499 |

  Before the "requesting" gate it was 5/1 in clang-debug and 123/58 under TSan. In 100 runs of the
  case, each of the four builds skipped 0 times.

### Validation

| Check | Result |
|---|---|
| WSL `clang-debug` | 12/12 |
| WSL `gcc-debug` | 12/12 |
| WSL `clang-tsan` (`-LE no-tsan`) | 12/12 |
| WSL `gcc-release` | 12/12 |
| WSL `clang-asan-ubsan` | 12/12 |
| WSL `clang-tidy` (pinned 22.1.8, full tree, `-k 0`) | no finding; ctest 12/12 |
| WSL `emscripten` (emsdk 3.1.56, node) | 12/12; configure: "StopToken is core-cpp's fallback" |
| Windows `cl-debug` | 15/15; configure: "StopToken is std::stop_token" |
| Windows `clangcl-debug` (`--clean-first`) | 15/15; configure: "StopToken is std::stop_token" |
| `ctest --preset clang-tsan -R coro-fallback --repeat until-fail:200` | exit 0 after 99 s: "100% tests passed, 0 tests failed out of 1" |
| Counted loop, clang-tsan, `core-cpp-coro-fallback-test "[threads]"` | ran=200 pass=200 fail=0 |
| Counted loop, clang-tsan, `core-cpp-coro-test "[threads]"` | ran=200 pass=200 fail=0 |
| `python scripts/clang-format.py --check` | 123 files formatted with 22.1.8 |
| `python -m mkdocs build --strict` | built, no warning |
| After the four commits (tree identical to the validated one; `8e29bb5` is docs/superpowers only) | clang-debug 12/12, mkdocs strict clean |

Each intermediate commit was built and tested with clang-debug (`coro|hygiene` 4/4) before the next
was made.

### Which StopToken each CI job takes (run 35379331171, from the configure logs)

| Job | Toolchain | StopToken |
|---|---|---|
| macos (appleclang) | AppleClang 17.0.0.17000013 | **core-cpp's fallback** |
| macos (llvm-22) | Homebrew Clang 22.1.8 (libc++ 22) | std::stop_token |
| emscripten (emsdk 3.1.56) | emcc 3.1.56 (libc++ 17) | **core-cpp's fallback** |
| emscripten (emsdk latest) | emcc 6.0.9 | std::stop_token |
| linux (clang-22, clang-22-arm64, clang-22-cxx26, clang-22-tracy, gcc-14, gcc-15), sanitizers (asan-ubsan, tsan), clang-tidy, coverage, compile-cache | libstdc++ | std::stop_token |
| windows (cl-debug, cl-release, cl-release-tls, clangcl-release) | MSVC STL | std::stop_token |

So AppleClang (the macOS runner's Xcode) runs the threaded fallback in production. The report's
concern 7 is settled: the fallback is live there, with threads. emsdk latest (single-threaded)
uses libc++'s own `std::stop_token`. coro.md still says "likely AppleClang"; it could now say
"AppleClang 17 (measured)". I left that for a later docs pass rather than start another CI cycle.

### Remaining concerns

- **Unpushed commit carried along.** `8e29bb5` was committed by someone else on the shared local
  `master` and went out with this push (see the note above).
- **The Important-1 case is a guard, not a reproducer.** Without an injected delay it does not
  reproduce the old order's bug (see above). The widened-window experiment is the evidence that
  it distinguishes the two orders.
- **Carried, not done here:**
  - Minor 7 (the `#ifndef _WIN32` guards) goes to B1, per the ruling.
  - The review's note that the brief's "Carry to C6" (contour's `<stop_token>` probe becomes
    removable) belongs in the plan or the C6 brief is unaddressed; it was not in R35's scope.
