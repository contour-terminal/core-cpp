# Brief for Task C2

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


### Task C2: tuidu PR (`contour-terminal/tuidu`, branch `build/core-cpp`)
**Delete:** `src/{coro,platform,testing,tui}/`, `scripts/get_contour_dirs.py`, `.gitignore` `/contour/` + `/src/crispy/`.

**CMake:**
- `src/CMakeLists.txt:1-14` and the `add_subdirectory` lines.
- `CMakeLists.txt:113-116` aliasing → `TUIDU_TESTING`.
- CPM core-cpp with TLS OFF.
- `src/tuidu/CMakeLists.txt:34,37` → `core::tui core::platform core::coro core::cli`.

**Codemod:** `rewrite.py --profile tuidu src/tuidu` (43 files).

**Hand edits:**
- `App_test.cpp:73,199,264`: the EventSource subclasses → `core::net::testing::ScriptedBackend` / the new TuiRuntime API.
- `Cli.cpp`: `core::cli` PascalCase types (`Command`, `Option`, `OptionList`, `FlagStore`, `Value`, `Verbatim`, `HelpDisplayStyle`).

**CI:** remove the crispy fetch steps (`build.yml:61-62,95-96`, `clang-tidy.yml:44-45`, `release.yml:189-195`, `clang-format.yml:36`). `.clang-tidy:71` → `'src/tuidu/[^.]'`.

**Docs:** the `AGENT.md` component map.

- [ ] Implement, verify (`clang-debug`, `clangcl-release`), commit, then open the draft PR titled "build: the TUI, platform and coroutine layers come from core-cpp".

