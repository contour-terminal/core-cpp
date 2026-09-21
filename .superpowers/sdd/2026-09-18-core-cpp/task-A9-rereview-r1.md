# Task A9 — re-review of fix rounds 1 and 2

Base `1556119` → head `origin/master` (`4f7fd11`). Read-only: nothing in the working tree, index,
HEAD or any branch was touched.

The prepared diff (`review-A9-fix.diff`) is **incomplete**: it omits `CHANGELOG.md` (283 insertions,
commits `bd69fda` and `d893594`), `src/core/Base64.hpp` (Minor #5) and `src/core/Utils_test.cpp`
(the missing-file and `joinHumanReadableQuoted` cases). Those four were read straight from git, so
every item below was reviewed against the real change, not the filtered file.

Every line number below is pinned to `4f7fd11`. `CHANGELOG.md` and
`tests/cmake/check-cmake-hygiene.cmake` have already moved under other agents' commits since
(`HEAD` is `73ec045` with uncommitted `net`/`platform` edits as I write); none of A9's own files
has changed.

---

### Finding Verdicts

#### Critical

**1. `--help` hangs on a space before a line feed — ADDRESSED.**
`src/core/cli/CLI.cpp:593-606` introduces `WrappedChunk` with a `consumed` field distinct from the
emitted `text`; `nextWrappedChunk()` (`:608-645`) returns `consumed = linefeed` for the line-feed
case (`:620`), so the trimmed spaces are counted as consumed even though they are not emitted, and
the caller advances by it (`src/core/cli/CLI.cpp:665`, `i += chunk.consumed`) while the cursor still
advances by `chunk.text.size()` (`:664`). Argument for termination on **every** input is below.
The ruling's "look for the same shape elsewhere in the wrapper" was also satisfiable: the only
other index loop in this file is the hyperlink scan in `stylizer()` (`CLI.cpp:544-576`), whose
`a = right` with `right >= b + 3 > a` advances by at least three per turn, and whose
`left` can never walk below `a` because `a` is always either `text.size()` or the index of a space.
Regression cases at `src/core/cli/CLI_test.cpp:259-301` (five sections: middle, very start, very
end, several in a row, nothing but spaces and line feeds). Run against the built `cl-debug` binary:
`CLI.helpText.space-before-linefeed` passes, 8 assertions, exit 0, no hang.

#### Important

**2. Five breaking changes moved to `Breaking` with migrations — ADDRESSED.**
`CHANGELOG.md:294` (`Flags::operator&=`, migration `f.disable(X::A)`), `:301` (`FNV`'s narrowed
constraint, migration "hash the members, or pass `std::bit_cast<std::array<unsigned char,
sizeof(T)>>(value)`", and it does now name `float`/`double` explicitly), `:310` (`decodeLength()`),
`:316` (`readFileAsString()`, migration "stop trimming trailing NULs / check for an empty string
instead of catching `filesystem_error`"), `:325` (`[[nodiscard]]`). Each migration is something a
consumer can act on: a concrete replacement spelling, not "review your call sites".
Nothing breaking is left under `Fixed` or `Added` — the two `Fixed` entries that still mention a
moved item explicitly point at the other section (`CHANGELOG.md:496` and `:508`), and grepping the
whole `Added` section for the five names returns nothing.

**3. `readFileAsString()`'s Doxygen contract — ADDRESSED.**
`src/core/Utils.hpp:619-641`: `std::filesystem::file_size(path, errorCode)` and `return {}` on
error. Checked the contract on every path by compiling the function standalone (g++ 15, libstdc++,
WSL) and running it over a directory, a missing path, a `chmod 000` file, a FIFO, `/dev/null` and a
`/proc` file: all answer empty, none throws. The `@return` at `:618` ("empty if it is empty or
could not be read") now holds for a missing file, a directory, and a file that exists but cannot be
opened (`file_size()` succeeds, the `ifstream` fails, `gcount()` is 0, `resize(0)`).
Case at `src/core/Utils_test.cpp:416-422`, passes.

**4. The `log/posix/` + `log/windows/` split — ADDRESSED, with one inaccurate claim (see New
Breakage).**
Both differences are split: `src/core/log/posix/TerminalQuery.cpp`,
`src/core/log/windows/TerminalQuery.cpp` (declared once in the public `LogSink.hpp:84,90`, because
`core::cli::App` asks them too) and `src/core/log/posix/ProcessId.cpp`,
`src/core/log/windows/ProcessId.cpp` (declared once in `src/core/log/detail/ProcessId.hpp:16`).
No file in `posix/` or `windows/` carries a file-wide guard of its own platform (Ruling R42).
The private header is in `SOURCES`, not `HEADERS` (`src/core/log/CMakeLists.txt:17`), so it is in
no `FILE_SET` and cannot become public API. Source lists at `:18-20`, including
`SOURCES_EMSCRIPTEN posix/ProcessId.cpp posix/TerminalQuery.cpp` — the one that would only have
failed at link time. Verified against the generated build graphs rather than trusting the lists:
`out/build/clang-debug/build.ninja` selects `src/core/log/posix/{ProcessId,TerminalQuery}.cpp`,
`out/build/cl-debug/build.ninja` selects `src\core\log\windows\{ProcessId,TerminalQuery}.cpp`, and
`out/build/emscripten/build.ninja:1411,1420` selects the two `posix/` files — with the objects
`out/build/emscripten/src/core/log/CMakeFiles/core-cpp-log.dir/posix/{ProcessId,TerminalQuery}.cpp.o`
present on disk, from a tree configured at 19:53, after `c524cac` (19:46). So Emscripten really does
compile them; this is not a list that merely looks right.
The namespace gate accepts both directories: `posix`, `windows` and `detail` are in
`CORE_CPP_HYGIENE_PRIVATE_DIRECTORIES` (`tests/cmake/check-cmake-hygiene.cmake:127`), so
`core::log` in `log/posix/` and `core::log::detail` (nested) both pass.

#### Minor

**5. `Base64.hpp`'s `LastDigitIndex` comment — ADDRESSED.** `src/core/Base64.hpp:45-47`: "The
largest index IndexMap maps a base64 digit to … Every other byte is 64, one past this." That is the
constant (63, the entry for `/`) and it justifies the `<=` at `:164` and `:197`.

**6. `asCTypeArgument`'s undefined wide-character case — ADDRESSED.** `src/core/Utils.hpp:555-580`
replaces it with `detail::toLowerChar()`/`toUpperChar()`, which pick `std::tolower`/`std::toupper`
through `unsigned char` for `sizeof(T) == 1` and `std::towlower`/`std::towupper` through
`std::wint_t` otherwise — the second of the two options the ruling offered. `<cwctype>` added at
`Utils.hpp:12`. The narrow path is byte-for-byte the behaviour the gate's fix established, so
finding #9 of the original 15 is intact. `constexpr` is dropped (the C library functions are not
constexpr); nothing used these in a constant expression.

**7. The `\1`–`\7` reading in the CHANGELOG — ADDRESSED.** `CHANGELOG.md:490-493`: "One reading did
change: `\1` through `\7` used to come back as the two literal characters and now open a
three-digit octal run. That is correct for anything `escape()` produced … hand-written or
third-party escaped text that meant a literal backslash before a digit has to spell the backslash
`\\`."

**8. `installLogging()`'s new failure mode in the CHANGELOG — ADDRESSED.** `CHANGELOG.md:464-466`.

**9. `escape()`'s Doxygen — ADDRESSED.** `src/core/Escape.hpp:24-33`. Checked against the switch at
`:36-53`: the pass-through range, the backslash and quote exceptions, the four named escapes, and
`\xHH`/`\OOO` are all as stated, and "never empty" holds on every branch.

#### R51

**10. `Times2D::operator[]` returns what `value_type` declares — ADDRESSED.**
`src/core/Times.hpp:185` adds `using value_type = iterator::value_type;` and `:195-198` returns
`{ first[i / second.size()], second[i % second.size()] }`. `operator[]` changed rather than
`value_type`, which is the direction the ruling preferred, and the reason is recorded
(`CHANGELOG.md:382-389`). There are no call sites to fix: `Times2D` is subscripted nowhere in
`src/`, and `times2D()`/`operator*` do not use it. The iterator agrees — `Times2DIterator::operator*`
(`Times.hpp:143`) yields the same `std::tuple<T1, T2>`. Case at `src/core/Times_test.cpp:81-103`
asserts the type identity (`STATIC_CHECK(std::is_same_v<decltype(grid[0]), decltype(*grid.begin())>)`)
*and* equality at every position, plus the inner-fastest order. Passes, 13 assertions.

**11. `joinHumanReadableQuoted`'s separator — ADDRESSED.** `src/core/Utils.hpp:229-230`:
`template <std::ranges::input_range Range>` / `std::string_view sep = ", "`. A call without a
separator compiles and produces `", "`, verified by running `utils.joinHumanReadableQuoted`
(`src/core/Utils_test.cpp:491-502`): `joinHumanReadableQuoted(words) == "\"one\", \"two\""` passes.
The signature is now identical in shape to `joinHumanReadable()` at `:206-207`, which it should
always have matched. `result += sep` replaces `result += std::format("{}", sep)`, which is the only
behavioural consequence and is a no-op for a string-like separator.

#### R55

**12. `escape()` gains `[[nodiscard]]` in the same Breaking entry — ADDRESSED, and wider than the
ruling named.** All six functions in `src/core/Escape.hpp` carry it: `:34`, `:57`, `:71`, `:87`,
`:92`, `:101`. The widening to `escapeMarkdown()` was confirmed by the controller. It went into the
existing Breaking entry rather than a new one (`CHANGELOG.md:325-332`), which is what the ruling
asked, and the entry enumerates the family ("`escape()` in all three of its spellings,
`escapeMarkdown()` in both of its, and `unescape()`") — an accurate count.

**13. `processId()`'s `#ifdef` gets the same platform split — ADDRESSED.** Covered under Important
#4. `LogSink.cpp` now calls `detail::processId()` at `:87` and includes `<core/log/detail/ProcessId.hpp>`
at `:5`; the removed `<cstdio>`, `<unistd.h>`, `<io.h>` and `<process.h>` are not needed by anything
left in the file (checked: no `stdout`, `stderr`, `printf`, `fileno`, `isatty` or `getpid` remains).
Provenance rows at `.agent/reference/provenance.md:100-104`.

**14. `Times::size()`/`operator[]` fixed and instantiated — ADDRESSED.** `src/core/Times.hpp:75`
(`static_cast<std::size_t>(count)`) and `:79-82` (`static_cast<T>(start + (static_cast<T>(i) * step))`).
`src/core/Times_test.cpp:108-165` instantiates both directly for both `times()` overloads — the
count-only form, start/count/step, a negative step, `std::size_t` as the value type, `std::uint8_t`
as a narrow unsigned one, subscript checked against iteration at every position, and two
`STATIC_CHECK` constant evaluations. Passes, 21 assertions.

**Would the test have caught the original defect?** Yes, at compile time, and *only* at compile
time. The old `return count;` is an `int`→`std::size_t` conversion and the old
`start + (i * step)` converts `step` to `std::size_t` and then narrows the sum back to `T`;
`-Wconversion` and `-Wsign-conversion` are on for GCC and Clang
(`cmake/CoreCppToolchain.cmake:77-78`) and every preset inherits `CORE_CPP_WERROR: ON`
(`CMakePresets.json:17`), so instantiating either member fails the build. At *runtime* the old code
would have passed every one of these sections, including the negative-step one — `10 + 2 * (size_t)(-2)`
wraps modulo 2^64 back onto 6 — so a test that only checked values would have proved nothing. The
one gap: no section instantiates a shape where `I` differs from `T` (e.g. `times(0.0, 10, 0.5)` or
`times(10, 4u, 3)`), which is where the two conversions are least alike.

---

### The Wrapper's Progress

Write `sub = text.substr(i)` for the text the chunk is taken from. Each turn of
`wordWrapped()` (`CLI.cpp:647-674`) does: skip a run of `trimChar`, take a chunk, add
`chunk.consumed` to `i`, break if `i == text.size()`.

`nextWrappedChunk()` returns one of five results, and `consumed` is never greater than `sub.size()`
in any of them, so `i` can never overshoot `text.size()` and the `==` break can never be jumped:

1. **A line feed at `sub[k]`, `k > 0`** (`:611-620`): `consumed = k >= 1`. Strict progress.
2. **A line feed at `sub[0]`**: `consumed = 0`, no progress — but `trimLeadingWhitespaces` is set
   `false`, so the *next* turn's `trimChar` is `'\n'` and `text[i] == '\n'`, so the skip loop at
   `:655-656` advances `i` by at least one. This is the only zero-progress result that does not
   break, and it cannot occur twice in a row. This is exactly the case the trimming used to produce
   with the *emitted* length; leaving the line feed itself unconsumed is what now guarantees the
   next turn moves.
3. **No line feed, whole text fits** (`:624-625` or `:632-633`): `consumed = sub.size()`, so
   `i == text.size()` and the loop breaks. Covers `sub` empty, which is the state the skip loop can
   leave behind at the end of a text.
4. **No line feed, cut at a space** (`:636-644`, `i > 0` branch): `consumed = i` where
   `1 <= i <= available - 1 < sub.size()`. Strict progress.
5. **No line feed, no space to cut at — a word longer than the line** (`:643-644`, `i == 0` branch):
   `consumed = available`, and `available >= 1` always, because
   `available = margin > cursor ? margin - cursor + 1 : 1u` (`:631`) floors at `1u`. Strict
   progress of at least one byte per turn. A margin of 0 or 1, or a cursor at or past the margin,
   lands here and emits one character per line rather than looping.

So every turn either breaks, advances `i` strictly, or is case 2 and is followed by a turn that
advances `i` strictly. `i` is monotone and bounded by `text.size()`, therefore the loop terminates
on every input.

I also checked this empirically rather than only on paper. I lifted `nextWrappedChunk()` and
`wordWrapped()` verbatim out of `CLI.cpp` at `origin/master` into a harness and ran **every** string
over `{'a', 'b', ' ', '\n'}` of length 0 to 8, against margins {0, 1, 2, 3, 5, 8, 80}, indents
{0, 1, 4} and starting cursors {0, 1, 3, 8, 100} — 9,175,005 cases with a 100,000-turn budget.
Result: `cases=9175005 failures=0`. No case exceeded the budget, none overshot the index, and in
every case every non-whitespace byte of the input survived into the output (so the fix does not
terminate by silently dropping text). That sweep contains, by construction, all five inputs the
task named: a text of only spaces and line feeds, a space before a line feed at the very start and
at the very end, a chunk trimmed to nothing, margins of zero and one, and words longer than the
margin.

The old state cannot be reproduced: it required `consumed == chunk.text.size()` with the two
differing, and `consumed` is no longer derived from the emitted text at any of the five return
sites.

One note on what changed relative to the *pre-A9 import* (`2e57ff7`), since the intermediate state
at `1556119` never shipped. The imported wrapper did not hang on this input, because
`return text.substr(0, i + 1)` kept one character where the rewrite's count-down keeps zero — but
it emitted that trailing space and then a fresh `'\n'` + indent, once per trailing space, so
`"abc   \ndef"` rendered as four indented lines. The new code renders it as two. That is a strictly
better rendering and it is covered in spirit by `CHANGELOG.md:457-458`, but see the third Minor
under New Breakage below.

---

### New Breakage in the Fix Diff

**Minor — `src/core/log/LogSink.cpp:42`: the claim "`core::log` has no `#ifdef` in its logic left"
is not true.** `appendNowStamp()` still selects `localtime_s` against `localtime_r` with
`#ifdef _WIN32`. That is one `#ifdef` branch per platform inside a module source, which
`.agent/rules/platform.md:25-27` says is split into the subdirectories rather than kept whole
("**A module's own directory holds only platform-independent code**"). The ruling named only
`processId()`, so the *fix* is complete as ruled — but three places now assert the absolute:
`CHANGELOG.md:483`, `task-A9-report.md` ("`core::log` now has no `#ifdef` in its logic") and
`.agent/reference/provenance.md:101` ("split out of `LogSink.cpp`'s **last** `#ifdef`"). Either
split the two-line local-time shim the same way, or soften all three sentences to name the one that
remains.

**Minor — `src/core/Times.hpp:79` and `:195`: two new `[[nodiscard]]` attributes that the CHANGELOG's
`[[nodiscard]]` Breaking entry does not cover.** `Times::operator[]` and `Times2D::operator[]` both
gained `[[nodiscard]]` in this round; `CHANGELOG.md:325-332` enumerates only `<core/Escape.hpp>` and
`readFileAsString()`. The practical risk is nil — neither member had ever been instantiated, which
is finding R55 #3's whole premise — but the entry reads as an exhaustive list and is not one. One
clause, or a sentence in the `Changed` entry at `:382-389`.

**Minor — the trailing-space rendering change is not in the `--help` entry.** `CHANGELOG.md:457-458`
records exactly one layout change ("a line whose text reaches exactly to the margin is no longer
broken onto a second line"). Against the imported behaviour there is a second one: a run of spaces
before a line feed used to emit one indented continuation line per space, and now emits none. A
consumer diffing rendered `--help` against contour's will see it. One clause on the same entry.

**Minor (process, not this round's fault) — the CHANGELOG is now internally inconsistent about where
a pre-release API break goes.** R51/R36 put `Times2D::operator[]` and `joinHumanReadableQuoted()`
under `Changed` (`CHANGELOG.md:382-392`) because nothing has shipped; the concurrent `core::tui`
work put its namespace rename under `Breaking` at `:257-266` explicitly *because* the preamble at
`:5-6` says "every break is listed under **Breaking** with a migration note", citing core-cpp#30.
A9 followed its ruling exactly, so this is a note for the controller, not a finding against the
implementer: two readings of the same preamble are live in one release section.

**Minor — no `TIMEOUT` on the `cli` test.** `src/core/cli/CMakeLists.txt:9` registers the binary
without one, so if this Critical regressed the case would sit at ctest's 1500-second default rather
than going red quickly. `core_cpp_add_test()` takes `TIMEOUT` (`cmake/CoreCppTargets.cmake:327-329`)
and `tests/CMakeLists.txt:87` uses it, and `.agent/rules/testing.md` asks every wait to be bounded.
A regression test whose failure mode is an infinite loop is the case that most wants one.

No Critical or Important breakage. Nothing in the 15 already-verified gate findings was undone:
I re-checked the narrow path of `toLower()`/`toUpper()` (`Utils.hpp:564-570`, unchanged in
behaviour), `readFileAsString()`'s binary read and truncation to `gcount()` (`:636-639`, preserved
above the new early return), `isStdErrTerminal()`'s Windows answer
(`log/windows/TerminalQuery.cpp:16-21`, still `::_isatty(::_fileno(stderr)) != 0`, not `true`), and
the `available` underflow guard (`CLI.cpp:631`, carried through the refactor intact).

---

### Verdict

**Fix round: All findings addressed, no new Critical/Important breakage.**

All fourteen items — the Critical, the three Important, the five Minor, R51's two and R55's three —
are ADDRESSED. Five Minor items are open, all of them documentation or test-hygiene: three
sentences that claim `core::log` has no `#ifdef` left when `LogSink.cpp:42` does; two
`[[nodiscard]]` additions in `Times.hpp` outside the CHANGELOG entry that reads as exhaustive; one
unrecorded `--help` rendering change; a CHANGELOG that now answers the "Breaking or Changed before
1.0" question two ways; and a `cli` test with no `TIMEOUT`. None of them blocks the round.

**Verification I ran** (read-only, no build, no suite run): the five new or touched cases against
the already built `cl-debug` binaries — `CLI.helpText.space-before-linefeed` (8 assertions),
`times.size_and_subscript` (21), `times2D.subscript_answers_the_same_element_iteration_does` (13),
`utils.joinHumanReadableQuoted` (4), `utils.readFileAsString` (5) — all exit 0; a 9.17-million-case
termination sweep of the lifted wrapper; a standalone probe of `readFileAsString()`'s contract over
a directory, a missing path, an unreadable file, a FIFO and `/dev/null`; and the three generated
build graphs for the platform source selection.
