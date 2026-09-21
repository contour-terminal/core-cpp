# Task A11: Phase A gate, pass 3 findings (`core::async`)

The Phase A gate ran `/code-review` over `a92e0e9..e45f730` (Task A5's import of contour's `Task`, `whenAll` and `whenAny`, the hand-rolled `StopToken` fallback, and Task A5b's rename to `core::async`). It found 15 defects.

Twelve are in this task. Three are deferred to Task B1, which rewrites this module's ownership and runners when fastcached's executors are grafted on; they are listed at the end so you do not fix them here.

Fix each one **test-first**: a case that fails before the fix, in `src/core/async/*_test.cpp`.

## Critical

1. **`src/core/async/WhenAny.hpp:184` — the parent-to-child cancellation bridge can destroy itself mid-call.**
   When the parent's token is stopped, `_parentReg`'s callback calls `_state.childStop.request_stop()`. If a child's awaitable resumes its coroutine from its own stop callback — which is the documented cancellation design for our runtime awaitables — the losers unwind inline, the last one transfers to the parent, `await_resume()` throws, the parent frame unwinds, and the `WhenAnyAwaiter` temporary is destroyed: `_parentReg` (the callback currently executing), `_state` (including the `childStop` whose `request_stop()` is on the stack) and the child frames all go with it. Control then returns into `request_stop()`'s callback loop on freed memory.
   - Our fallback `StopSource` survives this by accident, because `request_stop()` holds a `shared_ptr` copy of its state. `std::stop_source` on libstdc++ and MSVC holds a raw pointer, so **the std branch is a use-after-free** — that is, every platform except the ones running our fallback.
   - `WhenAny_test.cpp` cannot catch it: its `ManualEvent` registers no stop callback, so cancellation is always observed on a later manual resume. The test you write must resume from inside a stop callback, which is what the real awaitables do.
   - Fix so that nothing the awaiter owns is destroyed while a callback it owns is running. Say in the report which ownership rule you chose and why it holds for both `StopToken` branches.

## Important

2. **`src/core/async/CMakeLists.txt:5` — `core::async` links no threads, but its fallback needs them.**
   `StopToken.hpp`'s fallback includes `<thread>`, `<mutex>` and `<condition_variable>` and uses `std::this_thread::get_id()`, `std::mutex` and `std::condition_variable`, while the target is INTERFACE with no libraries. A consumer doing `target_link_libraries(app PRIVATE core::async)` — exactly what `.agent/guides/consumer-migration.md` shows, and what morph and tuidu will do — fails to link wherever pthread is separate and the fallback branch is taken (libc++ before 20 without `-fexperimental-library`: FreeBSD 15, AppleClang 17). Our own binaries hide it because `core::testing_main` pulls in `core::base`, which does link Threads.
   - `core_cpp_add_module` already accepts `PUBLIC_LIBS` for an INTERFACE kind; gate it on the same condition the WebAssembly subset uses, so single-threaded Emscripten still links nothing.
   - AGENT.md and the CHANGELOG both claim this module needs "nothing but the standard library": qualify that.
   - Prove it: a link that fails before the fix, or a consumer-smoke assertion.
3. **`src/core/async/WhenAny.hpp:217` — a winner that already ran is thrown away.**
   `await_resume()` throws `OperationCancelled` whenever the parent token is stopped, so `whenAny(readSocket(), timeout())` where the read completed and consumed bytes, and cancellation arrives before the last loser unwinds, loses those bytes with no way to recover them. `.agent/rules/async-and-net.md` states the opposite invariant: if a receive already completed with bytes, the data wins.
   - Latch "a winner was decided" alongside `decided`, and let it beat a later stop, instead of reading the parent token after the fact.
4. **`src/core/async/WhenAll.hpp:200` and `WhenAny.hpp:250` — the variadic overloads accept lvalues they cannot compile.**
   The constraint uses `std::remove_cvref_t`, so an lvalue `Task<void>` satisfies it and then fails inside `push_back` on the deleted copy constructor, giving a wall of errors from `<vector>` instead of "no matching overload". Require an rvalue, or take the parameters by value.
5. **`tests/cmake/check-cmake-hygiene.cmake:253` — the lowercase-namespace gate checks only the first namespace in a file**, while its own comment claims every segment of every namespace. `namespace core::async { namespace Detail { … } }` passes clean. A gate that does not report reads as passed. Fix the checker and add the self-test case that proves it.

## Minor

6. `src/core/async/WhenAll.hpp:200`: uses `std::is_same_v` and `std::remove_cvref_t` without including `<type_traits>`. `WhenAny.hpp` includes it; this is an oversight, and headers must be self-contained.
7. `src/core/async/Task_test.cpp:8`: names `std::runtime_error` without `<stdexcept>` (it compiles only because Catch2 drags it in) and includes `<string>`, which it does not use.
8. `src/core/async/Awaitable.hpp:36`: `Awaiter` and `HasStopToken` are dead — five sites hand-spell `requires { awaiting.promise().stopToken(); }` instead. Use the concept at all five, so the contract has one home. The header also includes `<utility>` without using it.
9. `src/core/async/WhenAny.hpp:229`: the cancel bridge is a `StopCallback<std::function<void()>>`, paying an allocation and an indirect call for one pointer of state. A named functor with `std::optional<StopCallback<…>>` removes both and the `<functional>` include.
10. `src/core/async/WhenAny.hpp:44`: `WhenAnyNoWinner` lives in `detail::` yet is part of the documented public result, so a consumer handling the empty case must reach into `detail::`, and a caller who forgets the check indexes out of range on a `SIZE_MAX` sentinel. Return `std::optional<std::size_t>`, or promote the constant out of `detail::`. Public API, so it is free now and breaking after v0.1.0.
11. `.clang-tidy:217`: `request_stop|stop_requested|stop_possible|get_token` were added to `FunctionIgnoredRegexp` as well as the method regexps, so a free function with those names escapes the naming rule for no reason. Remove them from the function regexp. The three ~800-character regexes are byte-identical duplicates that already drift from the TypeAlias/Typedef pair; if you can express them once without breaking the config, do.
12. `src/core/async/Task_test.cpp:153`: the deep-chain case crashes the whole binary at GCC `-Og` and `-O1`, as its own comment admits, because the skip keys on `__OPTIMIZE__`. Anyone building outside our presets — a distro package, a bisect, a developer adding `-Og` — loses every other case in the binary. Skip for GCC unless the optimisation level is known to give the tail call. Note also that the 100000-frame chain is torn down by plain recursion, which the tail-call reasoning in the docs does not cover.

## Deferred to Task B1 — do not fix here

B1 grafts fastcached's ownership, executors and runners onto this module, which rewrites exactly these three:
- **`Task.hpp:157`**: `await_resume()` and `result()` return a default-constructed `T` for a null handle, inventing a value and forcing every `T` to be default-constructible. B1's brief already says the requirement goes away; it should decide what a null handle yields instead.
- **`WhenAll.hpp:121`**: `makeWhenAllRunner` duplicates its promise's `unhandled_exception()` with a try/catch and wires the state twice; `WhenAnyRunner` shows the single-source form.
- **`WhenAny.hpp:60`**: the two runners, states and awaiters are ~200 lines of near-identical copy-paste, differing only in the latch step. One runner parameterised by an on-finish policy collapses both.

## Then

- The presets the constraints name on Windows and WSL, including the sanitizers and TSan; `python scripts/clang-format.py --check`, `ctest -L hygiene`, `mkdocs build --strict`.
- The fallback build matters here: run the tests with the `StopToken` fallback forced as well as with `std::stop_token`, since finding 1's hazard differs between them.
- CHANGELOG entries, with finding 10 under `Changed` and anything a consumer would notice recorded honestly.
- **Two other agents are working this same branch** (`src/core/{Utils,Flags,Escape,FNV,Base64}*`, `src/core/cli/`, `src/core/log/`; and `src/core/platform/`). Your files are `src/core/async/`, `.clang-tidy` and `tests/cmake/check-cmake-hygiene*.cmake`. Rebase before pushing (`git pull --rebase origin master`), never force-push, and report rather than fix anything of theirs.
- Push, watch CI and portability to green. A macOS `core::platform` failure is the platform agent's, not yours.
- Write your report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A11-report.md` with RED/GREEN per finding.
