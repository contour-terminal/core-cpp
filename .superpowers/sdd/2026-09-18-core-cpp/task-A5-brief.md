# Brief for Task A5

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A5: `core::coro` import (contour, namespace only)

**Files:** Create `src/core/coro/{Task,UniqueCoroHandle,Cancellation,WhenAll,WhenAny,Awaitable}.hpp` and `src/core/coro/{Task,WhenAll,WhenAny}_test.cpp` from `D:\contour\src\coro` at `6777ff05`. Merge `coro/testing/SuppressWindowsDialogs.hpp` into `core::testing` (A1).
- [ ] Tests first: import them, map `coro::` → `core::coro::`, build, expect FAIL. Import the headers, build, expect PASS on all four local configurations.
- [ ] **StopToken with a fallback (user direction, 2026-09-18), test first.** `src/core/coro/StopToken.hpp` defines `core::coro::StopToken`, `StopSource`, `StopCallback<F>` and `noStopState`.
  - Where `__cpp_lib_jthread` is defined, they are aliases of `std::stop_token`/`std::stop_source`/`std::stop_callback`/`std::nostopstate`. Otherwise they are a core-cpp implementation with the standard semantics:
    - `request_stop()` returns true exactly once and runs every registered callback once, on the requesting thread;
    - a callback constructed on an already-stopped token runs inline in its constructor;
    - `~StopCallback` deregisters, and blocks while that callback is running on another thread (it does not block when called from inside the callback itself);
    - `stop_possible()` is false for a token with no associated source;
    - copies share state.
  - Under single-threaded Emscripten the fallback uses no atomics or waiting.
  - `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK` (like A3's `CORE_RANGES_FORCE_FALLBACK`) selects the fallback even where std has it. The test target `core-cpp-coro-test` compiles the StopToken tests TWICE, once with the macro, so the fallback is exercised on every CI platform including TSan (concurrent `request_stop` vs callback destruction).
  - `Cancellation.hpp` (contour's `StopToken`/`thisCoroStopToken`/`HasStopToken`/`OperationCancelled`) uses these aliases.
  - No `-fexperimental-library` or other INTERFACE compile flag is added.
  - Carry to C6: contour's global `<stop_token>` probe (`CMakeLists.txt:97-137`) becomes removable if nothing in contour uses `std::stop_token` directly.
- [ ] Commit `coro: import contour's Task, cancellation and combinators as core::coro`.

