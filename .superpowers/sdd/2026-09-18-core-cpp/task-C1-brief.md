# Brief for Task C1

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


### Task C1: endo PR (`contour-terminal/endo`, branch `build/core-cpp`)
- [ ] **Delta check (first step).** Run `git log --oneline <provenance SHA>..origin/master -- <replaced paths>` in this consumer. If it lists anything applicable, port it into core-cpp first (patch release v0.1.x via contour-workflows:draft-release/publish-release) and bump this PR's pin. Record the result in the PR body (`delta: none` or the ported commits).
**Delete:**
- `scripts/{get_contour_dirs.py,contour-pin.json}`
- `.gitignore` lines for `/src/{vtparser,crispy,coro,net}`
- `src/tui/**`
- the moved `src/platform` files (per Part I §1; the endo-specific ones stay)
- `src/testing/{EnvHelper,ScopedTempDir,ScopedWorkingDirectory,SuppressWindowsDialogs}.hpp` (+ tests)

**CMake:**
- `src/CMakeLists.txt`: drop lines 1-48 (fetch), 50-75 (tracy shim), and 78-79, 84-85, 91-97 (vendored subdirectories). Add a configure refusal if a stale `src/{crispy,coro,net,vtparser}` exists.
- `CMakeLists.txt`: drop line 202 and lines 204-248 (the Emscripten BufferObject patch).
- `cmake/EndoThirdParties.cmake`: `CPMAddPackage(NAME core-cpp GITHUB_REPOSITORY contour-terminal/core-cpp GIT_TAG v0.1.0 VERSION 0.1.0 EXCLUDE_FROM_ALL YES SYSTEM YES OPTIONS "CORE_CPP_TESTING OFF" "CORE_CPP_WITH_TLS OFF" "CORE_CPP_WITH_TUI ${_native}")`, and remove the OpenSSL block (~198-215).
- Link: `src/shell/CMakeLists.txt:238,240` `vtparser tui` → `core::tui`; `:246` `net` → `core::net`; `crispy::core` → `core::base core::log core::cli` in `src/{endo-language,CoreVM,endo-test}/CMakeLists.txt`.
- `endo-platform` PUBLIC-links `core::platform`.
- Sanitizers: apply endo's `enable_sanitizers()` to each target in `get_property(... GLOBAL PROPERTY CORE_CPP_TARGETS)`. Verify with the `clang-debug` (ASan+UBSan) and `clang-tsan` presets.

**Codemod:**
- `python D:/core-cpp/tools/migrate/rewrite.py --profile endo src`
- `namespace tui {` forward declarations → `namespace core::tui {` (5 files)
- Qualify the ~300 bare-alias uses that the compiler reports.
- `endo::testing::InjectedShell` stays.

**CI:**
- Remove the `get_contour_dirs.py` steps (`build.yml:79-80,167-168,222-223,274-275`, `clang-tidy.yml:31`, `docs.yml:34`, `release.yml:68-69,150-151,193-194`, `packaging/deb/Dockerfile:66-72`).
- Remove the `clang-tidy.yml:94` excludes; `.clang-tidy:90` `HeaderFilterRegex` loses `tui`.

**Docs:** `AGENT.md:21-32,48-53,125-150,322-323`.

**Verify:** `clang-debug`, `clangcl-debug`, `clang-tsan`, `clang-release-static`, `emscripten-release` (base/log/cli only).

- [ ] Delete, then CMake, codemod, build, test, commit (grouped with contour-workflows:commit), then open the draft PR titled "build: coro, net, crispy, tui and the generic platform layer come from core-cpp".

