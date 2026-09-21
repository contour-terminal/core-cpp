# Task A9: fix round 1 (Ruling R52)

The review (`task-A9-review.md`) verdicts all 15 gate findings FIXED, and found one Critical regression the fixes introduced, three Important items and five Minor. Ruling R51's two deferred API items are in this round too, so you build and verify once.

## Critical — fix first

1. **`src/core/cli/CLI.cpp:598-604` — `--help` now hangs forever on any help text with a space before a line feed.**
   The chunk is trimmed of trailing spaces, but `i` advances by the trimmed chunk's length (`:647`), and the skip loop at `:639` skips `'\n'` but not `' '`, so the state reproduces itself and the loop appends the same bytes forever. The reviewer confirmed it against the built library: `cli::helpText()` on `"First line. \nSecond line."` ran until a 20-second timeout killed it, while the same text without the space returns instantly.
   The old code always consumed at least one character here, so this is a regression this task introduced, and no test covers it.
   - Fix it so the loop always makes progress, whatever the trimming did.
   - Add the case the reviewer used, plus a space before a line feed at the very start and end of a text, and a text of only spaces and line feeds.
   - Then look for the same shape elsewhere in the wrapper: any place where the index advances by a *derived* length rather than by what was consumed.

## Important

2. **The CHANGELOG records five breaking changes as `Fixed`.**
   `.agent/rules/library-hygiene.md:169-170` wants them under `Breaking` with a migration note, and the release already has that section. They are: `FNV`'s narrowed constraint (a source break that now also rejects `float` and `double` — state the migration: hash the members, or hash a `std::bit_cast` of the value), `Flags::operator&=`'s silent reversal (the migration prose exists, it is in the wrong section), `decodeLength()`'s and `readFileAsString()`'s changed answers, and the new `[[nodiscard]]` on `unescape()` and `readFileAsString()`, which fails a consumer building with `-Werror`.
   The text is written; only the placement is wrong.
3. **`src/core/Utils.hpp:599-603` — the new Doxygen states a contract the function does not keep.**
   It promises "empty if … could not be read", but `file_size()` at `:607` throws for a path that does not exist. Contour reads a CA PEM through this, so that is the ordinary failure. Use the `std::error_code` overload and return empty, which is what the `@return` already claims. Two lines.
4. **`src/core/log/LogSink.cpp:275-292` — do the platform split (my ruling).**
   Collapsing three copies of the branch into one was right, but the reason given for keeping an `#ifdef` was incomplete: `.agent/rules/platform.md:16-22` allows the interface to live in the module that needs it, with `posix/` and `windows/` sources in `SOURCES_POSIX`/`SOURCES_WINDOWS` — no new module edge, no dependency on `core::platform`.
   That is also what Ruling R40 does everywhere else in this repository, so `core::log` should not be the exception. Split the two functions into `src/core/log/posix/` and `src/core/log/windows/`, keep the single declaration, and leave no `#ifdef` in logic.

## Minor

5. `src/core/Base64.hpp:45-47`: the Doxygen on `LastDigitIndex` describes the sentinel (64), but the constant is 63 and the test is `<=`. Correct the comment.
6. `src/core/Utils.hpp:553-562`: `asCTypeArgument` leaves the wide-character case undefined — `static_cast<int>(ch)` for `sizeof(T) != 1` goes straight to `std::tolower`, which is defined only for `unsigned char` values and `EOF`. Nothing instantiates it that way today. Either return `EOF` for values outside `[0, UCHAR_MAX]`, or `static_assert(sizeof(T) == 1)` and give the wide types their own overload on `std::towlower`.
7. `src/core/Escape.hpp:121-126`: `\1` through `\7` used to come back as two literal characters and now open a three-digit octal run. Correct for anything `escape()` produced; say so in the CHANGELOG clause for hand-written or third-party escaped text.
8. `src/core/cli/App.cpp:299-307`: the new failure mode (if `create()` fails after the reset, logging stays on the console) is in a comment but not the CHANGELOG.
9. `src/core/Escape.hpp:24`: `escape()` changed behaviour without gaining Doxygen. Document what it now does.

## The two API items ruled earlier (R51)

10. **`Times2D::operator[]` returns the same type its `value_type` declares.** Today it returns only the inner element although `value_type` is a tuple. Align them, fix any call site, and prefer changing `operator[]` over redefining `value_type` unless the call sites say otherwise — say which you chose and why.
11. **`joinHumanReadableQuoted`'s separator stops being a deducible template parameter**, so its default is usable. `std::string_view sep = ", "` or equivalent.

Both are public API. They cost nothing before v0.1.0 and would be breaking after it, and per our unreleased-CHANGELOG rule they need no `Breaking` entry — but do record them under `Changed`.

## Then

- The presets the constraints name on Windows and WSL, including the sanitizers; `python scripts/clang-format.py --check`, `ctest -L hygiene`, `mkdocs build --strict`.
- Rebase before pushing: another agent is fixing `src/core/platform/` on the same branch. Never force-push.
- Push, watch CI and portability. The macOS `core::platform` failure is the other agent's, not yours; everything else must be green.
- Append "Fix round 1" to `task-A9-report.md`, with RED/GREEN for items 1, 3, 10 and 11.
