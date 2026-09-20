# tui

The terminal UI. Namespace `core::tui`, directory `src/core/tui/`, targets `core::tui_output` and
`core::tui` (only with `CORE_CPP_WITH_TUI`, which is on by default and off under Emscripten).

Imported from endo's `src/tui` at `f774a210`, which includes the coroutine-runtime work fastcached
upstreamed there. `runtime/TuiRuntime.hpp` and its test come from fastcached's own copy
(`5389e29a`), which carries one fix endo has not taken back yet.

!!! note "Status"
    Available. Its runtime still drives its own `EventSource`; Task B12 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    moves it onto [`core::net::EventLoop`](net.md) and deletes `runtime/EventSource.hpp`,
    `runtime/PollEventSource.*` and `runtime/WithTimeout.hpp`. `core::tui` does not link
    `core::net` until then.

## `core::tui_output`

A static library that links [base](base.md) and nothing else: no libunicode, no coroutines, not
even [platform](platform.md). Its row in the module table says `DEPS base`, so the configure
refuses any other link from it. A program that only prints styled text and progress — Lightweight's
`dbtool` — links this and takes nothing else with it.

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
  own stream.
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
- **Runtime.** `core::tui::runtime`: `TuiRuntime` drives coroutines against an `EventSource`
  (`co_await runtime.nextEvent()`, `delay()`, `waitReadable()`, `waitWritable()`), with
  `TerminalEventSource` over a real terminal, `PollEventSource` for headless work, `runModal()` and
  `withTimeout()`.
- **Test doubles.** `MockTerminalOutput` records what a renderer did semantically instead of
  emitting VT, `runtime::testing::MockEventSource` scripts a wait, and `TestHelpers.hpp` reads a
  rendered `Buffer` back as text.

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
registering one costs an application nothing it already had. An id is meaningful only to the
registry that handed it out: passing one to a different registry, or to a call with no registry,
highlights the line as plain text rather than as some other language. Well-known *file names*
(`CMakeLists.txt`, `.clang-format`, `.editorconfig`) stay built-in: an application knows what its
own configuration file is called and names the language itself.

## Layout

The module's root holds only platform-independent code. What goes through the operating system is
in `posix/` and `windows/`, which the per-platform source lists select, so no file there guards
itself with an `#ifdef` of its platform:

- `posix/` — the `termios` raw mode, the SIGWINCH self-pipe, `poll(2)`, the `write`/`read` retry
  loop, `isatty`, and the clipboard tools (`wl-paste`, `xclip`).
- `windows/` — the console modes and code pages, console input records decoded as UTF-8,
  `WaitForMultipleObjects`, the resize event, and `GetConsoleScreenBufferInfo`.

`TerminalInput`'s own handles live in an opaque `NativeState` those two define, so
`<core/tui/TerminalInput.hpp>` names neither `<termios.h>` nor `<windows.h>`. The one file that
still chooses its platform with an `#ifdef` is `runtime/PollEventSource.cpp`, which Task B12
deletes.
