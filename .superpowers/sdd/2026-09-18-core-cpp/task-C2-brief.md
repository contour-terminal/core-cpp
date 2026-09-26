# Brief for Task C2: tuidu migrates onto core-cpp v0.2.0

The plan text follows verbatim. The rulings in task-C1-brief.md apply here too, with endo replaced by tuidu. The parts that differ are listed after the plan text.

### Task C2: tuidu PR (`contour-terminal/tuidu`, branch `build/core-cpp`)
**Delete:** `src/{coro,platform,testing,tui}/`, `scripts/get_contour_dirs.py`, `.gitignore` `/contour/` + `/src/crispy/`.

**CMake:**
- `src/CMakeLists.txt:1-14` and the `add_subdirectory` lines.
- `CMakeLists.txt:113-116` aliasing → `TUIDU_TESTING`.
- CPM core-cpp with TLS OFF.
- `src/tuidu/CMakeLists.txt:34,37` → `core::tui core::platform core::async core::cli`.

**Codemod:** `rewrite.py --profile tuidu src/tuidu` (43 files).

**Hand edits:**
- `App_test.cpp:73,199,264`: the EventSource subclasses → `core::net::testing::ScriptedBackend` / the new TuiRuntime API.
- `Cli.cpp`: `core::cli` PascalCase types (`Command`, `Option`, `OptionList`, `FlagStore`, `Value`, `Verbatim`, `HelpDisplayStyle`).

**CI:** remove the crispy fetch steps (`build.yml:61-62,95-96`, `clang-tidy.yml:44-45`, `release.yml:189-195`, `clang-format.yml:36`). `.clang-tidy:71` → `'src/tuidu/[^.]'`.

**Docs:** the `AGENT.md` component map.

- [ ] Implement, verify (`clang-debug`, `clangcl-release`), commit, then open the draft PR titled "build: the TUI, platform and coroutine layers come from core-cpp".


# Rulings (2026-09-24)

1. Read D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-C1-brief.md rulings 1-8. Every one applies, with these substitutions:
   - repo: contour-terminal/tuidu
   - main checkout: D:\tuidu, which is never modified; you may only fetch and create worktrees against it
   - worktree: D:\tuidu-worktrees\core-cpp on branch build/core-cpp from origin/master
   - core-cpp source worktree: `git -C D:/core-cpp worktree add --detach D:/core-cpp-wt-tuidu v0.2.0`
2. Names: tuidu's `endo::coro`/`coro::` becomes `core::async::`. The EventSource mocks become `core::net::testing::ScriptedBackend` against `TuiRuntime(EventLoop&, Terminal&)`. The `crispy::cli` lowercase types become core::cli's PascalCase types.
3. The completion types live in `core::tui::completer::`. renames.json lacks rows for them (core-cpp#48), so qualify them by hand; don't patch the codemod locally.
4. The endo migration is the closest model: contour-terminal/endo PR #187.
