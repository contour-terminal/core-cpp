# Brief for Task C3

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


The common steps for every consumer are:
1. Create the worktree with superpowers:using-git-worktrees, using the Global Constraints location. Never pull into or build in the main checkout:
   ```powershell
   git -C D:\<repo> fetch origin
   git -C D:\<repo> worktree add D:\<repo>-worktrees\core-cpp -b <branch> origin/master
   ```
2. Pin with `GIT_TAG v0.1.0` + `VERSION 0.1.0`; iterate locally with `-DCPM_core-cpp_SOURCE=D:/core-cpp`.
3. Build and test with the repo's own presets on Windows and WSL.
4. Run superpowers:requesting-code-review, then open the PR with **contour-workflows:draft-pr**.
5. Drive CI to green with **contour-workflows:fix-ci**.


### Task C3: fastcached PR-A (`LASTRADA-Software/fastcached`)
- [ ] **Delta check (first step).** Run `git log --oneline <provenance SHA>..origin/master -- <replaced paths>` in this consumer. If it lists anything applicable, port it into core-cpp first (patch release v0.1.x via contour-workflows:draft-release/publish-release) and bump this PR's pin. Record the result in the PR body (`delta: none` or the ported commits).
- [ ] File a tracking issue first (the fastcached branch convention is `claude/<issue#>-<slug>`). Create the worktree; all C3 work happens in it:
  ```powershell
  git -C D:\fastcached fetch origin
  git -C D:\fastcached worktree add D:\fastcached-worktrees\core-cpp-tui -b claude/<n>-core-cpp-tui origin/master
  ```

**Delete:**
- `vendor/` (176 files), `cmake/VendorLinkRoots.cmake`
- `scripts/check-vendor-{verbatim,verbatim-selftest,figures,figures-selftest,link-canary,link-canary-selftest,link-roots-selftest,vocabulary,vocabulary-selftest}.cmake`, `scripts/check-sanitized-objects.cmake`
- The vendor tests in `src/tests/CMakeLists.txt` (~6985-7490).

**CMake:**
- `CMakeLists.txt:448` `add_subdirectory(vendor)` → `CPMAddPackage(core-cpp … "CORE_CPP_WITH_TUI ${FASTCACHED_BUILD_TUI}" "CORE_CPP_WITH_IMAGES OFF" "CORE_CPP_WITH_TLS OFF" "CORE_CPP_TESTING OFF")`, placed after libunicode.
- Lines 466-508: the per-target sanitizer loop now iterates `get_property(... GLOBAL PROPERTY CORE_CPP_TARGETS)` and keeps its refusal of an empty set.
- `src/apps/fastcache-cli/CMakeLists.txt:52-86,136-138`: `TARGET fastcache-tui` → `core::tui`.

**Hygiene fallout:**
- `scripts/lib/third-party-roots.txt` → an explicit `none` sentinel; update `CheckCommon.cmake:307-311`, `third-party-roots.sh`, and `check-third-party-roots{,-selftest}.cmake`.
- `check-target-file-guards.cmake`: remove the #1369 second reader (~152-450) and its self-tests.
- `cmake/ErrorPopups.cmake:54`, `scripts/local-gate.sh` (~380-382, 1650-1710), `tidy-sweep.sh:357-370`, `tsan-gate.sh` (716-832, 1490).
- `.clang-format-ignore`.
- Comment-only touch-ups as listed in the migration design.

**Codemod:** the 6 adapter files (`EndoSixelEncoder.cpp` → `TuiSixelEncoder.cpp`).

**Docs:** the AGENT.md and `.agent/` rows that mention `vendor/`.

- [ ] Verify: `clang-debug`, `clang-tsan`, `clang-asan-ubsan`, `ctest -L hygiene`, `clangcl-debug`, `cl-debug`, `scripts/local-gate.sh` (WSL). Open the draft PR titled "tui: the live-stats view links core-cpp's terminal UI, and vendor/ goes".

