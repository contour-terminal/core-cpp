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
- **A target that links less than its module has a row of its own, and its `DEPS` are the whole
  list** (`core_cpp_module_target`): another target of the module by name, or a module its
  module's row lists; no `DEPS` links no core-cpp target, not even the module's own. That is
  what holds `core::net_types` to nothing while `core::net` links `async` and `platform`, and
  what holds `core::tui_output` to `base`. Before rows had `DEPS`, the module's
  row bounded all its targets, so `core::net_types` could have linked `core::async` unrefused.
  `tests/cmake/check-layering.cmake` proves each refusal by name.
- **`DEPS` bounds only a target that is *given* a row.** A secondary target without one falls
  back to its module's row, which allows everything that row allows *and* the module's other
  targets. So a target that must link less than its module needs a row; leaving it out is not a
  tighter default, it is the module's default.
- **A module's targets may link each other by design** (Ruling R43). `core::net` links
  `core::net_types`, `core::net_tls` links `core::net`, `core::tui` links `core::tui_output`:
  that is one module split into the parts a consumer may take separately, not a layering
  violation, and a row's `DEPS` name a sibling by the sibling's own name.
- **The check sees the links it is given, and only those.** It reads the `PUBLIC_LIBS` and
  `PRIVATE_LIBS` of `core_cpp_add_module()`. A `core::` library behind a generator expression,
  or added by a bare `target_link_libraries()` after the call, is invisible to it — so a link
  that has to be spelled either way is a link the table is no longer enforcing.
- **An include across modules is an edge of that table.** `core::async` depends on the
  standard library only; `core::net` on `async` and `platform`; `core::tui_output` on `base`
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

## Provenance

- **Every file under `src/core/`, `cmake/portable/` and `cmake/FetchTransferBound.cmake` has a
  row in [`.agent/reference/provenance.md`](../reference/provenance.md)**: the upstream repo,
  upstream path and synced SHA it was imported or ported from, or `origin: core-cpp` for code
  written here. An import task adds its rows in the same commit as the import; a later port or
  fix that touches an already-listed file bumps its SHA in that commit too.
- **`tests/cmake/check-cmake-hygiene.cmake`'s `provenance` rule checks the table, not just its
  existence.** A file in scope without a row is refused by name, and a row naming a file that no
  longer exists is refused too, so a rename or deletion cannot leave a stale row behind.
- **The table is read mechanically, not just kept for humans.** Task B12b's catch-up check runs
  it against each row's upstream repo before v0.1.0, to find what has moved since the row's SHA
  was recorded. A consumer migration's delta check reads the same table to scope what it must
  compare.

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
  `posix/`, `linux/`, `bsd/`, `darwin/`, `windows/`, `emscripten/`, `tui/platform/`) are in no file set,
  and a consumer that includes one is on its own.
- **Grep the consumers before changing a public signature**: contour, endo, fastcached,
  tuidu, Lightweight's dbtool and morph ([`../reference/consumers.md`](../reference/consumers.md)
  says where each keeps its code). The pull request's "Consumer impact" section states what
  each of them must change.
- **Every public change has a CHANGELOG entry under `[Unreleased]`**, and a breaking one goes
  under **Breaking** with a migration note.
- **A correction is not finished until every document that carried the wrong version carries the
  right one.** A behaviour that is described in more than one place — the code's comments, a
  test, the CHANGELOG, an issue's list — is corrected in *all* of them or in none. Task A10
  asserted that real streams accept a `putback()` of a character the file does not hold; libc++
  refuted it, and the fix, the test and the issue were all put right within the hour while the
  CHANGELOG entry, written earlier, kept telling consumers the refuted story. It was invisible
  precisely because it had been written before the question arose. When a claim changes, grep the
  tree and the issues for the claim, not for the file you were editing.
- **SemVer, with `vX.Y.Z` tags.** In 0.x a minor release may break; a patch release never
  does. The literal in `project(core-cpp VERSION ...)` is the source of truth, and the release
  workflow refuses a tag that does not equal it.
- **Consumers pin a tag, or temporarily a full SHA; never a branch.** A branch pin is a
  dependency that changes under a consumer's CI without a commit in its repository.
- **Installing is `core_cpp_install()`'s, and nothing else calls `install()`.**
  `cmake/CoreCppInstall.cmake` installs what `core_cpp_add_module()` created, in the component
  `core-cpp`, behind `CORE_CPP_INSTALL` (default `PROJECT_IS_TOP_LEVEL`, so a vendoring or CPM
  consumer's install is unchanged). A module adds nothing to install: a public header goes in its
  `HEADERS` file set -- including a `detail/` header a public one includes, or the installed header
  cannot compile -- and a header-only dependency needed only to compile is linked as
  `$<BUILD_INTERFACE:...>`, or it becomes a link dependency no installed package can re-find. A
  target linking a dependency the build fetched is left out with a status line, never silently.
  `core-cpp.install` installs and consumes the build (core-cpp#5).

## Open work

- **[core-cpp#5](https://github.com/contour-terminal/core-cpp/issues/5)** — a vcpkg port, now
  that a release exports its targets.
- **[core-cpp#8](https://github.com/contour-terminal/core-cpp/issues/8)** — graduate
  fastcached's Logger, Base64/Sha256, Cli Options and EnumTable when a second consumer needs
  them.
