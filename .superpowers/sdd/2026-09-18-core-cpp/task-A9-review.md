# Task A9 review: Phase A gate, pass 1 (base, log, cli)

Diff reviewed: `b505db8..1556119`, restricted to `src/core/{Utils,Flags,Escape,FNV,Base64}*`,
`src/core/cli/`, `src/core/log/`, `CHANGELOG.md` and `.agent/reference/provenance.md`.
Everything under `src/core/platform/` and `src/core/testing/` is the concurrent session's and was
ignored.

### Finding Verdicts

**1. `CLI.cpp:611` — unsigned underflow in `wordWrapped()`. FIXED.**
`src/core/cli/CLI.cpp:617` computes `available = margin > cursor ? margin - cursor + 1 : 1u`, and
`:618` returns the whole text when `available >= text.size()`, so the index at `:627` is provably
`< text.size()` for *every* relation between cursor and margin — margin 0, margin 1, and a cursor
far past the margin all land on `available == 1`. `:629`'s `i > 0 ? i : available` guarantees a
non-empty chunk for a word with no whitespace, so the caller's loop terminates. I re-derived the
termination argument and confirmed it by running both versions of the pair of functions:
margin 0 finishes in 39 iterations, a 40-character word at margin 30 in 5.
*Test would fail without the fix:* yes — `CLI.helpText.narrow-margin` reaches `text[~4e9]` at
margins 0/1/8/20/40 (`printOptions()` sets `cursor = columnWidth + 1`, `CLI.cpp:818`).

**2. `CLI.cpp:841` — verbatim placeholder underflows the padding. FIXED.**
`CLI.cpp:800-804` folds `placeholder.size() + 2` into `maxOptionTextSize`, so
`columnWidth = leftPadding.size() + maxOptionTextSize + 2` is strictly greater than the verbatim
row's `leftSize = leftPadding.size() + 2 + placeholder.size()` (`:855`) by construction, not by
luck; `:856` and `:812` then saturate at 1 as a second line of defence, replacing the `assert`
that NDEBUG compiled out. Safe for every relation.
*Test would fail without the fix:* yes — `CLI.helpText.verbatim-longer-than-the-options` has an
empty option list, so `columnWidth` was 8 against a `leftSize` of 44; `detailedDescription()`
(`CLI.cpp:885`) reaches `printOptions()` on `verbatim.has_value()` alone, so the path is live.

**3. `CLI.cpp:597` — `text[SIZE_MAX]` for a leading line feed. FIXED for the reported input, but
the rewrite introduces a hang — see Critical #1.**
`CLI.cpp:598-604` counts down from the line feed, so `linefeed == 0` yields an empty chunk instead
of an out-of-bounds read, and the outer loop makes progress on the next pass because
`trimLeadingWhitespaces` is false and `text[i]` is the `'\n'` it then skips. Verified by running
the pair: "leading LF" terminates in 2 iterations.
*Test would fail without the fix:* yes — `CLI.helpText.leading-linefeed` reads `text[SIZE_MAX]`.

**4. `Utils.hpp:303` — `splitKeyValuePairs()` reads past the view. FIXED.**
`src/core/Utils.hpp:306` is `text.substr(iBeg)`. The only other `string_view(ptr + n, ...)` in the
file, `Utils.hpp:291`, already carries a length (`i - iBeg`); a grep over all of `src/core/`
outside `platform/` finds no remaining length-less construction.
*Test would fail without the fix:* yes, twice — `utils.splitKeyValuePairs.bounded` section 1 fails
deterministically everywhere (the `string_view` is a 7-char prefix of a 21-char `std::string`, so
`strlen` returns the tail), and section 2 backs the view with a `std::vector<char>` of exactly 7
bytes and no NUL, which is a genuine ASan heap-buffer-overflow. Both reach the length-less
constructor, because the delimiter is absent so the loop body never runs and the whole input is
"the last segment".

**5. `Utils.cpp:32` — `threadName()` resizes by `len - 1` with `len == 0`. FIXED.**
`src/core/Utils.cpp:26-41`: `FAILED(hr)` returns before anything is allocated, there is one exit
past the acquisition, `LocalFree(pwsz)` runs on every path past it, and a conversion length of 0
skips the resize and answers empty. Correct.
*Test would fail without the fix:* no — `utils.threadName` only asserts `CHECK_NOTHROW`. The
report says so plainly (the failure needs `GetThreadDescription()` to succeed and
`WideCharToMultiByte()` to then fail, which has no seam). Accepted.

**6. `App.cpp:77` — `screenWidth()` accepts `ws_col == 0`. FIXED.**
`src/core/cli/App.cpp:79`: `ioctl(...) != -1 && ws.ws_col > 0`.
*Test would fail without the fix:* not directly (static, anonymous namespace, no seam); covered
transitively by `CLI.helpText.narrow-margin`'s margin-0 case. Reported honestly.

**7. `Flags.hpp:84` — `operator&=` cleared instead of intersecting. FIXED.**
`src/core/Flags.hpp:92-96` is now `_value &= static_cast<value_type>(flag)`, the exact compound
form of `operator&` (`:65-68`), and `:86-90` adds the `Flags` overload. No ambiguity: `f &= X::A`
is an exact match for the `FlagType` overload. `Flags_test.cpp:100-125` compares `&=` against `&`
in both spellings and `|=` against `|` in both — and `&=`/`|=` are the only compound operators the
class has, so "every compound operator against its non-compound counterpart" is satisfied.
*Test would fail without the fix:* yes — `intersected &= Fruit::Apple` on `{Apple,Banana}` yielded
`{Banana}`, and `disjoint.none()` was false.

**8. `Base64.hpp:159` — `decodeLength()`'s scan never stops. FIXED.**
`src/core/Base64.hpp:47` names the bound `LastDigitIndex = 63` and `:164` tests `<= 63` against a
table whose out-of-alphabet entry is 64 (`:23-42`), so the scan stops at `=`, at `\n` and at junk;
`decode()` at `:197` uses the same constant, so the two can no longer drift. I checked the
allocation is not merely "smaller" but still sufficient: for every prefix length N,
`decodeLength` (`((N-1+3)/4)*3`) is `>=` the byte count `decode()` actually writes
(`((N+3)/4)*3 - ((4-N%4)&3)`), including the ragged N ≡ 1,2,3 (mod 4) cases. No under-allocation.
*Test would fail without the fix:* yes — `base64.decodeLength` asserted
`decodeLength("YWJj") == decodeLength("YWJj!!!!!!!!")`, which was 3 vs 9.

**9. `Escape.hpp:35` — escape/unescape do not round-trip. FIXED.**
`Escape.hpp:37` makes the printable range `[0x20, 0x7E]` inclusive; `:133-137` reads `\"` back as
a quote; `:121-126` opens an octal run on any octal digit and `:182-191` consumes exactly three,
with `buf[3] = '\0'` before `strtoul`. I walked all 256 values in both styles by hand: the named
escapes (`\\`, `\e`, `\t`, `\r`, `\n`, `\"`) each have a reader arm, printable bytes pass through,
and every numeric form is a fixed 3-digit octal (`{:03o}`, first digit 0–3, always an octal digit)
or a fixed 2-digit hex — both round-trip. `Hex2` clears `buf[2]` (`:198`), which a preceding octal
run would otherwise have left dirty.
*Test would fail without the fix:* yes — `escape('~') == "~"`, `unescape("\\\"") == "\""` and
`unescape("\\101") == "A"` each failed, and the 256-byte round-trip failed at 0x22 and above 0x40.
The task's boundary characters 0x20, 0x7E, 0x7F are all asserted (`Escape_test.cpp:28-38`).

**10. `FNV.hpp:61` — hashes padding bytes. FIXED, by constraint.**
`src/core/FNV.hpp:57` requires `std::has_unique_object_representations_v<V>`, which is a
constraint, not a hope: a padded type no longer binds the overload at all. `:68` reads the bytes
with `std::bit_cast<std::array<unsigned char, sizeof(V)>>`, so the `constexpr` is now true rather
than decorative. Hash *values* are unchanged for every type that still qualifies (`bit_cast`
yields the same object representation the `reinterpret_cast` walked), so nothing hash-keyed moves.
*Test would fail without the fix:* yes, as a build failure — `STATIC_CHECK(!HashableBytewise<Padded>)`
(`FNV_test.cpp:41`) is a `static_assert` that the old `is_trivially_copyable_v` constraint fails,
and `FNV_test.cpp:55`'s compile-time hash could not be constant-evaluated at all before.

**11. `Utils.hpp:550` / `CLI.cpp:555` — plain `char` into `<cctype>`. FIXED.**
`Utils.hpp:556` adds `detail::asCTypeArgument`, used at `:571` and `:588`; `CLI.cpp:555` casts to
`unsigned char` at the `isalpha()` site. A grep over `src/core/` outside `platform/` shows every
`<cctype>` call site in the tree now widens through `unsigned char`. The one gap is wide character
types — see Minor #2.
*Test would fail without the fix:* no. The report says so explicitly: `utils.toLower/toUpper.non-ascii`
and `CLI.helpText.non-ascii-help-text` pass before and after on glibc and the UCRT. Honest.

**12. `Utils.hpp:582` — `readFileAsString()` trailing NULs and non-ASCII paths. FIXED.**
`Utils.hpp:603-615` takes the `std::filesystem::path` itself, opens `std::ios::binary`, and
truncates to `in.gcount()`. The new `[[nodiscard]]` is right. The new Doxygen overstates the
contract — see Important #2.
*Test would fail without the fix:* on Windows yes (CRLF shortfall as trailing NULs; `path.string()`
throws for `grüße-日本語.txt`); on POSIX the function was already correct, so the sections pass
either way there. Reported as such.

**13. `App.cpp:293` — a second `installLogging()` silently reverts to stdout. FIXED.**
`App.cpp:305` resets `_logOutput` *before* `ScopedOutput::create()`. This is the right order and
not merely a reordering for tidiness: `_logOutput` is `std::unique_ptr<ScopedOutput>` (`App.hpp:111`),
and `unique_ptr::operator=` releases the old pointee *after* adopting the new one, so the previous
`ScopedOutput`'s destructor used to restore the snapshot the *new* one had already been installed
over. The `parseLogFileSpec()` call was hoisted above the reset (`:294`), which is required —
reading the parameter after the reset would be harmless, but reading it before keeps the reset the
last thing before `create()`.
*Test would fail without the fix:* yes — `cli::App::installLogging: a second call...` asserts the
error sink differs from the pre-install sink after the second install, and that the marker lands in
`second.log`; both were false.

**14. `LogSink.cpp:277` — `isStdErrTty()` returns `true` on Windows. FIXED (behaviour), with a
structural caveat.**
`LogSink.cpp:275-292` answers both streams through `::_isatty(::_fileno(stdout|stderr))` on
Windows; `LogSink.hpp:84,90` publishes them with Doxygen and `[[nodiscard]]`; `ScopedOutput`'s
private `isStdErrTty()` is gone and `App.cpp`'s two `#ifndef _WIN32` copies (`helpStyle()`,
`customizeLogStoreOutput()`) are replaced by calls. Three copies became one. The caveat is that it
is still a compile-time branch rather than an injected implementation — see Important #3.
*Test would fail without the fix:* yes on Windows — `a redirected standard stream is not a terminal`
redirects descriptors 1 and 2 with `_dup2` (which is what `_fileno(stdout)` resolves to) and the old
code answered `true` regardless. It uses `SKIP`, not `SUCCEED`, when the redirect cannot be set up.
On POSIX under ctest the streams are already pipes, so the case is tautological there — acceptable,
since the defect was Windows-only.

**15. `App.cpp:224` — `cli::parse()` throws although its contract says `std::nullopt`. FIXED.**
The choice — document what `parse()` throws (`CLI.hpp:135-141`, `:149-152`) and catch at the two
boundaries whose contract is a `bool` (`App.cpp:233-245`) — is one of the two the gate explicitly
allowed, and the report states it and the reason (the exception is the only carrier of *which*
option was wrong). `App::run()` already caught (`App.cpp:285`). `CLI_test.cpp:239-252` pins both
throwing paths so the declaration cannot drift from the code. This is not core-cpp#13's
`std::expected` conversion, as instructed.
*Test would fail without the fix:* yes — `cli::App::reparseParameters answers false...` let a
`ParserError` and a `std::invalid_argument` escape `CHECK(!app.parseParametersForTesting(...))`.

### Strengths

- The fixes address the *class* of defect, not the reported input. Finding 1's guard is derived
  from the relation between cursor and margin rather than special-casing margin 0; finding 2 makes
  `columnWidth` correct by construction and then saturates anyway; finding 10 closes the hazard
  with a constraint that makes the bad case not compile, rather than with a runtime check.
- Finding 8's fix names the sentinel once and makes `decode()` and `decodeLength()` share it, so
  the two cannot drift again — that is what made the original bug survive the import.
- The test evidence is honest about its own limits. Findings 5, 6 and 11 are marked "RED not
  observable" with the reason, rather than dressed up with a case that would have passed anyway.
  `utils.splitKeyValuePairs.bounded` section 2 is deliberately built so the view ends at the end of
  its heap block, which is what turns it into a real ASan report rather than a lucky read.
- `Escape_test.cpp` walks all 256 byte values through both numeric styles, which is the only
  assertion that actually proves "round-trip"; a handful of spot checks would not have.
- `LogSink_test.cpp`'s `ScopedCategoryState` and `ScopedRedirect` are RAII with deleted copy/move,
  and the redirect cases `SKIP` rather than silently pass when the descriptor cannot be redirected.
- Provenance is complete: every diverging file carries a "post-import fix (Task A9)" note naming
  what changed, and the three new test files have `origin: core-cpp` rows.
- The deferred items (`Times2D::operator[]`, `joinHumanReadableQuoted`) are reported with the
  one-line repair spelled out, not silently dropped — I confirmed both are still as described
  (`Times.hpp:177`, `Utils.hpp:223-224`).

### Issues

#### Critical

**1. `src/core/cli/CLI.cpp:598-604` — the finding-3 rewrite makes `--help` hang forever on any help
text with a space before a line feed.**

The linefeed branch now returns `text.substr(0, end)` after trimming trailing spaces *off the
chunk*, but the caller advances `i` by the chunk's length only (`CLI.cpp:647`), so the trimmed
spaces stay in front of `i`. On the next pass `trimLeadingWhitespaces` is false, so the skip loop
at `:639-641` skips `'\n'` and not `' '` — `i` does not move, the inner call sees `"   \n…"`, trims
back to `end == 0`, and returns an empty chunk. That state reproduces itself exactly, forever,
appending `'\n' + spaces(indent)` (23 bytes here) every iteration.

The old code could not do this: `return text.substr(0, i + 1)` with the `i > 0` guard always
yielded at least one character from this branch, so it made progress (badly formatted, but
terminating). This is a regression, not a pre-existing bug.

Confirmed against the built library, not just by reading. Linking a 20-line program against
`out/build/clang-debug/src/core/cli/libcore-cpp-cli.a` and calling
`cli::helpText(cmd, plainStyle(), 80)` on a single option whose `helpText` is
`"First line. \nSecond line."` (one space before the newline):

```
START
CALLING
real  0m20.262s        <- killed by `timeout 20`; "RETURNED" never printed
```

The same program with `"First line.\nSecond line."` returns immediately. A standalone copy of the
two functions confirms the old pair terminates in 5 iterations on the same input and the new pair
does not terminate in 200.

*Why it matters:* help text is human-written prose in consumers' sources; a space before a newline
is invisible and common. contour, endo and tuidu all draw `--help` through this function. The
failure is a spinning process with unbounded memory growth, not a misformatted line, and no test
covers it — `CLI.helpText.leading-linefeed` uses `"\nIts help text…"`, which has no trailing space
and so escapes it.

*How to fix:* the chunk's length and the input it consumed are two different numbers, and the code
conflates them. Either have the inner function report the consumed length separately (return the
chunk plus a count, so `i` advances past the trimmed spaces and the line feed), or — smaller —
have the outer loop skip trailing spaces as well as the line feed when `trimLeadingWhitespaces` is
false:

```cpp
// CLI.cpp:639-641
if (trimLeadingWhitespaces)
    while (i < text.size() && text[i] == ' ') ++i;
else
{
    while (i < text.size() && text[i] == ' ') ++i;   // the spaces the chunk was trimmed of
    if (i < text.size() && text[i] == '\n') ++i;     // and the line feed itself
}
```

Add a case with a trailing space before the newline to `CLI.helpText.leading-linefeed` (or a new
`CLI.helpText.trailing-space-before-linefeed`), and — since the failure is a hang rather than a
wrong answer — assert termination by running it in the normal suite, where a hang shows as a ctest
timeout.

#### Important

**1. `CHANGELOG.md:234-315` — four breaking changes are recorded under `Fixed`, not `Breaking`.**
`.agent/rules/library-hygiene.md:169-170` says "a breaking one goes under **Breaking** with a
migration note", and the release already has a `### Breaking` section (used for the `core::platform`
`bool` → `enum class` changes). Sitting under `Fixed` instead:
- **`core::FNV`'s byte-wise overload is a source break.** `has_unique_object_representations_v` is
  strictly narrower than `is_trivially_copyable_v`: besides padded structs it also rejects `float`
  and `double`. Code that compiled before now does not, with a constraint-failure diagnostic.
  Migration ("hash the members, or hash a `std::bit_cast` of the value") is not stated.
- **`core::Flags::operator&=` reverses its meaning silently** — it still compiles, and computes the
  complement of what it used to. The migration note *is* present in the prose ("a consumer that
  relied on the old spelling wants `disable()`"), just in the wrong section.
- **`core::base64::decodeLength()` returns a different number** for any input with padding or
  trailing bytes, and **`core::readFileAsString()` returns different bytes** on Windows. Both are
  the point of the fix, and both are things a consumer's own tests will notice.
- `[[nodiscard]]` was added to `core::unescape()` (`Escape.hpp:91`) and `core::readFileAsString()`
  (`Utils.hpp:603`); a consumer that discards either result and builds with `-Werror` now fails.

*How to fix:* move these five into `### Breaking` with a one-line migration each. The text is
already written; only the placement is wrong.

**2. `src/core/Utils.hpp:599-603` — the new Doxygen on `readFileAsString()` states a contract the
function does not keep.** It says "empty if it is empty or **could not be read**", but
`std::filesystem::file_size(path)` at `:607` throws `std::filesystem_error` for a path that does
not exist or cannot be stat'ed. The function is called on caller-supplied paths (contour reads a CA
PEM through it), so this is the ordinary failure, not an exotic one. Given that this task's theme
is making documented contracts true (finding 15), documenting a false one is the wrong direction.
*How to fix:* either use the `std::error_code` overload of `file_size()` and return empty on error,
matching what the comment promises, or amend the comment to say it throws `std::filesystem_error`
when the file cannot be opened. The first is two lines and is what the `@return` already claims.

**3. `src/core/log/LogSink.cpp:275-292` — the platform branch is a compile-time `#ifdef`, and the
report's stated reason for that is incomplete.** The report says the query "cannot move into
`core::platform`, because `platform` depends on `log` and the module table forbids the reverse
edge" — that part is true (`AGENT.md`'s module table). But `.agent/rules/platform.md:16-22` puts
the interface "in `src/core/platform/`, **or in the module that needs it**", implemented in the
module's own `posix/` and `windows/` subdirectories listed in `SOURCES_POSIX`/`SOURCES_WINDOWS`.
That shape was available to `core::log` without any new module edge and was not considered.
The outcome is a clear improvement — three copies of the branch became one, and no *logic* now
tests the platform — so this may well be the right trade. But the gate asked specifically for the
seam, so the decision should be a recorded ruling rather than an unexamined "cannot".
*How to fix:* either split the two functions into `src/core/log/posix/Terminal.cpp` and
`src/core/log/windows/Terminal.cpp`, or record the deviation (and the reason: two lines of
primitive, no state, nothing to inject) in `.agent/reference/provenance.md`'s row for `LogSink.cpp`
and let the controller rule.

#### Minor

**1. `src/core/Base64.hpp:45-47` — the Doxygen on `LastDigitIndex` describes 64, but the value is
63.** "What IndexMap holds for a byte that is not a base64 digit: one past the last index a digit
maps to" is a description of the *sentinel*; the constant is the last digit index, and the test is
`<=`. The code is right, the comment contradicts both the name and the value.
*Fix:* "The last index a base64 digit maps to; `IndexMap` stores 64 for every byte outside the
alphabet, so `entry <= LastDigitIndex` tests membership."

**2. `src/core/Utils.hpp:553-562` — `asCTypeArgument` leaves the wide-character case undefined.**
For `sizeof(T) != 1` it does `static_cast<int>(ch)` and hands that straight to `std::tolower`,
which is defined only for the values of `unsigned char` and `EOF`. `toLower<char32_t>` or
`toLower<wchar_t>` is therefore still UB for anything above 0xFF — the comment even says it "leaves
wider character types alone", which is the bug restated. Nothing in the tree instantiates them that
way today (the only call site is `CLI.hpp:224`, on `char`), so this is latent.
*Fix:* for the wider case either return `EOF` (leaving the character unchanged) for values outside
`[0, UCHAR_MAX]`, or `static_assert(sizeof(T) == 1)` and give the wide types their own overload
built on `std::towlower`.

**3. `src/core/Escape.hpp:121-126` — the octal reader's new behaviour for non-`escape()` input is
not recorded.** `\1` through `\7` used to fall into the `default:` arm and come back as the literal
two characters; they now open a three-digit octal run and swallow the next two bytes. That is
correct for anything `escape()` produced, and the CHANGELOG says the `\0dd` form still reads
identically — but it does not say that `\1`…`\7` in hand-written or third-party escaped text now
read differently.
*Fix:* one clause in the existing CHANGELOG entry.

**4. `src/core/cli/App.cpp:299-307` — the new failure mode is in a code comment but not the
CHANGELOG.** If `ScopedOutput::create()` fails after the reset, logging is left on the console
rather than on whatever was installed before. The comment explains and justifies it; the CHANGELOG
entry for finding 13 does not mention it.

**5. `src/core/Escape.hpp:24` — `escape()` gained a behaviour change without gaining Doxygen.**
`unescape()` was given a `///` block and `[[nodiscard]]`; `escape()`, whose printable range
changed, got only an implementation comment. Both are public API in the same header, and the
`[[nodiscard]]` is as warranted on one as on the other.

**6. `src/core/cli/CLI.cpp:617-618` — an undocumented layout change.** The new
`if (available >= text.size()) return text;` is an early return the old code did not have, and it
corrects an off-by-one: text that exactly reaches the margin is no longer broken onto a second
line. This is an improvement, but it changes rendered `--help` output for line lengths that land
exactly on the margin, and neither the CHANGELOG nor the code comment says so.

### Assessment

**Task quality:** Needs fixes.

Fourteen of the fifteen findings are fixed properly — several of them at the level of the defect
class rather than the reported input, with the sentinel constant in `Base64.hpp` and the
`has_unique_object_representations_v` constraint in `FNV.hpp` being the sort of fix that stops the
bug recurring — and the test evidence is unusually honest about which REDs it could and could not
reproduce. But finding 3's rewrite traded an out-of-bounds read for a non-terminating loop that a
single space before a newline in any help text triggers, which is a worse failure than the one it
replaced and is reproducible against the built library; that must be fixed and covered before this
lands.
