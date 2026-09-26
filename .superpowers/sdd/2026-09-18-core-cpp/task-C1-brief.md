# Brief for Task C1: endo migrates onto core-cpp v0.2.0

The plan text follows verbatim. The rulings after it override it.

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

- [ ] Register endo's own language highlighter through `core::tui`'s seam (core-cpp#24), so `.endo` files and ```endo fences highlight again on endo's side.
- [ ] Delete, then CMake, codemod, build, test, commit (grouped with contour-workflows:commit), then open the draft PR titled "build: coro, net, crispy, tui and the generic platform layer come from core-cpp".


# Rulings and facts that override the plan text (2026-09-24)

1. **Pin v0.2.0, not v0.1.0:** `CPMAddPackage(NAME core-cpp GITHUB_REPOSITORY contour-terminal/core-cpp GIT_TAG v0.2.0 VERSION 0.2.0 ...)`. Iterate locally with `-DCPM_core-cpp_SOURCE=<a clean worktree of core-cpp at tag v0.2.0>`. Create that worktree with `git -C D:/core-cpp worktree add --detach D:/core-cpp-wt-endo v0.2.0`.
2. **Worktrees only.** Never pull, check out, build or commit in `D:\endo`. The only commands allowed against it are `git -C D:\endo fetch origin` and `git -C D:\endo worktree add D:\endo-worktrees\core-cpp -b build/core-cpp origin/master`.
3. **Module names:**
   - The coroutine module is `core::async` (`<core/async/...>`), not `core::coro`.
   - The event source is `core::net::IoBackend`; `makeDefaultBackend` replaces `makeDefaultEventSource`, and `Interest` replaces `FdInterest`.
   - `tools/migrate/renames.json` and `rewrite.py --profile endo` are the source of truth.
   - Read core-cpp's CHANGELOG 0.1.0 and 0.2.0 Breaking sections, with the per-consumer summary for endo, and `.agent/guides/consumer-migration.md`.
4. **No regression, in stability, portability or performance.** This is the user's standing rule for every consumer. endo's full test suite, its `.endo` end-to-end suite and every CI leg (including Emscripten, where endo builds it) must stay green. If you find a core-cpp defect or gap, do NOT work around it in endo. Stop and report it with evidence; it gets fixed upstream and released.
5. **Compiler cache:** the local fastcache-cc serves stale objects when a header under CPM_core-cpp_SOURCE changes (fastcached#1597). Build with `-DUSE_COMPILER_CACHE=OFF` while CPM_core-cpp_SOURCE points at a local tree.
6. **Outward actions you may take:**
   - push `build/core-cpp` to endo's origin;
   - open the PR as a **draft** with the `contour-workflows:draft-pr` skill if available, else `gh pr create --draft`, including a "Consumer impact" section;
   - nothing else. No merge, and no ready-for-review.
7. **Commits:**
   - Each is small, semantic and green on its own.
   - Each ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`.
   - Follow endo's own AGENT.md and rules; they govern inside endo.
8. **Environment:**
   - WSL is shared. Run gates through `wsl.exe bash -l <script file>`, and keep logs in your scratchpad.
   - Never touch foreign build dirs or processes.
   - Run git on the Windows side for Windows-created worktrees.
   - Windows writes CRLF: write with Python write_bytes and check for `\r`.
   - With MSVC at /O2, keep calls out of `co_await` full-expressions (C4737).
   - **Never wait on a background notification without a bound.** Poll a log's tail or use a timeout. A lane of this project sat idle for 2.5 hours on a notification that never came.
