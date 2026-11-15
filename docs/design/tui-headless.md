# A headless TUI

The decision record for splitting `core::tui` into a part that needs a terminal and a part that
does not, so that a WebAssembly guest (`wasm32-wasip1`, no file descriptor, no thread, no
exceptions, no RTTI) can build component trees, lay them out, render them into a `Buffer` and turn
two frames into the bytes between them. It settles the shape before any code moves; each later
step is its own pull request and cites the section here it implements.

It was read against tag `v0.20.0` (`c349e94`). Every claim below names the file and line it was
read from at that tag.

## What stands in the way today

| # | Finding | Evidence at v0.20.0 |
|---|---|---|
| 1 | `tui` links the event loop and the platform layer, and is native only | `cmake/CoreCppModules.cmake`: `core_cpp_module(NAME tui KIND STATIC DEPS base platform async net PLATFORMS native WHEN CORE_CPP_WITH_TUI)`; `src/core/tui/CMakeLists.txt`: `PUBLIC_LIBS core::base core::tui_output core::platform core::async core::net unicode::unicode` |
| 2 | Only Emscripten is a known non-native target, and it switches the TUI off | `cmake/CoreCppOptions.cmake`, the `if(EMSCRIPTEN)` block forcing `CORE_CPP_WITH_TUI`, `CORE_CPP_WITH_TUI_OUTPUT`, `CORE_CPP_WITH_IMAGES` and `CORE_CPP_WITH_TLS` off |
| 3 | Source selection would hand WASI the POSIX sources | `cmake/CoreCppTargets.cmake`: `CORE_CPP_PLATFORM_POSIX` is `UNIX AND NOT EMSCRIPTEN`, and wasi-sdk's toolchain sets `UNIX` |
| 4 | `Screen` is built on a `Terminal` | `Screen.hpp:112`: `explicit Screen(Terminal& terminal, ScreenConfig config = {});` -- and `Screen.cpp` asks it four things only: `rows()`, `columns()`, `output()` and, for the inline viewport, `queryCursorPosition()` |
| 5 | `Terminal` names the platform layer in its header | `Terminal.hpp:4`: `#include <core/platform/Clock.hpp>` (its third constructor takes a `core::platform::IClock&`); `TerminalInput.hpp:4` includes `core/platform/Types.hpp` |
| 6 | Components reach `Screen`'s definition | `Component.cpp:5` includes `Screen.hpp` (it calls `componentDestroyed`, `invalidate`, `componentDetached`); `InputField.cpp:6-7` includes `Screen.hpp` and `Terminal.hpp` and uses neither; `runtime/Modal.hpp:20` includes `Screen.hpp` (and `runtime/TuiRuntime.hpp`) |
| 7 | Mock output is detected by RTTI | `Terminal.cpp:40`, `:50` and `:117`: `dynamic_cast<MockTerminalOutput*>` |
| 8 | `Buffer` throws | `Buffer.cpp:82` and `:89`: `throw std::out_of_range(...)`; `Screen.cpp` calls `Buffer::at` at six sites (`:144`, `:598`, `:611`, `:755`, `:957`, `:968`). `Buffer::tryAt` already exists and returns null outside |
| 9 | The base output has no OS-free body | `TerminalOutput::writeToDestination` (`TerminalOutput.hpp:394`) is virtual, but its default body is in `posix/TerminalOutput.cpp:84` and `windows/TerminalOutput.cpp:113` only |
| 10 | Seven places read the clock directly | `steady_clock::now()` in `HoverState.cpp`, `InputField.cpp`, `Screen.cpp`, `SemanticBlockClient.cpp`, `Spinner.cpp`, `TimerUtils.hpp`, `TreeTableView.cpp` |
| 11 | Text is measured with libunicode | 12 non-test files include it: `Buffer.cpp`, `Canvas.cpp`, `completer/FuzzyMatch.{hpp,cpp}`, `CompletionPopup.cpp`, `InputField.{hpp,cpp}`, `MarkdownTable.cpp`, `StyledText.cpp`, `Text.cpp`, `Unicode.{hpp,cpp}` |

Corrections to the plan that asked for this record (Contour's
`docs/superpowers/plans/2026-11-14-agent-orchestrator.md`, phase U):

- The module lives in `src/core/tui/`, not `src/tui/`.
- The spec counts 13 files using libunicode; at v0.20.0 it is 12, none of them a test.
- `SemanticBlockClient` and `ClipboardWriter` are on the terminal's side, not the headless side:
  the first reads replies through `TerminalInput` with a timed wait, the second writes through a
  `TerminalChannel` and takes a `core::platform::IClock`. Their clock injection (U.5) is therefore
  a change in the tty half.
- `runtime/Modal.hpp` keeps requiring `Terminal`, through `runtime/TuiRuntime.hpp` ->
  `runtime/TerminalInputSource.hpp` -> `Terminal.hpp`, not through `Screen.hpp`. That edge is the
  runtime's and stays: the runtime is outside the headless target. What changes is that its use of
  `Screen` (`draw()`, `dispatchEvent()`) no longer drags `Terminal` with it.
- Vendoring selects whole directories (`core_cpp_vendor_selects()` in
  `cmake/CoreCppVendor.cmake`), so a consumer that wants the headless part vendors module `tui`
  and configures `CORE_CPP_WITH_TUI_CORE` on and `CORE_CPP_WITH_TUI` off. `tui_core` is not a
  name `MODULES=` understands.

## Decisions

### The headless target is `core::tui_core`

A second target of module `tui`, in the same directory and namespace, with a row of its own --
exactly the shape `tui_output` already has:

```cmake
core_cpp_module_target(NAME tui_core MODULE tui KIND STATIC DEPS base tui_output
                       PLATFORMS any WHEN CORE_CPP_WITH_TUI_CORE)
```

It links `core::base`, `core::tui_output` and `unicode::unicode`, and nothing of `platform`,
`async` or `net`. `core::tui` links `core::tui_core` and keeps everything that needs a terminal,
a thread, the event loop or the file system.

**Contents.** Everything in `src/core/tui/` that reaches no terminal: `VtParser`, `InputEvent`,
`KeyCode`, `Modifier`, `KeyBindings`, `EditAction`, `Cell`, `Buffer`, `Canvas`, `Rect`, `Box`,
`Element`, `Text`, `StyledText`, `TextDecorator`, `Theme`, `Unicode`, `Sixel`, `HyperlinkEmitter`,
`EndProbe`, `Component`, `Screen`, `ScreenSurface` (new, below), `HoverState`, `Tooltip`,
`TimerUtils`, the widgets (`List`, `TreeTableView`, `Dialog`, `InputField`, `GhostTextHelper`,
`LogPanel`, `StatusBar`, `Spinner`, `QuestionComponent`, `CompletionPopup`, `FuzzyPickerPopup`,
`CommandPalettePopup`, `CommandRegistry`, `PopupKeyDispatch`, `ScrollableSelection`), the
completer (`completer/**`), the Markdown renderer (`Markdown*`, `GenericSyntaxHighlighter`),
`ImageProvider` (the interface only), `runtime/ModalComponent.hpp` (a pure `Component` contract,
kept at its path) and the test aids `MockTerminalOutput` and `TestHelpers`.

**Stays in `core::tui`.** `Terminal`, `TerminalInput`, `TerminalChannel`, `ClipboardWriter`,
`SemanticBlockClient`, the image file loaders (`FilesystemImageProvider`, `ImageLoader`, stb, and
their `posix/`/`windows/` halves), everything under `posix/` and `windows/`, `runtime/` but for
`ModalComponent.hpp`, and `testing/ScriptedTerminalChannel.hpp`.

**Option.** `CORE_CPP_WITH_TUI_CORE`, defaulting to `CORE_CPP_WITH_TUI` for the same reason
`CORE_CPP_WITH_TUI_OUTPUT` does: contour turns the TUI off today and must keep building nothing
there. `CORE_CPP_WITH_TUI` forces it on, and it forces `CORE_CPP_WITH_TUI_OUTPUT` on, as `tui`
already does. The libunicode dependency row moves from `CORE_CPP_WITH_TUI` to
`CORE_CPP_WITH_TUI_CORE`. Under Emscripten it stays forced off, as the whole TUI is today: its
`tui_output` dependency is native until the output has an OS-free body (U.2), and the WASI row
(U.11) is where a wasm target first switches it on.

No public header moves and no include path changes, so a consumer of `core::tui` changes nothing.

### `Screen` becomes tty-free; no second frame driver

`Screen` asks its terminal four things (finding 4). That is an interface, not a reason for a
second class: two frame drivers would mean two diff paths, two hover and tooltip state machines
and two places where a fix has to land. So `Screen` stays the one frame driver and moves into
`tui_core`, built on

```cpp
/// What a Screen draws on: a size, the output its frames are written to, and the one question the
/// inline viewport asks.
class ScreenSurface
{
  public:
    [[nodiscard]] virtual int columns() const noexcept = 0;
    [[nodiscard]] virtual int rows() const noexcept = 0;
    [[nodiscard]] virtual TerminalOutput& output() noexcept = 0;
    [[nodiscard]] virtual std::expected<std::pair<int, int>, QueryUnanswered> queryCursorPosition() = 0;
};
```

which `Terminal` implements with the members it already has (`QueryUnanswered` moves with it into
the headless target; `Terminal.hpp` still provides it through its include). `Screen(Terminal&)`
call sites keep compiling, through the derived-to-base conversion. `Screen::terminal()` becomes
`Screen::surface()`, the one breaking change of the split; no consumer calls it (endo, the one
consumer that constructs a `Screen`, was grepped). This is U.1's only non-move.

U.3 then adds the headless surface the plan asks for -- a size and a byte sink, answering the
cursor query with `QueryUnanswered` so the inline viewport falls back exactly as it does on a
silent terminal -- and the pure `diffFrames(previous, next, options)` that `Screen`'s three flush
paths call.

**As built (U.3).** The plan's "`Screen` constructible from a size and a byte sink" is
`HeadlessSurface(Size, ByteSink)` handed to the one `Screen` constructor, not a second constructor:
an owning one would make `Screen` hold a surface it sometimes owns and sometimes borrows, for a saving
of one line at the call site. Its output is a `ByteSinkOutput`, which overrides every member that
would otherwise reach the process's standard output, so the same surface behaves identically
natively and in a guest. The diff is two functions over one body: `writeFrameDiff(out, previous,
next, options)` writes through a `TerminalOutput` -- which `Screen` calls, so `MockTerminalOutput`
still sees each move and write and every existing `Screen` case keeps its meaning -- and
`diffFrames()` runs it into a `ByteSinkOutput` over a string and returns the bytes. What the three
flush paths differed in became `DiffOptions`: `DiffAddressing::Absolute` (fullscreen and fixed,
cursor-addressed, over an area) or `Relative` (inline, row by row from column 0), and, each off by
default, hyperlinks, Sixel images, row marks and reflow inhibition. Hiding, placing and showing the
cursor stay `Screen`'s, because only the frame driver knows where the cursor should end, so
identical frames diff to nothing. Absolute addressing gained images and marks, which `Screen`'s
fullscreen path never wrote and still does not ask for: an image there skips the cells it covers,
and a cell an image no longer covers is repainted.

### The output needs no operating system, and no RTTI (U.2)

**As built.** The five members of `TerminalOutput` that had bodies only under `posix/` and `windows/`
(`initialize`, `detectCapabilities`, `writeToDestination`, `isTerminal`, `updateDimensions`) gained
a third body in `portable/TerminalOutput.cpp`, on the C++ standard library alone: the C standard
output, not a terminal, the 80x24 default size, nothing probed. It sits in a new platform source
list, `SOURCES_PORTABLE`, compiled where the platform is neither POSIX nor Windows -- the same
injected-implementation shape `posix/` and `windows/` already have, not an `#ifdef` in the base
output. Native trees also compile it as an unlinked object library, so its body is checked long
before the first wasm build. `tui_output`'s row says `PLATFORMS any`; the options stay forced off
under Emscripten until U.11 gives a WebAssembly target a row.

The three `dynamic_cast<MockTerminalOutput*>` in `Terminal.cpp` asked one question -- does a
terminal stand behind this output, or does the output model the screen itself? -- so the output
now answers it: `virtual OutputKind kind() const` (`Device`, the default, or `Simulation`) and
`simulatedCursorPosition()`, which a simulation answers 1-based. An explicit property rather than a
type test also changes one edge: an output that is not a mock but reports `Simulation` is treated
as one, and the tests pin both directions.

### No exceptions in the headless half (U.4)

**As built.** `Buffer::at()` threw and `Buffer::tryAt()` returned null for the same question, so
both became one `Buffer::cellAt(Point)`, null outside. The no-op tests this needed found a real
abort -- `fill()` and `clearRect()` above or left of the buffer, and a fixed viewport past the
screen's edge, built reversed ranges that a hardened standard library aborts on -- and all three now
clip through `Rect::intersect()`.

The check that stays on is a configuration, not a grep: CI's `consumer-smoke (tui-core)` builds
`tests/consumer-tui-core` a second time with `-fno-exceptions -fno-rtti` over the whole build, so a
`throw`, a `try`, a `dynamic_cast` or a `typeid` anywhere in the closure -- core-cpp's or
libunicode's -- fails natively before any wasm toolchain is involved. libunicode's table generator
is a host tool and throws, so that build takes the libunicode tree the step before it generated the
tables into, exactly as a wasm build will. Its first run found two in
`core::base`: `detail::fail()`'s `try` block, redundant under its `noexcept`, and
`pathFromTrustedUtf8()`'s `throw`. The latter goes through `core::throwOrFail()`, which is the
`throw` with exceptions on and a `Require()`-style failure with them off; the decision lives in
that one function rather than in a macro at each site.

### Width tables: libunicode builds for `wasm32-wasip1` from pre-generated tables (U.7 option a)

Measured, not assumed. libunicode v0.9.3 already treats a source tree that carries its nine
generated files (`ucd.{h,cpp}`, `ucd_enums.h`, `ucd_fmt.h`, `ucd_ostream.h`,
`case_normalization_data.h`, `codepoint_properties_data.{h,cpp}`, `codepoint_properties_names.cpp`)
as pre-generated: its `src/libunicode/CMakeLists.txt` then skips both the `UCD.zip` download and the
table generator. With the files a native build had generated, and wasi-sdk 34:

- `cmake -DCMAKE_TOOLCHAIN_FILE=<wasi-sdk>/share/cmake/wasi-sdk-p1.cmake
  -DLIBUNICODE_SIMD_IMPLEMENTATION=none -DLIBUNICODE_BUILD_STATIC=ON
  -DCMAKE_CXX_FLAGS="-fno-exceptions -fno-rtti"` configures with
  "All generated source files found; skipping UCD download and code generation", and builds clean;
- a probe linking `libunicode.a` and `libunicode_ucd.a`, asking `unicode::width()` of `A`, U+4E2D,
  U+1F600 and U+0301 and the grapheme break between `a b` and `e` + U+0301, links to a 148 KiB
  module (`-Os`, `--gc-sections`, stripped) and answers correctly under Node's WASI preview 1.

`LIBUNICODE_SIMD_IMPLEMENTATION=none` is required: libunicode's default for an unknown processor is
`std` (`std::experimental::simd`). The table generator is never built for the guest; it is a host
tool and stays one.

Option (b), caller-supplied width and grapheme providers, is rejected: the guest would have to
carry the same tables anyway, or cross the host boundary once per code point, and every one of the
12 files would grow a provider parameter for a problem the library already solves.

**Where the tables come from.** libunicode's release assets carry no source tarball with the
generated files (v0.9.3 ships `unicode-query` binaries and `SHA256SUMS` only), and a fresh CPM fetch
for a wasm toolchain finds none, so it would try to build the generator for wasm and download
`UCD.zip`. The wasm build therefore takes libunicode in one of two ways that already work and need
no change here: a libunicode installed for the wasm target (the `FIND_PACKAGE libunicode 0.9.3`
path of its dependency row), or `CPM_libunicode_SOURCE` pointing at a libunicode tree a native
build has already generated into. CI (U.12) does the latter: one native configure of libunicode,
then the cross build against the same tree. Contour builds libunicode natively anyway, so its guest
build reuses that tree and downloads nothing. U.11 adds `LIBUNICODE_SIMD_IMPLEMENTATION none` to
the dependency row's options for a WASI build.

### `IClock` moves into `core::base`

`IClock`, `SteadyTimePoint`, `SteadyDuration`, `SteadyClock` and `ManualClock` move from
`core/platform/Clock.hpp` to `core/Clock.hpp`, namespace `core`. `core/platform/Clock.hpp` includes
it and keeps `core::platform::IClock` and the others as using-declarations; `CachedClock` and the
wall clocks stay in `platform`. The headless target then takes the same interface `Terminal` and the
event loop already take, rather than a second one.

**This breaks a forward declaration, and U.5 lists it under Breaking.** A using-declaration is not a
class, so code that forward-declares `namespace core::platform { class IClock; }` and later sees
`core/platform/Clock.hpp` fails ("conflicts with a previous declaration"). Contour does exactly that:
`src/vtbackend/screen/LinkDetector.hpp:33-36`. The consumer fix is to include
`<core/platform/Clock.hpp>` (or `<core/Clock.hpp>`) there, or forward-declare `core::IClock`
instead. Contour's re-vendor (U.14) makes that change in its own commit; no other consumer was found
forward-declaring a clock (endo, fastcached and Lightweight's `dbtool` grepped).

Not into `tui_core`: the clock is not the TUI's concept, and the event loop, which already uses it,
would then have to reach up into the TUI for it. Not "below platform" as a new module: one header
does not earn a module row. The header is `<chrono>` plus `<mutex>` for `ManualClock`, and both
compile for `wasm32-wasip1` with wasi-sdk 34 (checked with `ManualClock` itself). This is U.5's
first commit.

## Order

U.1 (this target), U.2 (OS-free output, no RTTI), U.3 (headless surface, `diffFrames`), U.4 (no
`throw`), U.5 (the clock), U.6 (`ScreenConfig` enums), U.7 (the libunicode recipe), U.8-U.10
(layout, `Responsive`, rounded borders), U.11-U.12 (WASI row, CI job), U.13 (release).
