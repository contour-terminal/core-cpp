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
| `src/core/net/AsyncBufferedReader.cpp` | contour-terminal/contour | `src/net/AsyncBufferedReader.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/AsyncBufferedReader.hpp` | contour-terminal/contour | `src/net/AsyncBufferedReader.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/AsyncBufferedReader_test.cpp` | contour-terminal/contour | `src/net/AsyncBufferedReader_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; the four C-style `for` loops are range-`for`s over `std::views::iota` |
| `src/core/net/CMakeLists.txt` | origin: core-cpp | - | - | replaces contour's `src/net/CMakeLists.txt`: the targets `core::net_types`, `core::net` and `core::net_tls`, and three test binaries; keeps its comment on the PUBLIC `Threads::Threads` |
| `src/core/net/DefaultEventSource.cpp` | contour-terminal/contour | `src/net/DefaultEventSource.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/DefaultEventSource.hpp` | contour-terminal/contour | `src/net/DefaultEventSource.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/Diagnostics.cpp` | contour-terminal/contour | `src/net/Diagnostics.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/Diagnostics.hpp` | contour-terminal/contour | `src/net/Diagnostics.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/EventLoop.cpp` | contour-terminal/contour | `src/net/EventLoop.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `IClock`, `SystemPipe` and `createSystemPipe()` are `core::platform`'s; the two iterator `for` loops over `_fdToTokens` are a `std::ranges::find_if` and a range-`for` over a `std::ranges::subrange`; post-import addition (core-cpp): `pumpOnce()` calls `IClock::refresh()` before it computes a wait's timeout and after the wait, as the merged `IClock` asks of a loop |
| `src/core/net/EventLoop.hpp` | contour-terminal/contour | `src/net/EventLoop.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `IClock`, `defaultSteadyClock()`, `SteadyTimePoint`, `NativeHandle`, `InvalidHandle` and `SystemPipe` are `core::platform`'s; no `NOLINT`; the provenance pointer names contour's `src/coro/README.md`; the constructor's comment says when the loop refreshes the clock (core-cpp) |
| `src/core/net/EventLoop_test.cpp` | contour-terminal/contour | `src/net/EventLoop_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `ManualClock`, `SystemPipe`, `createSystemPipe()`, `NativeHandle` and `InvalidHandle` are `core::platform`'s; a pipe read tests `ChannelResult::bytesRead()` where it compared a count; post-import addition (core-cpp): the case over a `CachedClock` that the loop refreshes before each timeout and after each wait |
| `src/core/net/EventSource.hpp` | contour-terminal/contour | `src/net/EventSource.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s (`<core/platform/Types.hpp>`) |
| `src/core/net/EventSourceParity_test.cpp` | contour-terminal/contour | `src/net/EventSourceParity_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `SystemPipe` and `createSystemPipe()` are `core::platform`'s; a pipe read tests `ChannelResult::bytesRead()` where it compared a count |
| `src/core/net/HttpServer.cpp` | contour-terminal/contour | `src/net/HttpServer.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/HttpServer.hpp` | contour-terminal/contour | `src/net/HttpServer.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/HttpServer_test.cpp` | contour-terminal/contour | `src/net/HttpServer_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; the `for` over `find()` positions is a `while` |
| `src/core/net/IListener.hpp` | contour-terminal/contour | `src/net/IListener.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/ISocket.hpp` | contour-terminal/contour | `src/net/ISocket.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/IoResult.hpp` | contour-terminal/contour | `src/net/IoResult.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces; `NetErrorCode`, `toString()`, `NetError` and `makeNetError()` split out to `NetError.hpp`, which it includes; the `core::net_types` target |
| `src/core/net/NetError.hpp` | contour-terminal/contour | `src/net/IoResult.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `NetErrorCode`, `toString()`, `NetError` and `makeNetError()`, split out of `IoResult.hpp`, with a file comment of its own; the `core::net_types` target |
| `src/core/net/NetError_test.cpp` | origin: core-cpp | - | - | the `core-cpp.net_types` test, which the Emscripten build runs too; the upstream file has no test |
| `src/core/net/PollEventSource.hpp` | contour-terminal/contour | `src/net/PollEventSource.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s (`<core/platform/Types.hpp>`); `_waitRotation`, which only the Windows half uses, is declared on every platform (`[[maybe_unused]]`), so the header has no platform branch (Ruling R40) |
| `src/core/net/Socket_test.cpp` | contour-terminal/contour | `src/net/Socket_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/Sockets.hpp` | contour-terminal/contour | `src/net/Sockets.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/SplitSocket.hpp` | contour-terminal/contour | `src/net/SplitSocket.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/Tls.cpp` | contour-terminal/contour | `src/net/Tls.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no `NOLINT`: contour's three `NOLINTNEXTLINE(readability-identifier-naming)` on `HandshakeGate`'s `await_*` hooks are gone, since `.clang-tidy` exempts the hook names; the `core::net_tls` target, which links OpenSSL PRIVATE |
| `src/core/net/Tls.hpp` | contour-terminal/contour | `src/net/Tls.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; the `core::net_tls` target |
| `src/core/net/Tls_test.cpp` | contour-terminal/contour | `src/net/Tls_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; the two two-reactor cases make the client context before the server thread starts, so no `REQUIRE` can unwind past a joinable `std::thread` (`.agent/rules/testing.md`); the `core-cpp.net_tls` test |
| `src/core/net/WithTimeout.hpp` | contour-terminal/contour | `src/net/WithTimeout.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/WriteQueue.cpp` | contour-terminal/contour | `src/net/WriteQueue.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/WriteQueue.hpp` | contour-terminal/contour | `src/net/WriteQueue.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/WriteQueue_test.cpp` | contour-terminal/contour | `src/net/WriteQueue_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/bsd/KqueueEventSource.cpp` | contour-terminal/contour | `src/net/KqueueEventSource.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s (`<core/platform/Types.hpp>`); moved to `bsd/` (Ruling R40: platform code lives in platform subdirectories); no file-wide guard on the kqueue platforms (`__APPLE__`, `__FreeBSD__`, `__OpenBSD__`, `__NetBSD__`): `SOURCES_BSD` selects the file (Ruling R42) |
| `src/core/net/bsd/KqueueEventSource.hpp` | contour-terminal/contour | `src/net/KqueueEventSource.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s (`<core/platform/Types.hpp>`); moved to `bsd/` (Ruling R40: platform code lives in platform subdirectories), and so private; no file-wide guard on the kqueue platforms (`__APPLE__`, `__FreeBSD__`, `__OpenBSD__`, `__NetBSD__`): `SOURCES_BSD` selects the file (Ruling R42) |
| `src/core/net/detail/PeerAddress.hpp` | contour-terminal/contour | `src/net/platform/PeerAddress.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces; moved from `platform/` to `detail/`, since a `core::net::platform` namespace would hide `core::platform` (Rulings R37 and R40); private, and it includes `<winsock2.h>` |
| `src/core/net/detail/ScopeGuard.hpp` | contour-terminal/contour | `src/net/detail/ScopeGuard.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/detail/WaitChunking.hpp` | contour-terminal/contour | `src/net/WaitChunking.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only; moved to `detail/` (Ruling R40), and so private |
| `src/core/net/detail/WouldBlock.hpp` | contour-terminal/contour | `src/net/detail/WouldBlock.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/linux/EpollEventSource.cpp` | contour-terminal/contour | `src/net/EpollEventSource.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s (`<core/platform/Types.hpp>`); moved to `linux/` (Ruling R40: platform code lives in platform subdirectories); no file-wide `#ifdef __linux__` guard: `SOURCES_LINUX` selects the file (Ruling R42) |
| `src/core/net/linux/EpollEventSource.hpp` | contour-terminal/contour | `src/net/EpollEventSource.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s (`<core/platform/Types.hpp>`); moved to `linux/` (Ruling R40: platform code lives in platform subdirectories), and so private; no file-wide `#ifdef __linux__` guard: `SOURCES_LINUX` selects the file (Ruling R42) |
| `src/core/net/posix/AcceptLoop.cpp` | contour-terminal/contour | `src/net/posix/AcceptLoop.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `PeerAddress.hpp` from `detail/`; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42); it keeps the `#ifdef __linux__` that accepts with `accept4()` and the `#ifndef __linux__` that sets `O_NONBLOCK` and `FD_CLOEXEC` with `fcntl()` elsewhere |
| `src/core/net/posix/AcceptLoop.hpp` | contour-terminal/contour | `src/net/posix/AcceptLoop.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/posix/FdPassing_test.cpp` | contour-terminal/contour | `src/net/FdPassing_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; a coroutine lambda's parameter `loop` is `eventLoop`, which the local `loop` shadowed (GCC `-Wshadow`); `sendWithFds()` checks the header `CMSG_FIRSTHDR()` returns before writing through it (GCC `-Wnull-dereference` at `-O3`); it converts `CMSG_SPACE()` and `CMSG_LEN()` to the type of the field they are stored in, `socklen_t` on macOS (Clang `-Wshorten-64-to-32`); moved to `posix/` (Ruling R40: platform code lives in platform subdirectories); no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42); it keeps the `#ifndef __APPLE__` around the check that the kernel closed the write end |
| `src/core/net/posix/FdUtils.hpp` | contour-terminal/contour | `src/net/posix/FdUtils.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; never had a file-wide guard; it keeps the `#ifndef MSG_NOSIGNAL` fallback and the `#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)` branch, which choose within POSIX |
| `src/core/net/posix/PollEventSource.cpp` | contour-terminal/contour | `src/net/PollEventSource.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | its POSIX half: contour's file split along its `#ifdef _WIN32` into `posix/` and `windows/` (Ruling R40); namespaces and includes; `NativeHandle` is `core::platform`'s; the index `for` is a range-`for` over `std::views::iota` |
| `src/core/net/posix/PosixListener.cpp` | contour-terminal/contour | `src/net/posix/PosixListener.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; the `addrinfo` walk is a `while` that steps to the next candidate first; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/posix/PosixListener.hpp` | contour-terminal/contour | `src/net/posix/PosixListener.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/posix/PosixSocket.cpp` | contour-terminal/contour | `src/net/posix/PosixSocket.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; the `cmsghdr` walk is a `while` that steps to the next header first, and the descriptor loop a range-`for` over `std::views::iota`; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42); it keeps the `#ifndef MSG_CMSG_CLOEXEC` fallback and the `#ifdef SO_NOSIGPIPE` option, which macOS and the BSDs need |
| `src/core/net/posix/PosixSocket.hpp` | contour-terminal/contour | `src/net/posix/PosixSocket.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/posix/SocketsPosix.cpp` | contour-terminal/contour | `src/net/posix/SocketsPosix.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `ensureWinsockInitialized()` is `core::platform`'s; the `addrinfo` walk is a `while` that steps to the next candidate first; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/posix/UnixListener.cpp` | contour-terminal/contour | `src/net/posix/UnixListener.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/posix/UnixListener.hpp` | contour-terminal/contour | `src/net/posix/UnixListener.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/posix/UnixSocket_test.cpp` | contour-terminal/contour | `src/net/UnixSocket_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; moved to `posix/` (Ruling R40: platform code lives in platform subdirectories); no file-wide `#ifndef _WIN32` guard: `SOURCES_POSIX` selects the file (Ruling R42) |
| `src/core/net/testing/CoroTestSupport.hpp` | contour-terminal/contour | `src/net/testing/CoroTestSupport.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/testing/EventSourceBackends.hpp` | contour-terminal/contour | `src/net/testing/EventSourceBackends.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/testing/InMemoryTransport.hpp` | contour-terminal/contour | `src/net/testing/InMemoryTransport.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes only |
| `src/core/net/testing/ScriptedEventSource.hpp` | contour-terminal/contour | `src/net/testing/ScriptedEventSource.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s (`<core/platform/Types.hpp>`) |
| `src/core/net/testing/posix/InMemoryTransport.cpp` | contour-terminal/contour | `src/net/testing/InMemoryTransport.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | its POSIX half: contour's file split along its `#ifdef _WIN32` into `testing/posix/` and `testing/windows/` (Ruling R42); namespaces and includes; `ensureWinsockInitialized()` is `core::platform`'s |
| `src/core/net/testing/windows/InMemoryTransport.cpp` | contour-terminal/contour | `src/net/testing/InMemoryTransport.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | its Windows half: contour's file split along its `#ifdef _WIN32` into `testing/posix/` and `testing/windows/` (Ruling R42); namespaces and includes; `ensureWinsockInitialized()` is `core::platform`'s; `WindowsLoopback.hpp` from `windows/`; it includes `<array>` itself, which contour's branch took from `WindowsLoopback.hpp` |
| `src/core/net/windows/PollEventSource.cpp` | contour-terminal/contour | `src/net/PollEventSource.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | its Windows half: contour's file split along its `#ifdef _WIN32` into `posix/` and `windows/` (Ruling R40); namespaces and includes; `NativeHandle` and `InvalidHandle` are `core::platform`'s; `WaitChunking.hpp` from `detail/` |
| `src/core/net/windows/SocketsWin32.cpp` | contour-terminal/contour | `src/net/windows/SocketsWin32.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `ensureWinsockInitialized()` is `core::platform`'s; the `addrinfo` walk is a `while` that steps to the next candidate first; no file-wide `#ifdef _WIN32` guards, around its body or its `<winsock2.h>` block: `SOURCES_WINDOWS` selects the file (Ruling R42) |
| `src/core/net/windows/WindowsListener.cpp` | contour-terminal/contour | `src/net/windows/WindowsListener.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; `PeerAddress.hpp` from `detail/`; the `addrinfo` walk is a `while` that steps to the next candidate first; no file-wide `#ifdef _WIN32` guards, around its body or its `<winsock2.h>` block: `SOURCES_WINDOWS` selects the file (Ruling R42); it keeps the `#ifndef IO_REPARSE_TAG_AF_UNIX` definition, for SDKs that lack it |
| `src/core/net/windows/WindowsListener.hpp` | contour-terminal/contour | `src/net/windows/WindowsListener.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifdef _WIN32` guard: `SOURCES_WINDOWS` selects the file (Ruling R42) |
| `src/core/net/windows/WindowsLoopback.cpp` | contour-terminal/contour | `src/net/platform/WindowsLoopback.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; moved from `platform/` to `windows/`, since a `core::net::platform` namespace would hide `core::platform`; no file-wide `#ifdef _WIN32` guards, around its body or its `<winsock2.h>` block: `SOURCES_WINDOWS` selects the file (Ruling R42) |
| `src/core/net/windows/WindowsLoopback.hpp` | contour-terminal/contour | `src/net/platform/WindowsLoopback.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces; moved from `platform/` to `windows/`, since a `core::net::platform` namespace would hide `core::platform`; no file-wide `#ifdef _WIN32` guards, around its body or its `<winsock2.h>` block: `SOURCES_WINDOWS` selects the file (Ruling R42) |
| `src/core/net/windows/WindowsSocket.cpp` | contour-terminal/contour | `src/net/windows/WindowsSocket.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifdef _WIN32` guard: `SOURCES_WINDOWS` selects the file (Ruling R42) |
| `src/core/net/windows/WindowsSocket.hpp` | contour-terminal/contour | `src/net/windows/WindowsSocket.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | namespaces and includes; no file-wide `#ifdef _WIN32` guard: `SOURCES_WINDOWS` selects the file (Ruling R42) |
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
| `src/core/tui/.clang-tidy` | origin: core-cpp | - | - | the one directory that departs from the root `.clang-tidy`, and says why |
| `src/core/tui/Box.cpp` | contour-terminal/endo | `src/tui/Box.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Box.hpp` | contour-terminal/endo | `src/tui/Box.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Buffer.cpp` | contour-terminal/endo | `src/tui/Buffer.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Buffer.hpp` | contour-terminal/endo | `src/tui/Buffer.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CMakeLists.txt` | origin: core-cpp | - | - | endo's `src/tui/CMakeLists.txt` is one `add_library`; this is the module-table form, and where the `core::tui_output` split is spelled |
| `src/core/tui/Canvas.cpp` | contour-terminal/endo | `src/tui/Canvas.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Canvas.hpp` | contour-terminal/endo | `src/tui/Canvas.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Cell.cpp` | contour-terminal/endo | `src/tui/Cell.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Cell.hpp` | contour-terminal/endo | `src/tui/Cell.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CommandPalettePopup.cpp` | contour-terminal/endo | `src/tui/CommandPalettePopup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CommandPalettePopup.hpp` | contour-terminal/endo | `src/tui/CommandPalettePopup.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CommandPalettePopup_test.cpp` | contour-terminal/endo | `src/tui/CommandPalettePopup_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CommandRegistry.cpp` | contour-terminal/endo | `src/tui/CommandRegistry.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CommandRegistry.hpp` | contour-terminal/endo | `src/tui/CommandRegistry.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CommandRegistry_test.cpp` | contour-terminal/endo | `src/tui/CommandRegistry_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Completer_test.cpp` | contour-terminal/endo | `src/tui/Completer_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CompletionPopup.cpp` | contour-terminal/endo | `src/tui/CompletionPopup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CompletionPopup.hpp` | contour-terminal/endo | `src/tui/CompletionPopup.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CompletionPopup_test.cpp` | contour-terminal/endo | `src/tui/CompletionPopup_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Component.cpp` | contour-terminal/endo | `src/tui/Component.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Component.hpp` | contour-terminal/endo | `src/tui/Component.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/CursorShape.hpp` | contour-terminal/endo | `src/tui/CursorShape.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Dialog.cpp` | contour-terminal/endo | `src/tui/Dialog.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Dialog.hpp` | contour-terminal/endo | `src/tui/Dialog.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/EditAction.hpp` | contour-terminal/endo | `src/tui/EditAction.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Element.hpp` | contour-terminal/endo | `src/tui/Element.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Error.hpp` | contour-terminal/endo | `src/tui/Error.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/FilesystemImageProvider.cpp` | contour-terminal/endo | `src/tui/ImageProvider.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | renamed with the class it defines; see `FilesystemImageProvider.hpp` |
| `src/core/tui/FilesystemImageProvider.hpp` | contour-terminal/endo | `src/tui/ImageProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `FilesystemImageProvider` and `isRemoteImageSource()` split out of `ImageProvider.hpp`, because they are the part that needs a decoder and so exist only with `CORE_CPP_WITH_IMAGES` |
| `src/core/tui/FuzzyMatch_test.cpp` | contour-terminal/endo | `src/tui/FuzzyMatch_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/FuzzyPickerPopup.cpp` | contour-terminal/endo | `src/tui/FuzzyPickerPopup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/FuzzyPickerPopup.hpp` | contour-terminal/endo | `src/tui/FuzzyPickerPopup.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/FuzzyPickerPopup_test.cpp` | contour-terminal/endo | `src/tui/FuzzyPickerPopup_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/GenericSyntaxHighlighter.cpp` | contour-terminal/endo | `src/tui/GenericSyntaxHighlighter.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/GenericSyntaxHighlighter.hpp` | contour-terminal/endo | `src/tui/GenericSyntaxHighlighter.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/GenericSyntaxHighlighter_test.cpp` | contour-terminal/endo | `src/tui/GenericSyntaxHighlighter_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/GhostTextHelper.hpp` | contour-terminal/endo | `src/tui/GhostTextHelper.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/HoverState.cpp` | contour-terminal/endo | `src/tui/HoverState.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/HoverState.hpp` | contour-terminal/endo | `src/tui/HoverState.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/HyperlinkEmitter.cpp` | contour-terminal/endo | `src/tui/HyperlinkEmitter.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/HyperlinkEmitter.hpp` | contour-terminal/endo | `src/tui/HyperlinkEmitter.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/ImageLoader.cpp` | contour-terminal/endo | `src/tui/ImageLoader.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `readClipboardImage()`, which was an `#ifdef` with a platform in each arm, is in `posix/` and `windows/` (Ruling R41); the `#pragma clang diagnostic` around stb's headers is gone, because core-cpp includes stb as a SYSTEM directory |
| `src/core/tui/ImageLoader.hpp` | contour-terminal/endo | `src/tui/ImageLoader.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/ImageLoader_test.cpp` | contour-terminal/endo | `src/tui/ImageLoader_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/ImageProvider.hpp` | contour-terminal/endo | `src/tui/ImageProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the interface and its configuration only; `FilesystemImageProvider` moved to a header of its own |
| `src/core/tui/InputEvent.hpp` | contour-terminal/endo | `src/tui/InputEvent.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/InputField.cpp` | contour-terminal/endo | `src/tui/InputField.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/InputField.hpp` | contour-terminal/endo | `src/tui/InputField.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/InputField_test.cpp` | contour-terminal/endo | `src/tui/InputField_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/KeyBindings.cpp` | contour-terminal/endo | `src/tui/KeyBindings.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/KeyBindings.hpp` | contour-terminal/endo | `src/tui/KeyBindings.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/KeyBindings_test.cpp` | contour-terminal/endo | `src/tui/KeyBindings_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/KeyCode.hpp` | contour-terminal/endo | `src/tui/KeyCode.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/List.cpp` | contour-terminal/endo | `src/tui/List.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/List.hpp` | contour-terminal/endo | `src/tui/List.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/LogPanel.cpp` | contour-terminal/endo | `src/tui/LogPanel.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/LogPanel.hpp` | contour-terminal/endo | `src/tui/LogPanel.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownHtml.cpp` | contour-terminal/endo | `src/tui/MarkdownHtml.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownHtml.hpp` | contour-terminal/endo | `src/tui/MarkdownHtml.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownHtml_test.cpp` | contour-terminal/endo | `src/tui/MarkdownHtml_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownImage_test.cpp` | contour-terminal/endo | `src/tui/MarkdownImage_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownInline.hpp` | contour-terminal/endo | `src/tui/MarkdownInline.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownRenderer.cpp` | contour-terminal/endo | `src/tui/MarkdownRenderer.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownRenderer.hpp` | contour-terminal/endo | `src/tui/MarkdownRenderer.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownRenderer_test.cpp` | contour-terminal/endo | `src/tui/MarkdownRenderer_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownTable.cpp` | contour-terminal/endo | `src/tui/MarkdownTable.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownTable.hpp` | contour-terminal/endo | `src/tui/MarkdownTable.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MarkdownTable_test.cpp` | contour-terminal/endo | `src/tui/MarkdownTable_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MockTerminalOutput.cpp` | contour-terminal/endo | `src/tui/MockTerminalOutput.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/MockTerminalOutput.hpp` | contour-terminal/endo | `src/tui/MockTerminalOutput.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Modifier.hpp` | contour-terminal/endo | `src/tui/Modifier.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/PopupKeyDispatch.hpp` | contour-terminal/endo | `src/tui/PopupKeyDispatch.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/PopupKeyDispatch_test.cpp` | contour-terminal/endo | `src/tui/PopupKeyDispatch_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/QuestionComponent.cpp` | contour-terminal/endo | `src/tui/QuestionComponent.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/QuestionComponent.hpp` | contour-terminal/endo | `src/tui/QuestionComponent.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/QuestionComponent_test.cpp` | contour-terminal/endo | `src/tui/QuestionComponent_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Rect.hpp` | contour-terminal/endo | `src/tui/Rect.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Renderer_test.cpp` | contour-terminal/endo | `src/tui/Renderer_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Screen.cpp` | contour-terminal/endo | `src/tui/Screen.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Screen.hpp` | contour-terminal/endo | `src/tui/Screen.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Screen_test.cpp` | contour-terminal/endo | `src/tui/Screen_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/ScrollableSelection.hpp` | contour-terminal/endo | `src/tui/ScrollableSelection.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/ScrollableSelection_test.cpp` | contour-terminal/endo | `src/tui/ScrollableSelection_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/SemanticBlockClient.cpp` | contour-terminal/endo | `src/tui/SemanticBlockClient.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/SemanticBlockClient.hpp` | contour-terminal/endo | `src/tui/SemanticBlockClient.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/SemanticBlockClient_test.cpp` | contour-terminal/endo | `src/tui/SemanticBlockClient_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/SgrBuilder.cpp` | contour-terminal/endo | `src/tui/SgrBuilder.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/SgrBuilder.hpp` | contour-terminal/endo | `src/tui/SgrBuilder.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Sixel.cpp` | contour-terminal/endo | `src/tui/Sixel.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Sixel.hpp` | contour-terminal/endo | `src/tui/Sixel.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Sixel_test.cpp` | contour-terminal/endo | `src/tui/Sixel_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/SmartCaseMatch_test.cpp` | contour-terminal/endo | `src/tui/SmartCaseMatch_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Spinner.cpp` | contour-terminal/endo | `src/tui/Spinner.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Spinner.hpp` | contour-terminal/endo | `src/tui/Spinner.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/StatusBar.cpp` | contour-terminal/endo | `src/tui/StatusBar.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/StatusBar.hpp` | contour-terminal/endo | `src/tui/StatusBar.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/StbImageImpl.cpp` | contour-terminal/endo | `src/tui/StbImageImpl.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the warning pragmas are gone: the module's CMakeLists.txt compiles this file with warnings off and `-fno-sanitize=undefined`, as per-source PRIVATE options |
| `src/core/tui/StyledText.cpp` | contour-terminal/endo | `src/tui/StyledText.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/StyledText.hpp` | contour-terminal/endo | `src/tui/StyledText.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/StyledText_test.cpp` | contour-terminal/endo | `src/tui/StyledText_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Terminal.cpp` | contour-terminal/endo | `src/tui/platform/TerminalShared.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | plus every `Terminal` member endo defined identically in `src/tui/platform/Terminal.cpp` and `src/tui/platform/TerminalWin32.cpp` (same commit) |
| `src/core/tui/Terminal.hpp` | contour-terminal/endo | `src/tui/Terminal.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TerminalInput.cpp` | contour-terminal/endo | `src/tui/platform/TerminalInput.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the members that touch no OS state, which endo had in both platform files |
| `src/core/tui/TerminalInput.hpp` | contour-terminal/endo | `src/tui/TerminalInput.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the OS state is the opaque `NativeState` the platform sources define, so this header includes neither `<windows.h>` nor `<termios.h>` (Ruling R41) |
| `src/core/tui/TerminalOutput.cpp` | contour-terminal/endo | `src/tui/platform/TerminalOutput.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `SyncGuard` and `syncGuard()` are here rather than per platform, because the sequences now go through `writeToDestination()`; the members that only compose bytes, which endo duplicated in `src/tui/platform/TerminalOutputWin32.cpp` (same commit); `copyToClipboard()` encodes through `core::base64::encode()` rather than a third copy of a base64 encoder |
| `src/core/tui/TerminalOutput.hpp` | contour-terminal/endo | `src/tui/TerminalOutput.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | post-import fix: `SyncGuard` holds the `TerminalOutput` it brackets instead of a native handle, so its `#if _WIN32` `NativeHandle` alias is gone (Ruling R41); `isTerminal()` added |
| `src/core/tui/TerminalOutput_test.cpp` | origin: core-cpp | - | - | the upstream file has no test; written for the `SyncGuard` and `isTerminal()` fix |
| `src/core/tui/TerminalProtocols.hpp` | contour-terminal/endo | `src/tui/TerminalProtocols.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TerminalProtocols_test.cpp` | contour-terminal/endo | `src/tui/TerminalProtocols_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TerminalQuery_test.cpp` | contour-terminal/endo | `src/tui/TerminalQuery_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TestHelpers.hpp` | contour-terminal/endo | `src/tui/TestHelpers.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Text.cpp` | contour-terminal/endo | `src/tui/Text.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Text.hpp` | contour-terminal/endo | `src/tui/Text.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TextDecorator.hpp` | contour-terminal/endo | `src/tui/TextDecorator.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Theme.cpp` | contour-terminal/endo | `src/tui/Theme.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Theme.hpp` | contour-terminal/endo | `src/tui/Theme.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TimerUtils.hpp` | contour-terminal/endo | `src/tui/TimerUtils.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Tooltip.cpp` | contour-terminal/endo | `src/tui/Tooltip.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Tooltip.hpp` | contour-terminal/endo | `src/tui/Tooltip.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TreeTableView.cpp` | contour-terminal/endo | `src/tui/TreeTableView.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TreeTableView.hpp` | contour-terminal/endo | `src/tui/TreeTableView.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/TreeTableView_test.cpp` | contour-terminal/endo | `src/tui/TreeTableView_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Unicode.cpp` | contour-terminal/endo | `src/tui/Unicode.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Unicode.hpp` | contour-terminal/endo | `src/tui/Unicode.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/Unicode_test.cpp` | contour-terminal/endo | `src/tui/Unicode_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/VtParser.cpp` | contour-terminal/endo | `src/tui/VtParser.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/VtParser.hpp` | contour-terminal/endo | `src/tui/VtParser.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/VtParser_test.cpp` | contour-terminal/endo | `src/tui/VtParser_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/Completer.cpp` | contour-terminal/endo | `src/tui/completer/Completer.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/Completer.hpp` | contour-terminal/endo | `src/tui/completer/Completer.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/CompletionItem.hpp` | contour-terminal/endo | `src/tui/completer/CompletionItem.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/CompletionProvider.hpp` | contour-terminal/endo | `src/tui/completer/CompletionProvider.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/FuzzyMatch.cpp` | contour-terminal/endo | `src/tui/completer/FuzzyMatch.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/FuzzyMatch.hpp` | contour-terminal/endo | `src/tui/completer/FuzzyMatch.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/SmartCaseMatch.cpp` | contour-terminal/endo | `src/tui/completer/SmartCaseMatch.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/completer/SmartCaseMatch.hpp` | contour-terminal/endo | `src/tui/completer/SmartCaseMatch.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/detail/XtVersion.hpp` | contour-terminal/endo | `src/tui/platform/TerminalOutput.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | `parseXTVersionName()` and the unscroll terminal list, which endo had verbatim in `src/tui/platform/TerminalOutputWin32.cpp` too; spelled `parseXtVersionName()` and `supportsUnscroll()` here |
| `src/core/tui/posix/ImageLoader.cpp` | contour-terminal/endo | `src/tui/ImageLoader.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the POSIX `readClipboardImage()` and its helper |
| `src/core/tui/posix/PosixIO.hpp` | contour-terminal/endo | `src/tui/platform/PosixIO.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | moved out of `tui/platform/`, which Ruling R40 does not keep |
| `src/core/tui/posix/Terminal.cpp` | contour-terminal/endo | `src/tui/platform/Terminal.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the destructor, `initialize()` and `shutdown()`: what SIGWINCH makes different. The NOLINTs on the file-scope signal state are gone with the `g` prefixes |
| `src/core/tui/posix/TerminalInput.cpp` | contour-terminal/endo | `src/tui/platform/TerminalInput.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the POSIX half, over `TerminalInput::NativeState`, which this file defines: Ruling R41 keeps `<termios.h>` out of `TerminalInput.hpp` |
| `src/core/tui/posix/TerminalOutput.cpp` | contour-terminal/endo | `src/tui/platform/TerminalOutput.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | only what goes through the OS: the write, the window size, the XTVERSION read and `isTerminal()` (core-cpp) |
| `src/core/tui/runtime/EventSource.hpp` | contour-terminal/endo | `src/tui/runtime/EventSource.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/Modal.hpp` | contour-terminal/endo | `src/tui/runtime/Modal.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/ModalComponent.hpp` | contour-terminal/endo | `src/tui/runtime/ModalComponent.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/Modal_test.cpp` | contour-terminal/endo | `src/tui/runtime/Modal_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/PollEventSource.cpp` | contour-terminal/endo | `src/tui/runtime/PollEventSource.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/PollEventSource.hpp` | contour-terminal/endo | `src/tui/runtime/PollEventSource.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/TerminalEventSource.hpp` | contour-terminal/endo | `src/tui/runtime/TerminalEventSource.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/TuiRuntime.cpp` | contour-terminal/endo | `src/tui/runtime/TuiRuntime.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/TuiRuntime.hpp` | LASTRADA-Software/fastcached | `vendor/endo/tui/runtime/TuiRuntime.hpp` | `5389e29a5eeca9c2319f43757bd7d6d0ac1c1a13` | endo `f774a210` plus fastcached `6483abd8`, which makes `DelayAwaiter::await_ready()` a constant and decides an elapsed deadline in `await_suspend()`: MSVC 19.44's ARM64 code generator loses the enclosing `try` of a `co_await` on an awaiter whose `await_ready()` reads the clock through a virtual `now()` |
| `src/core/tui/runtime/TuiRuntime_test.cpp` | LASTRADA-Software/fastcached | `vendor/endo/tui/runtime/TuiRuntime_test.cpp` | `5389e29a5eeca9c2319f43757bd7d6d0ac1c1a13` | endo `f774a210` plus fastcached `6483abd8`, which makes `DelayAwaiter::await_ready()` a constant and decides an elapsed deadline in `await_suspend()`: MSVC 19.44's ARM64 code generator loses the enclosing `try` of a `co_await` on an awaiter whose `await_ready()` reads the clock through a virtual `now()` |
| `src/core/tui/runtime/WithTimeout.hpp` | contour-terminal/endo | `src/tui/runtime/WithTimeout.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/posix/PollHelpers.hpp` | contour-terminal/endo | `src/tui/runtime/platform/PollHelpers.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | moved out of `runtime/platform/`; the file-wide `#if !defined(_WIN32)` is gone, because only POSIX translation units include it |
| `src/core/tui/runtime/posix/TerminalEventSource.cpp` | contour-terminal/endo | `src/tui/runtime/platform/TerminalEventSourcePosix.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | moved out of `runtime/platform/`; the file-wide `#if !defined(_WIN32)` is gone, because SOURCES_POSIX selects the file |
| `src/core/tui/runtime/testing/MockEventSource.hpp` | contour-terminal/endo | `src/tui/runtime/testing/MockEventSource.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/tui/runtime/windows/TerminalEventSource.cpp` | contour-terminal/endo | `src/tui/runtime/platform/TerminalEventSourceWin32.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | moved out of `runtime/platform/`; the file-wide `#if defined(_WIN32)` is gone, because SOURCES_WINDOWS selects the file |
| `src/core/tui/windows/ImageLoader.cpp` | contour-terminal/endo | `src/tui/ImageLoader.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the Windows `readClipboardImage()` |
| `src/core/tui/windows/Terminal.cpp` | contour-terminal/endo | `src/tui/platform/TerminalWin32.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | as `posix/Terminal.cpp`, for Windows; the file-wide `#if defined(_WIN32)` is gone, because SOURCES_WINDOWS selects the file |
| `src/core/tui/windows/TerminalInput.cpp` | contour-terminal/endo | `src/tui/platform/TerminalInputWin32.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | the Windows half, over `TerminalInput::NativeState`, which this file defines; the file-wide `#if defined(_WIN32)` is gone, because SOURCES_WINDOWS selects the file |
| `src/core/tui/windows/TerminalInput_test.cpp` | contour-terminal/endo | `src/tui/TerminalInputWin32_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | a Windows-only test source; the file-wide `#if defined(_WIN32)` is gone, and the named-mutex serialisation of its cases is kept |
| `src/core/tui/windows/TerminalOutput.cpp` | contour-terminal/endo | `src/tui/platform/TerminalOutputWin32.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | as `posix/TerminalOutput.cpp`, for Windows; the file-wide `#if defined(_WIN32)` is gone, because SOURCES_WINDOWS selects the file |
| `src/core/tui/windows/Win32Utf.hpp` | contour-terminal/endo | `src/tui/platform/Win32Utf.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | moved out of `tui/platform/`, which Ruling R40 does not keep |
