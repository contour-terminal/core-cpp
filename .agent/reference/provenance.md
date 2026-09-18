# Provenance

For every file under `src/core/`, `cmake/portable/` and `cmake/FetchTransferBound.cmake`: which
upstream file and commit it came from, or `origin: core-cpp` for code written here. This is the
mechanical record behind the Global Constraints' "Upstream sync discipline" —
[`tests/cmake/check-cmake-hygiene.cmake`](../../tests/cmake/check-cmake-hygiene.cmake)'s
`provenance` rule refuses a file in scope without a row, and a row naming a file that no longer
exists.

An import or port task (A4–A7, B1–B12) appends or bumps rows in the same commit as the import.
Task B12b reads this table mechanically, before v0.1.0, to catch up every row whose upstream has
moved since it was synced. A consumer migration's delta check (`.agent/guides/`) reads it the same
way. `NOTICE` and `CHANGELOG.md` record the same commits at the granularity of a whole import; this
table is the per-file index into them.

Repos: [`contour-terminal/contour`](https://github.com/contour-terminal/contour),
[`contour-terminal/endo`](https://github.com/contour-terminal/endo),
[`LASTRADA-Software/fastcached`](https://github.com/LASTRADA-Software/fastcached). A file adapted
from more than one upstream file (a merge) names its primary upstream in the table and lists the
others in notes.

| core-cpp path | upstream repo | upstream path | synced SHA | notes |
|---|---|---|---|---|
| `cmake/FetchTransferBound.cmake` | LASTRADA-Software/fastcached | `cmake/FetchTransferBound.cmake` | `eb9c9c68da8fadfd43b0b36366919cb462689f48` | verbatim; re-synced together with `CompileCache.cmake` (`cmake/portable/README.md`) |
| `cmake/portable/CompileCache.cmake` | LASTRADA-Software/fastcached | `cmake/portable/CompileCache.cmake` | `eb9c9c68da8fadfd43b0b36366919cb462689f48` | verbatim |
| `cmake/portable/README.md` | origin: core-cpp | - | - | documents the two verbatim files above |
| `src/core/Assert.hpp` | contour-terminal/contour | `src/crispy/Assert.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `Require`/`Guarantee`/`todo`/`unreachable`/`setFailHandler`; `fatal`/`SoftRequire` split out to `src/core/log/Assert.hpp` to close a base→log layering cycle |
| `src/core/Base64.hpp` | contour-terminal/contour | `src/crispy/Base64.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Base64_test.cpp` | contour-terminal/contour | `src/crispy/Base64_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/Config.hpp.in` | origin: core-cpp | - | - | generates `core/Config.hpp` |
| `src/core/Deferred.hpp` | contour-terminal/contour | `src/crispy/Deferred.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Defines.hpp` | contour-terminal/contour | `src/crispy/Defines.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `CRISPY_*` macros renamed `CORE_*` |
| `src/core/Environment.cpp` | contour-terminal/contour | `src/crispy/Environment.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import additions (Task A4): the process-environment writer (`setProcessEnvironmentVariable()`/`unsetProcessEnvironmentVariable()`), and on Windows `LiveEnvironment` reads a variable set to the empty string as set |
| `src/core/Environment.hpp` | contour-terminal/contour | `src/crispy/Environment.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import addition (Task A4): the process-environment writer |
| `src/core/Environment_test.cpp` | contour-terminal/contour | `src/crispy/Environment_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import additions (Task A4): the process-environment writer's cases |
| `src/core/Escape.hpp` | contour-terminal/contour | `src/crispy/Escape.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/FNV.hpp` | contour-terminal/contour | `src/crispy/FNV.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import fix (`48b261a`): the trivially-copyable overload no longer recurses forever for `T != unsigned char` |
| `src/core/FNV_test.cpp` | origin: core-cpp | - | - | written for the fix above |
| `src/core/Flags.hpp` | contour-terminal/contour | `src/crispy/Flags.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Generator.hpp` | contour-terminal/endo | `src/platform/Generator.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `endo::Generator` renamed `core::Generator`; the fallback is always defined, as `core::detail::GeneratorFallback`; `<version>` is included first, so whether `Generator` is `std::generator` no longer depends on what a translation unit included before it; `handle_type` renamed `HandleType`; `ENDO_GENERATOR_FORCE_FALLBACK` renamed `CORE_GENERATOR_FORCE_FALLBACK`; moved from `core::coro` to `core::base` (Task A5b): `core::async::Generator` would read as an asynchronous, `co_await`-able stream, and this one is synchronous |
| `src/core/Generator_test.cpp` | contour-terminal/endo | `src/platform/Generator_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | every case runs over `Generator` and over the fallback; asserts the include-order-independent choice |
| `src/core/Overloaded.hpp` | contour-terminal/contour | `src/crispy/Utils.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `crispy::Overloaded` (defined in `Utils.hpp`) and the ambient global `::Overloaded` merged into one `core::Overloaded` here |
| `src/core/Profiling.hpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Profiling.hpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | `FC_*` macros renamed `CORE_*` |
| `src/core/Profiling_test.cpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Profiling_test.cpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | - |
| `src/core/Ranges.hpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Ranges.hpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | `FastCache::Ranges` renamed `core::ranges`; `FC_RANGES_FORCE_FALLBACK` renamed `CORE_RANGES_FORCE_FALLBACK` |
| `src/core/Ranges_test.cpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Ranges_test.cpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | imported although the A3 brief did not list it: `Ranges.hpp`'s own documentation points to it as the proof that its fallbacks work |
| `src/core/Times.hpp` | contour-terminal/contour | `src/crispy/Times.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import fix (`6b1a4d7`): the postfix `operator++`/`operator--` return the prior position, not the new one |
| `src/core/Times_test.cpp` | contour-terminal/contour | `src/crispy/Times_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/UserInfo.cpp` | contour-terminal/contour | `src/crispy/UserInfo.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/UserInfo.hpp` | contour-terminal/contour | `src/crispy/UserInfo.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Utils.cpp` | contour-terminal/contour | `src/crispy/Utils.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Utils.hpp` | contour-terminal/contour | `src/crispy/Utils.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `Overloaded` split out to `Overloaded.hpp`; `views::enumerate` is now a function template |
| `src/core/Utils_test.cpp` | contour-terminal/contour | `src/crispy/Utils_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/async/Awaitable.hpp` | contour-terminal/contour | `src/coro/Awaitable.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespace only |
| `src/core/async/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/async/Cancellation.hpp` | contour-terminal/contour | `src/coro/Cancellation.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `OperationCancelled`, `ThisCoroStopToken`, `thisCoroStopToken()`; the `std::` aliases and their `#error` moved to `StopToken.hpp`; no `NOLINT` |
| `src/core/async/StopToken.hpp` | contour-terminal/contour | `src/coro/Cancellation.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | the `StopToken`/`StopSource`/`StopCallback` aliases of `std::` only, moved out of `Cancellation.hpp`; in place of contour's `#error` where `__cpp_lib_jthread` is missing, core-cpp's fallback (`detail::StopTokenFallback`, `StopSourceFallback`, `StopCallbackFallback`), always defined; the `constexpr` tag `NoStopState`; `<version>` included first; `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK` |
| `src/core/async/StopToken_test.cpp` | origin: core-cpp | - | - | built twice: `core-cpp.async` and, with `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK`, `core-cpp.async-fallback` |
| `src/core/async/Task.hpp` | contour-terminal/contour | `src/coro/Task.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespace only; no `NOLINT` |
| `src/core/async/Task_test.cpp` | contour-terminal/contour | `src/coro/Task_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | no `NOLINT`; the deep-chain case is skipped under Emscripten without `-mtail-call` and for GCC without `__OPTIMIZE__`, where symmetric transfer is not a tail call (core-cpp#15); also built over the `StopToken` fallback (`core-cpp.async-fallback`) |
| `src/core/async/UniqueCoroHandle.hpp` | contour-terminal/contour | `src/coro/UniqueCoroHandle.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespace only |
| `src/core/async/WhenAll.hpp` | contour-terminal/contour | `src/coro/WhenAll.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | no `NOLINT`; the final awaiter's local `state` renamed `join` (`-Wshadow`) |
| `src/core/async/WhenAll_test.cpp` | contour-terminal/contour | `src/coro/WhenAll_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespace only; no `NOLINT`; also built over the `StopToken` fallback |
| `src/core/async/WhenAny.hpp` | contour-terminal/contour | `src/coro/WhenAny.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | no `NOLINT`; the final awaiter's local `state` renamed `race` (`-Wshadow`) |
| `src/core/async/WhenAny_test.cpp` | contour-terminal/contour | `src/coro/WhenAny_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | no `NOLINT`; `ManualEvent::waiters` initialised to `nullptr` (`cppcoreguidelines-pro-type-member-init`); `failingRacer()` and `raceWithFailingWinner()` moved under `#ifndef _WIN32` with the one case that uses them (`-Wunused-function` on clang-cl); also built over the `StopToken` fallback |
| `src/core/cli/App.cpp` | contour-terminal/contour | `src/crispy/App.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import fix (Task A5b): the local `namespace CLI = core::cli;` alias is `cli`, lowercase like every other namespace (`readability-identifier-naming.NamespaceCase`) |
| `src/core/cli/App.hpp` | contour-terminal/contour | `src/crispy/App.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CLI.cpp` | contour-terminal/contour | `src/crispy/CLI.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CLI.hpp` | contour-terminal/contour | `src/crispy/CLI.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CLI_test.cpp` | contour-terminal/contour | `src/crispy/CLI_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/log/Assert.hpp` | contour-terminal/contour | `src/crispy/Assert.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `fatal()`/`SoftRequire()` moved here from crispy's `Assert.hpp` to close a base→log layering cycle; `Require`/`Guarantee`/`todo`/`unreachable`/`setFailHandler` stay in `src/core/Assert.hpp` |
| `src/core/log/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/log/LogSink.cpp` | contour-terminal/contour | `src/crispy/LogSink.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/log/LogSink.hpp` | contour-terminal/contour | `src/crispy/LogSink.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/log/LogSink_test.cpp` | contour-terminal/contour | `src/crispy/LogSink_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import fix (`fe62488`): reads the fixture files through a `contentsOf()` stringstream helper, not `std::istreambuf_iterator` |
| `src/core/log/LogStore.cpp` | contour-terminal/contour | `src/crispy/LogStore.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `logstore::` renamed `core::log::` |
| `src/core/log/LogStore.hpp` | contour-terminal/contour | `src/crispy/LogStore.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `logstore::` renamed `core::log::` |
| `src/core/platform/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/platform/Clock.hpp` | contour-terminal/endo | `src/platform/Clock.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | merged with contour `src/net/platform/Clock.hpp` (`6777ff05014f8ff163b071e8b0e942830119db80`) and fastcached `src/FastCache/Core/Clock.hpp` (`b461e8b6d367ed22e4bf2935717fa59360a64b7d`): fastcached's virtual no-op `refresh()`, `CachedClock`, `IWallClock`, `SystemWallClock`, `ManualWallClock`, `WallClockRef` and `defaultSystemWallClock()`, camelBack; `TimePoint`/`Duration` are `SteadyTimePoint`/`SteadyDuration` |
| `src/core/platform/Clock_test.cpp` | contour-terminal/endo | `src/platform/Clock_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | merged with fastcached `src/FastCache/Core/Clock_test.cpp` and `src/FastCache/Core/WallClockRef_test.cpp` (`b461e8b6d367ed22e4bf2935717fa59360a64b7d`), duplicates dropped; the concurrent `CachedClock` case uses `std::thread` and a bounded start gate instead of fastcached's `tests/BoundedWait.hpp`, and is compiled only where threads exist; the `WallClockRef` asserts over fastcached's own retainer types are dropped; the retainer and forwarder rows are test-local stand-ins for fastcached's retainer types |
| `src/core/platform/EnvironmentProvider.hpp` | contour-terminal/endo | `src/platform/EnvironmentProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `userName()` returns the optional it read rather than re-wrapping its value; declares `nativeEnvironmentProvider()` (core-cpp) |
| `src/core/platform/EnvironmentProvider_test.cpp` | contour-terminal/endo | `src/platform/EnvironmentProvider_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | plus two core-cpp cases for the native provider (export, read-through); the native cases go through the factory; runs under Emscripten too; plus core-cpp cases for `homeDirectory()` and `configHome()` over a `TestEnvironmentProvider` |
| `src/core/platform/FileInfoProvider.hpp` | contour-terminal/endo | `src/platform/FileInfoProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `FileEntry`'s string members have `{}` initializers, so designated initializers may omit them; declares `nativeFileInfoProvider()` (core-cpp) |
| `src/core/platform/FileInfoProvider_test.cpp` | contour-terminal/endo | `src/platform/FileInfoProvider_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the fixture directory is a `ScopedTempDir`, not a fixed path; plus a core-cpp case for `nativeFileInfoProvider()`; the lstat(2) cases test `PosixFileInfoProvider` and run under Emscripten too, where a symlink's target reads resolved against its directory |
| `src/core/platform/FileSystem.hpp` | contour-terminal/endo | `src/platform/FileSystem.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `Generator` is `core::Generator` |
| `src/core/platform/FileSystem_test.cpp` | contour-terminal/endo | `src/platform/FileSystem_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | reads a stream through a string stream, not `std::istreambuf_iterator`; the break-out case breaks conditionally (MSVC C4702) |
| `src/core/platform/FileUri.cpp` | contour-terminal/endo | `src/platform/FileUri.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/FileUri.hpp` | contour-terminal/endo | `src/platform/FileUri.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/FileUri_test.cpp` | contour-terminal/endo | `src/platform/FileUri_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/GlobMatch.cpp` | contour-terminal/endo | `src/platform/GlobMatch.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `namespace endo` renamed `core::platform`; the bracket expression is matched by a helper (readability-function-cognitive-complexity) |
| `src/core/platform/GlobMatch.hpp` | contour-terminal/endo | `src/platform/GlobMatch.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `namespace endo` renamed `core::platform` |
| `src/core/platform/GlobMatch_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/platform/MessageQueue.hpp` | contour-terminal/endo | `src/platform/MessageQueue.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/MessageQueue_test.cpp` | contour-terminal/endo | `src/platform/MessageQueue_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `std::thread` rather than `std::jthread`; the producer loop is a range-for |
| `src/core/platform/NativeFileSystem.cpp` | contour-terminal/endo | `src/platform/NativeFileSystem.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the `increment(ec)` loops are `while` loops; `strerror()` became `std::error_code::message()`; the recase temporary is `<name>.recase-<n>` |
| `src/core/platform/NativeFileSystem.hpp` | contour-terminal/endo | `src/platform/NativeFileSystem.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/PathUtils.cpp` | contour-terminal/endo | `src/platform/PathUtils.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/PathUtils.hpp` | contour-terminal/endo | `src/platform/PathUtils.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/PathUtils_test.cpp` | contour-terminal/endo | `src/platform/WindowsPlatform_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the `PathUtils` cases (`normalizePath`, `resolveDevicePath`, `canonicalCasePath`, `stripTrailingSeparator`, `isCaseOnlyRename`), split out so the WebAssembly subset can run them |
| `src/core/platform/PlatformError.hpp` | contour-terminal/endo | `src/platform/PlatformError.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/PlatformError_test.cpp` | contour-terminal/endo | `src/platform/PlatformError_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/SignalHandler.cpp` | contour-terminal/endo | `src/platform/SignalHandler.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the state is file-scope, not static members; `sigprocmask()` became `pthread_sigmask()` |
| `src/core/platform/SignalHandler.hpp` | contour-terminal/endo | `src/platform/SignalHandler.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the private static members (and their NOLINTs) moved into `SignalHandler.cpp` |
| `src/core/platform/SignalHandler_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/platform/StringUtils.hpp` | contour-terminal/endo | `src/platform/StringUtils.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/StringUtils_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/platform/SystemInfo.cpp` | contour-terminal/endo | `src/platform/SystemInfo.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/SystemInfo.hpp` | contour-terminal/endo | `src/platform/SystemInfo.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/SystemInfo_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/platform/SystemPipe.cpp` | contour-terminal/endo | `src/platform/SystemPipe.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | merged with contour `src/net/platform/SystemPipe.cpp` (`6777ff05014f8ff163b071e8b0e942830119db80`): both POSIX ends non-blocking and close-on-exec, `MSG_NOSIGNAL`, and a write the full channel refused reports done; endo's API (`PlatformError`) kept; `read()` returns a `ChannelResult` (core-cpp) |
| `src/core/platform/SystemPipe.hpp` | contour-terminal/endo | `src/platform/SystemPipe.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | merged with contour `src/net/platform/SystemPipe.hpp` (`6777ff05014f8ff163b071e8b0e942830119db80`): documents the non-blocking contract; `ChannelResult`, which `read()` returns (core-cpp) |
| `src/core/platform/SystemPipe_test.cpp` | contour-terminal/endo | `src/platform/SystemPipe_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | plus three core-cpp cases for the non-blocking contract; the read cases rewritten for `ChannelResult`: bytes, empty, end of stream, failure, on every platform |
| `src/core/platform/Types.hpp` | contour-terminal/endo | `src/platform/Types.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | includes no `<windows.h>`: on Windows `NativeHandle`/`ProcessId` are spelled `void*`/`unsigned long` and the Windows API calls are out of line in `windows/WindowsTypes.cpp`; the Windows fallback `STDIN_FILENO`/`SIG*` macros are dropped |
| `src/core/platform/Types_test.cpp` | contour-terminal/endo | `src/platform/WindowsPlatform_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the `platformRead` case, split out, and run under Emscripten too, where the drained pipe reads -1 with EAGAIN; plus core-cpp cases for `InvalidHandle`, the standard handles and the header not including `<windows.h>` |
| `src/core/platform/UserPaths.hpp` | contour-terminal/endo | `src/platform/UserPaths.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | reads through a `core::Environment`, a `core::LiveEnvironment` by default, rather than `std::getenv()` |
| `src/core/platform/UserPaths_test.cpp` | contour-terminal/endo | `src/platform/WindowsPlatform_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the `homeDirectory`/`configHome` cases, over a `core::testing::FakeEnvironment` instead of the process environment; the zero-argument call is checked to compile, not run |
| `src/core/platform/Wakeup.hpp` | contour-terminal/endo | `src/platform/Wakeup.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/Wakeup_test.cpp` | contour-terminal/endo | `src/platform/Wakeup_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `std::thread`, joined before the assertions, rather than `std::jthread` |
| `src/core/platform/WinsockInit.cpp` | contour-terminal/endo | `src/platform/WinsockInit.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | identical to contour `src/net/platform/WinsockInit.cpp` (`6777ff05014f8ff163b071e8b0e942830119db80`) but for the namespace |
| `src/core/platform/WinsockInit.hpp` | contour-terminal/endo | `src/platform/WinsockInit.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | identical to contour `src/net/platform/WinsockInit.hpp` (`6777ff05014f8ff163b071e8b0e942830119db80`) but for the namespace |
| `src/core/platform/linux/LinuxWakeup.cpp` | contour-terminal/endo | `src/platform/linux/LinuxWakeup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the eventfd is created in the member initializer |
| `src/core/platform/posix/PosixEnvironmentProvider.cpp` | contour-terminal/endo | `src/platform/posix/PosixEnvironmentProvider.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | reads through `core::LiveEnvironment` and writes through `core::setProcessEnvironmentVariable()` / `unsetProcessEnvironmentVariable()` instead of `getenv()`/`setenv()`/`unsetenv()`; defines `nativeEnvironmentProvider()` (core-cpp) |
| `src/core/platform/posix/PosixEnvironmentProvider.hpp` | contour-terminal/endo | `src/platform/posix/PosixEnvironmentProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/posix/PosixFileInfoProvider.cpp` | contour-terminal/endo | `src/platform/linux/LinuxFileInfoProvider.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `LinuxFileInfoProvider`, renamed `PosixFileInfoProvider` and moved to `posix/`: it uses nothing Linux-specific, and is the provider on Linux, macOS, the BSDs and Emscripten; defines `nativeFileInfoProvider()` (core-cpp) |
| `src/core/platform/posix/PosixFileInfoProvider.hpp` | contour-terminal/endo | `src/platform/linux/LinuxFileInfoProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `LinuxFileInfoProvider`, renamed `PosixFileInfoProvider` and moved to `posix/` |
| `src/core/platform/posix/PosixWakeup.cpp` | contour-terminal/endo | `src/platform/posix/PosixWakeup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/testing/InMemoryFileSystem.cpp` | contour-terminal/endo | `src/platform/testing/InMemoryFileSystem.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `removeAll()` uses `std::erase_if`; the symlink hop loop is a range-for |
| `src/core/platform/testing/InMemoryFileSystem.hpp` | contour-terminal/endo | `src/platform/testing/InMemoryFileSystem.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/testing/MockFileInfoProvider.hpp` | contour-terminal/endo | `src/platform/testing/MockFileInfoProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/testing/TestEnvironmentProvider.hpp` | contour-terminal/endo | `src/platform/testing/TestEnvironmentProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/windows/WindowsEnvironmentProvider.cpp` | contour-terminal/endo | `src/platform/windows/WindowsEnvironmentProvider.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | defines `nativeEnvironmentProvider()` (core-cpp) |
| `src/core/platform/windows/WindowsEnvironmentProvider.hpp` | contour-terminal/endo | `src/platform/windows/WindowsEnvironmentProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/windows/WindowsFileInfoProvider.cpp` | contour-terminal/endo | `src/platform/windows/WindowsFileInfoProvider.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | defines `nativeFileInfoProvider()` (core-cpp) |
| `src/core/platform/windows/WindowsFileInfoProvider.hpp` | contour-terminal/endo | `src/platform/windows/WindowsFileInfoProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/platform/windows/WindowsTypes.cpp` | contour-terminal/endo | `src/platform/Types.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the Windows definitions of `Types.hpp`, moved out of line so the header needs no `<windows.h>` |
| `src/core/platform/windows/WindowsWakeup.cpp` | contour-terminal/endo | `src/platform/windows/WindowsWakeup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/testing/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/testing/CatchMain.cpp` | origin: core-cpp | - | - | `core::testing_main`: Catch2's `main()`, the exit-code contract and the `LOG` filter |
| `src/core/testing/CatchMain_test.cpp` | origin: core-cpp | - | - | covers the `LOG` filter |
| `src/core/testing/EnvHelper.hpp` | contour-terminal/endo | `src/testing/EnvHelper.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | POSIX writes go through `core::setProcessEnvironmentVariable()`, and `ScopedEnv` reads through `core::LiveEnvironment`, instead of `setenv()`/`unsetenv()`/`getenv()` |
| `src/core/testing/EnvHelper_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/testing/Environment.hpp` | contour-terminal/contour | `src/crispy/testing/Environment.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `crispy::testing::FakeEnvironment` renamed `core::testing::FakeEnvironment` |
| `src/core/testing/ExitCode.cpp` | origin: core-cpp | - | - | - |
| `src/core/testing/ExitCode.hpp` | origin: core-cpp | - | - | - |
| `src/core/testing/ExitCode_test.cpp` | origin: core-cpp | - | - | - |
| `src/core/testing/ScopedTempDir.hpp` | contour-terminal/endo | `src/testing/ScopedTempDir.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/testing/ScopedTempDir_test.cpp` | contour-terminal/endo | `src/testing/ScopedTempDir_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/testing/ScopedWorkingDirectory.hpp` | contour-terminal/endo | `src/testing/ScopedWorkingDirectory.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/testing/ScopedWorkingDirectory_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/testing/SuppressWindowsDialogs.cpp` | contour-terminal/contour | `src/crispy/SuppressWindowsDialogs.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | the out-of-line body added in core-cpp (`26de633`); same four-way merge as `SuppressWindowsDialogs.hpp` below |
| `src/core/testing/SuppressWindowsDialogs.hpp` | contour-terminal/contour | `src/crispy/SuppressWindowsDialogs.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | merged with contour `src/coro/testing/SuppressWindowsDialogs.hpp` (same commit), endo `src/testing/SuppressWindowsDialogs.hpp` (`f774a210ce989e5947b8f61d715068b1dc96088c`) and fastcached `src/tests/WindowsErrorPopups.hpp` (`eb9c9c68da8fadfd43b0b36366919cb462689f48`) |
| `src/core/testing/SuppressWindowsDialogsAtStartup.cpp` | contour-terminal/endo | `src/testing/SuppressWindowsDialogsAtStartup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | verbatim |
