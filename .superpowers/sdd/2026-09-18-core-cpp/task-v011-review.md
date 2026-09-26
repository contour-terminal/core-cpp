# Task v0.1.1 review

**Range:** `1ad8b24..69599c6` (18 commits), read in `D:/core-cpp-wt-v011` at 69599c6.
**Reviewer:** read-only. Nothing was built or run except `scripts/check-await-ready.py` (42 definitions, clean) and its self-test (9/9).

**Verdict: CHANGES.** No blockers. Three should-fix findings, each a cheap fix. Item 1's behaviour preservation holds.

## (a) await_ready: behaviour preservation (no findings)

I checked every rewritten awaiter against its old `await_ready == true` path.

- **`DelayAwaiter`** (`EventLoop.hpp:893-907`). The null-loop and elapsed checks come first, before `_token` is read. `await_resume` then sees an empty token and an invalid `_park`, the same state the old `await_ready` path left. The elapsed-deadline re-read is now a single check rather than two, and nothing depends on there being two.
- **`TokenDelayAwaiter`** (`InterruptibleSleep.cpp:62-66`). The deadline is checked before `_flowToken` is read, so a stopped flow with an elapsed deadline still reports `Deadline`, as before.
- **`Task<T>` and `Task<void>` awaiters** (`Task.hpp:212-215`, `333-336`).
  - With no child, or a finished one, `await_suspend` returns `awaiting` before it touches the promise or propagates the token.
  - The symmetric transfer to self is correct, and `await_resume` is unchanged.
  - The new cases pin both paths: a finished task, and an empty task caught inside the awaiting coroutine.
- **`JoinAwaiter`**. An empty set returns false before the continuation, the bridge or the token are touched.
- **`ResultAwaitable`**. The coroutine path's done-check now lives in `Task`'s own `await_suspend`, which `await_suspend` already reached. `_settled` is only ever set on the non-task path.
- **The locked awaiters.** `AsyncQueue::PopAwaiter` (`AsyncQueue.hpp:299-316`), `SlotPark` (`ThreadedAddressResolver.cpp:159-173`) and `SerialGate::Awaiter` (`Tls.cpp:277-293`) all have the same shape:
  1. A locked early check, which the old `await_ready` did.
  2. Stop-callback registration.
  3. A second locked check, which also covers the cancelled flag, in the same critical section that publishes the park.

  A push, completion or gate release landing between the two checks is seen by the second one. So no wakeup is lost; this is the same window the old `await_ready`/`await_suspend` pair had, and it is closed the same way.
- **`await_suspend` returning false.** That resumes the awaiting coroutine at once, per [expr.await]. `await_resume` on each of these awaiters already handled the never-parked state, because the old ready path produced exactly that state.
- **TUI awaiters.** The buffered-input and agent checks come before the token and `isStopping()`, so buffered input still reaches a stopped flow. `hasBufferedInput()` may throw from `await_suspend`, which is non-noexcept. By [expr.await], the coroutine is then resumed and the exception rethrown, which matches the old propagation.

## Should-fix

### S1. The CHANGELOG files an observable public-API change under Changed in a patch release, and its migration hint is unsafe

`CHANGELOG.md:57-66`, `.agent/rules/library-hygiene.md:169-180`.

**The change breaks direct callers.** `X.await_ready()` now answers `false` where it answered `true` on nine public awaiters. Two things show this is breaking:
- This range had to rewrite core-cpp's own `SleepUntil_test.cpp` and `AsyncQueue_test.cpp` assertions.
- The report says fastcached's `SleepUntil_test` asserts `true` on its copy.

The hygiene rule says a breaking change goes under **Breaking** with a migration note, and that "a patch release never does [break]". So either:
- file it under Breaking and cut 0.2.0, or
- state explicitly, in the CHANGELOG and the awaiter docs, that calling `await_ready()` directly is not a supported contract.

That decision is the lead's. As written, the entry contradicts the rule.

**The migration hint is unsafe.** The hint is "`await_suspend(std::noop_coroutine())` answering `false` is the question now". It is safe only when the answer is false:
- On a `DelayAwaiter` with a future deadline, it files a real park with the loop (`EventLoop.hpp:933`). `DelayAwaiter` has no destructor to take that park back, so the loop is left holding a park for an awaiter that no longer exists.
- On `Task<T>::Awaiter` the call returns a handle, not a bool. It also wires the child's continuation to the noop handle and hands back the child without starting it.

Scope the hint to `sleepUntil`'s null-loop and elapsed cases, or drop it.

### S2. PortSharing's documentation promises load-balancing on macOS and the BSDs

`src/core/net/Sockets.hpp:53-56`, `src/core/net/posix/PosixListener.cpp:27-29`, `CHANGELOG.md:103-105`.

All three say the kernel spreads incoming connections across the listeners, via `SO_REUSEPORT` on Linux, the BSDs and macOS. Only Linux does that:
- Darwin delivers every connection to one socket, the newest.
- FreeBSD balances only under `SO_REUSEPORT_LB`.

This range's own test says so (`posix/PortSharing_test.cpp:115`: "the newest listener on macOS"), and is written not to race the listeners for that reason.

**Failure:** a macOS server that follows the header, one listener per loop, puts every connection on one loop and leaves the others idle.

**Fix:** state that `Shared` lets the binds coexist everywhere, and spreads connections only on Linux.

### S3. The CI Static-CRT step re-runs static-crt-mismatch in a tree whose /MD libraries were never built

`.github/workflows/build.yml:645-651`.

**What the step does:**
1. Configures the `-mt` tree.
2. Builds only `core-cpp-static-crt-smoke`. That target links the `_mt` twins, so the /MD `core-cpp-{base,log,platform,net}` are never built.
3. Runs `ctest -R '^core-cpp\.static-crt-'`.

**The problem:**
- The regex matches `core-cpp.static-crt-mismatch` too.
- That test's nested `cmake --build --target core-cpp-static-crt-mismatch` must first compile about 63 /MD translation units (base 3, log 5, platform 13, net 42, counted from `out/build/cl-release-mt/build.ninja`). It does so inside `TIMEOUT 300`.
- The step's comment says the OFF tree above already ran the mismatch half, so this run adds nothing.

**Why the local gates missed it:** they ran it only after a full 687-step build, never in this shape.

**Failure:** on a cold cache, the step turns red on a timeout.

**Fix:** use `-R '^core-cpp\.static-crt-smoke$'`.

## Nits

- **N1. `testing_main_mt` is not the mirror the doc comment promises.** `CoreCppTargets.cmake:215-216` claims "same usage requirements", but `src/core/testing/CMakeLists.txt:31-36` edits the original after `core_cpp_add_module`: the `CORE_CPP_SKIP_EXIT_CODE` property, and the `core::testing_dialogs` object link. The twin gets neither, so a /MT test binary on `core::testing_main_mt` would lack the CRT-dialog suppression. Nothing links it today. Either mirror those edits or exclude `testing_main` from twinning.
- **N2. `adoptSocket` gaps.**
  - It says ownership transfers "on EVERY path", but `new PosixSocket` / `new WindowsSocket` / `new IocpSocket` throwing `bad_alloc` leaks the handle (`SocketsPosix.cpp:149`, `SocketsWin32.cpp:132`, `:142`).
  - Under WFMO, the `WindowsSocket` constructor ignores a `WSACreateEvent` or `WSAEventSelect` failure (`WindowsSocket.cpp:42-49`). `adoptSocket` then returns success for a socket that never becomes ready, where its doc promises "why the loop could not take it". This is the same constructor `connect` uses, so it is not new.
- **N3. Buffer sizes are applied after the connection exists.** `SO_RCVBUF` goes on after `connect`/`accept` (`posix/StreamSocketOptions.cpp`, `AcceptLoop.cpp:47`). tcp(7) says a buffer must be set before `listen`/`connect` to affect window scaling. On the accept path it could go on the listening socket, from which accepted sockets inherit it. In practice Linux sizes its window scale from `tcp_rmem`'s maximum, so this is minor.
- **N4. The enum's doc and the CHANGELOG are incomplete.**
  - `PortSharing`'s doc (`UdpSocket.hpp:46-53`) still describes only the UDP meaning ("each hears what is broadcast"), now that the enum also means TCP listener sharing.
  - `CHANGELOG.md:98-100` says `PosixListener::bind` gained a trailing buffer-size parameter. It also gained `PortSharing sharing` (`posix/PosixListener.hpp:43`).
- **N5. The awaiter count is off by one.** "eleven other awaiters" (`CHANGELOG.md:13`) and "eleven more" (`async-and-net.md:484`), but the lists that follow name twelve.
- **N6. The scanner has two silent misses, and its self-test one untested exit.**
  - `check-await-ready.py`'s `DEFINITION` regex misses a definition with nested parentheses in `noexcept(...)` (`noexcept(noexcept(f()))`) and a ref-qualified `const&` one. A missed definition is not counted, so nothing reports it.
  - The self-test never asserts that `main()` exits 1 on a scan that found no definitions. The broken-scan case checks `scan()`'s counts only.
  - Otherwise the self-test proves the scan can fail: a virtual call, a member call, a lock, a test file and an out-of-line definition are all refused. The empty `ALLOWED` table is justified, and its stale-row check is tested.

## (b) adoptSocket ownership (apart from N2)

- **Error paths.** POSIX closes on `fcntl` failure. The IOCP path closes on `associate` failure. An invalid handle has nothing to close.
- **Thread assertion.** Both platforms assert `teardownIsSerialisedWithDispatch()`, which means "not running, or on the worker thread".
- **Socket choice.** A completion port yields `IocpSocket`, associated with `AlreadyAssociated`. Otherwise it is `WindowsSocket` on Windows and `PosixSocket` on POSIX.

## (c) CMake twins (apart from N1)

- **Links.** Twins map `core::X` to `core::X_mt` only where a twin exists, and the module-table DAG order guarantees dependency twins are declared first. INTERFACE modules are shared.
- **Flags.** `core_cpp_apply_toolchain` gives the same flags, and the include directories are the same.
- **CORE_CPP_TARGETS.** Twins are appended to it, and are `EXCLUDE_FROM_ALL`.
- **No global state.**
  - `CORE_CPP_BUILD_STATIC_RUNTIME_VARIANTS` is a normal variable in core-cpp's own directory scope.
  - The only global write is the existing `CORE_CPP_TARGETS` property.
  - The option carries the `CORE_CPP_` prefix.
- **Off MSVC.** The option costs one status line and declares no targets.
- **Consumer smoke.** `tests/consumer-cpm` is unchanged. Its CORE_CPP_TARGETS completeness check would also accept twins.

## (d) Sharing and SO_EXCLUSIVEADDRUSE (apart from S2)

- **Windows refuses sharing.** `Shared` is refused before any socket exists (`SocketsWin32.cpp:34`) and never maps to `SO_REUSEADDR`.
- **The WFMO listener is now exclusive.**
  - It sets `SO_EXCLUSIVEADDRUSE` and fails the candidate if the option is refused, the same as the IOCP listener.
  - Both listeners now agree, and `Socket_test`'s default-refusal case runs on every backend.
  - The known cost of `SO_EXCLUSIVEADDRUSE` is a rebind refused while old connections sit in TIME_WAIT. The IOCP listener already paid it, so this is not a regression.
- **UDP keeps SO_REUSEADDR.** UDP `Shared` still maps to `SO_REUSEADDR` on Windows. That is correct for datagrams, and consistent with the reason TCP refuses the mapping.

## (f) Hygiene (apart from S1, N4, N5)

- **Provenance.** Rows exist for all eleven new `src/core` files.
- **renames.json.** The `ReusePort` row now points at `ListenOptions::sharing`.
- **No banned constructs.** No `NOLINT` and no new diagnostic pragma.
  - The one new `#if` (`Awaitable.hpp:68-73`) checks compiler capability, not platform.
  - `#ifdef __APPLE__` moved unchanged into `posix/StreamSocketOptions.cpp`.
- **No CRLF.** The range diff contains no CR byte.

## (g) ARM64 leg

I found no YAML-level error.
- The `include` adds a distinct job, and `runs-on` falls back to `windows-2025`.
- `msvc-dev-cmd` gets `arch: arm64`.
- The preset's `architecture: external` is legal with Ninja.
- The CPM key names `runner.arch`.
- `cl 2>&1 | head -n 1` runs under bash `-eo pipefail`, and `cl` with no arguments exits 0.

**Unverified risks, since the leg has never run.** None of these is a finding, but any one can make the first run red, so run it once through `workflow_dispatch` before the tag:
- `choco install ninja` and `hendrikmuhs/ccache-action` on `windows-11-arm`, under x64 emulation.
- libunicode's `_M_ARM64` NEON path (`intrinsics.h:208`), which `core::tui` builds and which MSVC has never compiled in this CI.
- Whether the image's cl is still 19.44. On 19.51 or later the leg may no longer observe #1546, and the new "Compiler identity" step is what would show that.
