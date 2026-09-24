# Terminal UI

Rules for `src/core/tui/`: the `core::tui_output` leaf, the full `core::tui`, and its runtime.

The module arrives in Task A7, from endo's `src/tui` at `f774a210`, and its runtime moved onto
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
- **The leaf has an option of its own, `CORE_CPP_WITH_TUI_OUTPUT`, and that is load-bearing.**
  Gated on `CORE_CPP_WITH_TUI` like the libunicode row, the leaf existed only in the configuration
  that fetched libunicode and `UCD.zip`, so dbtool could not have it without what it was split off
  from. `tests/consumer-tui-output` asserts the leaf-only configuration fetches nothing.
- **The TUI never builds under Emscripten.** `CORE_CPP_WITH_TUI` and `CORE_CPP_WITH_TUI_OUTPUT`
  are forced off there.

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
  needs a real console. Input is a seam too, and it is TWO seams rather than one, because
  readiness and decoding are different questions and one double that answered both would be
  impersonating a scheduler: the runtime parks on `loop.waitReadable(inputHandle)` for readiness,
  which a case scripts at `core::net::testing::ScriptedBackend` or takes from a real
  `core::platform::SystemPipe`, and it decodes through an injected
  `core::tui::runtime::InputSource`, which a case scripts at
  `core::tui::runtime::testing::ScriptedInputSource`. Point the second at the first's pipe and the
  same case runs over every backend the platform builds.
- **A generic view talks to its data through a model interface** (tuidu's `TreeTableModel` is
  the example): the view stays domain-agnostic, and each application implements the model.
  Origin: [tuidu `AGENT.md`, "Dependency Injection via Constructor Injection"](https://github.com/contour-terminal/tuidu/blob/30107fbab72310fde5db89e7882eab288f6b541e/AGENT.md).
- **No application concept enters `core::tui`.** No disk-usage tree, no shell prompt, no cache
  dashboard: those live in the application, behind a model. tuidu held its copy of this code to
  the same rule for the same reason, so that one copy could serve several applications. Same
  origin, "The vendored libraries are shared with endo".

## The runtime is composition on the event loop

There is one scheduler: `core::tui::runtime::TuiRuntime(EventLoop&, Terminal&)`, or
`TuiRuntime(EventLoop&, InputSource&)` where the input is injected. endo's `EventSource`,
`PollEventSource`, `TerminalEventSource` and its own `WithTimeout` are deleted; the agent wakeup
becomes `loop.post([&]{ runtime.notifyAgentReady(); })`, and an interrupt goes `SignalHandler` →
`Wakeup` → `waitReadable`, which files nothing on the loop and therefore wakes nothing: it signals
a handle the loop is already watching.

- **Each source is a parked flow, one per handle** -- terminal input, resize, the interrupt
  wakeup, the POSIX signal fd -- and the runtime's destructor takes each one back off the loop.
  That obligation is the rule in [`async-and-net.md`](async-and-net.md), "An object that parks
  flows on a loop takes them back in its own destructor"; here it means **a `TuiRuntime` is
  destroyed before its loop, on the loop's thread**.
- **Which Windows wait serves the console handle is the backend's choice, not the TUI's.** Both
  can: WFMO waits on it directly and sweeps its set in chunks past 64 handles, and IOCP reaches it
  through the waitable-HANDLE bridge (see [`platform.md`](platform.md)). WFMO is still the default
  Windows backend -- Task B7b is what changes that -- so do not write that the console handle
  "goes through IOCP"; it goes through whichever backend `makeDefaultBackend()` returned, and the
  runtime is tested against every one the platform builds.
- **A handle at its end is never waited on again.** A terminal that hung up, a pipe whose
  writer closed, a console that went away: each stays readable for ever and yields nothing, so a
  flow that re-parks on it is resumed every turn -- 100% CPU, and with `SIGHUP` ignored nothing
  else ends the process ([core-cpp#49](https://github.com/contour-terminal/core-cpp/issues/49)).
  The source says so through `InputSource::inputClosed()`, and the runtime then stops watching
  and ends its input. "Read nothing" and "read the end" are different answers, and a read that
  cannot tell them apart is the bug: on a terminal an end of file is also what raw mode returns
  when nothing is pending, so it counts only when `poll(2)` confirms the hangup.
- **Only a waiter that can say "nothing happened" may be resumed with nothing.** `nextEvent()`
  yields an event or throws, so waking it for a focus change reports that change as a
  cancellation -- and `runModal` closes on a cancellation. `InputWake` is where that distinction
  is stated.

The event loop's own rules are in [`async-and-net.md`](async-and-net.md).

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
