# Library hygiene

What makes core-cpp safe to consume: through CPM, as a vendored copy, or as a subproject of a
build whose flags, cache and dependencies belong to somebody else. Six projects consume it
([`../reference/consumers.md`](../reference/consumers.md)), so a mistake here is paid six
times, usually in a repository whose authors never read this file.

The rules come from the design spec,
[Part I §1, §3, §4 and §5](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md),
and from why core-cpp exists at all: the coroutine, networking, platform and TUI code had
spread across contour, endo, fastcached and tuidu as near-verbatim copies, and the copies
drifted. When core-cpp was designed, tuidu's copy was two and a half months stale and no
longer built against contour's `master`, and fastcached kept a second coroutine runtime only
because its event loop could not wait on a console handle.

## No global state unless core-cpp is the build

- **Only `cmake/CoreCppTopLevel.cmake` touches global state**, and the top-level
  `CMakeLists.txt` includes it only `if(PROJECT_IS_TOP_LEVEL)`. That file selects the compiler
  cache, bounds dependency transfers, and sets `CMAKE_CXX_STANDARD`,
  `CMAKE_EXPORT_COMPILE_COMMANDS` and `CMAKE_COLOR_DIAGNOSTICS`. As a subproject, core-cpp
  changes none of its parent's launcher, standard, flags, environment or cache.
- **No `add_compile_options`, `include_directories`, `link_libraries` or other directory-wide
  command**, and no `set(CMAKE_...)` or `set(ENV{...})` outside that file. They reach every
  target of the consumer.
- **No PUBLIC or INTERFACE compile or link flag, ever.** Every flag is PRIVATE to the core-cpp
  target it is applied to (`core_cpp_apply_toolchain`). The C++ standard is the one usage
  requirement a library passes on (`cxx_std_23`), because its headers need it.
- **Sanitizers are top-level only.** `CORE_CPP_SANITIZERS` stops the configure when core-cpp
  is a subproject: instrumenting core-cpp's targets alone under a parent mixes instrumented
  and uninstrumented code, which is what makes ThreadSanitizer report races that are not
  there. A parent instruments core-cpp itself through the `CORE_CPP_TARGETS` global property.
- **All of this is checked, not intended.** `tests/cmake/check-cmake-hygiene.cmake`
  (`ctest -L hygiene`) refuses each shape by name, and its self-test proves every rule
  refuses. An exemption is a row in its allowlist with a reason, and a row that no longer
  allows anything is refused as stale.

## Names

- **Every option and cache variable is `CORE_CPP_`-prefixed, and every function and macro is
  `core_cpp_`-prefixed.** A consumer's cache is shared with every subproject it adds; an
  unprefixed `TESTING` or `WITH_TLS` collides.
- **The one exemption is by path:** `cmake/portable/CompileCache.cmake` and
  `cmake/FetchTransferBound.cmake` are verbatim copies from fastcached whose unprefixed options
  (`USE_COMPILER_CACHE`, `FASTCACHE_*`, `FASTCACHED_FETCH_*`) are shared on purpose across the
  organisation's projects, so one `-DUSE_COMPILER_CACHE=OFF` means the same thing everywhere.
- **Targets are `core-cpp-<name>`, aliased `core::<name>`, and their type is always explicit.**
  Consumers link the alias.
- **A module's namespace is its directory:** `src/core/net/` is `core::net`. Headers directly
  in `src/core/` are `core`. The `style` job's hygiene scan checks the first named namespace of
  every source under `src/core/` (`namespace-directory`).

## Layering

- **The module table is the dependency graph.** `cmake/CoreCppModules.cmake` lists every
  module in dependency order, with the modules it may link. `core_cpp_add_module` refuses a
  `core::<x>` link that the row does not list, so the layering is enforced at configure time.
- **An include across modules is an edge of that table.** `core::coro` depends on the
  standard library only; `core::net` on `coro` and `platform`; `core::tui_output` on `base`
  only. A header that includes across modules without a table edge is a layering violation
  even while the link happens to work transitively.
- **An edge is closed by moving the file to the layer that owns it**, not by widening the
  table. fastcached closed ten such edges in its networking layer that way, after they had sat
  unnoticed because an include graph drifts in silence: nothing fails, and the edge is found
  by whoever finally tries to lift the code out. Origin:
  [fastcached#100](https://github.com/LASTRADA-Software/fastcached/issues/100).

## Dependencies

- **The dependency table is the whole list** (`cmake/CoreCppDependencies.cmake`). A row is
  resolved from the parent's target first, then `find_package(QUIET)`, then CPM, and CPM only
  when `CORE_CPP_FETCH_DEPS` is on; otherwise the configure stops and names the option that
  needed the dependency.
- **A new dependency takes an option that gates it, a row, a classification and a CHANGELOG
  entry.** The classification says whether it may be fetched (`CPM ...`) or must come from the
  system (`NO_FETCH`, as OpenSSL and Threads do), and it decides whether contour's offline,
  vendored build can still succeed.
- **A row is added by the task that first needs it**, so a configure never fetches what
  nothing links.

## The WebAssembly subset stays single-thread safe

The modules and files listed as the WebAssembly subset (the design spec, Part I §1) build and
pass their tests under single-threaded Emscripten, with emsdk 3.1.56 and the latest release.
morph's browser clients depend on it.

- **No `std::thread`, no `std::jthread`, no blocking wait and no `Threads::Threads`** in the
  subset. Linking Threads there forces `-pthread`, and with it `SharedArrayBuffer`, onto every
  consumer page. `CORE_CPP_SINGLE_THREADED_WASM` is detected, not assumed, and the Threads row
  is skipped when it is on.
- **libc++ 17 is the floor**, the one emsdk 3.1.56 ships (17.0.4): check `__cpp_lib_*` before
  using a newer library facility.
- **A subset module is done only when the `emscripten` CI job is green on both emsdk
  versions.** The job also asserts that no compile or link command mentions `pthread`.

## Vendoring

contour carries a verbatim copy of core-cpp in `vendor/core-cpp`, so that distribution
packagers get no new dependency. The contract is in
[`docs/vendoring.md`](https://contour-terminal.github.io/core-cpp/vendoring/).

- **A vendored copy is byte-identical to a tag or a full SHA, and is never edited.** Fix
  upstream, cut a patch release, re-vendor. A local edit is the drift core-cpp exists to end,
  and the manifest check refuses it. *(endo's rule for its fetched copies:
  [endo `AGENT.md`, "Vendored sources"](https://github.com/contour-terminal/endo/blob/f774a210ce989e5947b8f61d715068b1dc96088c/AGENT.md))*
- **Changes flow outward.** core-cpp is the source of truth; a consumer adapts to its API, and
  nothing is re-synced from a consumer's copy back into core-cpp. *(contour's rule for its
  `coro` and `net`:
  [contour `AGENT.md`, "Repository layout"](https://github.com/contour-terminal/contour/blob/6777ff05014f8ff163b071e8b0e942830119db80/AGENT.md))*
- **What core-cpp itself vendors, it vendors the same way.** `cmake/portable/README.md`
  records the upstream commit of each verbatim file, and the nightly `downstream.yml` fails
  when fastcached's copy has moved on.

## What belongs in core-cpp

- **The graduation rule: a file moves into core-cpp when a second project needs it.** It must
  carry no PUBLIC flag and no dependency beyond the standard library and Threads, or a row in
  the dependency table. crispy's renderer half, vtparser and endo's shell-specific platform
  code stay where they are for that reason. *(the design spec, Part I §1)*
- **No consumer-specific concept enters core-cpp.** Every change is a generic improvement that
  makes sense to every consumer on its own merits. tuidu held its copies to exactly this rule
  ("no disk-usage notions, no `Node`, `Tree` or `Scanner`"), and it is what keeps six consumers
  able to share one copy. Origin:
  [tuidu `AGENT.md`, "The vendored libraries are shared with endo"](https://github.com/contour-terminal/tuidu/blob/30107fbab72310fde5db89e7882eab288f6b541e/AGENT.md).

## Public API and versioning

- **The public API is what a module's `FILE_SET HEADERS` lists.** Private headers (`detail/`,
  `posix/`, `linux/`, `darwin/`, `windows/`, `backend/`, `tui/platform/`) are in no file set,
  and a consumer that includes one is on its own.
- **Grep the consumers before changing a public signature**: contour, endo, fastcached,
  tuidu, Lightweight's dbtool and morph ([`../reference/consumers.md`](../reference/consumers.md)
  says where each keeps its code). The pull request's "Consumer impact" section states what
  each of them must change.
- **Every public change has a CHANGELOG entry under `[Unreleased]`**, and a breaking one goes
  under **Breaking** with a migration note.
- **SemVer, with `vX.Y.Z` tags.** In 0.x a minor release may break; a patch release never
  does. The literal in `project(core-cpp VERSION ...)` is the source of truth, and the release
  workflow refuses a tag that does not equal it.
- **Consumers pin a tag, or temporarily a full SHA; never a branch.** A branch pin is a
  dependency that changes under a consumer's CI without a commit in its repository.
- **No `install()` in 0.1.0.** Targets are install-ready through their file sets; exporting
  them is [core-cpp#5](https://github.com/contour-terminal/core-cpp/issues/5).

## Open work

- **[core-cpp#5](https://github.com/contour-terminal/core-cpp/issues/5)** — `install()` and an
  exported package config for distro unbundling, then a vcpkg port.
- **[core-cpp#8](https://github.com/contour-terminal/core-cpp/issues/8)** — graduate
  fastcached's Logger, Base64/Sha256, Cli Options and EnumTable when a second consumer needs
  them.
