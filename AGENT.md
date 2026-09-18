# core-cpp: agent guidelines

Tripwires and pointers. The reasoning behind each rule lives in the file it points to; read that
file before changing code in its area.

## What this repository is

The shared C++23 foundation of the Contour Terminal projects, in namespace `core`, one namespace
per directory under `src/core/`. It replaces copies of the same code in contour, endo,
fastcached and tuidu, and merges contour's and fastcached's coroutine and networking designs.
The design is [the spec](docs/superpowers/specs/2026-09-18-core-cpp-design.md); the order of work
is [the plan](docs/superpowers/plans/2026-09-18-core-cpp.md).

| Module | Namespace | Targets | Depends on |
|---|---|---|---|
| base | `core` | `core::base` | Threads |
| log | `core::log` | `core::log` | base |
| cli | `core::cli` | `core::cli` | base, log |
| platform | `core::platform` | `core::platform` | base, log, coro |
| coro | `core::coro` | `core::coro` (INTERFACE) | std only |
| net | `core::net` | `core::net_types`, `core::net`, `core::net_tls` | coro, platform |
| tui | `core::tui` | `core::tui_output`, `core::tui` | base (leaf); + platform, coro, net, libunicode |
| testing | `core::testing` | `core::testing`, `core::testing_dialogs`, `core::testing_main` | base; Catch2 for `testing_main` |

Only `testing` exists so far; the others arrive with Tasks A3 to A7 and Phase B. The module DAG is
the table in `cmake/CoreCppModules.cmake`, and configure refuses a link it does not list.

Consumers: contour (vendored), endo, fastcached, tuidu, Lightweight's `dbtool`, morph (CPM). Who
links what, and where each keeps its pin: [`.agent/reference/consumers.md`](.agent/reference/consumers.md).
The annotated tree: [`.agent/reference/source-map.md`](.agent/reference/source-map.md).

## Library rules

core-cpp lives inside other people's builds. Details: [`.agent/rules/library-hygiene.md`](.agent/rules/library-hygiene.md).

- **No global CMake state** outside `cmake/CoreCppTopLevel.cmake`, which is included only when
  core-cpp is top-level. No PUBLIC or INTERFACE flag, ever.
- **Every option and cache variable is `CORE_CPP_`, every function `core_cpp_`.** The fastcached
  compiler-cache module is the one exemption, by path.
- **Namespace = directory.** An include across modules is an edge of the module table, or it is
  a layering violation.
- **Public API = the module's `FILE_SET HEADERS`**, and a change to it is a CHANGELOG entry.
- **Grep the consumers before changing a public signature**: contour, endo, fastcached, tuidu,
  Lightweight's `dbtool`, morph. The pull request's "Consumer impact" says what each must change.
- **No new dependency** without an option that gates it, a row in
  `cmake/CoreCppDependencies.cmake`, a fetch-or-system classification and a CHANGELOG entry.
- **Never edit a vendored copy downstream**: fix here, release, re-vendor.
- **No consumer-specific concept enters core-cpp.**
- **The WebAssembly subset stays single-thread safe**: no `std::thread`, no blocking wait, no
  `Threads::Threads`, nothing newer than libc++ 18 without a feature-test macro.
- **The compiler cache is fastcache-cc when it answers** (`cmake/portable/CompileCache.cmake`,
  verbatim from fastcached, never edited here). Never set `USE_COMPILER_CACHE=OFF` locally except
  for coverage.

## The rulebook

`.agent/rules/` holds the load-bearing constraints. **Every rule there has already been a bug**,
usually a silent one. The bullets below are tripwires, not summaries. Link these files as plain
markdown, never with an `@` import, or every session loads all of them.

- **[`rules/cpp-guidelines.md`](.agent/rules/cpp-guidelines.md)**: the canonical C++ rules. No
  C-style `for`, no `NOLINT`, no diagnostic pragma, no raw owning pointer, no `k`/`g_` prefix;
  coroutine parameters by value; format and tidy at the pinned 22.1.8, never `-i` with another.
- **[`rules/design-principles.md`](.agent/rules/design-principles.md)**: inject every ambient
  resource; a constructed object is usable; behaviour is a table; fallible is `std::expected`;
  no `bool` in an API where an `enum class` says what it means.
- **[`rules/library-hygiene.md`](.agent/rules/library-hygiene.md)**: the library rules above,
  the graduation rule, SemVer with 0.x breaks recorded under Breaking.
- **[`rules/build-and-toolchain.md`](.agent/rules/build-and-toolchain.md)**: `build.ninja`, not
  `CMakeCache.txt`, shows the launcher; rebuild `clangcl-*` trees with `--clean-first` after a
  header edit
  ([fastcached#1531](https://github.com/LASTRADA-Software/fastcached/issues/1531));
  `CORE_CPP_WERROR` decides fatality, not which warnings exist; a gate that does not report reads
  as passed.
- **[`rules/testing.md`](.agent/rules/testing.md)**: exit 77 means all skipped; every wait is
  bounded and says what it waited for; `SKIP`, never `SUCCEED`, where a case could not run; a
  `REQUIRE` above a stop turns a red into a hang; assert what distinguishes.
- **[`rules/async-and-net.md`](.agent/rules/async-and-net.md)**: backends dispatch, the loop
  resumes; a loop-owned object dies on the loop's thread; a loop frees only what nothing else
  owns; one read and one write operation per socket; `close()` touches no member after completing;
  a profiling zone never spans `co_await`.
- **[`rules/platform.md`](.agent/rules/platform.md)**: an OS difference is an injected
  implementation, never an `#ifdef` in logic; no `<Windows.h>` in a public header.
- **[`rules/tui.md`](.agent/rules/tui.md)**: `core::tui_output` depends on base only; every byte
  goes through `writeToDestination`.

How-tos: [`.agent/guides/`](.agent/guides/) (team runs, Tracy, consumer migration, releasing).

## Design principles

Dependency injection by constructor; configuration fixed at construction; data-driven tables;
`std::expected` with monadic chaining (the only exception is `core::coro::OperationCancelled`);
`enum class` over `bool`; RAII for every handle. Details:
[`.agent/rules/design-principles.md`](.agent/rules/design-principles.md).

## C++ coding guidelines

C++23 with extensions off; types `CamelCase`, everything else `camelBack`, private members
`_camelBack`, constants `CamelCase` with no prefix; `// SPDX-License-Identifier: Apache-2.0` on
line one; `.hpp` with `#pragma once`, self-contained; `[[nodiscard]]` where ignoring a result is
a bug; Doxygen `///` on public API; zero warnings. The canonical text:
[`.agent/rules/cpp-guidelines.md`](.agent/rules/cpp-guidelines.md).

## Building

| Host | Debug | Release | More |
|---|---|---|---|
| Linux | `clang-debug`, `gcc-debug` | `clang-release`, `gcc-release` | `clang-asan-ubsan`, `clang-tsan`, `clang-tidy`, `clang-coverage`, `clang-tracy` |
| macOS | `appleclang-debug` | `appleclang-release` | the `clang-*` presets with Homebrew LLVM |
| Windows (VS dev shell) | `cl-debug`, `clangcl-debug` | `cl-release`, `clangcl-release` | `cl-release-tls` |
| any, with `EMSDK` set | | `emscripten` | tests run under node |

`cmake --preset <p> && cmake --build --preset <p> && ctest --preset <p>`, or
`cmake --workflow --preset ci-<p>`. Trees live in `out/build/<preset>`.

## Testing

Tests sit next to their sources (`Foo_test.cpp`) and are registered with `core_cpp_add_test`,
one binary per module linked to `core::testing_main`. Exit codes: 0 pass, 1 failure, 77 all
skipped, 2 nothing ran. Labels: `core-cpp`, the module, `hygiene`, `canary`, `loopback`,
`no-tsan`. `ctest -L hygiene` runs the checks over the tree and the build contract.

## Documentation

Factual, no marketing: what the code does, its limits and its current state. The site is
`mkdocs.yml` plus `docs/`; `mkdocs build --strict` must pass, and CI deploys it with the Doxygen
API reference to <https://contour-terminal.github.io/core-cpp/>.

## Releasing

The tag equals `project(VERSION)`, and `CHANGELOG.md` has its section; the release workflow
refuses otherwise. Use the `/draft-release` skill, then `/publish-release`. Details:
[`.agent/guides/releasing.md`](.agent/guides/releasing.md).

## Workflow checklist

1. `python scripts/clang-format.py --check` (the pinned clang-format).
2. The `clang-tidy` preset (the pinned clang-tidy), clean.
3. `clang-debug`, then `gcc-release`; on Windows `cl-debug` and `clangcl-release`
   (`--clean-first` on a cache-populated clang-cl tree).
4. `mkdocs build --strict` if `docs/`, `mkdocs.yml` or a public header's comments changed.
5. A CHANGELOG entry under `[Unreleased]`.
6. "Consumer impact" in the pull request body.
