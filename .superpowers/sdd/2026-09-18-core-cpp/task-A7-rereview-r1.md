# Task A7 — re-review of fix round 1 (and 1b)

Base `7618e80`, head `dc9f63f`, 13 fix commits (`cb68bdc..dc9f63f`) plus `63555d8`.
Read: `review-A7-fix1.diff` in full, plus the post-fix state of the files each finding names.
Read-only; no tree, index, HEAD or branch was touched.

### Finding Verdicts

**1. Stack buffer overflow in the assembly highlighter — ADDRESSED.**
The bound moved into the helper, which is what the finding preferred.
`GenericSyntaxHighlighter.cpp:416` is now
`[[nodiscard]] auto toLowerInto(std::string_view src, std::span<char> buf) -> std::string_view`
with `if (src.size() > buf.size()) return src;` as its first statement. All seven call sites
pass a `std::array` that converts to the span — the three that were unguarded
(`:1588-1589`, `:1605-1606`, `:1657-1658`, each now `std::array<char, 64> lowerBuf {}`) and the
four that guarded themselves (`:1796`, `:1867`, `:1934`, `:2020`), whose
`word.size() <= buf.size() ? toLowerInto(word, buf.data()) : word` ternary is gone with no change
of behaviour — the helper returns `src` in exactly that case. `grep "char [a-zA-Z_]*\[[0-9]"` over
`src/core/tui/` returns nothing, so no raw stack array is left for a call site to overrun. No token
of any length can reach a write past the end.

**2. Dialog frame at swapped coordinates — ADDRESSED.**
All six `Rect`s are `{ .x = <column>, .y = <row> }`: `Dialog.cpp:48` and `:53` (SelectDialog box and
list), `:151` and `:156` (ConfirmDialog box and interior), `:296` and `:301` (InputDialog box and
interior). Each of the three carries a comment naming the `Rect::x`-is-column /
`putString(row, col)` mismatch that caused it.

**3. `~SyncGuard()` flushes before the end marker — ADDRESSED.**
`TerminalOutput.cpp:309-315`: `_output->flush();` then `writeToDestination(EndSynchronizedOutput)`.
Move-assignment does the same at `:326-330`. Symmetric with the entry side, which flushes in
`TerminalOutput::syncGuard()` (`TerminalOutput.cpp:253-257`). The destructor's Doxygen is back to
"Flushes the bracketed output and ends synchronized output mode" (`TerminalOutput.hpp:100`). See
New Breakage for an inaccuracy the new *class-level* doc introduced about the constructor.

**4. SIGWINCH handler — ADDRESSED.**
`posix/Terminal.cpp:36-38` makes the file-scope pointer `std::atomic<TerminalInput*>` with a
`static_assert(is_always_lock_free)`; `:40-51` saves `errno` on entry and restores it on exit;
`:80` and `:122` are `store(..., memory_order_release)`. `posix/TerminalInput.cpp:51-57` sets
`O_NONBLOCK` on both ends of the self-pipe. Checked the write path for a new spin: `safeWrite`
(`posix/PosixIO.hpp:17-33`) retries only on `EINTR` and returns `-1` on `EAGAIN`, so a full pipe
now fails fast inside the handler instead of blocking, and the restored `errno` hides the `EAGAIN`
from the interrupted mainline. No test, by design (report concern 4); the fix list did not ask for
one and a signal race is not a bounded wait.

**5. VtParser's three buffers — ADDRESSED.**
`VtParser.hpp:29-51` adds `MaxPasteLength` (4 MiB), `MaxCsiParamLength` (256) and `MaxDcsLength`
(64 KiB), each with its reasoning; `:79-89` adds the private `abandonIfOverlong()`, implemented at
`VtParser.cpp:458-468` (clear, `shrink_to_fit`, `_state = Ground`).
Every append site is capped — I enumerated them rather than trusting the diff:
`_paramBuf` at `:587` → `:589` and `:606` → `:607`; `_dcsBuf` at `:1109` → `:1110`, `:1117` →
`:1119`, and `:1140` → `:1153` (after the ST check, which returns first); `_pasteBuf` at `:636` →
`:657`. There is no fourth append to any of the three. Recovery is to Ground on every path, so the
next byte is ordinary input; a paste emits what it collected, a CSI and a DCS drop it. A legitimate
large paste works: the cap fires only at `size > MaxPasteLength`, and the test feeds
`MaxPasteLength + 1`. Boundary wart at exactly the cap noted under New Breakage.

**6. `InputDialog::render()` width clamp — ADDRESSED, and applied at the source rather than at the
throw.** `Dialog.cpp:282` clamps `dialogWidth` itself (`std::max(0, std::min(...))`), so every
derived expression — `innerRect`'s `dialogWidth - 2`, `startCol`, `contentCol` — is computed from
the clamped value; `:309` clamps `inputWidth` too. `grep substr\|resize\|cmp_greater` over
`Dialog.cpp` finds exactly one throwing expression, `:328-329`, and it is behind the clamped
`inputWidth`. Negative widths that still reach a `Rect` are absorbed by `Canvas::fill`
(`Canvas.cpp:59-61`, early return on `clipped.empty()`) and `Canvas::drawBox` (`:80-82`, returns
below 2×2). Both `SelectDialog` and `ConfirmDialog` got the same clamp (`:39`, `:149`).

**7. Consumer's name on the wire — ADDRESSED.**
`Buffer.cpp:264` is `.linkId = std::format("{:x}", hash)`. CHANGELOG under Fixed names the
behaviour change with before/after bytes (`1f2e` where it was `endo-1f2e`); provenance row for
`Buffer.cpp` records it; `TerminalProtocols_test.cpp:36` asserts the neutral id.

**8. Completion order — ADDRESSED.** `completer/Completer.cpp:16-20`, `std::ranges::stable_sort`,
with the reason (equal default priorities, `gatherCompletions()` dropping later duplicates) in the
comment. CHANGELOG entry present.

**9. A test that asserts nothing — ADDRESSED, and the replacement can fail.**
`a default-constructed SyncGuard writes nothing` is deleted. The surviving moved-from case
(`TerminalOutput_test.cpp:99-118`) now drives a real `CapturingOutput`, asserts
`"\033[?2026h"` after the move and `"\033[?2026h\033[?2026l"` at scope exit — a moved-from
destructor that wrote its own end sequence produces a second `\033[?2026l` and fails it — and says
in a comment what a guard holding no output can and cannot be distinguished by. Two new cases
(`:68-79`, `:81-94`) fail without item 3's flush, which the report's RED transcript confirms
(`"[?2026h[?2026l" == "[?2026hinside[?2026l"`). I traced the move-assignment case: it uses two
distinct outputs precisely so the right-hand side's own entry flush cannot be what empties `first`,
which is the correct seam.

**Do the tests distinguish?** Items 2/6, 3/9 and 5: yes, and the report's RED transcripts match the
code I read (the `__pos (which is 10) > this->size() (which is 5)` is exactly
`5 - static_cast<std::size_t>(-5)`). Item 1: the case fails under `clang-asan-ubsan` only — without
a sanitizer the old code writes 100 bytes into 64 and still returns a view that matches no keyword,
so the assertion passes. The test says so in its own comment and the report says so; the dispatch
requires `clang-asan-ubsan` green and CI runs it, so the guard is exercised. Noted, not held open.

**10. Provenance under-records the import — ADDRESSED, and specific enough.**
`.agent/reference/provenance.md:15-25` adds two paragraphs. The first states that a `-` does not
mean byte-identical and enumerates A7's conversions with counts (110 `for`→`views::iota`, 21
→`while`, 87 `contains()`, 138 `const`, 9 pragma blocks, 1 `NOLINT`, 12 renamed constants, 2 De
Morgan, one helper deleted and one added) and says 84 of 141 differ, 79 with a bare `-`. The second
is the one the finding was actually for: *"a row is not a licence to re-sync by overwriting …
taking a `-` for 'replaceable with upstream' would revert all of the above silently, including the
`NOLINT` and the pragma blocks the rules forbid. Re-syncing a file means merging the upstream delta
into what is here, never replacing it."* That names B12b and the consumer delta checks explicitly.
All ten files the review listed have per-file notes: `ImageLoader_test.cpp`,
`MarkdownRenderer_test.cpp`, `Screen.cpp`, `KeyCode.hpp`, `VtParser.cpp`, `InputField.{cpp,hpp}`,
`LogPanel.{cpp,hpp}`, `QuestionComponent.{cpp,hpp}`, `Spinner.{cpp,hpp}`, `MockTerminalOutput.cpp`
— plus a row for every file this fix round changed.

**11. Dead public API — ADDRESSED.** `runtime/TerminalEventSource.hpp:100` is
`[[maybe_unused]] core::platform::NativeHandle _signalFd;` with a comment pointing at
`core::net::PollEventSource`'s precedent; the accessor and its Doxygen are gone. `grep signalFd`
over `src/` leaves only the constructor parameter (`:40`, `:44-45`), the POSIX reader
(`runtime/posix/TerminalEventSource.cpp:55,58`) and the unrelated
`core::platform::SignalHandler::signalFd()`. No public API left behind.

**12. `readability-enum-initial-value` scope — ADDRESSED.** Removed from the root `Checks:`
(`.clang-tidy`, the `-readability-enum-initial-value` line is gone) and from the root's disable
commentary; added to `src/core/tui/.clang-tidy`'s `Checks:` with the full `KeyCode` reasoning
carried over. Live again for base, log, cli, platform, async, net and testing.

**13. Cognitive complexity thresholded, and proved live — ADDRESSED.**
`src/core/tui/.clang-tidy` now carries
`CheckOptions: readability-function-cognitive-complexity.Threshold: '210'` and no disable; the root
keeps `Threshold: '50'` (`.clang-tidy:179`), and `InheritParentConfig: true` merges rather than
replaces, so the rest of the root's options survive. The proof the gate reports is in the report
(`--config` at 10 fires four functions in `InputField.cpp`, the largest at 200; the project config
is silent): a check that fires when asked and is silent at 210 with the largest function at 200 is
measuring, not disabled. core-cpp#22 is filed, OPEN, and cited in the file.

**14. `readability-static-accessed-through-instance` satisfied, hazard intact — ADDRESSED.**
`runtime/TuiRuntime.hpp:453` is `[[nodiscard]] constexpr bool await_ready() const noexcept
{ return false; }` — `static` dropped, the constant kept, and **no clock read**: the
`_deadline <= _runtime.clock().now()` test is still in `await_suspend` (`:459-460`). The MSVC ARM64
hazard the type's own comment describes is therefore unchanged. The disable is gone from
`src/core/tui/.clang-tidy`. The `static_assert` that only compiled while the function was static is
replaced by `TuiRuntime_test.cpp:340-352`, which builds the awaiter against a frozen `ManualClock`
with an already-elapsed deadline and asserts `CHECK_FALSE(elapsed.await_ready())` — it fails if
anyone puts the clock read back, so it asserts more than the `static_assert` did about behaviour
(slightly less about constant-evaluability; `constexpr` on the declaration covers that).

**15. Stale network note — ADDRESSED.** `docs/getting-started/building.md:26-32` names Catch2,
libunicode and stb, the hosts `github.com` / `codeload.github.com`, and libunicode's `UCD.zip` from
`www.unicode.org` as the one fetch outside GitHub, plus the `-DCORE_CPP_WITH_TUI=OFF` escape.

**16. Minor items — all four ADDRESSED.** The `TuiRuntime_test.cpp` provenance row now records
`got->bytesRead() == 1` at `:531` and why (`provenance.md:281`); `CoreCppDependencies.cmake:207-210,
221` pins `"BUILD_SHARED_LIBS OFF"` with the static-core-cpp/shared-libunicode reasoning;
`:229-235` gives stb its fetch-or-system classification (fetch-only, no `FIND_PACKAGE` because stb
ships no config package or version, `NO_FETCH` unset, a parent's pre-defined `stb_image` target is
the escape); core-cpp#23 is filed, OPEN, and the limitation is written into
`tests/cmake/check-cmake-hygiene.cmake:103-108` next to the rule.

**R45 (the other consumer-named strings) — ADDRESSED for the four sites.**
`GenericSyntaxHighlighter.cpp:2316-2323`: the `.endo-format` row is gone from
`FilenameLanguageTable`, and the table's Doxygen now states the admission rule ("a row belongs here
only when the name is well known beyond any one project"). `Theme.cpp:128-131`: the gradient pair is
described as blue → teal. `completer/FuzzyMatch.cpp:175-177`: the worked example is `editor.exe`.
`Theme.hpp:186-188`: the Doxygen names "a host application's own highlighter" instead of
`endo::TokenCategory`. `grep -i endo` over `src/core/tui/**.{cpp,hpp}` leaves only the four
deliberate API/data survivors and comments that describe the import itself.
The deletion is in CHANGELOG under Fixed with its consumer impact spelled out
(`detectLanguageFromPath(".endo-format")` now answers `LanguageId::None`; a consumer passes the
language to `highlightLine()` itself), and `GenericSyntaxHighlighter_test.cpp:155-157` asserts the
new answer. Nothing else changed behaviour silently: every behaviour change in the diff (OSC 8 id,
stable sort, the three caps, the SyncGuard flush, the dialog geometry and clamps, `.endo-format`)
has a CHANGELOG bullet under Fixed.

**Deferrals untouched.** `git diff --stat 7618e80..dc9f63f -- src/core/tui/runtime/` is three files:
`TerminalEventSource.hpp` (item 11), `TuiRuntime.hpp` (item 14) and `TuiRuntime_test.cpp`
(item 14). I read the four deferred defects in their post-fix state and all four are still there:
`DelayAwaiter::await_resume()` resets `_cancelReg` and leaves the `_timers` entry
(`TuiRuntime.hpp:473-478`, core-cpp#16); `pumpOnce()`'s `if (!hasParked) return;`
(`TuiRuntime.cpp:218-220`, core-cpp#17); `NextInputEventAwaiter::await_suspend` arms no
stop-callback (`TuiRuntime.hpp:299-310`, core-cpp#18); `WaitForMultipleObjects` over the raw handle
count with `WAIT_FAILED` mapped to interrupted (`runtime/windows/TerminalEventSource.cpp:46-55`,
core-cpp#19). `runtime/windows/` has no change at all. The fix round did not wander.

### New Breakage in the Fix Diff

**Minor — `src/core/tui/TerminalOutput.hpp:84-86`: the new class-level doc claims a flush the
constructor does not perform.** The restored text reads "The constructor flushes what was composed
before it and writes the begin sequence; the destructor flushes what was composed inside the region
and writes the end sequence. Both ends flush". The entry-side flush lives in
`TerminalOutput::syncGuard()` (`TerminalOutput.cpp:255`), not in
`SyncGuard::SyncGuard(TerminalOutput&)` (`TerminalOutput.cpp:305-308`), which only writes the begin
sequence. The explicit constructor is public and its own `@brief` (`TerminalOutput.hpp:94-95`)
still says only "Begins synchronized output mode on @p output." A caller who writes
`auto guard = SyncGuard { output };` on the strength of the class doc gets bytes composed *before*
the guard emitted *inside* the region. Harmless to rendering, wrong in a public header, and
introduced by this fix round. One sentence — attribute the entry flush to `syncGuard()`.

**Minor — `src/core/tui/VtParser.cpp:657` (and `:1153`): the cap does not allow for the terminator
in flight, so the usable length is the cap minus the terminator.** In `processPaste` the
`ends_with(PasteEnd)` test runs before the cap test, but the buffer has already grown. A paste whose
content is exactly `MaxPasteLength` bytes reaches `MaxPasteLength + 1` when the terminator's `ESC`
arrives; `ends_with` cannot match on one byte, so the cap fires, the stray `0x1B` is emitted as part
of the user's text and the remaining `[201~` is delivered from Ground as five key events. `_dcsBuf`
has the mirror case two bytes below `MaxDcsLength` (the ST check needs `ESC` and `\` both present).
Clean behaviour therefore ends at cap − 6 and cap − 2 respectively, not at the cap. At 4 MiB and
64 KiB this is unreachable in practice and the documented contract ("past the cap the paste ends
here") is not violated, but the constants are public and their Doxygen implies the cap is the
usable length. Either subtract the terminator from the comparison or say so in the constant's doc.

Nothing else: no released public signature changed (`TerminalEventSource::signalFd()` and
`DelayAwaiter::await_ready()`'s staticness were both introduced by A7 itself, still under
`[Unreleased]`), no test was weakened — the one deleted case is replaced by two that fail without
the fix plus a strengthened third — and every behaviour change has a CHANGELOG bullet.

### Out-of-Scope Observations

- **clang-tidy scopes by the main file, not the header.** Moving
  `-readability-enum-initial-value` into `src/core/tui/.clang-tidy` covers `KeyCode.hpp` only while
  every translation unit that includes it lives under `src/core/tui/`. `grep -rl
  "core/tui/KeyCode.hpp"` outside that directory returns nothing today (and `HeaderFilterRegex` is
  `src/core/`), so CI is green; the first `core::` TU elsewhere that includes it will re-surface the
  finding under the root config. Latent, not a defect in the fix — item 12 asked for exactly this.
- **`MockTerminalOutput::syncGuard()` still returns `{}`**, a no-op guard, so item 3's flush has no
  effect in the renderer tests (implementer's concern 6). Pre-existing at import and recorded in
  provenance; it is why the new cases had to be written against `CapturingOutput`.
- **The remaining endo-named API survives by design, and is on the record.**
  `LanguageId::Endo`, `registerEndoHighlighter()` (`GenericSyntaxHighlighter.hpp:53,146`) and the
  `.endo` / `endo` token rows (`:231`, `:305`) are unchanged; the implementer stopped because
  renaming is an API break and giving a host a token-registration seam is design, and said so in
  the CHANGELOG bullet rather than only in the report. If R45 is meant to reach them, it needs a
  ruling — the `.endo` extension and the `endo` fence tag arguably fall under its "a format a
  consumer already emits or reads" exemption; `LanguageId::Endo` and `registerEndoHighlighter()`
  are names, not bytes, and do not.
- **Test-suite cost.** The tui binary goes 9.3 s → 15.4 s under ASan and 11.4 s under TSan, from
  feeding the real 4 MiB `MaxPasteLength`. That is the price of exercising the shipped constant
  rather than an injected smaller one; worth knowing before the suite grows again.
- **A green obtained by re-running a failed job.** `windows (cl-release-tls)` failed on run
  35497139755 and passed on `gh run rerun --failed`. The diagnosis in the report (two concurrent
  link steps invoking the one unpacked `vcpkg.exe` for `z-applocal`, "Access is denied", milliseconds
  apart) is consistent with the quoted log and with `04ffa19` being a comment-only change. It is a
  workflow race, not this change; if it recurs it wants an issue against the job.

### Verdict

**Fix round: All findings addressed, no new Critical/Important breakage.**

Items 1–16 and R45 are all ADDRESSED, the memory-safety fixes hold on every path I traced, the four
deferred defects (core-cpp#16–#19) are untouched, and the tests for items 1, 2/6, 3/9 and 5 fail
without their fixes. Two Minor items in the fix diff — a class-level Doxygen sentence that credits
the `SyncGuard` constructor with a flush that `syncGuard()` performs, and a cap boundary that
excludes the in-flight terminator — are worth a follow-up commit but do not hold the round open.
