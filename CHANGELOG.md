# Changelog

All notable changes to core-cpp are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major version is 0, a minor
release may break the API; every break is listed under **Breaking** with a migration note. A
release tag `vX.Y.Z` equals the version in `project(core-cpp VERSION X.Y.Z)`, and the release
workflow refuses one without a section here.

## [Unreleased]

### Added

- The CMake framework: a module table that enforces the layering between modules, a dependency
  table resolved from the parent project, then `find_package`, then CPM, per-target toolchain
  tables (pedantic warnings, `CORE_CPP_WERROR`, sanitizers, coverage, clang-tidy), and no global
  state unless core-cpp is the top-level project.
- `core::testing` (Windows dialog suppression, usable without a test framework),
  `core::testing_dialogs` and `core::testing_main`, a Catch2 `main()` whose exit status is 0 when
  everything passed, 1 when anything failed or Catch2 reported an error, 77 when every test case
  skipped, and 2 when nothing ran.
- Configure, build, test and workflow presets for clang, GCC, AppleClang, MSVC and clang-cl, the
  sanitizers, clang-tidy, coverage and Tracy, and an `emscripten` preset for single-threaded
  WebAssembly whose tests run under node.
- Checks over the tree: the CMake and C++ hygiene rules with their self-test, the exit-code
  contract, and `tests/cmake/check-release.cmake`, which the release workflow runs on a tag.
- The documentation site, the API reference, the rulebook in `.agent/`, and the CI workflows.
- The module table's `PLATFORMS` column takes `any`, `native` or `wasm-subset`, and a module may
  list `SOURCES_EMSCRIPTEN`; `SOURCES_POSIX` is not compiled under Emscripten, which sets `UNIX`.

### Imported

Each file was read as a git blob at the commit named, and none contains a CR byte.

| From | Commit | What |
|---|---|---|
| [fastcached](https://github.com/LASTRADA-Software/fastcached) | `eb9c9c68da8fadfd43b0b36366919cb462689f48` | `cmake/portable/CompileCache.cmake` and `cmake/FetchTransferBound.cmake`, verbatim; the bounded bootstrap download in `cmake/CPM.cmake`; the Windows error-popup suppression, merged into `SuppressWindowsDialogs`; the hook-name `IgnoredRegexp` of `.clang-tidy` |
| [contour](https://github.com/contour-terminal/contour) | `6777ff05014f8ff163b071e8b0e942830119db80` | `.clang-format` and `.clang-tidy`, adapted; two copies of `SuppressWindowsDialogs`, merged; `LICENSE` |
| [endo](https://github.com/contour-terminal/endo) | `f774a210ce989e5947b8f61d715068b1dc96088c` | `SuppressWindowsDialogsAtStartup.cpp`, `WindowsDialogCanary.cpp`, the CPM 0.40.8 pin; a copy of `SuppressWindowsDialogs`, merged; `.github/clang-tidy-matcher.json` |

The rulebook and CI configuration adapt text from fastcached at
`b5ded89c5ae6ba5b45337335ce774c5ae6986d65`, contour and endo at the commits above, Lightweight at
`f57dc2e0704d885a3c642a63675873919fc2d128` and tuidu at
`30107fbab72310fde5db89e7882eab288f6b541e`; `NOTICE` lists the files.
