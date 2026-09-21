# Task: re-sync CompileCache.cmake

## What changed upstream

fastcached's `cmake/portable/CompileCache.cmake` moved from `eb9c9c68da8fadfd43b0b36366919cb462689f48`
(our recorded SHA, 2026-09-18) to `5a9dca0498f4c37c63a17270550ee51ca87ae0a3` (`origin/master`,
2026-09-20). The diff adds two empty-by-default cache variables,
`FASTCACHE_AUTO_INSTALL_HOST_SYSTEM` and `FASTCACHE_AUTO_INSTALL_HOST_PROCESSOR`, and a new
helper `_fc_auto_install_host()` that both `_fc_auto_install_select_row()` and the "no prebuilt
binary" reason message now call. When unset (the default) the helper falls back to
`CMAKE_HOST_SYSTEM_NAME`/`CMAKE_HOST_SYSTEM_PROCESSOR`, so behaviour is unchanged for every
existing caller. Set, they let a caller state the host `fastcache-cc` is fetched for, which
`scripts/check-compile-cache-autoinstall.cmake` needs so it can pin a *published* platform for
each test row instead of stopping at whichever host actually runs the check (its aarch64 leg for
issue #1432, per the added comment). Nothing else in the file changed.

`cmake/FetchTransferBound.cmake` compared byte-identical between the two commits, so it needed
no re-sync.

## What was synced

- Read both files as git blobs (`git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show
  origin/master:<path>`), confirmed neither contains a CR byte, and wrote
  `cmake/portable/CompileCache.cmake` byte-for-byte. `cmake/FetchTransferBound.cmake` was left
  untouched (identical content).
- `D:\fastcached`'s working tree was never touched — only `fetch` and `show` were run against it.
- Updated the recorded commit in `cmake/portable/README.md` and
  `.agent/reference/provenance.md` (both rows: `CompileCache.cmake` and
  `FetchTransferBound.cmake`, since README documents both at one shared commit) to
  `5a9dca0498f4c37c63a17270550ee51ca87ae0a3`.
- Left `NOTICE` unchanged: its "Imported at eb9c9c68..." block is the original-import provenance
  record shared with `cmake/CPM.cmake`'s bootstrap logic (which did not change), not a live
  sync pointer — bumping it would misstate that CPM.cmake was re-imported. README's table and
  `provenance.md`'s "synced SHA" column are the two places this repo tracks re-sync state.
- Added a `CHANGELOG.md` `[Unreleased]` → `### Added` entry describing the re-sync and the new
  cache variables.
- Nothing in the file looked wrong; no "improvements" were made.

## Verify: build/test evidence

**Windows** (`clangcl-debug`, VS 18 dev shell, amd64):
- `cmake --preset clangcl-debug` configure log: `[cache] Enabling fastcache-cc at
  127.0.0.1:6674 (C:/Users/chris/AppData/Local/fastcache-cc/bin/fastcache-cc.exe) for C/C++
  compilation`.
- `out/build/clangcl-debug/build.ninja` names the launcher on every compile rule, e.g.:
  `LAUNCHER = "...cmake.exe" -E env FASTCACHE_ADDR=127.0.0.1:6674 ...
  C:/Users/chris/AppData/Local/fastcache-cc/bin/fastcache-cc.exe`.
- `cmake --build --preset clangcl-debug --clean-first`: full rebuild, 318/318 targets, exit 0.
- `ctest --preset clangcl-debug`: **21/21 passed** (100%), including `core-cpp.cmake-hygiene`,
  `core-cpp.cmake-hygiene-selftest`, `core-cpp.vendor-selftest`, `core-cpp.layering`, and the
  three `windows-dialog-canary` cases.
- `ctest --preset clangcl-debug -L hygiene`: **7/7 passed**.
- `python scripts/clang-format.py --check`: 356 files formatted, no diffs.
- `-DUSE_COMPILER_CACHE=OFF` verify configure (scratch tree, clang-cl, no preset): log shows
  `[cache] Compiler caching disabled by USE_COMPILER_CACHE=OFF`; `CMakeCache.txt` has
  `CORE_CPP_CXX_COMPILER_LAUNCHER:INTERNAL=` (empty) and no `CMAKE_CXX_COMPILER_LAUNCHER` entry
  at all. Scratch tree removed afterwards.

**WSL** (`Ubuntu-26.04`, `clang-debug`):
- `cmake --preset clang-debug` configure log: `[cache] Enabling fastcache-cc at
  127.0.0.1:6674 (/home/christianparpart/.local/bin/fastcache-cc) for C/C++ compilation`, plus
  `[cache] Mapping debug paths for CXX so replayed objects name no checkout:
  -fdebug-prefix-map=...` — WSL's clang driver supports the path-map switch that clang-cl's
  driver does not (the Windows configure log said so explicitly: "Debug paths NOT mapped for
  CXX (no path-map switch on this driver)").
  This is upstream behaviour, unrelated to the resync's own diff, and is unchanged by it.
- `cmake --build --preset clang-debug`: already up to date from a prior build in this shared
  WSL tree (`ninja: no work to do`); left as-is rather than force a clean rebuild in a tree other
  sessions may be using.
- `ctest --preset clang-debug`: **19/19 passed** (100%), including `core-cpp.cmake-hygiene` and
  `core-cpp.vendor-selftest`.

## CI

- Pushed as commit `b505db8` on `master`.
- Push-triggered `Build`, run **35517620663**: **success**, every job green (linux clang-22/
  clang-22-cxx26/clang-22-arm64/clang-22-tracy/gcc-14/gcc-15, macOS appleclang/llvm-22, Windows
  clangcl-release/clangcl-debug (via style)/cl-release/cl-release-tls/cl-debug, sanitizers
  clang-asan-ubsan/clang-tsan, clang-tidy, coverage, compile-cache, both emscripten legs, all
  three consumer-smoke variants, `ci-ok`). No job reported a non-success conclusion; the only
  annotations were pre-existing GitHub Actions Node.js-20-deprecation notices and one transient
  cache-service 400 on the wasm leg that self-recovered.
- Push-triggered `Docs`, run **35517620683**: **success**.
- `gh workflow run downstream.yml -R contour-terminal/core-cpp` → run **35517631664**
  (`workflow_dispatch`): **success**, job "verbatim files match fastcached master" passed in 7s —
  this is the job that was failing nightly since 2026-09-19 (run 35430889453) and again this
  morning (run 35499910731); it is now fixed.
