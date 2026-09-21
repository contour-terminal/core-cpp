# Brief for Task C5

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


### Task C5: Lightweight PR (`LASTRADA-Software/Lightweight`, branch `feat/dbtool-core-tui`)
**Files:** `src/tools/dbtool/StandardProgressManager.{hpp,cpp}`, `src/tools/dbtool/main.cpp`, `src/tools/dbtool/CMakeLists.txt`, new `cmake/CoreCpp.cmake`, included inside `if(LIGHTWEIGHT_BUILD_TOOLS)` at `CMakeLists.txt:395`. That file saves and clears the directory `COMPILE_OPTIONS` around the CPM call and clears `CXX_CLANG_TIDY` on the core targets. `dbtool_lib` links `PRIVATE core::tui_output`, and CPM options have `CORE_CPP_WITH_TUI OFF`.

- [ ] **Tests first:** `src/tests/dbtool/StandardProgressManagerTests.cpp` must keep passing unchanged. Add a case in which, when the output is not a terminal, no `\x1b[?2026` appears in the captured stream.
- [ ] **Implement:**
  - `OStreamTerminalOutput final : core::tui::TerminalOutput` overrides `writeToDestination` to write to the injected `std::ostream` and flush.
  - The escape mapping:

    | Current code | core-cpp call |
    |---|---|
    | `\033[nA` | `moveUp(n)` |
    | `\r\033[K` | `carriageReturn(); clearToEndOfLine();` |
    | `\033[nB` + `\r` | `moveDown(n); carriageReturn();` |
    | `\n` | `linefeed()` |
    | `SynchronizedOutputGuard` (lines 36-86) | `syncGuard()` |

  - `main.cpp`: `IsStdoutTerminal` → `core::platform::isTerminal`, `HelpColors::Colored` via `buildSgrSequence`, keep `ConfigureWindowsConsole`.
- [ ] **Verify:** `clangcl-debug` `ctest -R StandardProgressManager`. `gcc-release -DLIGHTWEIGHT_BUILD_TOOLS=OFF -DLIGHTWEIGHT_BUILD_TESTS=OFF` configure log shows no core-cpp fetch. `python src/tests/test_dbtool.py --dbtool <out>/dbtool --test-env sqlite3`.
- [ ] Open the draft PR titled "feat(dbtool): draw progress through core-cpp's terminal output instead of hand-written escape sequences".

