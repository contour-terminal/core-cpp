# core-cpp Design Spec

> Approved 2026-09-18 during brainstorming. The implementation plan that argues from this spec is `docs/superpowers/plans/2026-09-18-core-cpp.md`.

## Context

The coroutine, networking, platform and TUI code has spread across these repositories as near-verbatim copies:

| Project | Where the shared code lives | How it gets it |
|---|---|---|
| contour | `D:\contour\src\{coro,net,crispy}` | canonical today |
| endo | `D:\endo\src\{coro,net,crispy,vtparser}` | fetched from contour `master` at configure time |
| endo | `src/{platform,tui,testing}` | endo's own, the newest copies |
| fastcached | `D:\fastcached\vendor\endo` | verbatim import of endo tui/platform and contour coro |
| fastcached | `src/FastCache/{Async,Net}` | a *different* first-party async design: PascalCase, IOCP/epoll/kqueue reactors, executors |
| tuidu | `D:\tuidu\src\{coro,platform,testing,tui}` | a June snapshot of endo (namespace `endo::coro`) |

Copies drift. tuidu is 2.5 months stale and already broken against contour master. fastcached keeps a second coroutine runtime only because its reactor cannot park on a console handle.

Lightweight and morph hold no copies:
- Lightweight gets a targeted adoption: dbtool's hand-rolled ANSI output moves to core-cpp.
- morph adopts core-cpp for what it needs, and the WebAssembly build is a hard constraint:
  - Its dependencies move from FetchContent to CPM.
  - Its TimeoutScheduler (native thread and browser `setTimeout` builds) and its base64 and WakeupPipe move onto core-cpp.
  - **Coroutines, the main motivation:** a `Completion<T>` becomes awaitable, and model handlers may be `core::async::Task<R>` coroutines driven on the model's strand.

Decisions made with the user during brainstorming:
- Merge both async designs now.
- Everything goes under `core::`.
- contour vendors verbatim, so distro packagers get no new dependency.
- Merge first, migrate once.
- Split crispy with a graduation rule.
- IOCP becomes the Windows default.
- The repository is public.
- vtparser stays in contour.
- crispy's generic part becomes plain `core` (`core::base` target), plus `core::log` and `core::cli`.
- morph uses CPM, adopts core-cpp's timers, leaf utilities and coroutine API, and must stay compatible with WebAssembly. core-cpp therefore supports single-threaded Emscripten for the subsets morph uses.

---

# Part I — Design spec (approved)

## 1. Modules, namespaces, targets

Namespace equals directory, both lowercase (user decision, 2026-09-18; `.clang-tidy` enforces `NamespaceCase: lower_case`). A header's outermost namespace is `core::<dir>`. Headers directly in `src/core/` are `core`. Nested helper namespaces (`detail`, `base64`, `views`, `testing`) are allowed inside. Real targets are named `core-cpp-<name>`, ALIASed `core::<name>`, and their type is always explicit.

| Directory | Namespace | Target(s) | Kind | Depends on | Origin |
|---|---|---|---|---|---|
| `src/core/*.hpp` | `core` | `core::base` | STATIC | Threads (+Tracy opt.) | contour crispy: Assert, Defines, Environment, Escape, FNV, Flags, Times, UserInfo, Utils, Overloaded, Deferred, Base64 (`core::base64`). fastcached `Core/Profiling.hpp` (`FC_*`→`CORE_*`) and `Core/Ranges.hpp`. endo `Generator` (`core::Generator`; `std::generator` where available) |
| `src/core/log/` | `core::log` | `core::log` | STATIC | base | crispy LogStore, LogSink (namespace `logstore`). `gsl::not_null` is replaced |
| `src/core/cli/` | `core::cli` | `core::cli` | STATIC | base, log | crispy CLI (`crispy::cli`), App |
| `src/core/platform/` | `core::platform` | `core::platform` | STATIC | base, log | endo platform, generic part (see §7 endo row). Merged Clock (endo + contour net/platform + fastcached Core/Clock: IClock, SteadyClock, CachedClock, ManualClock, IWallClock, WallClockRef). SystemPipe, WinsockInit, Wakeup, SignalHandler, PlatformError, NativeHandle |
| `src/core/async/` | `core::async` | `core::async` | INTERFACE | std only | contour coro + fastcached Async grafts (§2) |
| `src/core/net/` | `core::net` | `core::net_types` (INTERFACE: NetError, IoResult), `core::net` (STATIC), `core::net_tls` (STATIC, only if `CORE_CPP_WITH_TLS`) | | async, platform; TLS: OpenSSL PRIVATE | merged contour net + fastcached Net (§2) |
| `src/core/tui/` | `core::tui` | `core::tui_output` (STATIC leaf: TerminalOutput, SgrBuilder, SyncGuard, TerminalProtocols, CursorShape, Error, platform/PosixIO, Win32Utf), `core::tui` | STATIC | tui_output: base only. tui: + platform, async, net, libunicode, stb (opt.) | endo tui @ `f774a210`, which includes fastcached's upstreamed PR #184 |
| `src/core/testing/` | `core::testing` | `core::testing`, `core::testing_main` | STATIC | base; Catch2 | endo testing (ScopedTempDir, ScopedWorkingDirectory, EnvHelper, SuppressWindowsDialogsAtStartup, WindowsDialogCanary). The four SuppressWindowsDialogs variants are merged. The new CatchMain normalises exit codes |

- **WebAssembly subset** (`__EMSCRIPTEN__` without `__EMSCRIPTEN_PTHREADS__`; emsdk 3.1.56, which Qt 6.8 for WebAssembly and morph need, and latest). No `std::thread` and no `Threads::Threads` link there.

  | Module | Built under Emscripten |
  |---|---|
  | base, log, cli | fully |
  | async | everything except `ThreadPoolExecutor.hpp`, which `#error`s with a message under single-threaded Emscripten. `core::async::StopToken`/`StopSource`/`StopCallback` alias `std::` where `__cpp_lib_jthread` is available and fall back to a core-cpp implementation otherwise (libc++ 17 in emsdk 3.1.56, older FreeBSD libc++), as `Generator` does; no experimental-library flag is propagated |
  | platform | Types, NativeHandle, PlatformError, Clock, StringUtils, PathUtils, GlobMatch, FileUri |
  | net | `net_types`, IoBackend, EventLoop, timers, DeadlineTimer, WithTimeout, HostDrivenBackend and `testing/{TestLoop,ScriptedBackend,NullBackend}`. No sockets, DNS, TLS or HTTP |
  | testing | fully (the Windows-dialog parts are no-ops) |
  | tui | never |

  The module table's `PLATFORMS` column takes `any|native|wasm-subset`, and `SOURCES_EMSCRIPTEN` selects the subset.
- **Private code:** `detail/`, `posix/`, `linux/`, `darwin/`, `windows/`, `backend/` and tui `platform/` are private and are not in any FILE_SET. Each module's `testing/` subdir holds its DI fakes, which are public and compiled into the module.
- **Stays in contour:** crispy's renderer/contour-only half (BufferObject, Ring, StrongHash, Intrinsics, StrongLRU*, LRUCache, AlignedAllocator, Animation, Point, Size, ReadSelector, FileDescriptor, TrieMap, Sort, Comparison, Compose, ScopedTimer, StackTrace, InterpolatedString, Owned, Range, tracy-stub), and **vtparser** (single consumer, performance-critical, still evolving).
- **Graduation rule:** a file moves into core-cpp when a second project needs it. It must carry no PUBLIC flags and no dependency beyond std and Threads.
- **Stays in endo:** `endo::http` (curl) and endo-specific platform: Process, Pipe, ProcessProvider*, ProjectFileTree, InstallPaths, InterruptThrottle.

## 2. Unified async/net layer

**Layering:**
```
core::async  Task / DetachedTask / syncRun / whenAll / whenAny / AsyncQueue / ResumeOn / IExecutor / ThreadPoolExecutor
core::net    EventLoop (one per thread; implements async::IExecutor) ── drives exactly one ──► IoBackend
             IoBackend: readiness (ReadinessHandler attach/setInterest/detach) + completion (Windows ICompletionPort/CompletionOp)
             sockets: PosixSocket (readiness backends) | IocpSocket (IOCP)
```

**Rules:**
1. **Backends dispatch, the loop resumes.** Backend, completion, stop and threadpool callbacks only enqueue (`EventLoop::resumeSoon`). Coroutines resume only on the loop thread. This behaviour is identical on every OS.
2. **Backends per OS:**

   | OS | Default | Also built |
   |---|---|---|
   | Linux | Epoll | Poll |
   | macOS / BSD | Kqueue | Poll |
   | Windows | **IOCP** | Wfmo (contour's WSAEventSelect + WaitForMultipleObjects), kept one release as a fallback |
   | tests | Scripted | Null |
   | Emscripten (single-threaded) | HostDriven over `EmscriptenHostScheduler` | — |

   **The host-driven backend works on every OS and is fully testable natively.**
   - `HostDrivenBackend(IHostScheduler&)` has no readiness support: `attach` and `setInterest` return `NetErrorCode::Unsupported`.
   - `wait()` never blocks.
   - `wake()` asks the host to pump soon, coalesced.
   - After each turn the loop calls `armWakeAt(nextDeadline)`, and the backend asks the host to call `loop.runOnce(0)` then.
   - `IHostScheduler::callAfter(std::chrono::milliseconds, void (*)(void*) noexcept, void*)` is a DI seam:
     - `EmscriptenHostScheduler` uses `emscripten_async_call` (the browser's `setTimeout`);
     - `testing::ManualHostScheduler` drives it deterministically in native tests;
     - a Qt application could implement it with `QTimer`.
   - `run()` and `blockOn()` on a host-driven loop are precondition violations (asserted); the loop only advances through host pumps.

   IOCP readiness bridging:
   - Waitable HANDLEs (console input, Wakeup event, SystemPipe) use a threadpool wait whose callback only posts to the port. An optional `NtAssociateWaitCompletionPacket` path is taken only if a startup probe succeeds.
   - Socket readability uses a zero-byte `WSARecv`.
   - `lpOverlapped` points to a backend-owned refcounted `ReadinessSlot`, never into the handler.
3. **Thread-affinity guarantees**, asserted on every backend:
   - G1: exactly one thread dequeues a loop/port.
   - G2: every resumption happens in turn step 2 of `runOnce`.
   - G3: helper threads only post.
   - G4: a SOCKET is associated with exactly one port.
   - G5: `assertTeardownIsSerialisedWithDispatch` runs in every socket, listener and dial destructor.
4. **Teardown** (`~EventLoop`), in order:
   1. Assert that teardown is serialised with dispatch.
   2. `requestStop()` (root stop source) and move all parks to the ready queue.
   3. Run bounded drain passes.
   4. Abandon to a fixpoint: free the `abandon` roots of remaining `Parked` work (fastcached #1025).
   5. Destroy the spawned roots.
   6. Unregister wake.

   Objects registered with a loop must be destroyed before it.
5. **Cancellation is `std::stop_token` only.**
   - A cancel from the flow's own token throws `core::async::OperationCancelled`.
   - A cancel from the resource (`close()`, `cancelRead()`, a closed listener) returns `NetErrorCode::Cancelled` as a value.
   - Stop callbacks post `requestCancel(ParkId)` (generation-checked) through the inbound queue.
   - If a receive already completed with bytes, the data wins (#884).
   - `interruptibleSleepUntil` and `DeadlineTimer` stop polling.
   - `cancelPending` means the same on every backend.
6. **Task:** contour's `Task` (UniqueCoroHandle, stop token in the promise) is the base. From fastcached it gains:
   - `unownedRoot` propagation,
   - an awaiter that takes ownership from the rvalue Task,
   - `release()`,
   - no default-constructible `T` requirement,
   - `DetachedTask`, `syncRun`, `ParkedWork`/`detail::Parked`, `IExecutor` (with `using IExecutor::submit;` in every derived class, #1041), `ResumeOn`, `ThreadPoolExecutor`, `AsyncQueue` (stop-aware `pop`).
7. **Sockets:** fastcached's frame-free `ISocket` + `IoAwaitable`:
   - `read`/`write`/`writeVectored`/`waitReadable`/`cancelRead`/`shutdownWrite`/`handshakeIfNeeded`/`setReceiveDeadline`, plus contour's `readWithFd`.
   - Stop-aware, as `ResultAwaitable<R>`. Public `core::net::contract::*` slot guards.
   - One `PosixSocket` replaces fastcached Epoll/KqueueSocket and contour PosixSocket.
   - `core::async::asTask(aw)` for callers that store a Task.
8. **DNS, dial, UDP, blocking I/O, TLS, HTTP:**
   - DNS never runs on the loop (fastcached `IAsyncAddressResolver`/`ThreadedAddressResolver`). Contour's `connect()` is re-implemented over `makeConnector`.
   - fastcached's `IConnector`/`ConnectFlow`/`DialOptions`/`KeepAlive`/`SocketDeadline`/`IAdmissionControl`.
   - UDP and the blocking transports (`BlockingSocket`, `BlockingConnector`, `TcpClient`, `HealthProbe`).
   - Contour's `AsyncBufferedReader`, `WriteQueue`, `SplitSocket`, `WithTimeout`, `HttpServer`, Unix sockets and fd passing (POSIX).
   - TLS combines fastcached's TlsSocket record pump with contour's `ITlsContext` seam and client mode (CA pin, hostname/IP verification). `SelfSignedOptions{commonName,…}` removes the hard-coded `"fastcache-node"`. No OpenSSL type appears in any header.
9. **Profiling:** `CORE_ZONE_SCOPED[_N]`, `CORE_FRAME_MARK[_NAMED]`, `CORE_THREAD_NAME` and `CORE_PLOT` in `<core/Profiling.hpp>`. A zone never spans a `co_await`.
10. **TUI runtime:**
    - `core::tui::TuiRuntime(EventLoop&, Terminal&)` becomes composition: an input-pump coroutine over `loop.waitReadable(inputHandle, HandleKind::Waitable)`.
    - endo's `tui/runtime/{EventSource,PollEventSource,WithTimeout}.hpp` and `platform/PollHelpers.hpp` are deleted.
    - `TerminalEventSource` becomes an input adapter.

**Key interfaces** (these are contracts; tasks implement them exactly):
```cpp
namespace core::net {
enum class Interest : std::uint8_t { None = 0, Read = 0b01, Write = 0b10 };
enum class HandleKind : std::uint8_t { Fd, Socket, Waitable };
enum class BackendKind : std::uint8_t { Poll, Epoll, Kqueue, Iocp, Wfmo, HostDriven, Scripted, Null };
struct ReadinessHandler {
    NativeHandle handle = InvalidHandle; HandleKind kind = HandleKind::Fd; void* owner = nullptr;
    void (*onReadable)(ReadinessHandler&) noexcept = nullptr;
    void (*onWritable)(ReadinessHandler&) noexcept = nullptr;
    void (*onError)(ReadinessHandler&) noexcept = nullptr;
    detail::ReadinessSlotRef slot {};
};
class IoBackend {
  public:
    virtual ~IoBackend() = default;
    [[nodiscard]] virtual BackendKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::expected<void, NetError> attach(ReadinessHandler&) = 0;
    [[nodiscard]] virtual std::expected<void, NetError> setInterest(ReadinessHandler&, Interest) = 0; // must report kernel refusal
    virtual void detach(ReadinessHandler&) noexcept = 0;   // withdraws from in-flight batch (#475)
#if defined(_WIN32)
    [[nodiscard]] virtual ICompletionPort* completionPort() noexcept { return nullptr; }
#endif
    [[nodiscard]] virtual WaitResult wait(std::optional<platform::SteadyDuration> timeout) = 0; // only blocking call; never resumes
    virtual void wake() noexcept = 0;                                                        // only thread-safe member
    [[nodiscard]] virtual bool isHostDriven() const noexcept { return false; }
    virtual void armWakeAt(std::optional<platform::SteadyTimePoint> /*deadline*/) noexcept {} // host-driven backends only
};
class IHostScheduler {
  public:
    virtual void callAfter(std::chrono::milliseconds delay, void (*fn)(void*) noexcept, void* state) = 0;
  protected:
    ~IHostScheduler() = default;
};
class HostDrivenBackend final : public IoBackend { public: explicit HostDrivenBackend(IHostScheduler&); /* … */ };
#if defined(__EMSCRIPTEN__)
class EmscriptenHostScheduler final : public IHostScheduler { /* emscripten_async_call */ };
#endif
[[nodiscard]] std::unique_ptr<IoBackend> makeDefaultBackend();
[[nodiscard]] std::unique_ptr<IoBackend> makeBackend(BackendKind);   // nullptr if unavailable here

enum class IdlePolicy : std::uint8_t { Block, Return };
struct EventLoopOptions { IdlePolicy idle = IdlePolicy::Block; std::size_t dispatchBatch = 64; std::string_view name = {}; };
class EventLoop : public async::IExecutor {
  public:
    explicit EventLoop(IoBackend&, platform::IClock& = platform::defaultSteadyClock(), EventLoopOptions = {});
    void run(); RunOnceResult runOnce(std::optional<platform::SteadyDuration> maxWait = std::nullopt);
    template <typename T> T blockOn(async::Task<T>); std::size_t runUntilIdle();
    void stop() noexcept; void requestStop();
    using async::IExecutor::submit;
    void submit(std::coroutine_handle<>) override; void submit(async::ParkedWork) override;
    void schedule(platform::SteadyTimePoint, std::coroutine_handle<>); void schedule(platform::SteadyTimePoint, async::ParkedWork);
    [[nodiscard]] bool cancelPending(std::coroutine_handle<>) noexcept;
    void post(std::function<void()>); void spawn(async::Task<void>);          // spawn: O(1) self-unlink
    [[nodiscard]] TimerId addTimer(platform::SteadyTimePoint, TimerCallback, void* state); bool cancelTimer(TimerId) noexcept;
    [[nodiscard]] DelayAwaiter delay(platform::SteadyDuration) noexcept; [[nodiscard]] DelayAwaiter sleepUntil(platform::SteadyTimePoint) noexcept;
    [[nodiscard]] WaitHandleAwaiter waitReadable(NativeHandle, HandleKind = defaultHandleKind) noexcept;
    [[nodiscard]] WaitHandleAwaiter waitWritable(NativeHandle, HandleKind = defaultHandleKind) noexcept;
    void notifyHandleClosing(NativeHandle, FdWakePolicy);
    [[nodiscard]] bool running() const noexcept; [[nodiscard]] bool isOnWorkerThread() const noexcept;
    [[nodiscard]] bool teardownIsSerialisedWithDispatch() const noexcept;
    [[nodiscard]] platform::IClock& clock() const noexcept; [[nodiscard]] async::StopSource& rootStopSource() noexcept;
    void resumeSoon(async::ParkedWork); [[nodiscard]] ParkId registerPark(ParkEntry); void unregisterPark(ParkId) noexcept;
    void requestCancel(ParkId) noexcept;   // any thread
};
[[nodiscard]] DelayAwaiter sleepUntil(EventLoop* loopOrNull, platform::SteadyTimePoint);   // null/elapsed ⇒ no suspend
class PlatformLoop;                     // owns makeDefaultBackend() + is-a EventLoop
namespace testing { class TestLoop; }  // is-a EventLoop over NullBackend, IdlePolicy::Return; tick/drain
}
```

`runOnce` turn:
1. Swap the inbound queue. Run posts; resolve cancel requests by live `ParkId`.
2. Drain the ready queue.
3. `clock.refresh()`, then compute the timeout.
4. `backend.wait(timeout)`.
5. `clock.refresh()`, then fire expired timers FIFO by sequence.

**Rename map:** fastcached PascalCase becomes core camelBack. The full 44-row table is in the async design and is seeded into `tools/migrate/renames.json` (Task C0). Examples:
- `IsReady/Native/Release` → `done/handle/release`
- `SyncRun` → `syncRun`
- `IReactor` → `EventLoop`
- `PlatformReactor` → `PlatformLoop`
- `TestReactor` → `testing::TestLoop`
- `Submit/Schedule/CancelPending/Run/Stop/Clock` → camelBack
- `SleepUntil{&r,tp}` → `loop.sleepUntil(tp)`
- `InterruptibleSleepUntil` → `interruptibleSleepUntil(&loop, tok, tp)`
- `CancellationSource/Token` → `StopSource/StopToken`
- `ISocket::Read/Write/…` → camelBack
- `*Listener::Bind(...)` → `listen(loop, ListenOptions)`
- `NetErrorCode::BadFileHandle` → `BadHandle`
- `IClock::Now/Refresh` → `now/refresh`
- `FC_ZONE_*` → `CORE_ZONE_*`
- `<FastCache/Async|Net/…>` → `<core/async|net/…>`

contour/endo/tuidu deltas:
- `coro::` → `core::async::`, `net::` → `core::net::`
- `net::IClock` → `core::platform::IClock`
- `IListener::localPort` → `boundPort`
- `NetErrorCode::Other` → `SystemError`
- `EventSource`/`makeDefaultEventSource`/`FdInterest` → `IoBackend`/`makeDefaultBackend`/`Interest`

## 3. Build contract

- **CMake:** `cmake_minimum_required(VERSION 3.25...3.31)`, `project(core-cpp VERSION 0.1.0 LANGUAGES CXX)`. The version literal is the source of truth; the tag must equal it.
- **Global state:** only `cmake/CoreCppTopLevel.cmake` may touch it, and it is included only if `PROJECT_IS_TOP_LEVEL`. Every option, cache variable and function is `CORE_CPP_`/`core_cpp_`-prefixed. Modules are included by absolute path. `tests/cmake/check-cmake-hygiene.cmake` enforces this.
- **Options:**

  | Option | Default |
  |---|---|
  | `CORE_CPP_TESTING` | `PROJECT_IS_TOP_LEVEL` |
  | `CORE_CPP_BUILD_EXAMPLES` | `PROJECT_IS_TOP_LEVEL` |
  | `CORE_CPP_FETCH_DEPS` | ON |
  | `CORE_CPP_WITH_TUI` | ON |
  | `CORE_CPP_WITH_IMAGES` | ON (dependent on TUI) |
  | `CORE_CPP_WITH_TLS` | OFF |
  | `CORE_CPP_WITH_TRACY` | OFF |
  | `CORE_CPP_PEDANTIC` | `PROJECT_IS_TOP_LEVEL` |
  | `CORE_CPP_WERROR` | OFF (presets set ON) |
  | `CORE_CPP_CLANG_TIDY` | OFF; explicitly clears an inherited `CXX_CLANG_TIDY` |
  | `CORE_CPP_SANITIZERS` | "" (list; FATAL if not top-level) |
  | `CORE_CPP_COVERAGE` | OFF |

  Under Emscripten only the WebAssembly subset (§1) builds, and `CORE_CPP_WITH_TUI`/`TLS` are forced OFF.
- **Dependency table** (`cmake/CoreCppDependencies.cmake`). Resolution order is parent target, then `find_package(QUIET)`, then CPM only if `CORE_CPP_FETCH_DEPS`, else FATAL naming the option that needed it.

  | Dependency | Source |
  |---|---|
  | Threads | always, except single-threaded Emscripten (never linked there: it would force `-pthread`/SharedArrayBuffer) |
  | libunicode | v0.9.3 (`unicode::unicode`) when TUI; CPM options pass `PEDANTIC_COMPILER OFF` |
  | stb | 40-hex SHA, DOWNLOAD_ONLY → `stb_image` INTERFACE, when IMAGES |
  | OpenSSL | system only, when TLS |
  | Tracy | same tag as contour's `cmake/Tracy.cmake`, when TRACY |
  | Catch2 | 3.8.0, when TESTING |

- **Toolchain policy, per target:**
  - Always: `/utf-8 /permissive- /Zc:__cplusplus`, and PRIVATE `NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00`.
  - The pedantic union of endo's and fastcached's warning lists, each probed under `CORE_CPP_HAS_<flag>`.
  - `-Werror`/`/WX` only via `CORE_CPP_WERROR`.
  - A sanitizer table.
  - No PUBLIC flags, ever.
  - Every compiled target is appended to the global property `CORE_CPP_TARGETS`. A parent that applies sanitizers or coverage per target (fastcached `CMakeLists.txt:466-508`, endo `enable_sanitizers()`) reads it with `get_property(_t GLOBAL PROPERTY CORE_CPP_TARGETS)`, so no uninstrumented core-cpp code produces TSan false reports.
- **Tests:**
  - One binary per module, `core-cpp-<m>-test`, from `Foo_test.cpp` next to `Foo.cpp`.
  - Each is registered as `add_test(core-cpp.<m>)` with `SKIP_RETURN_CODE 77` and links `core::testing_main`.
  - Exit codes: 1 = any failure, 77 = everything skipped, 2 = nothing ran, 0 = pass. This fixes fastcached #1128/#1152.
  - Labels: `core-cpp`, `<module>`, `loopback`, `canary`, `hygiene`, `no-tsan`.
- **Style:** contour's `.clang-format` (110 columns, east const) with `<core/…>` include categories, and contour's `.clang-tidy` with `HeaderFilterRegex: '.*/src/core/.*'`. Naming IgnoredRegexps cover coroutine and STL hooks, so **no NOLINT**. Tools are pinned: `.clang-format-version` and `.clang-tidy-version` both say 22.1.8 (PyPI).
- **No `install()` in 0.1.0**; targets are install-ready through FILE_SET.
- **Compiler cache: fastcache-cc whenever it is available** (user requirement).
  - `cmake/portable/CompileCache.cmake` is a **verbatim** copy of fastcached `origin/master:cmake/portable/CompileCache.cmake`. Take fastcached's, not endo's, which is 80 lines older. The source SHA is recorded in `cmake/portable/README.md` and `NOTICE`.
  - It is included from `CoreCppTopLevel.cmake`, i.e. after `project()` and **before** `core_cpp_resolve_dependencies()`, so CPM-fetched dependencies (libunicode, Catch2, stb, Tracy) are compiled through the cache too.
  - Selection, as the module does it:
    1. fastcache-cc, when a fastcached daemon accepts its probe compile at `FASTCACHE_ADDR` (default `127.0.0.1:6674`);
    2. sccache, only with `-DALLOW_SCCACHE_FALLBACK=ON`;
    3. ccache;
    4. none.
  - It respects a launcher already set on the command line or in a preset, never fails a configure, and turns off with `-DUSE_COMPILER_CACHE=OFF` (which the `clang-coverage` preset sets). `-DFASTCACHE_AUTO_INSTALL=ON` / `-DFASTCACHE_AUTO_START=ON` install fastcache-cc and start a daemon.
  - When not top-level, core-cpp sets no launcher; its targets inherit the parent's. endo, fastcached and tuidu include the same module.
  - Its unprefixed options (`USE_COMPILER_CACHE`, `ALLOW_SCCACHE_FALLBACK`, `FASTCACHE_*`) are shared across the organisation's projects, so they are exempt by path from the `CORE_CPP_` prefix rule, as is `FetchTransferBound.cmake`.
  - Re-sync it verbatim whenever fastcached's copy changes. The `downstream.yml` nightly reports drift by diffing it against fastcached `master`.

## 4. CI, docs, guidelines, release

- **`build.yml`**, required check `ci-ok`:

  | Job | Contents |
  |---|---|
  | `style` | pinned format, cmake-hygiene, layering, namespace=directory |
  | `linux` | clang-22, gcc-14, gcc-15 (morph's compiler), clang-22 arm64, clang C++26 |
  | `macos` | macos-15, AppleClang and LLVM-22 |
  | `windows` | cl-release, clangcl-release, cl-debug, cl-release-tls (vcpkg) |
  | `sanitizers` | asan+ubsan, tsan (whole suite) |
  | `clang-tidy` | |
  | `emscripten` | matrix emsdk 3.1.56 and latest; builds the WebAssembly subset without `-pthread`; runs its tests under node (`CMAKE_CROSSCOMPILING_EMULATOR`), plus `tests/wasm/HostDrivenTimer_smoke.cpp` (built `-sASYNCIFY`; `emscripten_sleep` yields to the real browser/node event loop until a `HostDrivenBackend` timer and a coroutine `delay` fire) |
  | `consumer-smoke` | CPM (`CPM_core-cpp_SOURCE`, asserts no parent flags or launcher changed) and vendored offline (`docker --network none`, contour's configuration) |
  | `compile-cache` | ubuntu. Configure top-level with `-DFASTCACHE_AUTO_INSTALL=ON -DFASTCACHE_AUTO_START=ON`, assert `CMakeCache.txt`'s `CMAKE_CXX_COMPILER_LAUNCHER` names `fastcache-cc`, build, `fastcache-cc --zero-stats`, delete the object files, rebuild with `FASTCACHE_VERBOSE=1`, and assert that at least one compile reports `HIT` and that `fastcache-cc --show-stats` shows hits. This proves core-cpp builds through fastcache-cc when it is available. The other jobs keep ccache (`hendrikmuhs/ccache-action`), which the module picks when no daemon answers. |
  | `coverage` | not required |

  Other workflows: `docs.yml` (a required check), `release.yml` (tag == `project(VERSION)` and a CHANGELOG section, then a draft release plus a vendor tarball), `downstream.yml` (nightly: consumers against master, plus a diff of `cmake/portable/CompileCache.cmake` against fastcached `master` that fails on drift), `portability.yml` (nightly FreeBSD), `dependabot.yml`.
- **Docs:**
  - mkdocs-material `--strict` (fastcached config and pins).
  - Doxygen at `/api/`, run after mkdocs.
  - Deployed with `upload-pages-artifact@v3` + `deploy-pages@v4`, `site_url https://contour-terminal.github.io/core-cpp/`.
  - Nav: Getting started (CPM, vendoring, building, options) · Modules (index/layering, base, log, cli, platform, async, net, tui, testing) · Design notes (DI, error handling, coroutines and lifetimes, threading, portability, profiling) · Contributing (guidelines, releasing) · Changelog · API.
- **Guidelines:**
  - `AGENT.md` holds tripwires and pointers. `CLAUDE.md` is `@AGENT.md`.
  - `.agent/rules/`:
    - `README.md`
    - `cpp-guidelines.md`: canonical; merges contour AGENT.md, fastcached §C++ guidelines, Lightweight `.agent/cpp-guidelines.md`, endo.
    - `design-principles.md`
    - `library-hygiene.md`: new; covers vendoring-safety, the graduation rule and API/semver.
    - `build-and-toolchain.md`: generic sections of fastcached's.
    - `testing.md`: generic sections of fastcached's.
    - `async-and-net.md`: fastcached wire-and-protocol §Net boundary/§Sockets/§Dialing/§Lifetime.
    - `platform.md`, `tui.md`
  - `.agent/guides/`: `team-run.md`, `profiling-tracy.md`, `consumer-migration.md`, `releasing.md`.
  - `.agent/reference/`: `source-map.md`, `consumers.md`.
  - Dropped as domain-specific: fastcached compile-cache, distributed, consensus, metrics, storage, packaging and service-config rules.
  - Every carried rule cites its origin as a full URL (e.g. `LASTRADA-Software/fastcached#1128`).
- **Repository documents:** README, CHANGELOG (Keep a Changelog; 0.1.0 records every import SHA), CONTRIBUTING, SECURITY, LICENSE (Apache-2.0), NOTICE (contour, endo and "The fastcached Authors" with import SHAs), `.gitattributes` (`* text=auto eol=lf`), `.gitignore`, `.editorconfig`, and a PR template with a "Consumer impact" section.
- **Versioning:** SemVer with `vX.Y.Z` tags. In 0.x a minor may break (recorded under **Breaking** with a migration note). Consumers pin a tag, or a full SHA temporarily, never a branch.

## 5. Vendoring contract (`docs/vendoring.md`, `cmake/CoreCppVendor.cmake`)

- **The copy is verbatim** and byte-identical to a tag or full SHA. **No local changes**: fix upstream, cut a patch release, re-vendor.
- **Commands:**
  - `cmake -DMODE=sync -DREF=<tag> -DDEST=<dir> [-DREPO=<url|path>] [-DMODULES=base;log;cli;platform;async;net;testing] -P <copy>/cmake/CoreCppVendor.cmake`
    - Reads git **blobs** with `git -c core.autocrlf=false -c core.eol=lf`.
    - Refuses CR bytes, symlinks and submodules.
    - Writes `MANIFEST`: header lines `# repository/ref/commit/files`, then `<sha256>  <path>` sorted.
  - `MODE=check` refuses a hash mismatch, a missing file or an unlisted file. It needs no git.
- **File set:** `CMakeLists.txt`, `cmake/**`, `src/core/<selected modules>/**` (including tests), `src/core/*.hpp|cpp` (base), `LICENSE`, `NOTICE`, `README.md`, `CHANGELOG.md`, `.clang-format`, `.clang-tidy`.
- **Consumer obligations:**
  - `.gitattributes` has `vendor/core-cpp/** -text`, and `.clang-format-ignore` has `vendor/**`.
  - Set `CORE_CPP_FETCH_DEPS OFF`, then `add_subdirectory(vendor/core-cpp SYSTEM EXCLUDE_FROM_ALL)`.
  - Register a ctest verbatim check.

## 6. Sequencing

The order is **A** (skeleton and import) → **B** (merge) → **tag v0.1.0** → **C** (consumers, each migrates once).

Consumer PRs open as drafts pinned to `v0.1.0`; for local work, set `-DCPM_core-cpp_SOURCE=D:/core-cpp`. contour's PR merges after endo's and tuidu's, because both fetch from contour master today.

### Upstream sync (amendment, 2026-09-18)
Every import records its upstream SHA per file in `.agent/reference/provenance.md`. Before 0.1.0 there is one catch-up check against each upstream's `origin/master`. Every consumer swap starts with a delta check over the paths it replaces, so no upstream commit is dropped.

## 7. Consumer migration

| Repo (default branch) | Mechanism | PR |
|---|---|---|
| endo (`master`, contour-terminal) | CPM | `build/core-cpp`: stop fetching anything from contour (vtparser is unused); delete `src/tui`, the generic part of `src/platform` and `src/testing` helpers; codemod; drop OpenSSL. |
| tuidu (`master`, contour-terminal) | CPM | `build/core-cpp`: delete `src/{coro,platform,testing,tui}` and the crispy fetch; codemod (43 files); API drift: EventSource mocks, `crispy::cli` PascalCase types. |
| fastcached (`master`, LASTRADA-Software) | CPM | PR-A `claude/<n>-core-cpp-tui`: delete `vendor/`, the vendor-* checks and their scripts; adapter → `core::tui`. PR-B `claude/<m>-core-cpp-async-net`: delete `Async/`, `Net/` and the moved Core; staged semantic rename; benchmark gate. |
| Lightweight (`master`, LASTRADA-Software) | CPM, only under `LIGHTWEIGHT_BUILD_TOOLS` | `feat/dbtool-core-tui`: StandardProgressManager and main.cpp use `core::tui_output`. |
| contour (`master`, contour-terminal) | verbatim `vendor/core-cpp` (base, log, cli, platform, async, net, testing) | `build/vendor-core-cpp`: delete `src/{coro,net}` and crispy's generic half; codemod; link-what-you-include commit first; vtparser includes → `<core/…>`; stays a draft until endo and tuidu merge. |
| morph (`master`, LASTRADA-Software) | CPM, replacing FetchContent for glaze, Catch2 and Lightweight | PR-1 `build/core-cpp`: CPM; TimeoutScheduler's two builds → one wrapper over core-cpp EventLoop timers (native: its own thread; WebAssembly: HostDriven); `net/detail/base64.hpp` → `core::base64`; WakeupPipe → `core::platform::Wakeup`. PR-2 `feat/coroutines`: spec first (`docs/spec/core/coroutines.md`), awaitable `Completion<T>`, `Task<R>` model handlers on the model strand (not re-entrant by default), `morph::async::delay`, stop-token cancellation for execute deadlines. Follow-up issue for the remaining overlap (executors/strand, logger, FileIoOps, DateTime clock, `morph::net` on Windows). |

---

