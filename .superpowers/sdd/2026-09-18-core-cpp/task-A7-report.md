# Task A7 report: `core::tui_output` + `core::tui`

**Status: DONE_WITH_CONCERNS.** Every local preset and both CI workflows are green; the concerns
below are decisions the dispatch did not fix, not open failures.

Branch `master`, `c4a083a..7618e80`, 176 files, +47299/-25.

| Commit | Subject |
|---|---|
| `e8c81ab` | `tui: import endo's terminal output as the dependency-free core::tui_output leaf` |
| `7ab9e42` | `tui: SyncGuard brackets the output it was made from, not standard output` |
| `73d8b6a` | `tui: import endo's terminal UI as core::tui` |
| `7618e80` | `docs: DEPS bounds only a target that has a row, and siblings may link each other` |

## What was implemented

### The targets

`core::tui_output` is a STATIC leaf with its own row in `cmake/CoreCppModules.cmake`, `DEPS base`.
It holds `TerminalOutput` (with `SyncGuard`, `Style`, `RgbColor`, `UnderlineStyle`), `SgrBuilder`,
`TerminalProtocols`, `CursorShape`, `Error`, `posix/PosixIO.hpp` and `windows/Win32Utf.hpp`. It
links `core::base` PRIVATE and nothing else — the layering check refuses libunicode's users, the
coroutines and `core::platform` from it by name.

`core::tui` is the rest, `PUBLIC_LIBS core::base core::tui_output core::platform core::async
unicode::unicode`, plus `stb_image` PRIVATE with `CORE_CPP_WITH_IMAGES`. Both are `PLATFORMS
native` and `WHEN CORE_CPP_WITH_TUI`.

Test binaries: `core-cpp.tui_output` (`TerminalOutput_test.cpp`, `TerminalProtocols_test.cpp`) and
`core-cpp.tui` (28 sources, plus `windows/TerminalInput_test.cpp` on Windows). endo's
`test_main.cpp` is dropped for `core::testing_main`.

### Ruling R40 — the platform-directory layout

`tui/platform/` and `tui/runtime/platform/` do not survive. What each file became, and what changed
beyond the move:

| endo | core-cpp |
|---|---|
| `platform/PosixIO.hpp` | `posix/PosixIO.hpp` |
| `platform/Win32Utf.hpp` | `windows/Win32Utf.hpp` |
| `platform/TerminalOutput.cpp` | the byte composition → `TerminalOutput.cpp`; the OS half → `posix/TerminalOutput.cpp`; the XTVERSION reply reader → `detail/XtVersion.hpp` |
| `platform/TerminalOutputWin32.cpp` | `windows/TerminalOutput.cpp` (the same three splits) |
| `platform/TerminalShared.cpp` + `platform/Terminal.cpp` + `platform/TerminalWin32.cpp` | `Terminal.cpp` (everything that is not per-platform), `posix/Terminal.cpp` and `windows/Terminal.cpp` (destructor, `initialize()`, `shutdown()`) |
| `platform/TerminalInput.cpp`, `platform/TerminalInputWin32.cpp` | `TerminalInput.cpp` (the members neither platform's state reaches), `posix/TerminalInput.cpp`, `windows/TerminalInput.cpp` |
| `TerminalInputWin32_test.cpp` | `windows/TerminalInput_test.cpp`, Windows-only source, named-mutex serialisation kept (the name is now `core-cpp-tui-console-input-test`) |
| `ImageLoader.cpp`'s `readClipboardImage()` | `posix/ImageLoader.cpp`, `windows/ImageLoader.cpp` |
| `runtime/platform/PollHelpers.hpp` | `runtime/posix/PollHelpers.hpp` |
| `runtime/platform/TerminalEventSourcePosix.cpp` | `runtime/posix/TerminalEventSource.cpp` |
| `runtime/platform/TerminalEventSourceWin32.cpp` | `runtime/windows/TerminalEventSource.cpp` |

No file in a platform directory carries a file-wide guard of its platform; the per-platform source
lists select them. `runtime/{EventSource,PollEventSource}` stay as they are per R40, so
`runtime/PollEventSource.cpp` is the one file left that chooses with an `#ifdef`.

Deduplication beyond the move was deliberate. endo's two `TerminalOutput` files were ~500 lines
each and differed in four functions; its two `Terminal` files each repeated the fourteen members
`TerminalShared.cpp` sits beside. Keeping both copies would have meant fixing every warning and
every clang-tidy finding twice, on a platform each, and would have left the divergence that caused
the duplication in the first place. The shared text is byte-identical to endo's, function by
function; the one asymmetry endo has — the Windows `initialize()` probes capabilities and the POSIX
one does not — is preserved rather than harmonised.

### Ruling R41 — platform-clean public headers

- `TerminalInput.hpp`: the descriptors, the saved `termios`, the self-pipe, the console handles,
  modes and code pages and the resize event are an opaque `struct NativeState` held by
  `std::unique_ptr`, which `posix/TerminalInput.cpp` and `windows/TerminalInput.cpp` define. The
  header includes neither `<termios.h>` nor `<windows.h>`, its public API is unchanged, and its
  size and layout no longer depend on the platform.
- `TerminalOutput.hpp`: the `#if defined(_WIN32) using NativeHandle = void*` alias is gone entirely
  rather than being redirected at `core::platform::NativeHandle`. `SyncGuard` now holds the
  `TerminalOutput` it brackets, so there is no handle in the header to name. **`core::tui_output`
  therefore did not need `core::platform`, and its row stays `DEPS base`.**
- `ImageLoader.cpp`'s two `#if _WIN32` blocks were logic (`readClipboardImage()` and its
  `popen`-based helper), so they are `posix/ImageLoader.cpp` and `windows/ImageLoader.cpp`.
- `Terminal.hpp` and `TerminalInput.hpp` forward-declared `core::platform::IClock` and
  `core::platform::Wakeup` before opening their own namespace, which the `namespace-directory`
  hygiene rule refuses (it reads the *first* named namespace). Both now include the header.

### Test-first: `SyncGuard` and `isTerminal()`

`src/core/tui/TerminalOutput_test.cpp` is new (endo has no test for this file). Six cases: the
guard's begin sequence reaches `writeToDestination` after the flush that precedes it and its end
sequence after the guarded bytes; a moved-from guard writes exactly one end sequence; a
default-constructed guard writes none; `isTerminal()` dispatches through a `TerminalOutput&`; the
default destination's `isTerminal()` asks the operating system; and `copyToClipboard()` emits OSC 52
with the right base64 for all three padding cases.

### Docs and records

`docs/modules/tui.md` rewritten for what exists; `docs/modules/index.md` (row, dependency graph),
`README.md` (row, status paragraph), `AGENT.md` (module table, status paragraph),
`.agent/reference/source-map.md`, `.agent/reference/provenance.md` (163 rows), `CHANGELOG.md`
(Added for both targets and the two dependencies, Fixed for `SyncGuard`), `NOTICE` (endo's
`src/tui/**`, and a fastcached entry at `5389e29a` for the two runtime files).
`.agent/rules/library-hygiene.md` took the A6 re-review fold-in, in its own commit.

## TDD evidence

**RED, step one** — the test compiled against the imported code, before `isTerminal()` existed:

```
/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:32:52: error: only virtual member functions can be marked 'override'
/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:100:25: error: no member named 'isTerminal' in 'core::tui::TerminalOutput'
... 10 errors generated.
```

**RED, step two** — with `isTerminal()` added and `SyncGuard` still endo's:

```
/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:56: FAILED:
  CHECK( output.captured() == "before\033[?2026h" )
with expansion:  "before" == "before[?2026h"

/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:65: FAILED:
  CHECK( output.captured() == "before\033[?2026hinside\033[?2026l" )
with expansion:  "beforeinside" == "before[?2026hinside[?2026l"

/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:75: FAILED:
  CHECK( output.captured() == "\033[?2026h" )
with expansion:  "" == "[?2026h"

/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:79: FAILED:
  CHECK( output.captured() == "\033[?2026h\033[?2026l" )
with expansion:  "" == "[?2026h[?2026l"

test cases: 18 | 15 passed | 2 failed | 1 skipped
assertions: 31 | 27 passed |  4 failed
```

The skip is the "asks the operating system" case, run outside ctest — which is what it refuses to
guess at. **GREEN**, the same binary under ctest: `All tests passed (32 assertions in 18 test
cases)` — 32, not 31, so that case ran and its `CHECK_FALSE` held.

## Local results

| Preset / configuration | Build | Tests |
|---|---|---|
| `clang-debug` (WSL) | clean | 18/18 |
| `gcc-release` (WSL) | clean | 18/18 |
| `clang-asan-ubsan` (WSL) | clean | 18/18 |
| `clang-tsan` (WSL) | clean | 18/18 |
| `clang-tidy` (WSL, `--clean-first` after the `.clang-tidy` edits) | clean | — |
| `emscripten` (WSL, emsdk) | clean, `module tui: off` | 14/14 |
| `cl-debug` (VS dev shell) | clean | 20/20 |
| `clangcl-debug` (`--clean-first`) | clean | 20/20 |
| `clangcl-release` | clean | 20/20 |
| `-DCORE_CPP_WITH_IMAGES=OFF` (clang, one-off tree) | clean | 17/17 |

`python scripts/clang-format.py --check`: 351 files formatted with clang-format 22.1.8.
`mkdocs build --strict`: clean.

## CI

| Workflow | Run | Conclusion |
|---|---|---|
| `build.yml` | [35475447624](https://github.com/contour-terminal/core-cpp/actions/runs/35475447624) | **success**, all 21 jobs, `ci-ok` green |
| `portability.yml` (dispatched) | [35475468092](https://github.com/contour-terminal/core-cpp/actions/runs/35475468092) | **success**, FreeBSD (system clang) |

`build.yml` jobs: style, linux (clang-22, clang-22-arm64, clang-22-cxx26, clang-22-tracy, gcc-14,
gcc-15), macos (appleclang, llvm-22), windows (cl-debug, cl-release, cl-release-tls,
clangcl-release), sanitizers (clang-asan-ubsan, clang-tsan), clang-tidy, coverage, compile-cache,
emscripten (3.1.56 and latest), ci-ok.

## Files changed

156 files imported into `src/core/tui/` (140 from endo `f774a210`, 2 from fastcached `5389e29a`,
and `CMakeLists.txt`, `.clang-tidy`, `detail/XtVersion.hpp`, `TerminalOutput_test.cpp` written
here). Outside the module: `cmake/CoreCppModules.cmake` (two rows),
`cmake/CoreCppDependencies.cmake` (libunicode, stb, and the stb WRAP), `.clang-tidy` (a pointer to
the module's file), `AGENT.md`, `README.md`, `CHANGELOG.md`, `NOTICE`, `docs/modules/index.md`,
`docs/modules/tui.md`, `.agent/reference/{provenance,source-map}.md`,
`.agent/rules/library-hygiene.md`.

## Consumer impact

- **endo** — the codemod its Phase C migration already plans: `<tui/X>` → `<core/tui/X>`,
  `<platform/X>` → `<core/platform/X>`, `<coro/X>` → `<core/async/X>`, `tui::` → `core::tui::`,
  `endo::platform::` → `core::platform::`, `coro::` → `core::async::`. Beyond it: four files
  (`src/agent/ui/AgentInputComponent.cpp`, `src/shell/{AgentModeSession,Shell}.cpp`,
  `src/shell/builtins/InlineCommands.cpp`) that use `FilesystemImageProvider` or
  `isRemoteImageSource` must include `<core/tui/FilesystemImageProvider.hpp>` as well as
  `<core/tui/ImageProvider.hpp>`. `SyncGuard(NativeHandle)` is gone; nothing outside `src/tui`
  called it, and `output.syncGuard()` is unchanged.
- **fastcached** — the same, and `vendor/endo/tui` goes away. Its `DelayAwaiter` delta is already
  in what core-cpp imported, so PR-A does not have to carry it forward.
- **tuidu** — the same codemod, against a June snapshot; expect API drift beyond the names.
- **Lightweight `dbtool`** — links `core::tui_output` only, which still depends on `core::base`
  alone. `TerminalOutput::isTerminal()` is new and is exactly what a progress writer needs.
- **contour** — vendors base, log, cli, platform, async, net and testing; unaffected.

## Self-review

- **Every loop conversion is semantics-preserving, including the empty case.**
  `std::views::iota(a, b)` does not terminate when `b < a`, where `for (i = a; i < b; ++i)` simply
  does not run, so every converted bound whose expression can fall below its start is clamped with
  `std::max`. Bounds that are a `.size()` from zero, or a literal, are not. The loops that could not
  become a range — a step other than `++`, a counter the body moves, a look-ahead grapheme iterator,
  a compound condition — are `while` loops or carry an explicit `break`, and each was read before it
  was changed. `Screen::flushInline`, the riskiest (the body advances `row` past an image and then
  `continue`s), steps `row` on both paths; `Screen_test.cpp` covers it with 1534 lines.
- **The grapheme walks kept their look-ahead.** The endo idiom was `auto nextIt = it; ++nextIt;` to
  find a cluster's end. The `while` form advances a `next` iterator at the top of the body and reads
  the current one from it, which is the same positions, removes the per-iteration copy, and makes a
  `continue` in the body safe — which it was not in the `for` form for the two loops that had one.
- **`copyToClipboard()` now encodes through `core::base64::encode()`** instead of the third copy of
  a base64 encoder (endo had one in each platform file). I checked the padding arithmetic against
  endo's byte for byte and added a test for all three cases.
- **Two clang-tidy fix-its were wrong and were reverted by hand.**
  `readability-static-accessed-through-instance` rewrote `co_await runtime->delay(...)` to
  `co_await DelayAwaiter::runtime->delay(...)` in five places, which does not compile; and my own
  `find() != npos` rewriter put a `!` in front of the enclosing `CHECK(` in 26 places and collapsed
  a two-argument `find('.', at + 1)`. All were caught by the build, fixed, and the affected tests
  re-run.
- **Nothing is skipped that could have run.** The one `SKIP` is the case that cannot tell a real
  `isTerminal()` from a constant outside ctest; under ctest it runs.
- **The fd-1 half of "nothing reaches fd 1" is asserted indirectly.** The capture is the only sink
  the subclass has, so a guard writing to standard output leaves it empty — which is exactly what
  the RED showed. I did not add fd redirection: it needs `dup`/`_dup`, and the `tui_output` test
  binary is the one place that has to stay free of platform code to mean anything.

## Concerns

1. **The libunicode and stb rows did not exist.** The dispatch says they "resolve through
   `cmake/CoreCppDependencies.cmake` as they exist since A1"; they did not, and A7 added them, which
   the table's own rule ("a row is added by the task that first needs it") expects. libunicode is
   `v0.9.3` with `PEDANTIC_COMPILER OFF` and `PEDANTIC_COMPILER_WERROR OFF` as instructed. **stb is
   pinned to `f1c79c02822848a9bed4315b12c8c8f3761e1296`**, not endo's `GIT_TAG master`: a library
   cannot ship a fetch that changes under its consumers. That commit is the one endo's own CPM cache
   holds, so it is the code endo's TUI was written against.
2. **Configuring with `CORE_CPP_WITH_TUI=ON` now downloads `UCD.zip` from unicode.org** at configure
   time, once per CPM source cache — libunicode generates its tables from it. endo does the same, but
   it is new for core-cpp and it is the first dependency that fetches from outside GitHub. An
   offline or restricted network fails the configure rather than the build.
3. **`core::tui` does not link `core::net`, and its row does not list it.** The spec's Part I §1 row
   says it should. Nothing in the imported code includes `<core/net/...>`: the runtime drives its own
   `EventSource` until B12. I declared what is linked rather than what will be, so the table stays a
   statement of fact; B12 adds `net` to the row in the commit that first links it. Say so if you
   would rather the row ran ahead of the code.
4. **`FilesystemImageProvider` is a new public header**, split out of `ImageProvider.hpp`. Without
   the split, `CORE_CPP_WITH_IMAGES=OFF` leaves a class declared in a shipped header and defined
   nowhere, because `MarkdownRenderer` needs the `ImageProvider` interface either way. It costs the
   four endo files listed above one include. The OFF configuration is built and tested locally but
   has no CI leg.
5. **`src/core/tui/.clang-tidy` is a second configuration file**, disabling two checks for this
   directory alone, with the reasoning in it and a pointer from the root file.
   `readability-function-cognitive-complexity` is over its threshold in eighteen functions — nine
   `highlight*()` lexers (59–133), `VtParser::dispatchCsi` (98), `StyledText::fromMarkdown` (178),
   `InputField::render` (200), `Screen::flushInline` (121) and five more — for the same reason the
   root file already accepts for `readability-function-size`. `readability-static-accessed-through-
   instance` cannot be satisfied at all: `co_await` calls `DelayAwaiter::await_ready()` on the
   awaiter object, the language has no other spelling, and the check's fix does not compile. I chose
   a scoped file over widening the root list so the checks keep holding over base, log, cli,
   platform, net and testing. The alternative is a `-check` row in the root `.clang-tidy`.
6. **`readability-enum-initial-value` is off in the root `.clang-tidy`**, with a reason.
   `core::tui::KeyCode` groups its enumerators into numbered ranges (Unicode codepoints, 0x10000 for
   the non-printable keys, 0x20000 for Kitty's) and initializes the first of each; the check's two
   options do not cover that, and satisfying it means spelling out a hundred consecutive values.
   endo carried a `NOLINT`, which core-cpp does not allow. This one *is* a global relaxation.
7. **`TerminalEventSource` gained a public `signalFd()` accessor.** `_signalFd` is read only by the
   POSIX `waitForReadiness`, so clang-cl reported `-Wunused-private-field` on Windows — endo never
   saw it because it did not apply its pedantic set there. The accessor is honest (the caller
   supplied the handle) and B12 deletes the class, but it is API that endo does not have.
8. **`-Wmissing-designated-field-initializers` (new in clang) was answered in the headers.** Six
   aggregate members without a default member initializer got `{}` (`WaitOutcome::events`,
   `readyRead`, `readyWrite`, `ScreenConfig::fixedArea`, `CompletionItem`'s strings,
   `QuestionConfig`'s, `ListItem`'s). That is the house pattern the root `.clang-tidy` already
   defends, so it is a fix rather than an exemption, but it does change those headers.
9. **Twelve `constexpr` class constants were renamed to CamelCase** (`doubleClickTimeout` →
   `DoubleClickTimeout` and friends in `InputField`, `LogPanel`, `QuestionComponent`, `Spinner`).
   They are private, so no consumer sees them, but a reviewer diffing against endo will.
10. **`ImageLoader_test.cpp` lost a dead helper.** `generateMinimalPng()` was unused and its own
    comments say its CRC is a placeholder, so it never decoded; `-Wunused-function` found it.
11. **One upstream asymmetry is preserved, not fixed.** `TerminalOutput::initialize()` runs the
    XTVERSION probe on Windows and not on POSIX, where `detectCapabilities()` is called separately
    by `Terminal::initialize()` after raw mode. It looks like an oversight in endo; changing it is a
    behaviour change and not this task's.
12. **`MockTerminalOutput::syncGuard()` still returns a no-op guard.** With the fix it could return
    the base implementation and bracket its capture, but that would add `\033[?2026h`/`l` to what
    every existing renderer test sees. Left as endo has it.

---

# Fix round 1

Ruling R44's fix list (`task-A7-fixround1.md`): items 1–9 from the `/code-review` correctness pass,
items 10–16 from `task-A7-review.md`. All sixteen are done. The six defects deferred to Task B12
(core-cpp#16–#21) were not touched.

Twelve commits, `63555d8..04ffa19`.

## Inherited work

An earlier session was cut off mid-round and left five paths uncommitted: the item 1 fix and its
test (`GenericSyntaxHighlighter.{cpp,_test.cpp}`), the item 2 and 6 fix and a new `Dialog_test.cpp`,
and the `CMakeLists.txt` line registering it. I read the diff before touching anything, judged it
correct, and then did what makes it evidence rather than assertion: stashed the two `.cpp` fixes,
kept the tests, and ran them. Both tests failed without their fix (below). The only thing missing
was a provenance row for `Dialog_test.cpp`, which `ctest -L hygiene` refuses a file without; that
and the clang-format pass are mine. Nothing was redone.

## Per item

1. **Stack buffer overflow in the assembly highlighter.** `toLowerInto()` takes a
   `std::span<char>` and returns an oversized identifier unchanged, so the bound lives in one
   place rather than at seven call sites, three of which had forgotten it. The four call sites
   that guarded themselves no longer do. (`cb68bdc`)
2. **Dialog frames at swapped coordinates.** All six `Rect`s are `{ .x = column, .y = row }`.
   (`d6db46e`)
3. **`~SyncGuard()` did not flush.** The destructor and move-assignment flush before writing
   `CSI ?2026l`; the header's description of the destructor says "flushes" again. (`95cbf43`)
4. **SIGWINCH handler.** `errno` is saved and restored around the body, `activeInput` is a
   `std::atomic<TerminalInput*>` with a `static_assert` on lock-freedom, and both ends of the
   resize self-pipe are `O_NONBLOCK`. (`5aa494f`)
5. **Unbounded parser buffers.** `VtParser::MaxPasteLength` (4 MiB), `MaxCsiParamLength` (256) and
   `MaxDcsLength` (64 KiB) are public constants, each with the reasoning for its value. Past one
   the parser returns to Ground; a paste emits what it collected first, because that buffer holds
   the user's own text. (`5aeca8f`)
6. **`InputDialog::render()` threw on a narrow terminal.** All three dialogs clamp `dialogWidth`
   and `inputWidth` to zero. (`d6db46e`, with item 2)
7. **`endo-` on the wire.** The OSC 8 `id=` is the bare hash. A behaviour change, in the CHANGELOG
   and in provenance. (`99a1f63`)
8. **Completion order.** `std::ranges::stable_sort`. (`99a1f63`)
9. **A test that asserts nothing.** `a default-constructed SyncGuard writes nothing` is deleted —
   a guard with no output has nowhere to write, so no case can distinguish one implementation of
   `~SyncGuard()` from another — and the moved-from case, which is the real seam for a guard
   holding no output, says so. Two cases that fail without item 3's flush take its place.
   (`95cbf43`)
10. **Provenance under-recorded the import.** Two paragraphs in the table header: what the import
    applied beyond the mechanical rewrite, and that a re-sync merges the upstream delta rather
    than replacing the file. Per-file notes on the fifteen substantive ones, plus every file this
    fix round changed. (`7419297`)
11. **Dead public API.** `signalFd()` deleted, `_signalFd` `[[maybe_unused]]`. (`4b34ba6`)
12. **Global tidy relaxation.** `readability-enum-initial-value` moved from `.clang-tidy` to
    `src/core/tui/.clang-tidy`; it is live for base, log, cli, platform, async, net and testing
    again. (`0482754`)
13. **Cognitive complexity.** The disable is a
    `readability-function-cognitive-complexity.Threshold: '210'` under `CheckOptions`, so the
    number is measured. Paying the list down is **core-cpp#22**, cited in the file. (`0482754`)
14. **`readability-static-accessed-through-instance`.** `DelayAwaiter::await_ready()` is
    `constexpr bool ... const`, not `static`, and the disable is gone. The `static_assert` in
    `TuiRuntime_test.cpp` that only compiled while it was static is now a test case that calls it
    on an awaiter built against a frozen `ManualClock` — which asserts more, since it proves a
    deadline that has already elapsed still answers `false`. (`4b34ba6`, `0482754`)
15. **Stale network note.** `docs/getting-started/building.md` names the three fetches and the two
    hosts. (`3ee0782`)
16. **Minor items.** The `TuiRuntime_test.cpp` row records `got->bytesRead() == 1` (`7419297`);
    the libunicode row pins `BUILD_SHARED_LIBS OFF` with the reason (`3ee0782`); the stb row says
    why it is fetch-only with neither `FIND_PACKAGE` nor `NO_FETCH` (`3ee0782`); the hygiene
    rule's namespace-directory limitation is **core-cpp#23**, cited in
    `tests/cmake/check-cmake-hygiene.cmake` (`9eb66af`).

## Issues filed

| Issue | Title | Cited in |
|---|---|---|
| [core-cpp#22](https://github.com/contour-terminal/core-cpp/issues/22) | Bring core::tui's cognitive complexity back to the root threshold | `src/core/tui/.clang-tidy` |
| [core-cpp#23](https://github.com/contour-terminal/core-cpp/issues/23) | check-cmake-hygiene's namespace-directory rule forbids a leading forward-declaration block | `tests/cmake/check-cmake-hygiene.cmake` |

## RED/GREEN evidence

Items 1, 2 and 6 were shown red by stashing the two implementation fixes the interrupted session
had left in the tree and keeping its tests. Items 3, 5 and 9 are test-first from here.

**Item 1** — `clang-asan-ubsan`, `GenericSyntaxHighlighter.asm_token_longer_than_the_lowercase_buffer`
with `char lowerBuf[64]` restored:

```
==812263==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x77f447af0110 ...
WRITE of size 1 at 0x77f447af0110 thread T0
  This frame has 11 object(s):
    [208, 272) 'lowerBuf' (line 1578) <== Memory access at offset 272 overflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow
```

GREEN: `All tests passed (269 assertions in 70 test cases)` for `[highlight]`.

**Items 2 and 6** — `clang-asan-ubsan`, `Dialog_test.cpp` against endo's `Dialog.cpp`:

```
/mnt/d/core-cpp/src/core/tui/Dialog_test.cpp:127: FAILED:
  CHECK( graphemeAt(screen.buffer(), 7, 10) == "┌" )
with expansion:  " " == "┌"

/mnt/d/core-cpp/src/core/tui/Dialog_test.cpp:117: FAILED:
  CHECK( canvasToString(...).contains("alpha") )
with expansion:  false

/mnt/d/core-cpp/src/core/tui/Dialog_test.cpp:149: FAILED:
  CHECK_NOTHROW( dialog.render(screen.canvas()) )
due to unexpected exception with message:
  basic_string::substr: __pos (which is 10) > this->size() (which is 5)

test cases:  5 | 1 passed |  4 failed
assertions: 17 | 4 passed | 13 failed
```

`__pos` 10 for a string of 5 is `5 - static_cast<std::size_t>(-5)` wrapping, which is the defect
exactly. GREEN: `All tests passed (17 assertions in 5 test cases)`.

The fifth case, `every_dialog_renders_on_a_terminal_narrower_than_its_border`, passes both ways:
`SelectDialog` and `ConfirmDialog` never threw. It is a regression guard on the clamps, not
evidence, and is marked as such here rather than counted.

**Item 3 (and item 9's second half)** — the two new `SyncGuard` cases, written before the flush:

```
/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:74: FAILED:
  CHECK( output.captured() == "\033[?2026hinside\033[?2026l" )
with expansion:  "[?2026h[?2026l" == "[?2026hinside[?2026l"

/mnt/d/core-cpp/src/core/tui/TerminalOutput_test.cpp:95: FAILED:
  CHECK( first.captured() == "\033[?2026hinside\033[?2026l" )
with expansion:  "[?2026h[?2026l" == "[?2026hinside[?2026l"

test cases: 19 | 16 passed | 2 failed | 1 skipped
assertions: 32 | 30 passed | 2 failed
```

Both ends that write the end sequence were red, which is why both were fixed. GREEN under ctest:
`test cases: 19 | 18 passed | 1 skipped`, `assertions: 32 | 32 passed`.

**Item 5** — the three bounded-buffer cases, written before the caps (the constants existed so the
cases could name them; nothing enforced them):

```
VtParser.Paste.a_paste_past_the_cap_is_emitted_and_ends
/mnt/d/core-cpp/src/core/tui/VtParser_test.cpp:825: FAILED:
  REQUIRE( events.size() == 1 )   with expansion: 0 == 1

VtParser.Csi.a_parameter_string_past_the_cap_is_abandoned
/mnt/d/core-cpp/src/core/tui/VtParser_test.cpp:848: FAILED:
  REQUIRE( events.size() == 1 )   with expansion: 0 == 1

VtParser.Dcs.a_payload_past_the_cap_is_abandoned
/mnt/d/core-cpp/src/core/tui/VtParser_test.cpp:863: FAILED:
  REQUIRE( events.size() == 1 )   with expansion: 0 == 1

test cases: 3 | 3 failed
assertions: 6 | 3 passed | 3 failed
```

Zero events in each: the parser was still in `PasteBody`, `CsiParam` and `DcsBody`, consuming
bytes into a buffer nothing would ever close. GREEN: `All tests passed (242 assertions in 75 test
cases)` for `VtParser.*`.

**Item 13** — that the gate reports rather than merely passes. Asked for the check explicitly at a
threshold of 10, it fires; at the configured 210 it is silent, and the largest function is 200:

```
$ clang-tidy -p out/build/clang-tidy --checks='-*,readability-function-cognitive-complexity' \
      --config="{CheckOptions: {readability-function-cognitive-complexity.Threshold: '10'}}" \
      src/core/tui/InputField.cpp
InputField.cpp:199:18: warning: function 'render' has cognitive complexity of 200 (threshold 10)
InputField.cpp:994:18: warning: function 'handleKey' has cognitive complexity of 47 (threshold 10)
InputField.cpp:724:18: warning: function 'executeAction' has cognitive complexity of 32 (threshold 10)
InputField.cpp:2171:18: warning: function 'handleMouse' has cognitive complexity of 13 (threshold 10)

$ clang-tidy -p out/build/clang-tidy src/core/tui/InputField.cpp   # the project's own config
(no output)
```

At the root's 50 the first three would fire, so `src/core/tui/.clang-tidy`'s 210 is what is in
effect — and a nineteenth function over 210 would now be a build failure, which a disabled check
could never be.

## Local results

| Preset / configuration | Build | Tests |
|---|---|---|
| `clang-debug` (WSL) | clean | 18/18 |
| `gcc-release` (WSL) | clean | 18/18 |
| `clang-asan-ubsan` (WSL) | clean | 18/18 |
| `clang-tsan` (WSL) | clean | 18/18 |
| `clang-tidy` (WSL, `--clean-first` after the `.clang-tidy` edits) | clean | — |
| `emscripten` (WSL, emsdk 3.1.56) | `module tui: off`, no work to do | 14/14 |
| `cl-debug` (VS dev shell) | clean | 20/20 |
| `clangcl-debug` (`--clean-first`) | clean | 20/20 |
| `clangcl-release` (`--clean-first`) | clean | 20/20 |

`python scripts/clang-format.py --check`: 352 files formatted with clang-format 22.1.8.
`mkdocs build --strict`: clean.

Two notes on the numbers. The `clang-tidy` tree needed `--clean-first`, because `.clang-tidy` is
not a build input; the first such run stopped on a finding of its own —
`bugprone-implicit-widening-of-multiplication-result` on `4 * 1024 * 1024` widening to
`std::size_t` in `VtParser.hpp` — which is why the caps are written `std::size_t { 4 } * 1024 *
1024`. The `emscripten` tree had nothing to rebuild: `core::tui` is off there, and none of the
other changed files is in the WebAssembly subset. Every preset's configure re-ran libunicode's,
because pinning `BUILD_SHARED_LIBS OFF` changes its CPM options.

The tui test binary costs a few seconds more than before: 9.3 s → 15.4 s under ASan, 11.4 s under
TSan. That is the 4 MiB paste the cap test feeds, and it is the price of exercising the real
constant rather than a smaller one injected for the test.

## CI

| Workflow | Run | Conclusion |
|---|---|---|
| `build.yml` (`9eb66af`) | [35496614625](https://github.com/contour-terminal/core-cpp/actions/runs/35496614625) | **success**, all 21 jobs, `ci-ok` green |
| `docs.yml` (`9eb66af`) | [35496614582](https://github.com/contour-terminal/core-cpp/actions/runs/35496614582) | **success**, strict build and Pages deploy |
| `portability.yml` (dispatched) | [35496620406](https://github.com/contour-terminal/core-cpp/actions/runs/35496620406) | **success**, FreeBSD (system clang) |
| `build.yml` (`04ffa19`) | [35497139755](https://github.com/contour-terminal/core-cpp/actions/runs/35497139755) | **success** after re-running one job, `ci-ok` green |

The second `build.yml` run is the one-line comment fix on top; the first covers every code change.

`windows (cl-release-tls)` failed once in the second run and passed on a `gh run rerun --failed`.
It is runner infrastructure, not the change: the same job passed on `9eb66af` with identical code,
`04ffa19` edits only a comment in a `.clang-tidy` file, and the failure was two concurrent link
steps invoking the same `vcpkg.exe`:

```
[316/322] Linking CXX executable src/core/net/core-cpp-net_tls-test.exe
FAILED: [code=1] src/core/net/core-cpp-net_tls-test.exe
  ... && vcpkg.exe z-applocal --target-binary=.../core-cpp-net_tls-test.exe ...
Access is denied.
[317/322] Linking CXX executable src/core/net/core-cpp-net-test.exe
FAILED: [code=1] src/core/net/core-cpp-net-test.exe
  ... && vcpkg.exe z-applocal --target-binary=.../core-cpp-net-test.exe ...
Access is denied.
```

Both failures are the `z-applocal` post-link step, milliseconds apart, on the one `vcpkg.exe` the
job unpacked. Worth an issue against the workflow if it recurs; once is not a pattern.

## Not fixed here, on purpose

core-cpp#16–#21, the six deferred to Task B12, are untouched. Nothing in `runtime/` changed except
`TerminalEventSource.hpp`'s `[[maybe_unused]]` (item 11) and `TuiRuntime.hpp`'s `await_ready()`
(item 14), neither of which is one of them.

## Concerns

1. **`MaxPasteLength` is a judgement, not a measurement.** 4 MiB is far above any interactive
   paste and far below "unbounded", but it is a number I chose. A consumer that pastes a file
   into a TUI editor will see a paste past it split: the first 4 MiB arrives as a `PasteEvent`
   and the rest as ordinary key events, because the parser is back in Ground. Emitting the
   collected text rather than dropping it is what the fix list asked for; chunking (staying in
   `PasteBody` and emitting every 4 MiB) would serve a large legitimate paste better, and would
   be a behaviour change no reviewer has asked for. Worth a second opinion.
2. **The DCS cap assumes DCS carries only terminal answers.** 64 KiB is generous for XTGETTCAP
   and DECRQSS. If a consumer ever feeds `VtParser` a DCS that carries an image — a Sixel coming
   *in* rather than going out — the cap is too small. Nothing in core-cpp does that today.
3. **The threshold of 210 is the status quo, not a target.** `InputField::render` is at 200, so
   the check now bites at +5%. That is deliberate (core-cpp#22 is the work to pay it down), but a
   renderer that grows 10 points will fail the build rather than merely be noticed.
4. **Item 4 has no test.** A SIGWINCH handler's `errno` discipline and a pipe's `O_NONBLOCK` are
   not reachable from a unit test without racing a real signal against a real syscall, which is
   the kind of wait `.agent/rules/testing.md` says not to write. The fix list did not ask for one
   either. The evidence is the code and the reasoning in the comments.
5. **`.endo-format` and `endo-signature` are still in `core::tui`.** `GenericSyntaxHighlighter`'s
   language table maps `.endo-format` to YAML and `Theme.cpp` calls two colours "endo-signature".
   Item 7 named only `Buffer.cpp:263`, so I fixed only that; but by the same rule — no
   consumer-specific concept in core-cpp — those two are the same finding at lower stakes (they
   are not on the wire). Not mine to decide unilaterally; flagging it.
6. **`MockTerminalOutput::syncGuard()` still returns a no-op guard**, as the implementer's own
   concern 12 noted. Item 3's flush therefore has no effect in renderer tests, which is why the
   `SyncGuard` cases had to be written against `CapturingOutput` instead.

---

# Fix round 1b

Ruling R45: the two consumer-named strings concern 3 of fix round 1 flagged, plus two more of the
same kind found while looking. One commit, `dc9f63f`.

## Which case each one is

The ruling's test is whether a name is part of a wire or file format a consumer already emits or
reads. **None of the four is.** Item 7's OSC 8 `id=` was the only one on the wire, and that was
fixed in round 1.

| Site | What it was | Case | What changed |
|---|---|---|---|
| `GenericSyntaxHighlighter.cpp:2320` | `{ .token = ".endo-format", .language = LanguageId::Yaml }` in the well-known-filename table | internal; nothing to rename | row deleted |
| `Theme.cpp:131-132` | `// Blue (endo-signature)`, `// Teal (endo-signature)` | internal (comments) | describe the colours |
| `completer/FuzzyMatch.cpp:176-177` | the worked example matched `"endo.exe"` against `".../src/shell/endo.exe"` | internal (comment) | a generic `editor.exe` |
| `Theme.hpp:188-189` | `categoryColorFromIndex()`'s Doxygen named `endo::TokenCategory` as the other caller | internal (public-header Doxygen) | "a host application's own highlighter" |

`.endo-format` deserves its own sentence, because "rename it neutrally" has no meaning for it:
there is no neutral name for one project's configuration file. It is a *recognition* row, not a
format core-cpp emits or parses, so the internal case taken to its conclusion is to delete it.
`detectLanguageFromPath(".endo-format")` now answers `LanguageId::None` instead of
`LanguageId::Yaml`, and the table's comment says a row belongs there only when the name is well
known beyond one project. **Consumer impact:** endo loses YAML highlighting of its own config
file until C1 adds one check before it calls `detectLanguageFromPath()` — it is the side that
knows what its configuration file is called. In the CHANGELOG under Fixed.

## Not fixed, and why

`LanguageId::Endo`, `registerEndoHighlighter()`, the `.endo` extension row and the `endo`
fence-tag row are the same finding and are untouched. They are not prose:

- `LanguageId::Endo` and `registerEndoHighlighter()` are public API. The neutral spelling is
  mechanical (`LanguageId::Custom`, `registerCustomHighlighter()`) and endo updates two call
  sites, but it is an API break and wants a ruling rather than my judgement.
- The `.endo` and `endo` token rows are consumer-specific *data* in two `constexpr` tables with
  no registration seam. Deleting them without one loses `.endo` detection for good; adding one
  (`registerLanguageToken()`, or a host-supplied table) is API design, which the task says to
  stop and report on rather than patch.

Both are noted in the CHANGELOG entry so the gap is on the record rather than in my head.

## Verification

| Preset | Build | Tests |
|---|---|---|
| `clang-debug` (WSL) | clean | 18/18 |
| `gcc-release` (WSL) | clean | 18/18 |
| `clang-tidy` (WSL) | clean | — |
| `clangcl-debug` (`--clean-first`) | clean | 20/20 |

`python scripts/clang-format.py --check`: 352 files formatted with clang-format 22.1.8.
`mkdocs build --strict`: clean (`Theme.hpp`'s Doxygen changed).

The sanitizer presets were not re-run: the only executable change is one row leaving a
`constexpr` table, which the `clang-debug` and `gcc-release` cases cover, and CI runs both
sanitizers anyway.

| Workflow | Run | Conclusion |
|---|---|---|
| `build.yml` (`dc9f63f`) | [35498496844](https://github.com/contour-terminal/core-cpp/actions/runs/35498496844) | **success**, all 22 jobs, `ci-ok` green |
| `docs.yml` (`dc9f63f`) | [35498496845](https://github.com/contour-terminal/core-cpp/actions/runs/35498496845) | **success**, strict build and Pages deploy |
