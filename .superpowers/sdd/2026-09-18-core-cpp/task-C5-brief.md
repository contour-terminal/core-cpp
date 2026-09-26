# Brief for Task C5: Lightweight dbtool adopts core-cpp v0.2.0 tui_output

The plan text follows verbatim. Where the rulings after it disagree with the plan, the rulings win.

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


# Rulings (2026-09-24)

1. Rulings 1-8 of D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-C1-brief.md apply here, with these substitutions:
   - repo LASTRADA-Software/Lightweight;
   - main checkout D:\Lightweight, never modified;
   - worktree D:\Lightweight-worktrees\core-cpp on branch `feat/dbtool-core-tui` from origin/master;
   - core-cpp source worktree `git -C D:/core-cpp worktree add --detach D:/core-cpp-wt-lw v0.2.0`;
   - pin v0.2.0.
2. Lightweight is LASTRADA-Software, so read its AGENT.md / .agent rules before starting. If its branch convention differs from `feat/dbtool-core-tui`, follow the repo's own convention and tell me.
3. core-cpp is fetched ONLY under LIGHTWEIGHT_BUILD_TOOLS. A configure with tools OFF must show no core-cpp fetch; check that in the configure log.
4. Scratch files go under scratchpad\C5\ only.
