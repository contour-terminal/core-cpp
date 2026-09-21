# Brief for Task A7

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A7: `core::tui_output` + `core::tui` import (endo HEAD)

**Files:** `src/core/tui/**` from `D:\endo\src\tui` at `f774a210` (156 files), mapping `tui::` → `core::tui::`.
- Split the `core::tui_output` leaf (TerminalOutput, SgrBuilder, SyncGuard, TerminalProtocols, CursorShape, Error, `platform/PosixIO`, `Win32Utf`); verify it includes neither libunicode nor async.
- `stb` goes behind `CORE_CPP_WITH_IMAGES` (`StbImageImpl.cpp` keeps `-fno-sanitize=undefined`).
- For now, runtime/ keeps its own EventSource (it is replaced in B12).

- [ ] **Step 1: Test first for the output fixes.** In `TerminalOutput_test.cpp`, add:
  - `SyncGuard writes through writeToDestination` (a mock output captures `\x1b[?2026h`/`l`, and nothing reaches fd 1).
  - `isTerminal() reflects the destination`.

  Run it: expected FAIL, because today's `SyncGuard` writes `STDOUT_FILENO` (`D:\endo\src\tui\platform\TerminalOutput.cpp:355-359`).
- [ ] **Step 2:** Implement `SyncGuard` via `writeToDestination` and add `[[nodiscard]] virtual bool isTerminal() const noexcept`. Expected: PASS.
- [ ] **Step 3:** Import the rest of the TUI and its tests. The `TerminalInputWin32_test` named-mutex serialisation must be kept. Run all four configurations.
- [ ] **Step 4:** Commit `tui: import endo's terminal UI as core::tui with a dependency-free core::tui_output leaf`.

