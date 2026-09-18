# tui

The terminal UI. Namespace `core::tui`, directory `src/core/tui/`, targets `core::tui_output` and
`core::tui` (only with `CORE_CPP_WITH_TUI`, which is on by default and off under Emscripten).

!!! note "Status"
    Not imported yet. Task A7 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    imports endo's `src/tui` at `f774a210`; Task B12 moves its runtime onto `core::net::EventLoop`.

## Planned contents

- **`core::tui_output`**, a leaf that depends on [base](base.md) only: `TerminalOutput`,
  `SgrBuilder`, `SyncGuard`, `TerminalProtocols`, `CursorShape`. A program can draw styled output
  and progress with it and nothing else.
- **`core::tui`**: terminal input, the screen, components and widgets, themes, key bindings, and
  a runtime composed on the event loop. It also depends on [platform](platform.md),
  [async](async.md), [net](net.md) and libunicode, and on stb when `CORE_CPP_WITH_IMAGES` is on.
