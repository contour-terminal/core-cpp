# Review: Task A7 — `core::tui_output` + `core::tui` import

**Verdict up front: the task's requirements are met.** Every ruling the dispatch names (R40, R41,
the target rules, the per-target `DEPS` addendum, the file-wide-guard addendum) is satisfied, and I
verified each one mechanically rather than from the report. What is wrong is smaller and all of it
is fixable in one round: the provenance table misreports 79 files as unmodified when they carry
house-style rewrites, one suppression is at the wrong scope, one is avoidable outright, and one
public accessor exists only to silence a warning where the repository already has a one-line answer
for that exact problem.

### Spec Compliance

**Brief**

- ✅ **Files: `src/core/tui/**` from endo `f774a210`, 156 files, `tui::` → `core::tui::`.** 164
  files under `src/core/tui/`; 157 upstream files map onto them. Upstream `src/tui/CMakeLists.txt`
  and `src/tui/test_main.cpp` are correctly dropped; `src/tui/ImageProvider.{hpp,cpp}` became three
  files (see below). I mapped every upstream blob through the mechanical rewrite and diffed:
  57 files are byte-identical after the rewrite, 84 differ (see Important #1), and 23 are
  split/merged/new and not 1:1 comparable. Namespaces present under `src/core/tui/`: `core::tui`,
  `core::tui::runtime`, `core::tui::protocols`, `core::tui::detail`, `core::tui::test`,
  `core::tui::runtime::testing`, plus one nested `detail` and one `std` specialization. No stray
  namespace.
- ✅ **`core::tui_output` leaf split.** `src/core/tui/CMakeLists.txt:15-32`. Its `FILE_SET HEADERS`
  is `CursorShape.hpp Error.hpp SgrBuilder.hpp TerminalOutput.hpp TerminalProtocols.hpp`;
  `detail/XtVersion.hpp`, `posix/PosixIO.hpp` and `windows/Win32Utf.hpp` are in `SOURCES`,
  `SOURCES_POSIX` and `SOURCES_WINDOWS`, so none of them is public.
- ✅ **"verify it includes neither libunicode nor async."** I read every `#include` in all twelve
  `tui_output` translation units and headers. The only non-`std`, non-OS include in the whole target
  is `<core/Base64.hpp>` (`src/core/tui/TerminalOutput.cpp:4`). No libunicode, no `core/async`, no
  `core/net`, no `core/log`, and no `core/platform`.
- ✅ **stb behind `CORE_CPP_WITH_IMAGES`; `StbImageImpl.cpp` keeps `-fno-sanitize=undefined`.**
  `src/core/tui/CMakeLists.txt:44-69`. The option is per-source and `PRIVATE`
  (`set_source_files_properties(... COMPILE_OPTIONS ...)`), gated on `CORE_CPP_GCC_OR_CLANG`, with
  `-w` replacing the ten diagnostic pragmas endo carried in the file itself.
- ✅ **`runtime/` keeps its own `EventSource`.** `runtime/EventSource.hpp`,
  `runtime/PollEventSource.*` and `runtime/TerminalEventSource.hpp` are imported unsplit.
- ✅ **Step 1/2, test-first `SyncGuard` + `isTerminal()`.** `src/core/tui/TerminalOutput_test.cpp`
  is new, six cases, and `7ab9e42` is a self-contained commit carrying the test, the fix, the
  CHANGELOG line and the provenance bump together — which is exactly the shape the dispatch asked
  for. The report's RED transcript is consistent with the code: `isTerminal()` did not exist before
  (`TerminalOutput.hpp:331` adds it) and `SyncGuard` held a `NativeHandle`.
- ✅ **Step 3, `TerminalInputWin32_test` named-mutex serialisation kept.**
  `src/core/tui/windows/TerminalInput_test.cpp:54`, renamed to
  `Local\core-cpp-tui-console-input-test`. All seven of its upstream `SKIP`s survive verbatim.
- ✅ **Step 4, commit subject** matches (`73d8b6a`), and all four commits carry
  `Signed-off-by: Christian Parpart <christian@parpart.family>`.

**Ruling R40 (platform directories)**

- ✅ `tui/platform/` and `tui/runtime/platform/` are gone; the sources are in `posix/`, `windows/`,
  `detail/`, `runtime/posix/` and `runtime/windows/`, matching the dispatch's table row for row.
- ✅ **No namespace `core::tui::platform` exists** (grep over the whole tree: zero hits).
- ✅ **Platform directories are in no `FILE_SET`** (`CMakeLists.txt:26-31, 188-198`).
- ✅ **No file-wide platform guard in `posix/`, `windows/`, `runtime/posix/`, `runtime/windows/`.**
  I grepped those four directories for `_WIN32`, `WIN32`, `__linux__` and `__APPLE__`: zero hits.
  The only `#if` anywhere under `src/core/tui/` is `runtime/PollEventSource.cpp:6,17`, which R40
  explicitly exempts and which `docs/modules/tui.md` names.
- ✅ Every `.cpp`/`.hpp` under `src/core/tui/` appears in `src/core/tui/CMakeLists.txt`; nothing is
  orphaned.

**Ruling R41 (platform-clean public headers)**

- ✅ **No public tui header includes `<windows.h>` or `<termios.h>`.** The only two hits across all
  public headers are prose in Doxygen comments (`TerminalInput.hpp:137`, `TerminalOutput.hpp:95`).
- ✅ **`TerminalInput.hpp` uses an opaque `struct NativeState` behind `std::unique_ptr`**
  (`TerminalInput.hpp:140,147`), defined by `posix/TerminalInput.cpp` and `windows/TerminalInput.cpp`.
- ✅ **Its public API is unchanged.** I diffed the declaration list of endo's `public:` section
  against core-cpp's: the only differences are `endo::platform::NativeHandle` →
  `core::platform::NativeHandle` (twice) and `endo::platform::Wakeup*` → `core::platform::Wakeup*`.
  Nothing added, nothing removed, no signature altered.
- ✅ **`TerminalOutput.hpp`'s `#if _WIN32` `NativeHandle` alias is gone**, and gone entirely rather
  than redirected: `SyncGuard` now holds `TerminalOutput*` (`TerminalOutput.hpp:115`). This is what
  lets the leaf's row stay `DEPS base`, and the report's claim about it is accurate.
- ✅ **`ImageLoader.cpp`'s two `#if _WIN32` blocks were logic and were split**:
  `posix/ImageLoader.cpp` (the `popen` helper and the wl-paste/xclip table) and
  `windows/ImageLoader.cpp` (the `std::nullopt` answer). Faithful to upstream.

**Targets (Part I §1) and the `DEPS` addendum**

- ✅ `core_cpp_module_target(NAME tui_output MODULE tui KIND STATIC DEPS base PLATFORMS native WHEN
  CORE_CPP_WITH_TUI)` — `cmake/CoreCppModules.cmake:204-205`; `PRIVATE_LIBS core::base` and nothing
  else at `CMakeLists.txt:32`. `core_cpp_check_layering()` is called with
  `"${arg_PUBLIC_LIBS};${arg_PRIVATE_LIBS}"` (`cmake/CoreCppTargets.cmake:248`), so the row binds
  both kinds of link, and `tests/cmake/check-layering.cmake` proves the refusal machinery by name.
- ✅ **The leaf's test binary links only the leaf.** `core_cpp_add_test(tui NAME tui_output ...)`
  resolves `tested` to `core::tui_output` (`cmake/CoreCppTargets.cmake:30-35`), so
  `TerminalOutput_test.cpp` and `TerminalProtocols_test.cpp` compile and link against `core::base`
  plus `core::testing_main` alone. That is a stronger proof of the split than a hygiene grep would
  have been.
- ✅ `core::tui` declares `DEPS base platform async`, links `core::tui_output` as a sibling (R43),
  `unicode::unicode` PUBLIC and `stb_image` PRIVATE. The PUBLIC/PRIVATE split is right: three public
  headers include libunicode (`Unicode.hpp`, `InputField.hpp`, `completer/FuzzyMatch.hpp`), and no
  public header mentions stb.
- ✅ Both targets are `PLATFORMS native WHEN CORE_CPP_WITH_TUI`; `cmake/CoreCppOptions.cmake:52`
  forces `CORE_CPP_WITH_TUI` and `CORE_CPP_WITH_IMAGES` off under Emscripten.
- ⚠️ **`core::tui` does not link `core::net`, against the spec's §1 row.** I agree with the call:
  nothing under `src/core/tui/` includes `<core/net/...>` (verified by grep), and a table that
  states intent instead of fact stops enforcing anything. It is documented in six places
  (`cmake/CoreCppModules.cmake:202-203`, `AGENT.md`, `README.md`, `docs/modules/index.md`,
  `docs/modules/tui.md`, `CHANGELOG.md`) and B12 is named as the commit that adds it. The spec
  document itself is unchanged, which I read as correct — specs are not amended per task — but the
  controller should confirm that reading.

**Dependencies**

- ✅ libunicode row (`cmake/CoreCppDependencies.cmake:738-745`): `v0.9.3`, `PEDANTIC_COMPILER OFF`
  and `PEDANTIC_COMPILER_WERROR OFF` as instructed, `WHEN CORE_CPP_WITH_TUI`, `SYSTEM YES`. 0.9.3
  matches endo's `LIBUNICODE_REQUIRED_VERSION` at `f774a210`, and the reason comment is carried over
  faithfully.
- ✅ stb row (`cmake/CoreCppDependencies.cmake:765-770`): `WHEN CORE_CPP_WITH_IMAGES`,
  `DOWNLOAD_ONLY YES`, `WRAP core_cpp_stb_target`, pinned to
  `f1c79c02822848a9bed4315b12c8c8f3761e1296` — a full 40-hex SHA, not endo's `GIT_TAG master`. The
  right call for a library.
- ⚠️ I cannot verify the claim that this SHA "is the one endo's own CPM cache holds"; endo's
  manifest says only `master`.
- ✅ Both resolve through `core_cpp_resolve_dependency`, which honours a parent project that already
  provides `unicode::unicode` or `stb_image` (`cmake/CoreCppDependencies.cmake:76-80`).

**Tests**

- ✅ All 31 endo tui test files came along, plus the new `TerminalOutput_test.cpp` = 32. None was
  dropped. endo's `test_main.cpp` is replaced by `core::testing_main`.
- ✅ Every `SKIP` upstream had is present; **no `SUCCEED` anywhere** under `src/core/tui/`.
- ✅ No assertion-free `TEST_CASE` (I scanned all test files for bodies with no
  `CHECK*`/`REQUIRE*`/`SKIP`; the single hit was a false positive on `CHECK_THROWS_AS`).

**Hygiene and docs**

- ✅ SPDX line one on every source file under `src/core/tui/` (the only file without one is
  `.clang-tidy`, which is YAML config, as at the root).
- ✅ **Provenance coverage is exact**: every one of the 164 files has a row
  (`.agent/reference/provenance.md:222-385`), and no row names a file that does not exist. SHAs are
  full 40-hex, endo `f774a210ce989e5947b8f61d715068b1dc96088c` and fastcached
  `5389e29a5eeca9c2319f43757bd7d6d0ac1c1a13` for the two `TuiRuntime` files, with the `6483abd8`
  rationale spelled out.
- ✅ `docs/modules/tui.md` rewritten, `docs/modules/index.md` row and mermaid graph updated
  (`tui --> net` removed, `tui --> base` added), `README.md`, `AGENT.md`, `NOTICE`, `CHANGELOG.md`
  `[Unreleased]` (Added for both targets and both dependencies, Fixed for `SyncGuard`).
- ✅ The `.agent/rules/library-hygiene.md` Layering paragraph from A6's re-review is present, in its
  own commit (`7618e80`), and states all three points the dispatch asked for.
- ❌ `docs/getting-started/building.md:26` is now wrong about what the first configure fetches — see
  Important #4.
- ⚠️ I did not re-run the suite, clang-format or mkdocs; the report's local matrix and the two green
  CI runs are taken as given.

### Strengths

- **The leaf is real, not nominal.** `core::tui_output` links `core::base` and nothing else, its
  test binary links only the leaf, and I confirmed by reading every include that not one line of it
  reaches libunicode, the coroutines, `core::net`, `core::log` or even `core::platform`. Dropping
  the `NativeHandle` alias from `SyncGuard` instead of redirecting it at
  `core::platform::NativeHandle` is what made that possible, and it is the better of the two options
  R41 offered.
- **R40 and R41 are satisfied to the letter and verifiable in seconds.** Zero platform guards in the
  platform directories, zero `core::tui::platform`, `TerminalInput`'s public API byte-for-byte
  unchanged apart from the namespace. The one surviving `#ifdef` is the one the ruling exempted, and
  the docs name it.
- **The `SyncGuard` fix is the right fix, and its commit is the right shape.** Test, fix, CHANGELOG
  and provenance in one commit; the bug (sequences going to fd 1 regardless of the output) is
  described precisely in `CHANGELOG.md` with the upstream file and line.
- **Deviations are declared rather than smoothed over.** The `net` omission, the `FilesystemImageProvider`
  split, the stb pin and the two suppressions are each argued in the file that implements them, not
  only in the report. `cmake/CoreCppModules.cmake:202-203` and `src/core/tui/CMakeLists.txt:3-14`
  are model comments.
- **The `ImageProvider` split is minimal and correct.** The interface, `PreparedImage`,
  `ImageRenderConfig` and `SixelSupportFn` stay in `ImageProvider.hpp` (always built, because
  `MarkdownRenderer` takes the interface by reference); only `FilesystemImageProvider` and
  `isRemoteImageSource()` move. Cost to consumers: one include, listed by file in the report.
- **Test fidelity is complete.** 31 of 31 upstream tests, all `SKIP`s preserved, the Windows named
  mutex kept, `MockTerminalOutput` deliberately left as endo has it so no existing renderer test
  changes meaning.

### Issues

#### Critical

None.

#### Important

**1. `.agent/reference/provenance.md:222-385` — 79 rows say `-` for files that are not verbatim.**

The table's own header (lines 3-14) says Task B12b "reads this table mechanically, before v0.1.0, to
catch up every row whose upstream has moved", and that a consumer migration's delta check reads it
the same way. Everywhere else in the table a `-` means nothing beyond the trivial, and any
adaptation gets a note ("`CRISPY_*` macros renamed `CORE_*`", "post-import fix (`48b261a`)").

I mapped every endo blob through the mechanical rewrite and diffed. 84 of the 141 comparable files
differ beyond it, and **79 of those 84 carry a `-`**. Across 527 differing hunks:

- 110 hunks convert a C-style `for` to `std::views::iota`, 21 convert one to a `while`;
- 87 rewrite `find(...) != npos` to `contains(...)` in tests;
- 138 add `const`;
- 9 delete a `#pragma clang diagnostic` block;
- 12 private `constexpr` constants are renamed to CamelCase (`InputField.hpp:369,388,399`,
  `LogPanel.hpp:74-78`, `QuestionComponent.hpp:132-136`, `Spinner.hpp:144-146`);
- a dead test helper is deleted (`ImageLoader_test.cpp`, 39 lines);
- a `NOLINT` is removed (`KeyCode.hpp:13`);
- two conditions are rewritten by De Morgan (`VtParser.cpp:916`, `KeyCode.hpp:170`);
- a new `firstOf()` helper replaces four `output.find(...)->n` dereferences in
  `MarkdownRenderer_test.cpp`;
- `Screen::flushInline`'s loop becomes a `while` with `++row` on three separate paths
  (`Screen.cpp:809,813,861`).

Why it matters: B12b and the endo/fastcached/tuidu migrations will treat these 79 files as
re-syncable by taking the newer upstream blob and re-applying the namespace rewrite. Doing that
silently reverts every one of the changes above, including the `NOLINT` removal and the pragma
deletions that the rules require. The record is the only thing standing between a future task and
that mistake.

Fix (cheap): add a paragraph to the table header saying that every `contour-terminal/endo`
`src/core/tui/*` row also carries the house-style conversions Task A7 applied across the import
(C-style loops to ranges, `find()!=npos` to `contains()`, added `const`, deleted diagnostic
pragmas), and give a per-file note to the substantive ones — `ImageLoader_test.cpp`,
`MarkdownRenderer_test.cpp`, `Screen.cpp`, `KeyCode.hpp`, `VtParser.cpp`, `InputField.{hpp,cpp}`,
`LogPanel.{hpp,cpp}`, `QuestionComponent.{hpp,cpp}`, `Spinner.{hpp,cpp}`, `MockTerminalOutput.cpp`.

**2. `src/core/tui/runtime/TerminalEventSource.hpp:72` — a public accessor added only to silence a
warning, when the repository already has the one-line answer.**

`signalFd()` has no caller anywhere in the tree (I grepped `src/core/` — the only uses of `_signalFd`
are the two lines in `runtime/posix/TerminalEventSource.cpp:55,58` that `poll()` it). It exists
solely so clang-cl stops reporting `-Wunused-private-field` on Windows. That is dead public API
added to an OS difference, which is what R40 and the design rules both push back on.

**Task A6 already solved this exact problem the clean way**: `src/core/net/PollEventSource.hpp:52`
reads `[[maybe_unused]] std::size_t _waitRotation = 0;`. `[[maybe_unused]]` on a non-static data
member is standard C++, is not a `NOLINT` and is not a diagnostic pragma, and clang honours it for
this warning.

Fix: make `_signalFd` `[[maybe_unused]]` (`TerminalEventSource.hpp:103`) and delete the accessor and
its Doxygen block (lines 68-72). One line added, five removed, no public API change, and it matches
the precedent set one task earlier.

**3. `.clang-tidy:65-71, 152` — `readability-enum-initial-value` is disabled project-wide for one
enum in one module, when the narrower scope already exists.**

The justification is `core::tui::KeyCode` (`src/core/tui/KeyCode.hpp:13`, whose upstream `NOLINT`
was correctly removed), and the argument for the enum itself is sound. But the fix is at the wrong
grain: `src/core/tui/.clang-tidy` exists, sets `InheritParentConfig: true`, and already carries two
other `-check` lines. Putting this third one there costs one line and keeps the check live over
base, log, cli, platform, async, net and testing — which is exactly the reasoning the tui file gives
for the other two. The report itself flags this as "the one *global* relaxation"; it does not have
to be.

Fix: move `-readability-enum-initial-value,` from `.clang-tidy:152` into
`src/core/tui/.clang-tidy:33-35`, and move the comment block from `.clang-tidy:65-71` with it.

**4. `docs/getting-started/building.md:26` understates what a first configure now downloads.**

The line still reads "Network access for the first configure, to fetch Catch2 with CPM, unless it is
installed." With `CORE_CPP_WITH_TUI` and `CORE_CPP_WITH_IMAGES` — both ON by default — the configure
now also fetches libunicode and stb, and libunicode's own configure downloads `UCD.zip` from
unicode.org to generate its tables. That is core-cpp's first fetch from outside GitHub, and it
fails the *configure*, not the build, on a restricted network. The implementer raised it as
concern 2 and then documented it nowhere.

Fix: extend that bullet to name libunicode (`CORE_CPP_WITH_TUI`) and stb (`CORE_CPP_WITH_IMAGES`),
and say that libunicode reaches unicode.org for `UCD.zip`, with `-DCORE_CPP_WITH_TUI=OFF` as the way
out. `docs/getting-started/options.md:16-17` already lists the options and can link to it.

**5. `src/core/tui/.clang-tidy:34` — disabling `readability-function-cognitive-complexity` outright
leaves nothing enforcing the "18" the file names.**

The reasoning in lines 4-24 is good and the named functions genuinely are what they say they are
(nine hand-written lexers, a CSI dispatch, Markdown's inline grammar, four renderers). But the file
ends with "the number to watch is 18, and a change that adds a nineteenth is adding complexity
rather than inheriting it" — and with the check off, nothing watches it. A nineteenth arrives
unremarked.

Also: **the claim that all 18 are inherited is unverified, and the import cannot have helped.** The
21 `for`→`while` conversions move the increment into the body and add a `break` or `continue`
(`Screen.cpp:809,813,861`, `Text.cpp:321-327`, `InputField.cpp:588,1848`), and cognitive complexity
counts exactly those. The 18 is core-cpp's number, not endo's, and no one measured endo's.

Fix (narrow rather than disable): replace the `-readability-function-cognitive-complexity` line with
a raised threshold in the same file — `readability-function-cognitive-complexity.Threshold: '210'`
under `CheckOptions`, just above the largest (`InputField::render` at 200). New code over that is
still caught, the eighteen still pass, and the comment's promise becomes enforceable. Then file an
issue to pay the lexers down, referenced from the comment.

**6. `src/core/tui/.clang-tidy:35` — `readability-static-accessed-through-instance` is disabled for
164 files because of one member function, and it *can* be satisfied.**

The file says the check "cannot be satisfied" because `co_await` calls
`DelayAwaiter::await_ready()` on the awaiter object. That is true only because the function is
`static`, and staticness is not what the MSVC workaround needs. The type's own comment
(`runtime/TuiRuntime.hpp:433-438`) is explicit: the ARM64 code generator loses the enclosing `try`
"on a temporary awaiter whose `await_ready` **reads the clock through its virtual `now()`**". The
fix is that `await_ready` returns a constant and the deadline check moved to `await_suspend` —
neither of which requires `static`.

`[[nodiscard]] constexpr bool await_ready() const noexcept { return false; }` at
`runtime/TuiRuntime.hpp:448` preserves the fix exactly, satisfies the check, and lets the whole
`-readability-static-accessed-through-instance` line go. Cost: a one-word divergence from
fastcached `5389e29a`, which is a provenance note — and the row at
`.agent/reference/provenance.md:373-374` is already carrying a note about this very function.

#### Minor

- **`src/core/tui/TerminalOutput_test.cpp:82-89`** — "a default-constructed `SyncGuard` writes
  nothing" asserts that `output.captured()` is empty, but `output` was never written to and the
  guard was never attached to it, so the case can only fail on a crash. It does not assert what
  distinguishes. Make it distinguish: write through `output` inside the scope and check the capture
  holds those bytes and no `\033[?2026h`/`l`.
- **`.agent/reference/provenance.md:374`** — the `TuiRuntime_test.cpp` note covers the `6483abd8`
  delta but not the real API adaptation in the file: `*got == 1` became `got->bytesRead() == 1`
  (`runtime/TuiRuntime_test.cpp:531`), because core-cpp's `SystemPipe::read` returns a different
  type from fastcached's. Add it — it is precisely the kind of thing a delta check needs to see.
- **`cmake/CoreCppDependencies.cmake:738-745`** — the libunicode row omits `"BUILD_SHARED_LIBS OFF"`,
  which endo sets (`cmake/EndoThirdParties.cmake:171` at `f774a210`). A consumer configuring with
  `BUILD_SHARED_LIBS=ON` would get a shared libunicode linked `PUBLIC` from a static `core::tui`,
  i.e. a runtime dependency nobody asked for. Either pin it off as endo does, or say in the comment
  why leaving it to the consumer is deliberate.
- **`cmake/CoreCppDependencies.cmake:765-770`** — the stb row has no `FIND_PACKAGE` and is not
  `NO_FETCH`, so it is fetch-only. The library rules require "a fetch-or-system classification";
  the comment explains the pin but never states that stb is never taken from the system. One
  sentence.
- **`src/core/tui/Terminal.hpp:16` and `TerminalInput.hpp:4-5`** — a forward declaration of
  `core::platform::IClock` / `Wakeup` was replaced by a full include, because the
  `namespace-directory` hygiene rule reads the *first* named namespace in the file and refuses a
  foreign one. The resolution is defensible (tui already depends on platform PUBLIC), but the
  checker is what forced extra compile-time coupling into two public headers. Worth an issue against
  `tests/cmake/check-cmake-hygiene.cmake` to let a leading forward-declaration block through, rather
  than letting this precedent spread.

### Assessment

**Task quality: Needs fixes.**

The import itself is the strongest part of this task: R40 and R41 are satisfied to the letter, the
`core::tui_output` leaf is genuinely dependency-free and its test binary proves it, all 31 upstream
tests came along with their `SKIP`s intact, and the `net` omission is the right judgement documented
in six places. The six Important items are all small and mechanical — a provenance paragraph, a
`[[maybe_unused]]`, two `.clang-tidy` lines moved or narrowed, one word deleted from
`await_ready()`, and one documentation bullet — but #1 and #2 should not ship as they stand: the
first misinforms Task B12b and every consumer migration about 79 files, and the second adds dead
public API where the repository already established the correct one-line fix one task earlier.
