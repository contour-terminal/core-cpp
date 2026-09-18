# Terminal UI

Rules for `src/core/tui/`: the `core::tui_output` leaf, the full `core::tui`, and its runtime.

The module arrives in Task A7, from endo's `src/tui` at `f774a210`, and its runtime moves onto
`core::net::EventLoop` in Task B12 (the design spec,
[Part I §1 and §2](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md)).
Its consumers are endo, tuidu, fastcached's console tools and Lightweight's `dbtool`.

## Two targets, and the leaf stays a leaf

- **`core::tui_output` depends on `core::base` only**: `TerminalOutput`, `SgrBuilder`,
  `SyncGuard`, `TerminalProtocols`, `CursorShape`, `Error`, and the private `platform/PosixIO`
  and `Win32Utf`. Lightweight's `dbtool` links only this, to draw progress and colour without
  taking an event loop, libunicode or coroutines. An include of libunicode or `core::async` in
  a leaf file is a layering violation even if it links.
- **`core::tui` is everything else** and may depend on `platform`, `async`, `net`, libunicode,
  and stb when `CORE_CPP_WITH_IMAGES` is on.
- **The TUI never builds under Emscripten.** `CORE_CPP_WITH_TUI` is forced off there.

## Output goes through the destination, never to a file descriptor

- **Every byte the TUI writes goes through `TerminalOutput::writeToDestination`.** endo's
  `SyncGuard` wrote its synchronized-output markers (`CSI ? 2026 h` / `l`) straight to
  `STDOUT_FILENO`, so a test with a mock output captured the frame and not its markers, and a
  consumer that renders into a buffer got stray bytes on its real stdout. Task A7 lands the fix
  test-first.
- **`isTerminal()` answers for the destination, not for the process's stdout.** Whether to emit
  colour, synchronized output or cursor queries is a property of where the bytes go; piped
  output gets plain text. dbtool's acceptance is exactly that: progress on a TTY, plain text
  when piped.

## Dependency injection

- **`Terminal` takes its `TerminalOutput`**, and tests use `MockTerminalOutput`; a TUI test never
  needs a real console. Input is a seam too: after Task B12 the runtime reads input through an
  input pump on the loop (`loop.waitReadable(inputHandle, HandleKind::Waitable)`), and tests
  drive it with `core::net::testing::ScriptedBackend`.
- **A generic view talks to its data through a model interface** (tuidu's `TreeTableModel` is
  the example): the view stays domain-agnostic, and each application implements the model.
  Origin: [tuidu `AGENT.md`, "Dependency Injection via Constructor Injection"](https://github.com/contour-terminal/tuidu/blob/30107fbab72310fde5db89e7882eab288f6b541e/AGENT.md).
- **No application concept enters `core::tui`.** No disk-usage tree, no shell prompt, no cache
  dashboard: those live in the application, behind a model. tuidu held its copy of this code to
  the same rule for the same reason, so that one copy could serve several applications. Same
  origin, "The vendored libraries are shared with endo".

## The runtime is composition on the event loop

After Task B12 there is one scheduler: `core::tui::TuiRuntime(EventLoop&, Terminal&)`. endo's
`EventSource`, `PollEventSource` and its own `WithTimeout` are deleted; the agent wakeup becomes
`loop.post()`, and an interrupt goes `SignalHandler` → `Wakeup` → `waitReadable`. On Windows the
console input handle is parked through the IOCP backend's thread-pool bridge (see
[`platform.md`](platform.md)). The event loop's own rules are in
[`async-and-net.md`](async-and-net.md).

## Windows console tests

- **A test that needs a real console skips when there is none** (exit 77 through
  `core::testing_main`), and a test that touches the process's console input serialises with
  its siblings on a named mutex: the console is one per process, and ctest runs binaries in
  parallel.
- **A test executable never opens a modal dialog** (see
  [`build-and-toolchain.md`](build-and-toolchain.md)). One held an endo test run for 58 minutes.

## Images

`stb_image` is compiled in one translation unit (`StbImageImpl.cpp`), behind
`CORE_CPP_WITH_IMAGES`, and that file alone keeps `-fno-sanitize=undefined`, as a source-file
property: endo recorded "known UBSan false positives in stb_image_resize2", which is third-party
code core-cpp does not fix. Origin:
[endo `src/tui/CMakeLists.txt`](https://github.com/contour-terminal/endo/blob/f774a210ce989e5947b8f61d715068b1dc96088c/src/tui/CMakeLists.txt).
