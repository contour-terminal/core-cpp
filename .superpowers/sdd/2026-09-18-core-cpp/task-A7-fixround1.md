# Task A7: fix round 1 (Ruling R44)

Two reviews ran on A7: the `/code-review` skill (xhigh) over `c4a083a..7618e80`, and the spec-and-quality review in `task-A7-review.md`. This file is the combined fix list. The `/code-review` findings are quoted below with their file:line, summary and failure scenario, since that review posts no file.

Most of these are endo's bugs, imported faithfully. They are ours now: core-cpp is where they get fixed, and endo picks the fixes up in C1 when it drops its own `src/tui`.

## Fix in this round

1. **Stack buffer overflow in the assembly highlighter** — `src/core/tui/GenericSyntaxHighlighter.cpp:1579,1596,1648`.
   - `char lowerBuf[64]` is filled by `toLowerInto()`, which writes `src.size()` bytes with no bound check. The other call sites (`:1786,1857,1924,2010`) all guard with `word.size() <= buf.size()`; these three do not.
   - Reachable from any rendered text: a ```asm fence with an identifier, `%register` or `.directive` longer than 64 characters smashes the frame.
   - Fix: guard all three the way the other call sites do, or make `toLowerInto` take the buffer size and truncate. Prefer the latter, so the next call site cannot get it wrong.
   - Add a test with a >64-character assembly token.
2. **Dialog draws its frame at swapped coordinates** — `src/core/tui/Dialog.cpp:47,52,162,167,295,300`.
   - All three dialogs build `Rect { .x = startRow, .y = startCol, … }`, but `Rect::x` is the left column and `Rect::y` the top row (`Rect.hpp:47-48`). The text is placed correctly through `putString(row, col)`, so the frame and its contents end up in different places.
   - Fix all six sites, and add `Dialog_test.cpp` (the module has none) asserting the frame's position for one dialog.
3. **`~SyncGuard()` does not flush before ending synchronized output** — `src/core/tui/TerminalOutput.cpp:306`.
   - Anything composed inside the guarded region and still in `_buffer` is emitted after `CSI ?2026l`, so it lands outside the synchronized region. `Screen::flush()`'s `applyCursorShape()` is the live example.
   - `syncGuard()` flushes on the way in, so the two ends are asymmetric, and 7ab9e42 dropped "and flushes" from the destructor's doc.
   - Fix: flush, then write the end marker, in both the destructor and move-assignment. Restore the doc. Then strengthen the test (item 9) so it fails without the flush.
4. **SIGWINCH handler clobbers `errno`, and its pipe write can block** — `src/core/tui/posix/Terminal.cpp:20,23` and `posix/TerminalInput.cpp:49,53`.
   - The handler reads a plain `TerminalInput*` (not `volatile sig_atomic_t` / atomic) and calls `::write()` without saving and restoring `errno`, so a resize between a failed syscall and its `errno` check makes the mainline take the wrong branch.
   - The write end of the self-pipe is never set `O_NONBLOCK` (only the read end is), so a full pipe blocks inside the handler.
   - Fix: save and restore `errno` around the handler body, set `O_NONBLOCK` on the write end, and make the handler's pointer access signal-safe.
5. **VtParser's buffers grow without bound** — `src/core/tui/VtParser.cpp:568,619,1107`.
   - `_pasteBuf`, `_paramBuf` and `_dcsBuf` have no cap, and `timeout()` (`:444`) only resolves a bare Escape. A `ESC[200~` whose terminator never arrives grows the process forever, from untrusted bytes on stdin.
   - Fix: cap each buffer, and on overflow return to Ground and either drop or emit what was collected. Say in the code what the cap is and why. Add a test for each of the three buffers.
6. **`InputDialog::render()` can throw on a narrow terminal** — `src/core/tui/Dialog.cpp:324`.
   - `inputWidth = dialogWidth - 4` has no floor, and a negative value wraps in the `substr` argument, throwing `std::out_of_range` out of a `render()` no caller expects to throw.
   - Fix: clamp `dialogWidth` and `inputWidth` to at least zero (or to a sane minimum) before use, and test a 3-column canvas.
7. **A consumer's name is on the wire** — `src/core/tui/Buffer.cpp:263`.
   - `addHyperlink()` mints the OSC 8 id as `std::format("endo-{:x}", hash)`. AGENT.md's rule is that no consumer-specific concept enters core-cpp, and every consumer's hyperlinks would carry another project's name.
   - Fix: drop the prefix, or use a neutral one. Note it in the CHANGELOG as a behaviour change, and in provenance.
8. **Completion order depends on the standard library** — `src/core/tui/completer/Completer.cpp:15`.
   - `addProvider()` uses `std::ranges::sort`, so providers of equal priority (the default) land in a library-dependent order, and `gatherCompletions()` drops later duplicates by text. `Sixel.cpp:110-118` documents fixing exactly this class of defect.
   - Fix: `std::ranges::stable_sort`, so registration order is the tiebreak.
9. **A test that asserts nothing** — `src/core/tui/TerminalOutput_test.cpp:82`.
   - "a default-constructed SyncGuard writes nothing" constructs the guard against no output, then asserts on an unrelated capture. It passes however SyncGuard behaves.
   - Fix: give it a seam so the case can fail, or delete it and say in the neighbouring case what is and is not covered. `.agent/rules/testing.md` requires asserting what distinguishes.
   - Also make a case fail without item 3's flush.

## Deferred, with issues filed (do not fix here)

These live in `src/core/tui/runtime/` and `runtime/windows/`, which **Task B12 deletes** when the TUI runtime moves onto `core::net::EventLoop`. None of it ships before then: v0.1.0 is cut in B13, after B12. Each one gets a core-cpp issue, and B12's plan text gains a line naming it.

- core-cpp#16 **Stale timer entry after a cancelled delay (use-after-free)** — `runtime/TuiRuntime.hpp:468`. `DelayAwaiter::await_resume()` resets its stop-callback but never removes its `_timers` entry, so `fireExpiredTimers()` can call `.done()` on a destroyed frame. `WaitFdAwaiter` detaches; this does not.
- core-cpp#17 **`blockOn()` spins at 100% CPU** — `runtime/TuiRuntime.cpp:218`, `runtime/PollEventSource.cpp:32-37,72-75`. With nothing parked and no fds, `pumpOnce()` returns immediately and `blockOn()` loops unbounded. `.agent/rules/testing.md` wants every wait bounded.
- core-cpp#18 **Input awaiters arm no stop-callback** — `runtime/TuiRuntime.hpp:299`. A cancelled flow stays in the single `_inputWaiter` slot forever, and the next waiter trips `assert(!_inputWaiter)` in debug or overwrites it in release.
- core-cpp#19 **`WaitForMultipleObjects` over 64 handles** — `runtime/windows/TerminalEventSource.cpp:47`. Exceeding the limit returns `WAIT_FAILED`, which the code maps to "interrupted" and cancels the root flow. `core::net` already solves this with `detail/WaitChunking.hpp`, which B12 inherits.

Also deferred, with an issue each, because both need design rather than a patch:
- core-cpp#20 **Non-BMP input becomes invalid UTF-8 on Windows** — `windows/Win32Utf.hpp:18` encodes each UTF-16 surrogate as its own 3-byte sequence (CESU-8), and `VtParser::emitUtf8` decodes lone surrogates without validation. Typing an emoji into a console app puts invalid UTF-8 in the edit buffer. Needs surrogate-pairing state in `windows/TerminalInput.cpp` plus validation in the parser.
- core-cpp#21 **Alt+Backspace is undeliverable** — `VtParser.cpp:562` treats only 0x20..0x7E as Alt+character, so `ESC 0x7F` becomes Escape plus Backspace, and `KeyBindings.cpp:476`'s binding can never fire outside the Kitty protocol.

## From the spec-and-quality review (`task-A7-review.md`, same round)

10. **Provenance under-records the import** — `.agent/reference/provenance.md:222-385`.
    - 84 of 141 comparable files differ beyond the mechanical rewrite, and 79 of those carry a bare `-`. The differences are house-style conversions this task applied: 110 C-style loops to `std::views::iota`, 21 to `while`, 87 `find(...) != npos` to `contains(...)`, 138 added `const`, 9 deleted diagnostic pragma blocks, 12 renamed private constants, one removed `NOLINT`, two De Morgan rewrites, a deleted dead test helper, and a new test helper.
    - This matters because B12b and every consumer's delta check read this table mechanically. Treating those 79 files as re-syncable would silently revert all of it, including the `NOLINT` and pragma removals the rules require.
    - Fix: a paragraph in the table header saying every endo `src/core/tui/*` row also carries A7's house-style conversions, plus a per-file note on the substantive ones the review lists (`ImageLoader_test.cpp`, `MarkdownRenderer_test.cpp`, `Screen.cpp`, `KeyCode.hpp`, `VtParser.cpp`, `InputField.*`, `LogPanel.*`, `QuestionComponent.*`, `Spinner.*`, `MockTerminalOutput.cpp`).
11. **Dead public API added to silence a warning** — `src/core/tui/runtime/TerminalEventSource.hpp:68-72,103`.
    - `signalFd()` has no caller; it exists only so clang-cl stops reporting `-Wunused-private-field`.
    - A6 already solved this correctly one task earlier: `src/core/net/PollEventSource.hpp:52` uses `[[maybe_unused]]` on the member, which is standard C++ and neither a NOLINT nor a pragma.
    - Fix: mark `_signalFd` `[[maybe_unused]]`, delete the accessor and its Doxygen.
12. **A global tidy relaxation that belongs in the module** — `.clang-tidy:65-71,152`.
    - `readability-enum-initial-value` is disabled project-wide for one enum in one module, while `src/core/tui/.clang-tidy` already exists with `InheritParentConfig: true` and two `-check` lines.
    - Fix: move the disable and its comment into `src/core/tui/.clang-tidy`, keeping the check live for base, log, cli, platform, async, net and testing.
13. **Cognitive complexity: narrow, do not disable** — `src/core/tui/.clang-tidy:34`.
    - The comment says "the number to watch is 18", but with the check off nothing watches it, and A7's own `for`→`while` conversions can only have raised the counts (nobody measured endo's).
    - Fix: replace the disable with `readability-function-cognitive-complexity.Threshold: '210'` under `CheckOptions` in the same file, just above the largest function (`InputField::render`, 200). Then file an issue to pay the lexers down and cite it in the comment.
14. **`readability-static-accessed-through-instance` can be satisfied** — `src/core/tui/.clang-tidy:35`.
    - The check is disabled because `co_await` calls `DelayAwaiter::await_ready()` on the object, which only conflicts because the function is `static`. The MSVC ARM64 workaround needs `await_ready` to return a constant and the deadline check to live in `await_suspend` — not staticness (see the type's own comment at `runtime/TuiRuntime.hpp:433-438`).
    - Fix: drop `static` from `await_ready()`, keep the constant return, and remove the disable.
15. **The network note is stale** — `docs/getting-started/building.md:26`.
    - It still says a first configure fetches only Catch2. With TUI and images on by default it also fetches libunicode and stb, and libunicode's configure downloads `UCD.zip` from unicode.org, which is core-cpp's first fetch outside GitHub.
    - Fix: say so, and name the hosts.
16. **Minor items from the same review:**
    - `.agent/reference/provenance.md:374`: the `TuiRuntime_test.cpp` note covers the `6483abd8` delta but not the real API adaptation, `*got == 1` → `got->bytesRead() == 1` (`runtime/TuiRuntime_test.cpp:531`), which core-cpp's `SystemPipe::read` signature forced.
    - `cmake/CoreCppDependencies.cmake:738-745`: the libunicode row omits `"BUILD_SHARED_LIBS OFF"`, which endo pins. A consumer building shared would get a shared libunicode linked PUBLIC from a static `core::tui`. Pin it, or say why not.
    - `cmake/CoreCppDependencies.cmake:765-770`: the stb row is fetch-only with no `FIND_PACKAGE` and no `NO_FETCH`. The library rules want an explicit fetch-or-system classification: one sentence.
    - File an issue against `tests/cmake/check-cmake-hygiene.cmake`: the namespace-directory rule reads the first named namespace, so a leading forward-declaration block for another module's type forces a full include instead (`src/core/tui/Terminal.hpp:16`, `TerminalInput.hpp:4-5`). Let such a block through.

Item 9 above and the review's first Minor are the same finding; fix it once.

## Then

- Re-run the presets the dispatch names, plus `clang-format --check` and `mkdocs build --strict`.
- Push, watch CI, and dispatch and watch `portability.yml`.
- Append "Fix round 1" to `task-A7-report.md` with RED/GREEN for items 1, 2, 3, 5, 6 and 9.
