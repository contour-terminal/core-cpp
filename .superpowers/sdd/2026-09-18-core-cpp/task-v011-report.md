# Task v0.1.1 report

**Status:** done, and the full gate set is green on the final SHA **69599c6**. Nothing is pushed, tagged
or released. Worktree: `D:/core-cpp-wt-v011`, detached.

**Range:** `1ad8b24..69599c6`, 18 commits:

| Commit | What |
|---|---|
| c5681f9 | fix(async,net,tui): every await_ready reads a member or answers a constant |
| 853c2c5 | test(hygiene): core-cpp.await-ready refuses a call in an await_ready body |
| f4899f0 | ci: a windows (cl-release-arm64) leg on windows-11-arm |
| 23722b5 | docs: await_ready stays trivial, and the changelog for fastcached#1546 |
| b6ee2e3 | net: ListenOptions::sharing, so one listener per loop can bind a port |
| c01d411 | test(net): #ifndef _WIN32 around the port-sharing cases (superseded by d3a86ee) |
| b8b1f6d | fix(async,net,tui): await_ready stays a const member, pinned without a static |
| 47198ce | net: one helper for every connected stream socket's options, and buffer sizes |
| a8941f0 | fix(net): the WFMO listener claims its port exclusively |
| 593c638 | net: adoptSocket, a connected socket onto the loop the caller chooses |
| d260562 | test(net): what the full gates found in the new cases |
| 4a9564a | fix(net): TestLoop's counters include what was handed over between turns |
| 8ad730f | test(net): AdoptSocket_test checks each raw socket before using it |
| d3a86ee | test(net): platform differences in the new cases live in posix/ and windows/ files |
| 73b49bf | docs(async): awaitReadyIsConstantFalse's #if is a compiler-capability check |
| f56aa45 | test(net): makeSocketPair on Windows builds its sockets through adoptSocket |
| 69599c6 | cmake: CORE_CPP_MSVC_STATIC_RUNTIME_VARIANTS, a static-CRT twin of every compiled module |

## 1. await_ready (fastcached#1546)

### Audit

Every `await_ready` definition under `src/core`. "Trivial" means a member read, a comparison of members, or
a constant.

| Awaiter | File | Body before | Verdict | Now |
|---|---|---|---|---|
| `DelayAwaiter` | net/EventLoop.hpp | `_loop == nullptr \|\| _deadline <= _loop->clock().now()` | call (virtual) -- the known instance | constant `false`; both checks in `await_suspend` |
| `TokenDelayAwaiter` | net/InterruptibleSleep.cpp | `_deadline <= _loop.clock().now()` | call (virtual) | constant; deadline in `await_suspend` |
| `Task<T>::Awaiter`, `Task<void>::Awaiter` | async/Task.hpp | `!_child \|\| _child.get().done()` | call | constant; `await_suspend` returns `awaiting` |
| `ResultAwaitable` | net/IoAwaitable.hpp | `_task.has_value() ? _task->await_ready() : _settled` | call | `return _settled;` (member read) |
| `JoinAwaiter` (`whenAll`/`whenAny`) | async/Join.hpp | `_tasks.empty()` | call | constant; empty check in `await_suspend` |
| `AsyncQueue::PopAwaiter` | async/AsyncQueue.hpp | `scoped_lock`, `empty()`, atomic `load` | call + construction | constant; the first locked check in `await_suspend` answers it |
| `SlotPark` | net/ThreadedAddressResolver.cpp | `scoped_lock`, `done` | construction | constant; locked check first in `await_suspend` |
| `SerialGate::Awaiter` | net/Tls.cpp | `scoped_lock`, `!_busy \|\| _abandoned` | construction | constant; locked check first in `await_suspend` |
| `NextInputEventAwaiter`, `NextEventForAwaiter` | tui/runtime/TuiRuntime.hpp | `_runtime.hasBufferedInput()` | call (callout, virtual `takePending`) | constant; checked first in `await_suspend` |
| `NextActivityAwaiter` | tui/runtime/TuiRuntime.hpp | `hasBufferedInput() \|\| agentPending()` | call | same |
| `NextAgentReadyAwaiter` | tui/runtime/TuiRuntime.hpp | `_runtime.agentPending()` | call | same |
| `SleepOnLoop` (test double) | net/testing/TestLoop_test.cpp | `loop->clock().now() >= deadline` | call | constant; `await_suspend` returns bool |
| `WaitHandleAwaiter` | net/EventLoop.hpp | `_handle == InvalidHandle` | trivial | unchanged |
| Cancellation, ResumeOn, Task final, Join runner, ReadinessDial, IocpOperation, WindowsSocket | various | `false` | trivial | unchanged |

**Every moved decision is asked in `await_suspend` BEFORE the flow's stop token is read.** A flow that is
already stopped therefore still resumes normally where `await_ready` used to answer true.

**Why the constant is not `static`.** c5681f9 made them `static constexpr`. clang-tidy's
`readability-static-accessed-through-instance` then reported every `co_await` on them: 391 findings here,
and consumer code that runs the check would be hit the same way. b8b1f6d made them
`constexpr bool await_ready() const noexcept { return false; }`.

**How the constant is pinned.** The pin is `core::async::awaitReadyIsConstantFalse<A>()` in `Awaitable.hpp`.
- It asks through a never-defined `extern A const&` in a constant expression (P2280), so a call or a
  member read in `await_ready` fails to compile.
- It is effective on GCC 14, Clang 22, clang-cl 22 and MSVC 19.51. It asserts nothing on MSVC before
  19.51 (19.44 included) and on Clang before 20; its doc comment says so, and calls the `#if` a
  compiler-capability check, per the ruling.
- It cannot name the three file-local awaiters (TokenDelayAwaiter, SlotPark, SerialGate::Awaiter). The
  scan `scripts/check-await-ready.py` covers those.

### RED / GREEN

- **Pins (RED):** the pins failed to compile on clang-debug before the fix (5 test TUs).
- **Pins, after the rework:** putting the old Task `await_ready` back makes Task_test.cpp:51 fail with
  "not an integral constant expression".
- **Ordering mutation:** reading the token before the deadline check turns the new SleepUntil
  "deadline already gone ... flow already stopped" case red (`task.result()` throws).
- **Scan:** `check-await-ready.py` reports 23 problems at 1ad8b24 and 0 of 42 definitions at HEAD. Its
  self-test has 9 cases and goes red when the checker is blinded to constructions.
- **New behaviour pins:** elapsed `sleepUntil` with a stopped flow; elapsed `interruptibleSleepUntil`;
  awaiting a finished Task; awaiting an empty Task, caught inside the awaiting coroutine.

## 2. ListenOptions::sharing

`PortSharing::Shared` sets `SO_REUSEPORT` on POSIX and is refused with `Unsupported` on Windows.
- **RED:** a compile error before the field existed. On POSIX, a mutation that ignores `sharing` fails
  `REQUIRE(second.has_value())`.
- **GREEN:** passes on poll, epoll, and the Windows refusal.
- **WFMO fix (a8941f0):** widening the default-refusal case to every backend turned it red on wfmo. The
  WFMO listener bound with `SO_REUSEADDR`, which lets another socket take the port over. It now uses
  `SO_EXCLUSIVEADDRUSE`.
- **Test layout (d3a86ee):** the POSIX and Windows cases now live in `posix/PortSharing_test.cpp` and
  `windows/PortSharing_test.cpp`.

## 3. Connected-socket options and buffer sizes

One helper, `detail::applyStreamSocketOptions`, now serves every dial and every accept on every platform.
`SocketBufferSizes` (two `std::optional<std::size_t>`) sits on both `ListenOptions` and `DialOptions`.

- **RED by mutation, POSIX accept without the helper:** `noDelay` false, and receive buffer 131072 < 150000
  (poll and epoll).
- **RED by mutation, IOCP `AcceptEx` without the helper:** `noDelay` false, and 65536 < 150000 on both
  buffers.
- **GREEN:** poll, epoll, iocp and wfmo.
- **Known limit:** on Linux, the dialled send-buffer `>=` check also passes without the option, because
  autotuning grows the buffer. The helper case (`!= before`) is what distinguishes there.

## 4. adoptSocket

`adoptSocket(EventLoop&, platform::NativeHandle, std::string peerAddress)`:
- owns the handle on every path and closes it on failure;
- changes no socket options;
- asserts it runs on the loop's thread;
- builds IocpSocket, WindowsSocket or PosixSocket, whichever the loop's backend drives.

f56aa45 routes Windows `makeSocketPair` through it, so that choice is made in one place.

- **RED by mutation (ruling 3):** `adoptSocket` answering `Unsupported`.
  - On POSIX (poll and epoll), both cases went red: 4 failed assertions.
    - `AdoptSocket_test.cpp:88`: `REQUIRE(adopted.has_value())`.
    - `AdoptSocket_test.cpp:130`: the `BadHandle` expectation.
  - On Windows (iocp and wfmo), the same two cases went red, 4 failed assertions. The mutant was written
    as `if (handle != nullptr) return Unsupported`, because a plain early return trips C4702, which is an
    error under /WX.
- **GREEN:** on all four backends.

## 5. TestLoop counters

- **RED:** `pendingSubmissions() 0 == 1` and `pendingTimers() 0 == 1` after a submit or schedule from the
  test thread outside a turn.
- **GREEN:** both read 1, then 0 after a drain.
- **How:** new `EventLoop::inboundSubmissionCount()` and `inboundScheduledCount()`, read under
  `_inboundMutex`.

## 6. CORE_CPP_MSVC_STATIC_RUNTIME_VARIANTS

- **What it declares:** with an MSVC-ABI compiler, `core_cpp_add_module` declares
  `core-cpp-<name>-mt` / `core::<name>_mt` for every STATIC module. Each twin has the same sources, flags
  and usage requirements, uses `MultiThreaded$<$<CONFIG:Debug>:Debug>`, and links the twins of its
  module deps. INTERFACE modules are shared.
- **Where the twins go:** they join `CORE_CPP_TARGETS` and are `EXCLUDE_FROM_ALL`.
- **Other compilers:** they ignore the option with one status line.
- **RED:** `core-cpp.static-crt-mismatch` builds a /MT program against `core::net` and `core::log`, and
  passes only when the link fails naming RuntimeLibrary. Measured:
  `LNK2038 mismatch detected for 'RuntimeLibrary': value 'MDd_DynamicDebug' doesn't match value 'MTd_StaticDebug'`.
- **GREEN:** `core-cpp.static-crt-smoke`, with the option on, links the same program against
  `core::net_mt` and `core::log_mt` and runs a loopback echo and a log line.
- **Bug found on the first configure:** `if(x MATCHES ... AND TARGET ...${CMAKE_MATCH_1}...)` expands the
  variable before the match runs, which linked tui's twin to `core::async_mt`.
- **Prefix check:** `CORE_CPP_`-prefixing is enforced by check-cmake-hygiene's existing
  `unprefixed-option` rule, which its self-test covers.
- **CI:** build.yml runs the ON tree on the cl-release and clangcl-release legs.

## Gates on 69599c6 (the whole range, run once)

WSL trees are incremental; the step counts are as the log printed them. Windows OFF trees used
`--clean-first`; Windows ON trees are fresh trees (`out/build/<preset>-mt`).

| Gate | Build | Tests |
|---|---|---|
| clang-debug (WSL, clang 22.1.2) | exit 0 | 65/65 pass, 1 skipped (text-encoding: WSL git cannot read a Windows worktree) |
| gcc-release (gcc 14.3) | exit 0 | 65/65 pass, 22 skipped (NDEBUG canaries, text-encoding) |
| clang-asan-ubsan | exit 0 | 65/65 pass, 1 skipped |
| clang-tsan | exit 0 | 65/65 pass, 1 skipped |
| ctest -L hygiene (clang-debug) | -- | 31/31 pass |
| clang-tidy (tidy-record, fresh tree) | exit 0, 618/618, 260 statements, canary reported | 0 findings |
| clang-debug, option ON (the no-op path) | exit 0, 618/618 | status line "is ignored: Clang does not target the MSVC ABI"; 0 `-mt` targets |
| cl-debug, option OFF | exit 0, 610/610 | 70/70 pass |
| cl-debug, option ON | exit 0, 687/687 | 71/71 pass (static-crt-smoke and static-crt-mismatch included) |
| cl-release, option OFF | exit 0, 610/610 | 70/70 pass, 24 skipped (NDEBUG canaries) |
| cl-release, option ON | exit 0, 687/687 | 71/71 pass |
| clangcl-release, option OFF | exit 0, 610/610 | 70/70 pass, 24 skipped |
| clangcl-release, option ON | exit 0, 687/687 | 71/71 pass |
| clang-format --all --check (22.1.8) | 536 files clean | -- |
| ruff (0.16.8) --all --check | 27 files clean | -- |
| mkdocs build --strict | exit 0 | -- |

Tree-level checks, all green: await-ready and its self-test, preset-coverage (18/18), tree-level-coverage
(28), ambient-reads (389), text-encoding (834), and check-renames (0 failures over 192 public headers).

## ARM64 CI leg

- **Added in f4899f0:** the `cl-release-arm64` preset (architecture arm64, external) with build, test and
  `ci-` presets; a matrix `include` running it on `windows-11-arm` with `msvc-dev-cmd arch: arm64`; a CPM
  cache key per `runner.arch`; and a "Compiler identity" step (`cl | head -1`, checked under bash
  pipefail).
- **Preset coverage:** green.
- **Not run:** nothing is pushed, and this machine has no ARM64 MSVC toolset.
- **Unverified:** choco ninja and ccache-action under emulation on `windows-11-arm`, and the image's
  MSVC version.
- **The compile-time pin is inert there:** on 19.44 `awaitReadyIsConstantFalse` asserts nothing, so that
  leg relies on its ctest and on the scan.

## Concerns

- **Foreign files in the worktree root.** `check-upstream-drift.py`, `check-upstream-drift-selftest.py`,
  `msg1.txt` and `msg2.txt` appeared untracked in the worktree root, stamped 16:09. They are not mine:
  they are another lane's work, a fix to `scripts/check-upstream-drift.py` plus two commit messages. I
  moved them intact to my scratchpad, under `foreign-untracked-from-wt-v011/`, rather than deleting or
  committing them.
- **Behaviour change on public API.** `X.await_ready()` now answers `false` on nine public awaiters, as
  recorded under Changed. fastcached's own `SleepUntil_test` asserts `true` on its copy, and will need the
  `await_suspend` form once it migrates.
- **TuiRuntime awaiters.** Their `await_ready` became `noexcept`, and `await_suspend` now calls
  `hasBufferedInput()`, which allocates. An exception there propagates the same way as before.
- **Twins and non-core dependencies.** A twin links non-core dependencies (OpenSSL, libunicode) as given.
  A consumer linking `net_tls_mt` or `tui_mt` into a /MT program must provide those built /MT too; this is
  documented in options.md.
- **Hygiene misreport.** The check-cmake-hygiene misreport (many provenance violations reported as one
  stale-allowlist) is the lead's to file.
- **Review.** No `/code-review` was run, because the brief said no subagents. Self-review only.
- **Perf lane.** The perf lane in `D:/core-cpp-wt-perf` touches EventLoop and the backends. This range
  touches EventLoop.hpp (DelayAwaiter, and the two inbound counters) and the listener and dial files; the
  rebase is yours, as stated.

## Fix round (review task-v011-review.md), on a2b92cd -> a55cede

The release is v0.2.0 (ruling S1). Four commits:
f3a73be (S3: the CI step runs only `^core-cpp\.static-crt-smoke$`),
286ca2a (N1: `NO_STATIC_RUNTIME_TWIN` on core_cpp_add_module, passed by testing_main),
54c2f33 (N6: the scanner reads a nested `noexcept(...)` and ref-qualifiers; the self-test covers both and main()'s exit 1),
a55cede (S1, S2, N2 to N5).

- **S1.** The await_ready entry is now under Breaking, with a migration note: co_await the awaiter and never call await_ready. The noop_coroutine hint is dropped. The #1546 Fixed entry says a `co_await` is unchanged. Provenance notes say v0.2.0.
- **S2.** listen() uses `SO_REUSEPORT_LB` where it is defined (the `#if` is on a constant), and `SO_REUSEPORT` otherwise. Sockets.hpp, PosixListener and the CHANGELOG say per platform that Linux and FreeBSD spread connections, while on macOS and the other BSDs the newest listener gets every connection.
- **N2.** adoptSocket closes the handle if allocation throws, on all three paths. Under WFMO it goes through the new `WindowsSocket::adopt`, which returns an event setup failure as an error.
  - RED by mutation (WFMO back to the bare constructor): "a handle that is no longer a socket" fails, `!true`.
  - POSIX and IOCP already refused that handle.
- **N3.** `applySocketBufferSizes` runs before connect (all three dial paths) and before listen (all three listeners). applyStreamSocketOptions now takes only KeepAlive.
  - Sizes set on the AcceptEx socket before issuing it did not survive the accept (measured: 65536 read back), so IOCP relies on the listener like the others.
  - The new case reads the sizes off the listening socket. RED by mutation (the listener calls removed): Windows 9 failed assertions (iocp and wfmo: listener and accepted, 65536 < 150000); POSIX 6 (poll and epoll: 16384 or 131072 < 150000).
- **N4, N5.** PortSharing's doc covers TCP. The CHANGELOG names PosixListener's `sharing` parameter. "eleven" is now "twelve".

Gates on a55cede:

| Gate | Build | Tests |
|---|---|---|
| cl-debug --clean-first | 611/611 | 70/70 pass |
| cl-release --clean-first | 611/611 | 70/70 pass |
| clangcl-release --clean-first | 611/611 | 70/70 pass |
| cl-release, static-CRT ON (fresh tree) | 688/688 | 71/71 pass (smoke and mismatch) |
| clang-debug (WSL, incremental) | exit 0 | 65/65 pass, 1 skipped (text-encoding) |
| gcc-release | exit 0 | 65/65 pass (NDEBUG skips) |
| clang-asan-ubsan | exit 0 | 65/65 pass |
| clang-tsan | exit 0 | 65/65 pass |
| hygiene (clang-debug) | -- | 31/31 pass |
| clang-tidy (tidy-record, 22.1.8) | 621/621 | 0 findings, canary reported |
| clang-format --all / ruff --all / mkdocs --strict | -- | clean / clean / exit 0 |
| await-ready, preset-coverage, tree-level-coverage, ambient-reads, text-encoding, upstream-drift, each with its self-test | -- | all exit 0 |

The WSL legs ran 19:20:17 to 19:44:14.
