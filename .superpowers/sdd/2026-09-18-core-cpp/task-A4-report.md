# Task A4 report: `core::platform`, `core::coro` (Generator) and the `core::testing` helpers

Status: **DONE** (CI result below).

## Commits (oldest first, all on `origin/master`)

| SHA | Subject |
|---|---|
| `fe95520` | base: a process-environment writer that never frees what a reader may hold |
| `c7b11d0` | testing: scoped temp dir, working directory and env helpers |
| `48aae7b` | platform: import endo's generic platform layer as core::platform with one merged clock |

Each message ends with a blank line and `Signed-off-by: Christian Parpart <christian@parpart.family>`.
Every file is LF-only (checked: no CR byte in any staged file), and nothing under `.superpowers/`
was committed. Each intermediate commit was built and tested on its own in a detached worktree
(WSL `gcc-release`: `fe95520` 9/9, `c7b11d0` 9/9, hygiene included), so all three are buildable.
The provenance rows are in the same commit as the files they describe.

## Sources and SHAs

- endo `f774a210ce989e5947b8f61d715068b1dc96088c` (= endo `origin/master`), read as blobs.
- contour `6777ff05014f8ff163b071e8b0e942830119db80` (`src/net/platform/{Clock,SystemPipe,WinsockInit}.*`).
- fastcached: `git -C D:\fastcached fetch origin` first; `origin/master` =
  **`b461e8b6d367ed22e4bf2935717fa59360a64b7d`** (fetched nothing new). Files:
  `src/FastCache/Core/{Clock.hpp,Clock_test.cpp,WallClockRef_test.cpp}`. There is no `Clock.cpp`
  or `WallClock*` file; `WallClockRef_test.cpp` also includes fastcached's CacheEngine,
  FleetHistory and SchedulerService, whose asserts were dropped (see below).
- No blob had a CR byte (`grep -lr $'\r'` on the extracted tree: none). No working tree of endo,
  contour or fastcached was read, built or modified.

### Imported files (core-cpp path ← upstream path; all endo at `f774a210`)

Every row is in `.agent/reference/provenance.md` with the full SHA and a note of what changed.

| core-cpp | upstream |
|---|---|
| `src/core/platform/{Types,PlatformError,Wakeup,SignalHandler,SystemPipe,WinsockInit,MessageQueue,FileSystem,NativeFileSystem,FileInfoProvider,EnvironmentProvider,UserPaths,PathUtils,GlobMatch,FileUri,SystemInfo,StringUtils}.hpp` | endo `src/platform/*.hpp` (Types.hpp: Windows halves moved out) |
| `src/core/platform/{NativeFileSystem,SignalHandler,SystemInfo,SystemPipe,WinsockInit,PathUtils,GlobMatch,FileUri}.cpp` | endo `src/platform/*.cpp` |
| `src/core/platform/Clock.hpp` | endo `src/platform/Clock.hpp` **merged** with contour `src/net/platform/Clock.hpp` (`6777ff05`) and fastcached `src/FastCache/Core/Clock.hpp` (`b461e8b6`) |
| `src/core/platform/linux/{LinuxFileInfoProvider.hpp,.cpp,LinuxWakeup.cpp}`, `posix/{PosixEnvironmentProvider.hpp,.cpp,PosixWakeup.cpp}`, `windows/{WindowsEnvironmentProvider,WindowsFileInfoProvider}.{hpp,cpp}`, `windows/WindowsWakeup.cpp` | endo `src/platform/{linux,posix,windows}/…` |
| `src/core/platform/windows/WindowsTypes.cpp` | endo `src/platform/Types.hpp` (its Windows function bodies) |
| `src/core/platform/testing/{InMemoryFileSystem.hpp,.cpp,MockFileInfoProvider.hpp,TestEnvironmentProvider.hpp}` | endo `src/platform/testing/…` |
| tests: `Clock_test` (merged with fastcached `Clock_test.cpp` + `WallClockRef_test.cpp` at `b461e8b6`), `SystemPipe_test`, `FileSystem_test`, `PlatformError_test`, `EnvironmentProvider_test`, `FileInfoProvider_test`, `FileUri_test`, `Wakeup_test`, `MessageQueue_test` | endo `src/platform/*_test.cpp` |
| `PathUtils_test.cpp`, `Types_test.cpp`, `UserPaths_test.cpp` | endo `src/platform/WindowsPlatform_test.cpp`, **split** so the wasm subset can run its part |
| `src/core/coro/Generator.hpp`, `Generator_test.cpp` | endo `src/platform/Generator{.hpp,_test.cpp}` |
| `src/core/testing/{ScopedTempDir,ScopedWorkingDirectory,EnvHelper}.hpp`, `ScopedTempDir_test.cpp` | endo `src/testing/…` |
| origin core-cpp (tests the upstream never had) | `GlobMatch_test`, `StringUtils_test`, `SignalHandler_test`, `SystemInfo_test`, `EnvHelper_test`, `ScopedWorkingDirectory_test`; CMakeLists of `platform/` and `coro/` |

## Files left in endo, and why

`Process`, `Pipe`, `WaitResult`, `ProcessProvider` (+ `linux/`, `darwin/`, `windows/` providers,
`ProcStatusParser`), `ProjectFileTree`, `InstallPaths`, `InterruptThrottle`, `windows/{WindowsPipe,
WindowsProcess}.cpp`, `testing/{MockPipe,MockProcessManager,MockProcessProvider}.hpp`, their tests
(`Process_test`, `Pipe_test`, `ProjectFileTree_test`, `InterruptThrottle_test`,
`{Linux,Darwin}ProcessProvider_test`), `test_main.cpp` (core::testing_main replaces it), and
`src/testing/{SuppressWindowsDialogs*,WindowsDialogCanary}` (A1 merged those). They are the
shell's process/job machinery (spec Part I §1 "Stays in endo"). **No generic file depends on
them** (checked by include graph), so no NEEDS_CONTEXT. The only coupling: endo's Windows process
code used the `STDIN_FILENO`/`SIG*` fallbacks `Types.hpp` defined on Windows; core-cpp's
`Types.hpp` no longer defines them (migration row added; endo's process code defines its own at C1).
The `namespace endo { using … }` compatibility aliases were not imported.

## The Clock merge

One header, `<core/platform/Clock.hpp>`, header-only, namespace `core::platform`:

- `SteadyTimePoint`, `SteadyDuration` (endo/contour names).
- `IClock`: `now()` (pure), and fastcached's `refresh()` — virtual, **no-op by default**, with
  fastcached's contract text (the event loop calls it after the wait and before computing the
  timeout; safe from any thread). Copy/move deleted.
- `SteadyClock`, `defaultSteadyClock()` (endo/contour).
- `CachedClock` (fastcached): initial sample in the constructor, CAS publication that only moves
  forward.
- `ManualClock` with `advance()`/`setNow()` (endo/contour; fastcached `Advance`/`SetNow`).
- `IWallClock` (with fastcached's LOAD-BEARING comment on its deleted copy/move),
  `SystemWallClock`, `ManualWallClock` (`advance`/`setNow`), `defaultSystemWallClock()`,
  `WallClockRef` (implicit from `IWallClock const&`, deleted from `IWallClock const&&`, `get()`,
  `now()`).
- fastcached-specific wording (CacheEngine, EXPIREAT, reactor) was generalised; the fastcached#1028
  and #1446 references are kept.

Tests: one `Clock_test.cpp`. Duplicates dropped (fastcached's SteadyClock-monotonic, starts-at,
Advance, SetNow merged into endo's cases; "multiple Advances accumulate" folded into endo's
advance case). Kept every distinct behaviour: refresh no-op, CachedClock construct/hold/
never-backwards/concurrent-convergence, the four `WallClockRef` static_assert rows, the
borrowed-clock test. Added: `ManualWallClock` advance/setNow, `defaultSystemWallClock` singleton,
and a static_assert that `IWallClock` is neither copy- nor move-constructible. The concurrent case
uses `std::thread` with a joiner and a bounded start gate (fastcached's `tests/BoundedWait.hpp` is
reactor test infrastructure, not importable) and is compiled only where threads exist.

### Rename table (added to `.agent/guides/consumer-migration.md`)

| From | To |
|---|---|
| `<FastCache/Core/Clock.hpp>`, `FastCache::{IClock,SteadyClock,CachedClock,ManualClock,IWallClock,SystemWallClock,ManualWallClock,WallClockRef}` | `<core/platform/Clock.hpp>`, same names in `core::platform` |
| `FastCache::TimePoint`, `Duration` | `SteadyTimePoint`, `SteadyDuration` |
| `IClock::Now`, `Refresh` | `now`, `refresh` (row existed) |
| `ManualClock::Advance/SetNow`, `ManualWallClock::Advance/SetNow` | `advance/setNow` |
| `IWallClock::Now`, `WallClockRef::Now`, `WallClockRef::Get` | `now`, `now`, `get` |
| `FastCache::DefaultSystemWallClock()` | `core::platform::defaultSystemWallClock()` |
| `net::{IClock,SteadyClock,ManualClock,defaultSteadyClock,SteadyTimePoint,SteadyDuration}` | `core::platform::…` |
| `net::NativeHandle`, `InvalidHandle`, `platformRead/Write/Close`, `<net/platform/NativeHandle.hpp>` | `core::platform::…`, `<core/platform/Types.hpp>` |
| `net::ensureWinsockInitialized`, `net::createSystemPipe` (NetError/IoResult) | `core::platform::…` (PlatformError) |
| `endo::platform::`, `<platform/X.hpp>`, `endo::testing::`, `<testing/X.hpp>` | `core::platform::`, `<core/platform/X.hpp>`, `core::testing::`, `<core/testing/X.hpp>` |
| `endo::Generator`, `ENDO_GENERATOR_FORCE_FALLBACK` | `core::coro::Generator`, `CORE_GENERATOR_FORCE_FALLBACK` |
| `endo::containsGlobChars`/`globMatchFilename`; the `namespace endo` aliases incl. `endo::TestEnvironment` | `core::platform::…`; `TestEnvironmentProvider` |

## Deviations from a verbatim import (each with its reason; all in provenance notes)

1. **`Types.hpp` without `<windows.h>`** (`.agent/rules/platform.md`: a public header never
   includes it). `NativeHandle = void*`, `ProcessId = unsigned long`, `InvalidHandle` via
   `reinterpret_cast`; Windows bodies in `windows/WindowsTypes.cpp`, with `static_assert`s that
   the spellings equal `HANDLE`/`DWORD`. The `STDIN_FILENO`/`SIG*` Windows fallback macros are gone.
2. **SystemPipe = endo's API + contour's behaviour.** contour's copy (the one contour's
   `EventLoop::post()` and endo's HttpServer actually use) is non-blocking/close-on-exec with
   `MSG_NOSIGNAL` and reports a full-buffer write as done; endo's blocked. Taking endo's verbatim
   would have regressed contour at A6 (a blocking `post()` producer). Local `isWouldBlock` and
   `makeNonBlockingCloexec` helpers, since `core::net` does not exist yet.
3. **Generator.** Namespace `core::coro`; the fallback is always defined as
   `detail::GeneratorFallback` and every test runs over both; `<version>` is included first
   (endo's picked `std::generator` depending on what the file included first — on MSVC too — so a
   virtual function returning one could have two types in one program); **libstdc++ gets the
   fallback** because GCC 14 `-O2 -Wnull-dereference` fires inside libstdc++'s own
   `std::generator` (measured in `gcc-release`), which `-Werror` makes fatal and no core-cpp code
   can fix. Net effect: MSVC uses `std::generator`, everything else the fallback — what endo did
   in practice, now deterministic.
4. **Environment access without `getenv/setenv/unsetenv`** (clang-tidy `concurrency-mt-unsafe`,
   and `core/Environment.hpp` already says first-party code never calls `setenv()`):
   - new `core::setProcessEnvironmentVariable()` / `unsetProcessEnvironmentVariable()` in
     `core::base` (commit `fe95520`): on POSIX a copy-on-write `environ` block published under
     `LiveEnvironment`'s mutex and never freed (a `getenv()` elsewhere may hold an old block);
     Windows `SetEnvironmentVariableA`.
   - `PosixEnvironmentProvider` reads through `LiveEnvironment`, exports through the writer;
   - `UserPaths` takes a `core::Environment const&` (zero-arg overloads read `LiveEnvironment`);
     its tests use `FakeEnvironment` instead of mutating the process;
   - `EnvHelper`: POSIX through the writer/`LiveEnvironment`, Windows keeps `_putenv_s`
     (measured: `LiveEnvironment` sees it).
   - Found by the writer's test: `LiveEnvironment::get` on Windows read an **empty value as
     unset**; fixed (CHANGELOG `Fixed`).
5. `SignalHandler`: static data members and private static handlers moved to file scope (the 7
   NOLINTs), `sigprocmask` → `pthread_sigmask` (tidy).
6. `FileEntry`/`testing::FileEntry` string members got `{}` initialisers (clang 22
   `-Wmissing-designated-field-initializers` in the tests' designated initialisers).
7. `NativeFileSystem`: `strerror` → `error_code::message()`; recase temp name `.recase-<n>`.

## NOLINTs removed (7) and loops converted (9)

| Where | Resolution |
|---|---|
| `SignalHandler.hpp` ×7 `NOLINT(readability-identifier-naming)` on `_callback`, `_signalFd`, `_sigintPending`, `_interruptWakeup`, `_sigChldPending`, `_sigTstpPending`, `_sigContPending` | code: file-scope variables in `SignalHandler.cpp` (A3's App pattern) |

| Loop | Conversion |
|---|---|
| `MessageQueue_test` `for (i = 0; i < 10; ++i)` | `std::views::iota(0, 10)` |
| `NativeFileSystem.cpp` ×2 `for (; !ec && it != end; it.increment(ec))` | `while` with `increment(ec)` at the end of the body (one of them around a `co_yield`) |
| `PosixEnvironmentProvider::keys` `for (char** env = environ; …; ++env)` | `while` over the block |
| `InMemoryFileSystem::resolveSymlinks` `for (hop = 0; hop < MaxHops; ++hop)` | iota |
| `InMemoryFileSystem::removeAll` ×3 erase-while-iterating loops | `std::erase_if` (returns the count the loops summed) |
| `WindowsEnvironmentProvider::keys` `for (p = envBlock; *p;)` | `while` |

clang-tidy 22.1.8 (`clang-tidy` preset, WSL) found 15 more, all fixed in code:
`bugprone-optional-value-conversion` ×3 (`userName()`), `readability-container-contains` ×4,
`readability-function-cognitive-complexity` (GlobMatch 54 > 50: bracket matching and the star
backtrack extracted), `concurrency-mt-unsafe` ×4 (`sigprocmask`), `modernize-return-braced-init-list`
×2, `cppcoreguidelines-prefer-member-initializer` (LinuxWakeup), plus `misc-const-correctness`
×33 in the tests and one `readability-redundant-declaration` / `use-concise-preprocessor-directives`
pair in `Environment.cpp`. MSVC `cl-debug`: C4702 (unreachable loop increment after an
unconditional `break`) in `FileSystem_test`; the break is conditional now. No `-Wno-*` row, no
pragma, no NOLINT added.

## WebAssembly subset

- `platform` row `PLATFORMS wasm-subset`; `SOURCES_EMSCRIPTEN` = `FileUri.cpp GlobMatch.cpp
  PathUtils.cpp` (Types, PlatformError, Clock, StringUtils are header-only). **There is no
  `NativeHandle.hpp`**: `NativeHandle` is in `Types.hpp`, as in endo; contour's
  `net/platform/NativeHandle.hpp` is a trimmed copy of it, which A6 replaces with `Types.hpp`.
- Subset tests under node: `Clock`, `FileUri`, `GlobMatch`, `PathUtils`, `PlatformError`,
  `StringUtils`, `Types` (140 assertions in 43 test cases on 3.1.56). Compiled out there: the
  concurrent `CachedClock` case (threads) and `platformRead`'s closed-pipe EOF case (Emscripten's
  PIPEFS reports EAGAIN, not EOF, after the writer closes — measured `-1 == 0`).
- `coro` row `any`, INTERFACE, std only: Generator is the fallback on libc++ 17 and passes
  (12 assertions / 10 cases); `core::testing` helpers pass under node (28 assertions).
- `ninja -t commands | grep -c pthread` = 0.

## TDD RED/GREEN

1. **Import (tests first).** Tests and their CMake registration only, no sources: WSL
   `clang-debug` failed with 17 × `fatal error: 'core/…​.hpp' file not found` (every platform test
   and `core/coro/Generator.hpp`). Same for the testing helpers (3 × file not found). GREEN: all.
2. **Writer.** `Environment_test` cases first: `no member named 'setProcessEnvironmentVariable'`.
   GREEN on POSIX; on Windows it then went RED on `CHECK(live.get(Name) == "")` → `{?} == ""`
   (the empty-value bug), GREEN after the fix.
3. **Generator include order.** With `<version>` removed from the header: gcc-debug and MSVC
   `cl-debug` both fail `static assertion failed: the standard library has std::generator, so
   Generator must be it`. GREEN with it.
4. **SystemPipe non-blocking.** endo's `SystemPipe.cpp` swapped back in: the 3 new cases fail
   (`O_NONBLOCK`/`FD_CLOEXEC` checks, the full-channel REQUIRE, the empty-read). GREEN merged
   (2069 assertions in 5 cases).
5. **Types.hpp without windows.h.** endo's `Types.hpp` swapped back in on clang-cl:
   `static assertion failed due to requirement '1 == 0': CORE_CPP_TYPES_INCLUDED_WINDOWS_H == 0`.
   GREEN with the new header.

## Local results (final tree `48aae7b`)

| Configuration | Result |
|---|---|
| Windows `clangcl-debug` (`--clean-first`) | build OK, ctest **14/14** |
| Windows `cl-debug` (`--clean-first`) | build OK, ctest **14/14** |
| WSL `clang-debug` | build OK, ctest **11/11** |
| WSL `gcc-debug` | build OK, ctest **11/11** |
| extra: WSL `gcc-release` | build OK, ctest 11/11 (after the libstdc++ Generator change) |
| extra: WSL `clang-tidy` preset, pinned 22.1.8 | clean, ctest 11/11 |
| extra: WSL `emscripten` 3.1.56 under node | build OK, ctest 11/11, 0 pthread commands |
| `python scripts/clang-format.py --check` | 112 files formatted with 22.1.8 |
| hygiene (`core-cpp.cmake-hygiene`) | clean (137 files) |
| `python -m mkdocs build --strict` | rc 0 |

## CI

CI run [35356388834](https://github.com/contour-terminal/core-cpp/actions/runs/35356388834) on
`48aae7b`: **success**, all 21 jobs (20 plus `ci-ok`): linux (gcc-14, gcc-15, clang-22,
clang-22-cxx26, clang-22-tracy, clang-22-arm64), macos (appleclang, llvm-22), windows (cl-debug,
cl-release, cl-release-tls, clangcl-release), emscripten (emsdk 3.1.56, emsdk latest), sanitizers
(clang-asan-ubsan, clang-tsan), clang-tidy, coverage, compile-cache, style, ci-ok. Docs run
[35356389009](https://github.com/contour-terminal/core-cpp/actions/runs/35356389009): success.

## Concerns

1. **Scope additions beyond a verbatim import**, each forced by a rule and flagged for review:
   the process-environment writer in `core::base` (to avoid NOLINT on `setenv`), SystemPipe taking
   contour's non-blocking behaviour (else A6 regresses contour), `Types.hpp` dropping `<windows.h>`
   and the Windows `SIG*`/`STD*_FILENO` fallbacks (endo's process code must define its own at C1),
   `UserPaths` gaining `core::Environment` overloads.
2. **Generator on libstdc++ is the fallback**, not `std::generator`, because of GCC 14's
   `-Wnull-dereference` inside libstdc++. Consumers see no API difference (both are input ranges,
   endo used the fallback there in practice).
3. **Exceptions remain** where upstream threw: `Wakeup`'s constructor (filed
   [core-cpp#14](https://github.com/contour-terminal/core-cpp/issues/14), linked from the rulebook
   and module page, same treatment as #13) and the `ScopedTempDir`/`ScopedWorkingDirectory`
   fixtures (intended: a test that cannot set up fails).
4. **`bool` parameters in imported API** (`FileSystem::openWrite(path, bool append)`,
   `copyFile(…, bool overwrite)`) conflict with "enum class over bool"; kept for C1's sake. Not
   filed.
5. **The writer never frees** what it publishes: each `set`/`unset` costs one pointer block
   (~8 bytes × environment size). Fine for a shell's exports and tests; documented.
6. **Local clang-cl header deps**: the local fastcache-cc predates the #1531 fix, so a clang-cl
   rebuild after a header edit silently skipped a TU (seen once); every clang-cl result above is
   from `--clean-first`.

## Fix round 1 (ruling R31, with the user's refinements)

Dispatch R31 after the approving review (`task-A4-review.md`), pulled onto `0cf5bf7`. The user
then refined items #2, #3, #4, #5 (no longer deferred), #7 and #8, and decided core-cpp#14 (closed:
`Wakeup`'s constructor keeps throwing). Items #1 and #6 stand as first done.

### Commits (oldest first, all on `origin/master`)

| Commit | Subject | Item |
|---|---|---|
| `2cfa713` | platform: say what the code does without endo's shell in the wording | #4 |
| `c7c89aa` | base: an environment write that changes nothing publishes nothing | #6 |
| `d093148` | platform: native provider factories for a composition root | #2 |
| `b1e5934` | platform: test the wall-clock borrow through retainers, and the zero-argument UserPaths | #1 (#3 superseded by `2af8d90`) |
| `2991ab1` | docs: platformRead's EOF under Emscripten, and the test-fixture exception carve-out | #7, #8 (both superseded below) |
| `e418d66` | platform: SystemPipe::read tells bytes, an empty channel and the end of the stream apart | #5 |
| `5357fbc` | platform: one POSIX file-info provider for Linux, macOS, the BSDs and Emscripten | #2 |
| `2af8d90` | platform: test the user paths through fake environments only | #3 |
| `18c5b9d` | platform: say what the remaining comments mean for any program, not a shell | #4 |
| `c0253cb` | platform: test platformRead's drained pipe under Emscripten as it behaves there | #7 |
| `629629e` | docs: exceptions are for unrecoverable conditions, and Wakeup's is one | #8, #14 |

### Per item (the final state)

1. **Retainer rows** (`Clock_test.cpp`): test-local `Retainer` (explicit, takes and stores a
   `WallClockRef`) and `Forwarder(ManualClock&, WallClockRef, int)`, which hands the borrow on to
   two retainers among other arguments. Paired `std::is_constructible` rows prove an lvalue is
   accepted and an rvalue (`SystemWallClock`, `ManualWallClock`) refused, directly and through the
   forwarder. For contrast, `OverloadRetainer`/`OverloadForwarder` show a deleted
   `IWallClock const&&` overload refusing a direct temporary but letting one through a forwarding
   constructor. A runtime case checks both hops still answer as the borrowed clock.
2. **A default provider per platform, through public factories** returning
   `std::unique_ptr<Interface>`, so endo includes no private header:

   | Factory | Windows | Linux, macOS, the BSDs | Emscripten |
   |---|---|---|---|
   | `nativeEnvironmentProvider()` | `WindowsEnvironmentProvider` | `PosixEnvironmentProvider` | `PosixEnvironmentProvider`, over the environment Emscripten's libc keeps for the module |
   | `nativeFileInfoProvider()` | `WindowsFileInfoProvider` | `PosixFileInfoProvider` (`lstat(2)`: every field, symlinks unfollowed with their targets, blocks, device, inode) | `PosixFileInfoProvider`, over Emscripten's virtual filesystem |

   endo's `LinuxFileInfoProvider` used nothing Linux-specific, so it became the generic POSIX
   provider (`posix/PosixFileInfoProvider`, renamed and moved) rather than being copied into a
   second, identical one. Both POSIX providers and their tests joined the WebAssembly subset.
   Emscripten's `readlink()` (3.1.56) resolves a relative symlink target against the link's
   directory, which the factory documents and the two symlink-target tests accept there.
   **Tests per platform:** each factory's case (dynamic type, then real use: set/export/unset for
   the environment, listing a directory with a file and a subdirectory for file info) runs on
   the Linux, macOS, Windows and both Emscripten CI jobs; the lstat cases run on Linux, macOS and
   Emscripten. No CI job runs a BSD proper.
3. **User paths through fakes only.** `homeDirectory()`/`configHome()` are one function each,
   whose `core::Environment const&` parameter defaults to a `core::LiveEnvironment`; a call without
   one runs the body the `FakeEnvironment` cases test. The case comparing against the live
   environment is gone; `static_assert(requires { homeDirectory(); })` checks the zero-argument
   call exists without making it. `EnvironmentProvider`'s own zero-argument `homeDirectory()` and
   `configHome()` had no test; seven cases over a `TestEnvironmentProvider` now cover them.
4. **Wording.** `2cfa713` reworded every flagged phrase (nothing was deleted): "a program", "a
   process", "a command line", "in-process work", "a program that runs jobs in the foreground and
   background". `18c5b9d` did the rest: a PATH lookup (execvp(3)'s or a command interpreter's), a
   hostname asked for on every redraw of a status line, whatever shows the user the directory,
   SIGTSTP from the controlling terminal, and the environment writer's example (a variable a
   program exports to its children).
5. **`SystemPipe::read` returns `std::expected<ChannelResult, PlatformError>`.** `ChannelResult`
   (in `SystemPipe.hpp`) is default-constructed empty, `ChannelResult::bytes(n)` or
   `ChannelResult::endOfStream()`; queries `empty()` (nothing yet, the writer still there),
   `isEndOfStream()` (the writer closed and everything was read) and `bytesRead()`; defaulted
   `operator==`. It has no `size()`: a container's `size()` is zero exactly when it is
   `empty()`, and the end of the stream is neither (clang-tidy's container check made the point).
   A would-block (and EINTR) is empty, `recv()` returning 0 is the end of the stream, and only a
   real failure is `PlatformError::IoError`, on Windows as on POSIX. A zero-byte read returns
   empty without touching the channel. On Windows the `recv()` length is clamped to `INT_MAX`.
   Tests on every platform: data, empty, end of stream (via `shutdown()` of the write direction:
   the bytes first, then the end, which stays), failure (POSIX: `dup2` of `/dev/null` over the
   read end, so `recv()` gets ENOTSOCK; Windows: `shutdown(SD_RECEIVE)`, so WSAESHUTDOWN), and
   the zero-byte read. No caller in core-cpp; the platform page, the migration guide (a new row:
   test `isEndOfStream()` where code tested for 0, `empty()` where it tested for a would-block),
   CHANGELOG and provenance say it.
6. **Writer** (`src/core/Environment.cpp`): an identical write publishes no new block; the mutex
   comment says it serialises the writer; the header says not to use it between `fork()` and
   `exec()`.
7. **Emscripten's pipe EOF.** `Types_test`'s drained-pipe case now runs under Emscripten too and
   checks what happens there: -1 with errno EAGAIN (6 assertions pass under node locally). The
   reason, from Emscripten's `library_pipefs.js`: its pipes act as if the read end were always
   non-blocking, so an empty pipe reads EAGAIN whether or not the writer has closed, and nothing
   tells a finished pipe from one that is only empty for now. `platformRead()`'s doc and the
   platform page say so and how code that must run there learns the writer is done.
8. **The exceptions rule** (`cpp-guidelines.md`) now reads: recoverable errors return
   `std::expected`; exceptions are for unrecoverable conditions, and for cancellation
   (`OperationCancelled`). It defines unrecoverable (no caller up to `main()` has a meaningful
   alternative), gives examples (`Wakeup`'s channel refused, a fixture that cannot set up, memory
   exhaustion), lists what is recoverable (missing file, refused permission, malformed input,
   closed peer, empty channel, timeout, full buffer, busy resource: when in doubt, a value), and
   keeps precondition violations as assertions. `design-principles.md` and `AGENT.md` say the
   same. **Wakeup:** `Wakeup.hpp` documents the constructor's `std::runtime_error` as that
   unrecoverable condition; the #14 links are gone from `cpp-guidelines.md`, `platform.md`
   (rules, open work) and `docs/modules/platform.md`. Spec, plan and #14 untouched.

### RED/GREEN

- Writer short-circuit: the "publishes nothing for a write that changes nothing" case failed first
  (`processEnviron() == published`, `0x…3110 == 0x…3df0`); GREEN with the short-circuit.
- Factories: the tests failed to compile first (`use of undeclared identifier
  'nativeEnvironmentProvider'` / `'nativeFileInfoProvider'`).
- Retainer rows: with `WallClockRef`'s deleted rvalue constructor removed, 5 negative rows fire,
  the `Retainer` and `Forwarder` rows among them.
- `ChannelResult`: the new tests failed to compile against the old API (`no member named
  'ChannelResult' in namespace 'core::platform'`). With the type in place but the old mapping (0
  as empty, a would-block as `IoError`), 2 of 8 SystemPipe cases failed: the empty case
  (`REQUIRE( got.has_value() )`) and the end of stream (`second->isEndOfStream()`,
  `!second->empty()`, the repeated end). GREEN with the new mapping, on Linux and Windows.
- Emscripten file info: under node the two symlink-target cases failed first (`"/tmp/core_ls_test_…/real.txt" == "real.txt"`), which is how the readlink deviation was found.
- The `EnvironmentProvider` `homeDirectory()`/`configHome()` cases pin behaviour that already
  existed; they passed on first run.
- Caught locally and folded in before each push: GCC's `-Wshadow` (the `Forwarder` parameters),
  clang-cl's `-Wunused-function` (the symlink-target helper on Windows), clang-tidy's
  `readability-container-size-empty` (which led to `bytesRead()`).

### Local results (final tree `629629e`)

| Configuration | Result |
|---|---|
| Windows `clangcl-debug` (`--clean-first`) | build OK, ctest **14/14** |
| Windows `cl-debug` (`--clean-first`) | build OK, ctest **14/14** |
| WSL `clang-debug` | build OK, ctest **11/11** |
| WSL `gcc-debug` | build OK, ctest **11/11** |
| extra: WSL `gcc-release` | build OK, ctest 11/11 (before the `bytesRead()` rename) |
| extra: WSL `clang-tidy` preset, pinned 22.1.8 | clean, ctest 11/11 |
| extra: WSL `emscripten` 3.1.56 under node | build OK, ctest 11/11 (before the `bytesRead()` rename, which touches nothing in the subset) |
| `python scripts/clang-format.py --check` | 112 files formatted with 22.1.8 |
| hygiene (`core-cpp.cmake-hygiene`, in every ctest above) | clean |
| `python -m mkdocs build --strict` | rc 0 |

Commands, and the line each printed (Windows from a VS 18 developer shell,
`Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`; WSL from Ubuntu 26.04):

```text
# Windows, for P in clangcl-debug cl-debug
cmake --preset P
cmake --build --preset P --clean-first      -> no error, no warning; exit 0
ctest --preset P                            -> 100% tests passed, 0 tests failed out of 14

# WSL, for P in clang-debug gcc-debug gcc-release clang-tidy
cmake --preset P && cmake --build --preset P && ctest --preset P
                                            -> 100% tests passed, 0 tests failed out of 11

# WSL, emsdk 3.1.56
source ~/emsdk/emsdk_env.sh
cmake --preset emscripten && cmake --build --preset emscripten && ctest --preset emscripten
                                            -> 100% tests passed, 0 tests failed out of 11
node out/build/emscripten/src/core/platform/core-cpp-platform-test.js "platformRead*"
                                            -> All tests passed (6 assertions in 1 test case)

# the SystemPipe cases alone
out/build/clang-debug/src/core/platform/core-cpp-platform-test "[systempipe]"
                                            -> All tests passed (2088 assertions in 8 test cases)
core-cpp-platform-test.exe "[systempipe]"   (clangcl-debug)
                                            -> All tests passed (32 assertions in 6 test cases)

python scripts/clang-format.py --check      -> clang-format.py: 112 file(s) are formatted with clang-format 22.1.8
python -m mkdocs build --strict             -> INFO - Documentation built in 0.38 seconds
```

Hygiene is `core-cpp.cmake-hygiene`, one of the 11 (WSL) and 14 (Windows) tests above. Of the
eight SystemPipe cases, two are POSIX-only (`O_NONBLOCK`/`FD_CLOEXEC`, a full channel), hence 6
on Windows; the 2088 assertions on Linux are mostly the full-channel write loop.

### CI

Pushed `2991ab1..629629e` (six commits on top of the first pass, which went out as
`0cf5bf7..2991ab1`).

- **Final:** Build run [35365981481](https://github.com/contour-terminal/core-cpp/actions/runs/35365981481)
  on `629629e`: **success**, all 21 jobs, `ci-ok` included: linux (gcc-14, gcc-15, clang-22,
  clang-22-cxx26, clang-22-tracy, clang-22-arm64), macos (appleclang, llvm-22), windows
  (cl-debug, cl-release, cl-release-tls, clangcl-release), emscripten (emsdk 3.1.56, emsdk
  latest), sanitizers (clang-asan-ubsan, clang-tsan), clang-tidy, coverage, compile-cache, style,
  ci-ok. Docs run [35365981527](https://github.com/contour-terminal/core-cpp/actions/runs/35365981527):
  success.
- First pass: Build run [35360519932](https://github.com/contour-terminal/core-cpp/actions/runs/35360519932)
  on `2991ab1`: success, 21/21; Docs run
  [35360519915](https://github.com/contour-terminal/core-cpp/actions/runs/35360519915): success.

### Concerns

1. **One POSIX file-info provider, not a Linux one plus a generic one.** The user asked for Linux,
   Windows and a generic POSIX provider for macOS/BSD. endo's Linux provider was already generic
   POSIX, so it is now that provider for all of them; a separate Linux copy would have been
   identical. A Linux-specific provider (`statx`, say) would be a new addition. No CI job runs a
   BSD proper; macOS is the non-Linux POSIX system CI covers.
2. **The spec still states the old exceptions rule**, and `design-principles.md` cites it ("the
   design spec, Part I §2"); the controller is amending the spec and plan.
3. **`core::cli` still throws `ParserError`** for malformed input, which the new rule calls
   recoverable; core-cpp#13 (open) covers it.
4. **Emscripten's `readlink()`** in 3.1.56 resolves relative targets; the tests accept that or the
   verbatim text, so a later emsdk that returns the text verbatim passes too.
5. `EnvironmentProvider::homeDirectory()`/`configHome()` repeat `UserPaths`' logic;
   core-cpp#7 (unifying the two environment seams) is where that would go.
6. Concern 3 in the original report ("filed core-cpp#14") is settled by the user's decision.

## Fix round 2 (ruling R32)

From the re-review (`task-A4-rereview-r1.md`), pulled onto `904b6bc`.

### Commits (oldest first, all on `origin/master`)

| Commit | Subject | Item |
|---|---|---|
| `9e5922e` | base: an environment block that names a variable twice is published over | 1 |
| `a92e0e9` | docs: what the WebAssembly subset may not use, and two precisions | 2, 3, 4 |

### Per item

1. **The identical-write short-circuit took the last duplicate** (`src/core/Environment.cpp`).
   `unchanged` was re-set for every entry with the name, so the last one decided, while readers
   take the first. The fix is the reviewer's: `unchanged = !removed && value && …; removed = true;`,
   so only a first and single entry that already reads the value short-circuits, and a second
   entry makes the writer publish, which drops the duplicates. The comment says why. The new test
   ("the process-environment writer publishes for a variable named twice", POSIX) installs a
   block through a new test-local `InstalledBlock` fixture (a copy of the environment plus given
   entries, the original put back on destruction). It has two sections: `X=old` then `X=same`
   (the reported case: after setting `same`, X must read `same` and be named once), and `X=same`
   then `X=old` (the write must publish a new block, name X once, and read `same`). In the same
   file, a comment in the neighbouring test lost its "shell" example ("a program re-applying its
   configuration").
2. **`.agent/rules/platform.md`** now says what the subset may not use: `std::thread`, blocking
   waits, `Threads::Threads`, sockets, child processes and signal handlers. It also says the
   filesystem is allowed (Emscripten's virtual one, which the POSIX file-info provider lists and
   `lstat()`s), and so are its in-memory pipes, which `platformRead()` reads.
3. **`docs/modules/platform.md`**'s factory table has the "(3.1.56 at least)" hedge for
   Emscripten's `readlink()`, as `FileInfoProvider.hpp` has.
4. **`unsetProcessEnvironmentVariable()`** states the `fork()`/`exec()` restriction and its reason
   in its own doc, and that removing an unset variable publishes nothing.

### RED/GREEN (item 1)

RED, the test added before the fix, `core-cpp-base-test "the process-environment writer publishes
for a variable named twice"` on WSL `clang-debug`:

```text
  the value only the second entry holds
/mnt/d/core-cpp/src/core/Environment_test.cpp:372: FAILED:
  CHECK( live.get(Name) == "same" )
with expansion:
  {?} == "same"
/mnt/d/core-cpp/src/core/Environment_test.cpp:373: FAILED:
  CHECK( entriesNaming(Name) == 1 )
with expansion:
  2 == 1
test cases: 1 | 1 failed
```

(`{?}` is the `std::optional` Catch2 cannot print; it held `old`.) The second section, first
entry already `same`, passed before the fix too: the old code's last-entry comparison happened to
publish there. It is kept as the guard for the other half of the rule.

GREEN, after the fix: `core-cpp-base-test "[environment]"` -> `All tests passed (48 assertions in
9 test cases)`.

### Commands and output (final tree `a92e0e9`)

```text
# Windows, VS 18 developer shell (Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64)
cmake --preset clangcl-debug
cmake --build --preset clangcl-debug --clean-first   -> no error, no warning; exit 0
ctest --preset clangcl-debug                         -> 100% tests passed, 0 tests failed out of 14

# WSL (Ubuntu 26.04), for P in clang-debug gcc-debug clang-tidy
cmake --preset P && cmake --build --preset P && ctest --preset P
                                                     -> 100% tests passed, 0 tests failed out of 11

python scripts/clang-format.py --check               -> clang-format.py: 112 file(s) are formatted with clang-format 22.1.8
python -m mkdocs build --strict                      -> INFO - Documentation built in 0.35 seconds
```

Hygiene (`core-cpp.cmake-hygiene`) is one of those ctest tests. `gcc-debug` and `clang-tidy` go
beyond what was asked; the emscripten job ran in CI.

### CI

Pushed `904b6bc..a92e0e9`. Build run
[35367968230](https://github.com/contour-terminal/core-cpp/actions/runs/35367968230) on `a92e0e9`:
**success**, all 21 jobs, `ci-ok` and both emscripten jobs (emsdk 3.1.56, emsdk latest) included.
Docs run [35367968260](https://github.com/contour-terminal/core-cpp/actions/runs/35367968260):
success.
