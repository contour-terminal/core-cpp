# tui

The terminal UI. Namespace `core::tui`, directory `src/core/tui/`, targets `core::tui_output`
(with `CORE_CPP_WITH_TUI_OUTPUT`) and `core::tui` (with `CORE_CPP_WITH_TUI`). Both are on by
default and off under Emscripten; `CORE_CPP_WITH_TUI` forces `CORE_CPP_WITH_TUI_OUTPUT` on, and
`CORE_CPP_WITH_TUI_OUTPUT` defaults to whatever `CORE_CPP_WITH_TUI` is.

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
  operating system about the process's standard output, and a subclass answers for its own.
- `SyncGuard` brackets a frame in DEC mode 2026 so a terminal does not paint a half-drawn one. It
  writes through the `TerminalOutput` it was made from, so a retargeted output is bracketed on its
  own stream, and only when that output's `isTerminal()` is true: on any other destination it
  flushes at both ends and writes no sequence, so a caller takes one unconditionally.
- `buildSgrSequence()` turns a `Style` into one SGR sequence.
- `core::tui::protocols` holds the sequence constants the input and output sides share (the Kitty
  keyboard protocol, bracketed paste, mouse and focus tracking, colour-scheme notification,
  win32-input-mode, OSC 8), `appendHyperlinkOpen()`, and `parseSixelFromDeviceAttributes()`, which
  reads a DA1 answer.
- `Result<T>` and `VoidResult`, the module's `std::expected` aliases.

## `core::tui`

The rest, on top of `core::tui_output`, [platform](platform.md), [async](async.md) and libunicode,
and on stb when `CORE_CPP_WITH_IMAGES` is on. Native only: there is no terminal under Emscripten.

- **Input.** `TerminalInput` puts the terminal in raw mode, enables the protocols above and decodes
  what comes back through `VtParser` into an `InputEvent` — keys (`KeyCode`, `Modifier`), mouse,
  paste, focus, resize, and the protocol reports a query waits for. `Terminal` pairs it with a
  `TerminalOutput` and owns the query round-trips (`queryCursorPosition()`, `queryCellSize()`,
  `queryDecMode()`, `queryDeviceAttributes()`), each on an injected clock and each bounded.
- **Drawing.** `Buffer` is a grid of `Cell`s, `Canvas` a clipped view of one, and `Screen` the
  renderer that diffs a frame against the last and writes only what changed, inline, full-screen or
  in a fixed area. `Theme`, `StyledText`, `Text`, `Box`, `Rect` and `HyperlinkEmitter` sit under it.
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
  `ImageProvider` interface `MarkdownRenderer` takes.
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
  answers true from then on. The input waits deliver what was read before the end (and a pending
  agent message, for `nextActivity()`), and then throw `core::async::OperationCancelled` without
  parking. An application that catches that cancellation asks `inputClosed()` to tell "the
  terminal is gone, exit" from an interrupt. Before 0.2.1 the runtime re-parked on a handle at its
  end, which answers at once and yields nothing, so with `SIGHUP` ignored a hung-up terminal spun
  the process at 100% CPU ([core-cpp#49](https://github.com/contour-terminal/core-cpp/issues/49)).
- **Test doubles.** `MockTerminalOutput` records what a renderer did semantically instead of
  emitting VT, `runtime::testing::ScriptedInputSource` scripts the decoding and, with
  `closeInput()`, the end of the input (readiness comes from
  `core::net::testing::ScriptedBackend` or from a real `core::platform::SystemPipe`), and
  `TestHelpers.hpp` reads a rendered `Buffer` back as text.

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
