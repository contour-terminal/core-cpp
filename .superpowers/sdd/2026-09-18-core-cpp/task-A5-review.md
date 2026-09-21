### Spec Compliance

**Verdict: ❌ One requirement is not met.** The fallback is meant to have "the standard semantics". Two concurrent paths depart from them: `stop_possible()` (Important 1) and `~StopCallback` on a reused address (Important 2). Every other requirement of the brief and dispatch is met.

- ✅ `src/core/coro/StopToken.hpp:44`: `<version>` is included before the choice is made. `:46-52` and `:500-531` alias `std::stop_token`, `stop_source`, `stop_callback` and `nostopstate` where `__cpp_lib_jthread >= 201911L` holds and `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` is not defined. Otherwise the aliases name `detail::StopTokenFallback`, `StopSourceFallback`, `StopCallbackFallback` and `NoStopStateFallback`, which are always defined. The shape is the same as `Generator.hpp` and `Ranges.hpp`.
- ✅ `request_stop()` returns true exactly once. The flag is tested and set under the lock (`StopToken.hpp:206-222`). It runs each callback once, on the requesting thread: a node is popped and marked running under the lock (`:259-268`), no lock is held during the invocation, and the node is not touched after it.
- ✅ A callback built on a stopped token runs inline in its constructor. `registerCallback` decides under the same lock (`:227-237`, `:479-483`).
- ✅ Mostly: `~StopCallback` unlinks a linked node, waits for a callback running on another thread, and returns at once from inside its own callback (`:243-253`). The exception is a node that was never registered: it still consults `_running` by address (Important 2).
- ❌ `stop_possible()` is false without a state, and false after the last source goes without a request (`:195`, `:326`, `:405`). Under concurrency, though, it can return false after stop *was* requested (Important 1).
- ✅ Copies share state: `shared_ptr` state, a source count, and copy-and-swap assignment (`:355-384`).
- ✅ Single-threaded Emscripten gets no atomics, no lock and no wait. `Shared<T> = T`, the `Lock` is empty and `wait` is a no-op. `<atomic>`, `<mutex>`, `<condition_variable>` and `<thread>` are not included (`:55-63`, `:118-153`). The threaded tests are compiled out (`StopToken_test.cpp:15-23`, `:279-440`).
- ✅ Ruling R33: `core-cpp-coro-fallback-test` compiles StopToken, Task, WhenAll and WhenAny with the macro, as ctest `core-cpp.coro-fallback` with labels `core-cpp;coro` (`src/core/coro/CMakeLists.txt:16-31`).
  - No other translation unit in that binary includes a coro header. Outside `coro/`, only `platform/FileSystem.hpp` includes one, `Generator.hpp`, and `testing_main` does not link `platform`.
- ✅ `core_cpp_add_test` gained `NAME` (one-value) and `DEFINITIONS` (`cmake/CoreCppTargets.cmake:191-225`). The target stays `core-cpp-<name>-test` and the ctest `core-cpp.<name>`. Definitions are `PRIVATE`. No INTERFACE or PUBLIC flag is added, and there is no `-fexperimental-library`.
- ✅ `Cancellation.hpp:14` includes `StopToken.hpp`, and the `#error` is gone.
- ✅ The imports are faithful. I mapped the upstream blobs at `6777ff05` (`coro::` to `core::coro::`, NOLINT lines dropped) and diffed them against the imported files. The only differences are:
  - clang-format's include order;
  - the recorded `-Wshadow` renames (`WhenAll.hpp:75-77`, `WhenAny.hpp:94-103`);
  - the recorded `WhenAny_test.cpp` changes;
  - the deep-chain skip.

  `UniqueCoroHandle.hpp` is byte-identical after mapping.
- ✅ Provenance has 11 rows with the full 40-hex SHA or `origin: core-cpp` (`.agent/reference/provenance.md:59-72`). NOTICE, the CHANGELOG (Added entries and the Imported row), `source-map.md`, `coro.md` and `index.md` are updated.
- ✅ Nothing more was needed for `SuppressWindowsDialogs`: provenance row `:153` already exists from A1. `test_main.cpp` was not imported, and the README was folded into `coro.md` with its origin cited.
- ✅ The deep-chain case uses `SKIP`, never `SUCCEED` (`Task_test.cpp:143-151`). Its scope is Minor 3.
- ⚠️ I did not re-verify the CI run 35374365149 or the local preset results. Both are the implementer's claims.
- ⚠️ Nobody knows which branch AppleClang (Xcode 16) takes: report concern 7, and Minor 5 below.
- ⚠️ The brief's "Carry to C6" (contour's `<stop_token>` probe becomes removable) exists only in the report. It needs to go into the plan or the C6 brief.

### Strengths

- **The fallback is a sound design.**
  - The synchronisation is a policy (`StopStateSync`) with two definitions, and the logic is written once over it.
  - The callback list is intrusive, so registration allocates nothing.
  - Nodes are popped one at a time under the lock, so a callback can destroy another, still-linked callback mid-iteration.
  - `request_stop` holds no lock while a callback runs, so nested `request_stop` calls and registrations cannot deadlock.
  - `StopSourceFallback::request_stop` keeps a local `shared_ptr` (`:391-397`), so a callback may destroy the source.
  - The condition variable lives in the state, which the waiting destructor keeps alive, and the notify runs after the unlock.
- **Lock order and lifetimes hold for everything the brief names.** There is one mutex per state, and it is never held across a callback. A callback may destroy itself (the requester-thread check). A callback may be registered during `request_stop` (it runs inline, because the flag is set under the lock first). A source may be destroyed while tokens remain (tokens and callbacks hold the state).
- **The evidence is strong.** The implementer neutered three behaviours and exactly the three matching cases failed. A TSan neuter reported a data race (exit 66). Every threaded wait is bounded and all assertions come after the joins.
- **ODR is handled by construction** (R33): the macro is compiled into its own binary. The macro's hazard is written where a reader meets it: the header comment, the `CMakeLists.txt` comment and `coro.md`.
- **The measured limits are recorded factually, not hidden.** The skip is a `SKIP` with a measured reason.
- There is no NOLINT, no pragma and no C-style loop anywhere in `src/core/coro` (grepped). `.clang-tidy` exemptions are added with their reason, following the existing pattern.

### Issues

#### Critical

None.

#### Important

1. **`stop_possible()` can report "not possible" after stop was requested** (`src/core/coro/StopToken.hpp:195`).
   - **What is wrong.** `isStopPossible()` evaluates `_stopRequested || _sourceCount != 0` as two separate atomic loads. Consider this interleaving:
     - Thread A reads `_stopRequested == false`.
     - Thread B calls `request_stop()`, which returns true, and then destroys the last `StopSource`, so the count is 0.
     - Thread A reads `_sourceCount == 0` and returns false.

     No instant exists where "not requested and no sources" was true, so the answer is not linearizable. It also contradicts a later `stop_requested() == true`.
   - **Why it matters.** The fallback is not only the WebAssembly shim. Contour's own probe comment says libc++ gates `<stop_token>` up to version 19 (`D:\contour` `CMakeLists.txt:97-103` @ `6777ff05`), and Apple's libc++ is likely gated as well. So consumers without `-fexperimental-library` (endo, tuidu, fastcached on macOS or FreeBSD) run the threaded fallback in production. "Request stop, then drop the source" is the common shutdown shape. Code that registers a stop callback only `if (token.stop_possible())` can then miss a cancellation and hang.
   - **Fix.** Read the count first: `return _sourceCount != 0 || _stopRequested;`. Add a comment explaining why the order is correct:
     - a count of 0 is terminal, because only a live source can be copied;
     - once the count is 0, nothing can change `_stopRequested`;
     - so the second load returns the final answer.

     Alternatively, pack the flag and the count into one atomic word, as libstdc++ does.

2. **A `StopCallback` that was never registered can wait for, and deadlock on, an unrelated callback that reuses its address** (`StopToken.hpp:243-253` with `:479-483`).
   - **What is wrong.** `deregisterCallback` decides "running on another thread" by comparing addresses (`_running == &node`), and it also does this for a callback that ran inline because stop was already requested.
   - **How it deadlocks.**
     1. Callback X runs on the requesting thread A.
     2. X destroys itself, which is allowed. It then blocks on thread B, for example on a future or a join.
     3. Thread B builds callback Z in X's freed storage. Coroutine frames and pooled allocators reuse same-size blocks readily. Z is on the same (stopped) token, so it runs inline.
     4. B destroys Z. `_running == &Z` and the requester is A, not B, so B waits for X's callback to return. X's callback is waiting for B. Deadlock.

     Report concern 3 calls this "only a spurious wait, never a deadlock". That is incorrect. With `std::stop_callback` this sequence does not block.
   - **Fix (libstdc++'s approach).** Keep the state only when registration succeeds. In `start()`, write `if (_state && !_state->registerCallback(*this)) { _state.reset(); invoke(*this); }`. The destructor then deregisters only nodes that were linked. A linked node cannot share an address with a running one: once a callback runs, stop is requested, and every later callback runs inline.
   - **Test.** This can be tested deterministically:
     - Hold the callback in a `std::optional`. The callback resets the optional, signals thread B, and waits, bounded, for B's "done" flag.
     - B `emplace`s a new callback into the same optional (the same address) on the stopped token, then resets it, then sets "done".
     - Today B blocks until A's bounded wait times out, so the check fails. After the fix it passes.

#### Minor

3. **The deep-chain skip is too narrow for GCC and broader than measured for Emscripten** (`src/core/coro/Task_test.cpp:143-151`). I measured the real `Task.hpp` in WSL, with depth 100000 and an 8 MiB stack. "Recursion" is `sumDown`; "loop" is one coroutine awaiting `answer()` 100000 times:

   | Compiler | -O0 | -Og | -O1 | -O2 |
   |---|---|---|---|---|
   | g++ 14.3 recursion | SIGSEGV | SIGSEGV | SIGSEGV | pass |
   | g++ 14.3 loop | SIGSEGV | pass | pass | pass |
   | g++ 15 recursion | SIGSEGV | SIGSEGV | SIGSEGV | pass |
   | g++ 15 loop | SIGSEGV | pass | pass | pass |

   - GCC 15 behaves like 14.3, so applying the GCC condition to every version is right.
   - `-Og` and `-O1` define `__OPTIMIZE__` but do not enable sibling-call optimisation. The case is not skipped there, and it crashes the whole binary. No preset uses those levels (`CMakePresets.json` is Debug, Release or RelWithDebInfo), so today's CI is unaffected.
   - The Emscripten branch skips every emsdk, and builds with `-mtail-call`, but only 3.1.56 was measured.
   - **Fix.** Key the Emscripten skip on `defined(__EMSCRIPTEN__) && !defined(__wasm_tail_call__)`. Name `-Og` and `-O1` in the comment and in `docs/modules/coro.md:75-80` as unskipped levels that overflow. Optionally, measure stack growth at a modest depth (the address of a local at the leaf against the root), so a missing tail call becomes a clean SKIP or FAIL instead of a crash.
   - **Nothing else in the suite depends on deep synchronous chains.** The whenAll and whenAny cases use 2 or 3 children. WhenAll's start-phase guard keeps child completion from nesting inside `await_suspend`. The StopToken rounds are plain loops, and a Generator is resumed from its caller.

4. **The docs describe the limit only as a "chain".** `docs/modules/coro.md:75-80` and `CHANGELOG.md:82-85` say so, and a reader will take "chain" to mean nesting.
   - The table above shows that at GCC `-O0`, a *loop* of 100000 sequential, synchronously completing awaits in one coroutine overflows too. Consecutive synchronous completions pile up stack until the coroutine really suspends.
   - That is the pattern a read loop over buffered data produces, in `core::net` on WebAssembly (morph) or in a GCC debug build.
   - **Fix.** Say so explicitly, and open the tracking issue for B1/A6 that the report suggests.

5. **The docs understate where the fallback is live.** `coro.md:41-43` and `CHANGELOG.md:69-70` name only libc++ 17 / emsdk 3.1.56. The earlier `coro.md` text said "libc++ 17 to 19", and contour's probe comment agrees. Apple's libc++ in Xcode 16 very likely takes the fallback too; the report calls this unknown.
   - **Fix.** Write "libc++ before 20 without `-fexperimental-library` (emsdk 3.1.56's libc++ 17, FreeBSD 15's base Clang 19, likely AppleClang)".
   - Make the choice visible per CI job, for example with a configure-time `check_cxx_source_compiles` plus `message(STATUS)` in the coro test's CMake. That settles concern 7. It also tells us whether the macOS jobs exercise the std branch at all.

6. **The fallback's lifetime paths have test gaps** (`src/core/coro/StopToken_test.cpp`).
   - No case has a callback destroy a *different*, still-linked callback during `request_stop`, which must then not run.
   - No case has a callback destroy both itself and the last `StopSource` while no token remains. Removing the local `state` copy at `StopToken.hpp:391-397` would pass the whole suite, even under ASan.
   - The race case at `:380-414` captures `roundsThatRan` but never asserts that the running-callback interleaving occurred. On a platform where the requester never wins, it passes vacuously. Assert `roundsThatRan > 0`, or `SKIP` when it is 0.

7. **The inherited `#ifndef _WIN32` guards hide two exception cases on Windows** (`Task_test.cpp:203`, `WhenAny_test.cpp:162`). `WhenAll_test.cpp`'s throwing cases (`:94`, `:116`) have no guard and pass with cl and clang-cl. Keeping the guards is right for a faithful A5 import. Removing them is a follow-up worth a ticket, since today they cost Windows coverage for nothing measured.

**Recommended heavier validation, not run here.** After fixes 1 and 2:
- run `core-cpp-coro-fallback-test "[threads]"` under clang-tsan with a few hundred repeats, for example `ctest --preset clang-tsan -R coro-fallback --repeat until-fail:200`;
- confirm in the macOS CI log which branch AppleClang takes.

### Assessment

**Task quality:** Needs fixes

The StopToken fallback is well designed and well tested, R33 is followed exactly, and the contour imports are faithful. The fallback is live with real threads on libc++ before 20, so its two concurrency divergences from `std::stop_token` matter: the unordered `stop_possible()` read, and the address-based wait that can deadlock. Both are one-line fixes, and the task should not be trusted until they are made. The rest is documentation precision and test hardening.
