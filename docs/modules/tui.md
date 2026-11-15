# tui

The terminal UI. Namespace `core::tui`, directory `src/core/tui/`, targets `core::tui_output`
(with `CORE_CPP_WITH_TUI_OUTPUT`), `core::tui_core` (with `CORE_CPP_WITH_TUI_CORE`) and `core::tui`
(with `CORE_CPP_WITH_TUI`). All three are on by default and off under Emscripten; each forces the
one below it on (`CORE_CPP_WITH_TUI` forces `CORE_CPP_WITH_TUI_CORE`, which forces
`CORE_CPP_WITH_TUI_OUTPUT`), and the two lower options default to whatever `CORE_CPP_WITH_TUI` is on
a first configure (a cache entry keeps its value after that, so turning the TUI off in an existing
tree leaves them on).

Imported from endo's `src/tui` at `f774a210`, which includes the coroutine-runtime work fastcached
upstreamed there. The runtime has since been rewritten onto [`core::net::EventLoop`](net.md).

!!! note "Status"
    Available. Its runtime is composed on [`core::net::EventLoop`](net.md), so `core::tui` links
    `core::net`; `core::tui_output` still links [base](base.md) alone.

## `core::tui_output`

A static library that links [base](base.md) and nothing else: no libunicode, no coroutines, not
even [platform](platform.md). Its row in the module table says `DEPS base`, so the configure
refuses any other link from it. A program that only prints styled text and progress — Lightweight's
`dbtool` — links this and takes nothing else with it: configured with `CORE_CPP_WITH_TUI` off and
`CORE_CPP_WITH_TUI_OUTPUT` on, core-cpp builds the leaf alone and neither finds nor fetches
libunicode. Before 0.2.1 the leaf had no option of its own and came only with `CORE_CPP_WITH_TUI`,
which fetches libunicode and, through libunicode's configure, `UCD.zip`.
`tests/consumer-tui-output` is that consumer, and CI's `consumer-smoke (tui-output)` builds it.

- `TerminalOutput` buffers escape sequences and flushes them: styled text (`Style`, `RgbColor`,
  `UnderlineStyle`), cursor movement, erasing, the alternate screen, the scroll region,
  double-width and double-height lines, cursor shapes (`CursorShape`), sixel payloads, OSC 52
  clipboard writes and OSC 8 hyperlinks, plus the queries a capability probe sends (cursor
  position, cell size, DECRQM, DA1).
- `writeToDestination()` is the one seam every byte goes through. Override it to retarget the
  stream — a capture buffer in a test, a pipe, a second terminal — without reimplementing any of
  the composition. `isTerminal()` answers for that same destination: the default asks the
  operating system about the process's standard output, and a subclass answers for its own. On a
  platform that is neither POSIX nor Windows the default destination is the C standard output
  stream (`portable/`, the `SOURCES_PORTABLE` list), which is not a terminal and has no size to ask.
- `kind()` says what stands behind an output: `OutputKind::Device` (a terminal, or a pipe, a file or
  a capture standing in for one) or `OutputKind::Simulation`, which `MockTerminalOutput` reports and
  which answers its cursor itself through `simulatedCursorPosition()`. `Terminal` reads it to decide
  whether to set up input and ask questions; the TUI uses no RTTI.
- `SyncGuard` brackets a frame in DEC mode 2026 so a terminal does not paint a half-drawn one. It
  writes through the `TerminalOutput` it was made from, so a retargeted output is bracketed on its
  own stream, and only when that output's `isTerminal()` is true: on any other destination it
  flushes at both ends and writes no sequence, so a caller takes one unconditionally.
- `ClipboardProtocol.hpp` composes both terminal clipboard protocols and reads their answers, with
  no I/O: `encodeOsc52()`, `encodeOsc5522Write()` (kitty's protocol: any MIME type, 4096-byte
  chunks), `parseOsc5522WriteStatus()`, and `describe()` for a `ClipboardWriteError`.
  `TerminalOutput::copyToClipboard()` uses the same OSC 52 encoder.
- `buildSgrSequence()` turns a `Style` into one SGR sequence.
- `core::tui::protocols` holds the sequence constants the input and output sides share (the Kitty
  keyboard protocol, bracketed paste, mouse and focus tracking, colour-scheme notification,
  win32-input-mode, OSC 8), `appendHyperlinkOpen()`, `appendMouseTrackingChange()`, which writes the change
  between two `MouseTracking` modes, and `parseSixelFromDeviceAttributes()`, which reads a DA1 answer.
- `Result<T>` and `VoidResult`, the module's `std::expected` aliases.

## `core::tui_core`

The headless half: everything below that needs no terminal under it -- `VtParser` and
`InputEvent`, `Cell`, `Buffer`, `Canvas`, `Component`, the widgets, the completer, the Markdown
renderer, `Theme`, `Unicode` and `Screen`. It links `core::base`, `core::tui_output` and libunicode,
and nothing of [platform](platform.md), [async](async.md) or [net](net.md); configured with
`CORE_CPP_WITH_TUI` off and `CORE_CPP_WITH_TUI_CORE` on, core-cpp builds it without the terminal
half.

- **The surface.** `Screen` draws on a `ScreenSurface` -- a size, a `TerminalOutput` and the one
  cursor query the inline viewport asks -- which `Terminal` implements on a real terminal.
  `HeadlessSurface(Size, ByteSink)` is the surface for a program with no terminal at all: its output
  is a `ByteSinkOutput`, which hands every flushed byte to the sink and asks the operating system
  nothing (no size, no probe, no terminal test), so it behaves the same natively and in a guest.
  Its cursor query answers `QueryUnanswered::NotAsked`, so it is meant for the fullscreen and fixed
  viewports; an inline viewport on it falls back as on a silent terminal. `SinkDestination` says
  whether a terminal stands past the sink (frames bracketed in DEC mode 2026) or not, and
  `resize()` takes the host's new size. `MockScreenSurface` is the test double, over a
  `MockTerminalOutput`.
- **The diff.** `diffFrames(previous, next, DiffOptions)` returns the bytes that turn one `Buffer`
  into the other; `writeFrameDiff(out, previous, next, options)` writes the same bytes into a
  `TerminalOutput`, and is what every `Screen` flush path and `Buffer::writeTo()` call. Both are
  pure in their frames. `DiffOptions` holds what the flush paths differed in: `DiffAddressing`
  (`Absolute`, cursor-addressed, for fullscreen and fixed; `Relative`, row by row from column 0,
  for inline), `DiffRepaint` (`Changed` cells or `All`), the `area` covered, and, each off by
  default, `DiffHyperlinks`, `DiffImages`, `DiffRowMarks` and `DiffReflow`. The cursor is not the
  diff's: hiding, placing and showing it stay `Screen`'s, so identical frames diff to nothing.
- **No exceptions, no RTTI.** A cell is reached through `Buffer::cellAt(Point)`, which is null
  outside the buffer, so an out-of-range read or write is a null check; nothing in the headless
  half throws or asks for a type at run time. The one throw `core::base` keeps on this path,
  `pathFromTrustedUtf8()`'s, goes through `core::throwOrFail()`, which fails as `Require()` does
  when exceptions are compiled out.
- **Time is injected.** Nothing in the headless half reads `std::chrono::steady_clock` itself: the
  hover delay (`HoverState`, and so `Screen`), a `Spinner`'s frames, `InputField`'s and
  `TreeTableView`'s multi-click windows and `remainingMs()` (`<core/tui/TimerUtils.hpp>`) all measure
  on a `core::IClock` (`<core/Clock.hpp>`, [base](base.md)) passed to their constructor, by default
  `core::defaultSteadyClock()`. A test passes a `core::ManualClock` and advances it, so no case
  sleeps. `SemanticBlockClient`, in the terminal half, takes one the same way.

- **Layout.** `core::tui::layout::solve(root, available)` (`<core/tui/layout/Solve.hpp>`) is a flexbox
  layout as a pure function: a tree of `Node`s -- each a `Style`, its children and a preferred `Size` -- and a
  `Rect` in, one `Rect` per node out, in depth-first pre-order with the root first. A `Style` holds the container's
  `Direction` (`Row`, `Column`), `Wrap`, `Justify` (`Start`, `Center`, `End`, `SpaceBetween`), `Align` (`Stretch`,
  the default, `Start`, `Center`, `End`) and `gap`, and the item's `grow`, `shrink` (1 by default), `basis` (the
  preferred main size when absent) and `minSize`/`maxSize` (0 is unbounded). Everything is integer cells, with no
  floating point, so equal input lays out identically everywhere:
    - a line's `gap`s are subtracted first;
    - leftover cells go to the growing children in proportion to `grow`, each taking the whole part of its share
      and the cells that remain going one at a time to the earliest growing children (10 over three equal growers
      is 4/3/3, over `grow` 1:2 it is 4/6);
    - a deficit is taken from the shrinking children in proportion to `shrink` times the basis, the cells that
      remain one at a time from the latest;
    - a child pushed past its `minSize` or `maxSize` is frozen there and the rest shared again among the others,
      pass after pass, until no limit is crossed or no child can take more;
    - a line's leftover is placed by `justify` (`Center` puts the odd cell after the children, `SpaceBetween` gives
      the odd cells to the earliest gaps), each child across the line by `align`;
    - with `Wrap::Wrap` a child whose basis would not fit after the line's starts the next line; lines are as tall
      as their tallest child's preferred cross size, and the container's leftover cross space is shared among them
      one cell at a time from the first;
    - what still does not fit is clipped to the parent: the last children lose cells first, and no size is ever
      negative -- a container with no width gives every node in it a width of 0.
    - sizes, gaps and positions above `MaxCells` (2^20) are capped to it, `grow`/`shrink` above `MaxWeight`
      (2^16) likewise, and a node's children past `MaxChildren` (2^20) are not laid out -- each of them, and every
      node beneath it, gets a zero-size rectangle at the parent's origin. Within these three bounds no sum
      overflows (a weight total is at most 2^56), and a share's product of deficit and weight is formed exactly
      even past 64 bits, so no input overflows.

- **The layout pass.** A `Component` lays out its children with `solve()` over its own size: each child added with
  a `LayoutParams::flex` item -- the item half of a `layout::Style`: `grow`, `shrink`, `basis`, `minSize`,
  `maxSize` -- has its `area` set by its parent, from that item, the larger of the item's `minSize` and the child's
  `minSize()` hint, and the child's `preferredSize()` as the basis where the item names none. How a component
  places those children (direction, wrap, justify, align, gap) is its own `layoutStyle()`, given to its
  constructor and fixed; a plain `Component()` lays them out in a row. Children added without a flex item keep the
  `area` they are given and take no room, so a tree mixes both. The pass runs whenever a component's size changes
  (`setArea()` first calls the new `onResize(Size)` hook) and whenever a flex child is added, removed, shown or
  hidden. A component whose `preferredSize()` or `minSize()` may have changed calls `hintsChanged()`: its
  parent is marked, and `Screen` lays every marked component's children out again once, before the next frame
  is drawn (without a screen, at the parent's next layout pass); `Responsive` does so on every swap. A
  component that leaves the screen -- a `Responsive`'s old layout among them -- takes its focus, its hover
  and the pointer capture with it, so nothing is asked of it after it is gone. It solves one level at a time: a child's
  place depends on its style and hints, not on its own children, and each child that changed size lays out its own.
  `Screen` sizes its root to the viewport at every frame and on every `ResizeEvent`, so a child of the root with
  `.flex = layout::Style { .grow = 1 }` fills the screen.
- **`FlexBox`** (`<core/tui/Component.hpp>`) is the container that draws nothing: `FlexBox(layout::Style)` and
  `add(std::unique_ptr<Component>, layout::Style item)`, which owns the child, so a layout can be built and
  returned as one object.
- **`Responsive`** (`<core/tui/Responsive.hpp>`) shows one of several layouts chosen by its size:
  `Responsive(std::vector<Breakpoint>)`, each `Breakpoint { minColumns, minRows, make }`, listed narrowest first.
  On every resize it picks the **last** breakpoint whose minima the size meets (`Responsive::choose()`, a pure
  function) and calls that breakpoint's `make` only when the choice changes: a resize within a band lays the same
  layout out again, one across a band destroys it and builds the next. The layout fills the container. Nothing is
  built before the first non-empty size, nor at a size no breakpoint's minima are met at.

```cpp
auto dashboard = core::tui::Responsive { {
    { .minColumns = 0, .minRows = 0, .make = [] { return listOnly(); } },          // < 60 columns
    { .minColumns = 60, .minRows = 0, .make = [] { return listOverDetails(); } },  // 60-99
    { .minColumns = 100, .minRows = 0, .make = [] { return sideBySide(); } },      // >= 100
} };
screen.root().addChild(dashboard, { .flex = core::tui::layout::Style { .grow = 1 } });
```

- **Borders.** Every widget that draws a frame takes a `BorderStyle` (`<core/tui/Box.hpp>`: `None`, `Single`,
  `Double`, `Rounded` -- `╭ ╮ ╰ ╯` --, `Heavy`, `Dashed`) and draws its corners, lines and junctions from
  `BorderChars::fromStyle()` of it, never from a literal: `Box` and the three dialogs through their config's
  `border`, `QuestionComponent` (its left bar) through `QuestionConfig::border`, `CompletionPopup(BorderStyle)`,
  `CommandPalettePopup(BorderStyle, clock)`, `FuzzyPickerPopup(BorderStyle, clock)`, `Tooltip(BorderStyle)`, and
  the tooltip a `Screen` shows through `ScreenConfig::tooltipBorder`. Each default is the frame the widget drew
  before: `Single` for the completion popup and the tooltip, `Rounded` for the others. `Borders_test.cpp` renders
  each of them in every style with corners and refuses a corner of any other style, so a widget that hard-codes
  one fails there.

```cpp
auto bytes = std::string {};
auto surface = core::tui::HeadlessSurface { core::tui::Size { .width = 80, .height = 24 },
                                            [&bytes](std::string_view chunk) { bytes += chunk; } };
auto screen = core::tui::Screen { surface };
screen.root().addChild(view, { .area = { .x = 0, .y = 0, .width = 80, .height = 24 } });
screen.draw(); // bytes now holds the frame; the next draw() appends only what changed
```

Two checks keep the half headless. `core-cpp.tui-core-symbols` reads tui_core's includes and its
archive's symbols for anything of the terminal half, `platform`, `async` or `net`.
`tests/consumer-tui-core` is a CPM consumer that renders a component through `HeadlessSurface` with
the terminal half off; CI's `consumer-smoke (tui-core)` builds and runs it twice, the second time
with `-fno-exceptions -fno-rtti` across the whole build (`core::base`, `core::tui_output`,
`core::tui_core` and libunicode), which is the language mode a WebAssembly guest compiles in. Why it
is shaped this way, and what remains before a WebAssembly guest builds it:
[A headless TUI](../design/tui-headless.md).

A guest measures text with the same libunicode tables, built for `wasm32-wasip1` from a tree a native
configure generated them into (`CPM_libunicode_SOURCE`), so the guest's build downloads nothing.
`tests/wasi-unicode` is that build, and `core-cpp.unicode-wasi` runs its width and grapheme table
under Node's WASI wherever `WASI_SDK_PREFIX` names a wasi-sdk.

## `core::tui`

The rest, on top of `core::tui_core`, [platform](platform.md), [async](async.md) and
[net](net.md), and on stb when `CORE_CPP_WITH_IMAGES` is on: `Terminal`, `TerminalInput`,
`TerminalChannel`, `ClipboardWriter`, `SemanticBlockClient`, the image file loaders and the
runtime. Native only: there is no terminal under Emscripten. The list below covers both halves.

- **Input.** `TerminalInput` puts the terminal in raw mode, enables the protocols above and decodes
  what comes back through `VtParser` into an `InputEvent` — keys (`KeyCode`, `Modifier`), mouse,
  paste, focus, resize, and the protocol reports a query waits for. `Terminal` pairs it with a
  `TerminalOutput` and owns the query round-trips (`queryCursorPosition()`, `queryCellSize()`,
  `queryDecMode()`, `queryDeviceAttributes()`), each on an injected clock and each bounded.
- **Clipboard.** `ClipboardWriter` copies through the terminal rather than an OS clipboard API, so
  it works over SSH: OSC 5522 where the terminal says it knows mode 5522, OSC 52 otherwise. The
  first copy asks with DECRQM followed by DA1, which every terminal answers, so the probe ends at
  the DA1 reply and times out only when nothing answers at all; the answer is kept for the
  writer's lifetime. An OSC 5522 copy waits for the terminal's status (`DONE` or an error such as
  `EPERM`); an OSC 52 copy cannot be confirmed, and a non-text MIME type over it is refused before
  anything is sent. Each copy opens a `TerminalChannel` from an injected factory:
  `openControllingTerminal()` opens `/dev/tty` (or `CONIN$`/`CONOUT$`) beside standard I/O, so
  redirected output does not stop a copy, with echo and line buffering off while it is open; a
  process outside the terminal's foreground process group gets a write-only channel and OSC 52.
  Before an exchange it will wait on -- the probe, an OSC 5522 write -- the writer discards input
  already queued (`TerminalChannel::discardPendingInput()`), so a reply that arrived after an
  earlier exchange gave up waiting does not answer this one; typeahead goes with it. An OSC 52 copy
  waits for nothing and discards nothing. A reply that
  arrives after the channel has closed cannot be caught: a terminal that confirms an OSC 5522 copy
  later than the status timeout leaves that reply for whatever reads the terminal next.
  `VtParser` decodes OSC replies into `OscResponse` only when constructed with
  `OscRecognition::Response`; by default `ESC ]` stays Alt+].
- **Mouse.** `TerminalInput::setMouseTracking()`, or `Terminal::setMouseTracking()`, asks the terminal for a
  `MouseTracking` mode: `Buttons` (DEC 1000), `Drag` (1002) or `AnyMotion` (1003), each with SGR encoding
  (1006). The default is `Off`, because a terminal that reports the mouse stops selecting text on a
  click-and-drag; an application opts in. Contour's passive mode 2029 is enabled either way, and where the
  terminal confirms it, hover tooltips raise the mode to `AnyMotion` (`mouseTracking()` answers the raised
  mode). `VtParser` decodes every mode's reports into `MouseEvent`s: motion with a button held is a `Move`
  of that button, motion with none a `Move` of button 3.
- **Pointer.** `Screen` routes a mouse event to the top-most visible component under it -- overlays first,
  the one shown last on top, then the tree -- and `componentAt(row, col)` answers the same question for a
  caller, such as a drag source looking for its drop target. A press makes the component that handled it
  the capture target: until the release, every move and that release go to it, in its own coordinates and
  wherever the pointer is, so a component sees its whole drag. The release ends the capture, as do the
  next press, `releasePointer()`, and the component leaving the screen -- destroyed, removed from the tree
  or hidden as an overlay. Scroll events are never captured, and a press hides the tooltip, so none opens
  mid-drag.
- **Drawing.** `Buffer` is a grid of `Cell`s, `Canvas` a clipped view of one, and `Screen` the
  renderer that diffs a frame against the last and writes only what changed, inline, full-screen or
  in a fixed area. A cell is reached through `Buffer::cellAt(Point)`, null outside the buffer.
  `Theme`, `StyledText`, `Text`, `Box`, `Rect` and `HyperlinkEmitter` sit under it.
- **Components.** `Component` and the widgets over it: `InputField` (multi-line editing, undo, kill
  ring, ghost text, selection), `List`, `TreeTableView`, `Dialog`, `StatusBar`, `LogPanel`,
  `Spinner`, `ProgressBar`, `Tooltip`, `QuestionComponent`, and the popups `CompletionPopup`,
  `CommandPalettePopup` and `FuzzyPickerPopup` with `PopupKeyDispatch` and `ScrollableSelection`.
- **Completion.** `core::tui::completer`: `Completer` with `CompletionConfig`,
  `CompletionProvider`, `CompletionItem`, `FuzzyMatch` with `FuzzyConfig` and `FuzzyMatchResult`,
  and `SmartCaseMatch` with `SmartCaseConfig`. The namespace is the one the directory names, as
  `runtime/` is `core::tui::runtime`; endo's TUI is one flat `namespace tui`, and the import kept
  that until [core-cpp#30](https://github.com/contour-terminal/core-cpp/issues/30).
- **Markdown and syntax.** `MarkdownRenderer` with `MarkdownTable`, `MarkdownHtml` and the inline
  grammar, and `GenericSyntaxHighlighter`, a lexer per language, with
  `SyntaxHighlighterRegistry` for the languages core::tui does not ship (below).
- **Images.** With `CORE_CPP_WITH_IMAGES`: `loadImage()`, `resizeImage()` and `readClipboardImage()`
  over stb, `encodeSixel()`, and `FilesystemImageProvider`, which implements the always-present
  `ImageProvider` interface `MarkdownRenderer` takes. A file name is UTF-8 on every platform: stb is
  built with `STBI_WINDOWS_UTF8`, so on Windows it opens the name through the wide API.
- **Runtime.** `core::tui::runtime`: `TuiRuntime` gives the TUI its input vocabulary --
  `co_await runtime.nextEvent()`, `nextEventFor()`, `nextActivity()`, `nextAgentReady()` -- and
  forwards everything else to the [event loop](net.md) it is constructed on: `blockOn()`,
  `spawn()`, `delay()`, `sleepUntil()`, `waitReadable()`, `waitWritable()`, the clock and the root
  stop source. Input arrives through an injected `InputSource`, which names the handles to watch
  and decodes what is ready behind them; `TerminalInputSource` is that over a `Terminal`, and
  `TuiRuntime(loop, terminal)` makes one for you. `runModal()` drives a modal to its result; for a
  deadline use `core::net::withTimeout(&runtime.loop(), …)`.
- **The end of the input.** When the input handle reaches its end -- a terminal that hung up (EIO,
  or an end of file that `poll(2)` confirms as a hangup), a pipe whose writer closed, a Windows
  console that was closed or detached, or a handle the loop refuses to watch -- the source's
  `inputClosed()` answers true, the runtime stops watching the handle, and `runtime.inputClosed()`
  answers true from then on. One end reaches no handle: a Windows console whose host process died
  is never signalled again. The runtime therefore asks the source, through `probeEnd()`, every
  `TuiRuntimeOptions::endProbe` (one second by default), and ends the input the same way once the
  console can no longer answer; a source whose end does arrive as readiness answers
  `EndProbe::NotNeeded` the first time and is not asked again. `TerminalInput::poll()` asks the
  same question between slices of at most a second of its wait, so a `poll(-1)` on such a console
  returns. The input waits deliver what was read before the end (and a pending agent message, for
  `nextActivity()`), and then throw `core::async::OperationCancelled` without
  parking. An application that catches that cancellation asks `inputClosed()` to tell "the
  terminal is gone, exit" from an interrupt. Before 0.2.1 the runtime re-parked on a handle at its
  end, which answers at once and yields nothing, so with `SIGHUP` ignored a hung-up terminal spun
  the process at 100% CPU ([core-cpp#49](https://github.com/contour-terminal/core-cpp/issues/49)).
- **Test doubles.** `MockTerminalOutput` records what a renderer did semantically instead of
  emitting VT, `runtime::testing::ScriptedInputSource` scripts the decoding and, with
  `closeInput()`, the end of the input (readiness comes from
  `core::net::testing::ScriptedBackend` or from a real `core::platform::SystemPipe`), and
  `TestHelpers.hpp` reads a rendered `Buffer` back as text. `testing::ScriptedTerminal` is a
  terminal on a `ManualClock` for anything built on `TerminalChannel`: how it answers the OSC 5522
  probe and a write, and every byte it was sent, decoded back with `decodedPayload()`,
  `mimeType()` and `target()`.

## Registering a language

`GenericSyntaxHighlighter` ships a lexer for the languages of `LanguageId` — C and C++, CMake,
Python, Bash, Markdown, JSON, YAML, git diffs, assembly, PowerShell, CMD, XML and INI — and for no
others. An application that has its own language teaches it to core::tui rather than core::tui
shipping it, which is what keeps one application's vocabulary out of a library four of them link
([core-cpp#24](https://github.com/contour-terminal/core-cpp/issues/24)).

A `SyntaxHighlighterRegistry` is that seam. It is an ordinary object: construct one, fill it, and
pass it to whatever renders the text. There is no process-wide registry, so two parts of one
program can hold different ones and a test never has to undo a registration.

```cpp
auto highlighters = core::tui::SyntaxHighlighterRegistry {};

auto const wobble = highlighters.registerLanguage({
    .name = "wobble",
    .extensions = { ".wob" },
    .fenceTags = { "wobble", "wob" },
    .highlight = [](std::string_view line, core::tui::HighlightState state) {
        return highlightWobbleLine(line, state); // the application's own lexer
    },
});
if (!wobble)
    log("wobble was refused: {}", wobble.error().token);
```

`registerLanguage()` returns a `LanguageId` of its own, from the reserved range that begins at
`FirstRegisteredLanguageId`, or a `LanguageRegistrationFailure` saying which name, extension or
fence tag was already claimed. It refuses rather than shadows — replacing would repoint an id
already handed out, and whoever held that id would get a wrong answer that looks right — and a
refused definition leaves the registry exactly as it was.

Every entry point then takes the registry as a trailing argument that defaults to `nullptr`,
meaning the built-in languages alone:

```cpp
auto renderer = core::tui::MarkdownRenderer { output, theme, &highlighters };  // ```wobble fences
auto const styled = core::tui::StyledText::fromMarkdown(text, width, &theme, &highlighters);
auto const language = core::tui::detectLanguageFromPath("draft.wob", &highlighters);
auto const [map, next] = core::tui::highlightLine(line, language, state, &highlighters);
```

A registry answers for the built-in languages too — built-in rows are consulted first — so
registering one costs an application nothing it already had.

!!! warning "A registered id belongs to the registry that issued it"
    Registered ids are dense from `FirstRegisteredLanguageId` in registration order and carry
    nothing that identifies their registry, so passing one to a *different* registry is a
    precondition violation — the same contract a `std::vector::iterator` has with its container.
    If that registry issued an id in the same position, the line is highlighted as **its**
    language, silently and wrongly; only an id past the end of it gives plain text. A program that
    holds one registry, which is the shape this is designed for, cannot hit this. One that holds
    two keeps each id with its own registry.

    Built-in ids — everything below `FirstRegisteredLanguageId` — are not issued by anybody and
    *are* portable: they mean the same language in any registry and in none.

The three built-in tables the module ships are `ExtensionLanguageTable`, `FenceTagLanguageTable`
and `FilenameLanguageTable`, and a registered language claims extensions and fence tags but never
a **file name**: `CMakeLists.txt`, `.clang-format` and `.editorconfig` are well known beyond any
one project, whereas an application knows what its own configuration file is called and names the
language for it itself, rather than asking `detectLanguageFromPath()` to guess. A registration
whose extension would be shadowed by a file-name row is refused rather than left dead.

## Layout

The module's root holds only platform-independent code. What goes through the operating system is
in `posix/` and `windows/`, which the per-platform source lists select, so no file there guards
itself with an `#ifdef` of its platform:

- `posix/` — the `termios` raw mode, the SIGWINCH self-pipe, `poll(2)`, the `write`/`read` retry
  loop, `isatty`, and the clipboard tools (`wl-paste`, `xclip`).
- `windows/` — the console modes and code pages, console input records decoded as UTF-8,
  `WaitForMultipleObjects`, the resize event, and `GetConsoleScreenBufferInfo`.

`TerminalInput`'s own handles live in an opaque `NativeState` those two define, so
`<core/tui/TerminalInput.hpp>` names neither `<termios.h>` nor `<windows.h>`. No file in this
module chooses its platform with an `#ifdef`, and `runtime/` has no platform directory at all: the
multiplexed wait its two `TerminalEventSource` bodies held is the event loop's, on every
platform.
