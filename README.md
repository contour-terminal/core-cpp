# core-cpp

[![Build](https://github.com/contour-terminal/core-cpp/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/contour-terminal/core-cpp/actions/workflows/build.yml)
[![Docs](https://github.com/contour-terminal/core-cpp/actions/workflows/docs.yml/badge.svg?branch=master)](https://contour-terminal.github.io/core-cpp/)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)

core-cpp is the shared C++23 foundation of the Contour Terminal projects: base utilities,
logging, command-line parsing, an operating-system layer, coroutines, an event loop with
sockets and TLS, and a terminal UI. It replaces the near-verbatim copies of this code that
contour, endo, fastcached and tuidu each carried, and it merges contour's and fastcached's two
coroutine and networking designs into one. Everything is in namespace `core`, one namespace per
directory. A defined subset (base, log, cli, coro and testing, and parts of platform and net) is
held to building and passing its tests under single-threaded WebAssembly.

**Status: 0.1.0 is in development.** The build framework, `core::base`, `core::log`,
`core::cli`, `core::platform` and `core::testing` exist; the other modules are imported and
merged by the tasks of the
[implementation plan](docs/superpowers/plans/2026-09-18-core-cpp.md), and the table below says
which. Nothing is tagged yet.

## Modules

| Module | Namespace | Target(s) | Depends on | Contents | Status |
|---|---|---|---|---|---|
| base | `core` | `core::base` | Threads | assertions, environment, escaping, hashing, flags, time, `Base64`, profiling macros, range helpers | **available** |
| log | `core::log` | `core::log` | base | log store and sinks | **available** |
| cli | `core::cli` | `core::cli` | base, log | command-line parser, application scaffold | **available** |
| platform | `core::platform` | `core::platform` | base, log, coro | clocks, wakeup, signals, pipes, file system, environment, paths | **available** |
| coro | `core::coro` | `core::coro` (header-only) | the standard library | `Task`, cancellation, `whenAll`/`whenAny`, generators, executors, `AsyncQueue` | `Generator` **available**; the rest planned (A5, B1) |
| net | `core::net` | `core::net_types`, `core::net`, `core::net_tls` | coro, platform; OpenSSL for TLS | event loop and backends (epoll, kqueue, IOCP, poll, host-driven), sockets, dialling, timers, TLS, HTTP server | planned (A6, B2-B11) |
| tui | `core::tui` | `core::tui_output`, `core::tui` | base; the full TUI also platform, coro, net, libunicode | terminal output, input, widgets, runtime | planned (A7, B12) |
| testing | `core::testing` | `core::testing`, `core::testing_main` | base; log and Catch2 for `testing_main` | Windows dialog suppression, a fake environment, scoped temporary directory, working directory and environment variable, a Catch2 `main()` with the `LOG` filter and a normalised exit code | **available** |

The layering is enforced: a module links only the modules its row in
[`cmake/CoreCppModules.cmake`](cmake/CoreCppModules.cmake) lists.

## Using it with CPM

```cmake
CPMAddPackage(
    NAME core-cpp
    GITHUB_REPOSITORY contour-terminal/core-cpp
    GIT_TAG v0.1.0
    SYSTEM YES              # core-cpp headers never trip your -Werror
    EXCLUDE_FROM_ALL YES    # build only what you link
    OPTIONS "CORE_CPP_WITH_TUI ON" "CORE_CPP_WITH_TLS OFF")
target_link_libraries(myapp PRIVATE core::coro core::net core::tui)
# local development against a checkout: -DCPM_core-cpp_SOURCE=/path/to/core-cpp
```

As a subproject core-cpp changes nothing of its parent's: no compiler launcher, no C++
standard, no directory-wide flag, and every option is `CORE_CPP_`-prefixed. Pin a tag, never a
branch. The options are listed in
[the documentation](https://contour-terminal.github.io/core-cpp/getting-started/options/).

## Vendoring

A project that must build without fetching anything can carry a verbatim copy instead:
`cmake/CoreCppVendor.cmake` copies a tag's files into your tree and writes a manifest of their
hashes, and a check refuses any local change, missing file or extra file. contour consumes
core-cpp this way. The contract, the file set and your obligations as a consumer are in
[`docs/vendoring.md`](docs/vendoring.md).

## Building

Every build uses a preset and builds into `out/build/<preset>`:

```sh
cmake --preset clang-debug && cmake --build --preset clang-debug && ctest --preset clang-debug
cmake --workflow --preset ci-gcc-release      # configure, build and test in one step
```

| Host | Presets |
|---|---|
| Linux | `clang-debug`, `clang-release`, `gcc-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`, `clang-tidy`, `clang-coverage`, `clang-tracy` |
| macOS | `appleclang-debug`, `appleclang-release`, and the `clang-*` presets with Homebrew LLVM |
| Windows (from a Visual Studio developer shell) | `cl-debug`, `cl-release`, `clangcl-debug`, `clangcl-release`, `cl-release-tls` |
| Any, with emsdk (`EMSDK` set) | `emscripten`: single-threaded WebAssembly, tests run under node |

The build goes through fastcache-cc when a fastcached daemon answers, otherwise ccache; see
[`cmake/portable/README.md`](cmake/portable/README.md).

## Requirements

- CMake 3.25 or newer, and Ninja.
- A C++23 compiler: clang 22, GCC 14, AppleClang from Xcode 16, or Visual Studio 2022 or newer
  (`cl` or `clang-cl` 22).
- Python 3, for the formatting and tool-version scripts.
- For WebAssembly: emsdk 3.1.56 or newer, and node.
- Dependencies are resolved from your project, then `find_package`, then fetched with CPM:
  Catch2 3.8.0 (tests), libunicode (TUI), stb (TUI images), Tracy (profiling), and OpenSSL from
  the system (TLS).

## Used by

| Project | How |
|---|---|
| [contour](https://github.com/contour-terminal/contour) | vendored |
| [endo](https://github.com/contour-terminal/endo) | CPM |
| [fastcached](https://github.com/LASTRADA-Software/fastcached) | CPM |
| [tuidu](https://github.com/contour-terminal/tuidu) | CPM |
| [Lightweight](https://github.com/LASTRADA-Software/Lightweight) `dbtool` | CPM |
| [morph](https://github.com/LASTRADA-Software/morph) | CPM, including its WebAssembly build |

Each migrates onto core-cpp after `v0.1.0` is tagged.

## Documentation

<https://contour-terminal.github.io/core-cpp/>, with the API reference at
<https://contour-terminal.github.io/core-cpp/api/>. Contributing: [`CONTRIBUTING.md`](CONTRIBUTING.md).

## License

Apache License 2.0; see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE), which names every project
code was imported from and the commit it was imported at.
