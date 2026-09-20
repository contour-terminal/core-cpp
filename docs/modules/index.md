# Modules

core-cpp is one CMake project with one module per directory under `src/core/`. A module's
namespace is its directory (`src/core/net/` is `core::net`; headers directly in `src/core/` are
`core`), its real targets are named `core-cpp-<name>`, and consumers link the aliases
`core::<name>`.

| Module | Namespace | Target(s) | Kind | Depends on | Status |
|---|---|---|---|---|---|
| [base](base.md) | `core` | `core::base` | static | Threads; Tracy (optional) | available |
| [log](log.md) | `core::log` | `core::log` | static | base | available |
| [cli](cli.md) | `core::cli` | `core::cli` | static | base, log | available |
| [platform](platform.md) | `core::platform` | `core::platform` | static | base, log | available |
| [async](async.md) | `core::async` | `core::async` | header-only | Threads | `StopToken`, `Task`, `whenAll`, `whenAny` available; executors: Task B1 |
| [net](net.md) | `core::net` | `core::net_types`, `core::net`, `core::net_tls` | header-only, static, static | async, platform; OpenSSL for `net_tls` | contour's event loop, sockets, TLS and HTTP server available; the merge with fastcached's: Tasks B2 to B11 |
| [tui](tui.md) | `core::tui` | `core::tui_output`, `core::tui` | static | `tui_output`: base; `tui`: also platform, async, libunicode, stb (optional) | endo's terminal UI available, native only; its runtime moves onto `core::net::EventLoop` in Task B12 |
| [testing](testing.md) | `core::testing` | `core::testing`, `core::testing_dialogs`, `core::testing_main` | static, object, static | base; log and Catch2 for `testing_main` | available |

The task numbers refer to the
[implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md).

## Layering

A module may link only the modules its row in
[`cmake/CoreCppModules.cmake`](https://github.com/contour-terminal/core-cpp/blob/master/cmake/CoreCppModules.cmake)
lists, and every one of those must appear in an earlier row. The configure refuses anything else,
so the graph below is enforced rather than documented. A module's further targets
(`core::net_types` and `core::net_tls` beside `core::net`) have rows of their own. A row says where
its target builds when that differs from its module, which is how `core::net_types` builds under
Emscripten while the rest of `net` does not, and its `DEPS` are all that target may link: another
target of the module by name, or a module its module's row lists. `core::net_types` names none and
links nothing; `core::net_tls` names `net`. A target that follows its module's row links what that
row lists and the module's other targets:

```mermaid
graph BT
    log --> base
    cli --> base
    cli --> log
    platform --> base
    platform --> log
    net --> async
    net --> platform
    tui_output --> base
    tui --> base
    tui --> tui_output
    tui --> platform
    tui --> async
    testing --> base
    testing --> log
```

`async` depends on no other core-cpp module, so it can be used without anything else from
core-cpp. It does link Threads, because the `StopToken` fallback synchronises its stop state with
a `std::mutex`, a `std::condition_variable` and `std::this_thread::get_id()`; a single-threaded
Emscripten build takes neither, and links nothing at all. `tui_output` depends on `base` only, so a program can write styled terminal output
without an event loop, coroutines or libunicode; Lightweight's `dbtool` uses it that way.

## Public and private headers

A module's public headers are the ones in its `FILE_SET HEADERS`, included as
`<core/<module>/<Header>.hpp>`. Its `detail/`, `posix/`, `linux/`, `bsd/` (Apple and the
BSDs), `darwin/`, `windows/` and `emscripten/` subdirectories, and the TUI's `platform/`, are
private and in no file set. A module's own directory holds only platform-independent code; what
one platform needs is in those subdirectories, which CMake's per-platform source lists select. A
module's
`testing/` subdirectory holds its test doubles; they are public and compiled into the module, so
a consumer's tests can use them.

## The WebAssembly subset

Under single-threaded Emscripten (emsdk 3.1.56 and the latest release, no pthreads) only this
subset builds, and CI runs its tests under node:

| Module | Under Emscripten |
|---|---|
| base, log, cli | fully |
| async | everything except `ThreadPoolExecutor.hpp` |
| platform | Types (with `NativeHandle`), PlatformError, Clock, StringUtils, PathUtils, GlobMatch, FileUri, and the POSIX `EnvironmentProvider` and `FileInfoProvider` behind `nativeEnvironmentProvider()` and `nativeFileInfoProvider()` |
| net | `net_types` today; `IoBackend`, `EventLoop`, timers, `DeadlineTimer`, `WithTimeout`, the host-driven backend and the test doubles from Tasks B3 to B5; never sockets, DNS, TLS or HTTP |
| testing | fully (the Windows parts are no-ops) |
| tui | never |

Code in the subset uses no `std::thread`, no blocking wait and no `Threads::Threads`, and checks a
`__cpp_lib_*` feature-test macro before using a library facility newer than libc++ 17, which
emsdk 3.1.56 ships.
