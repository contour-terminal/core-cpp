# Task B13a, re-review of fix round 2 (`aae25fc`)

Scope: the three items of the dispatch only — the new Important (`LanguageId::Last`'s doc), the
guard's own stated limit, and the two Minors (the streaming split, both `renames.json` notes).
Nothing else in `src/core/tui` was re-opened.

**Verdict: all three items ADDRESSED.** No new Critical or Important defect in these 121 lines.

## How this was verified

Nothing in `D:\core-cpp` was edited. All mutation ran in a private detached worktree,
`<scratchpad>/wt-b13a2`, at `aae25fc` (this round's commit), removed at the end of the session.
Build tree: `out/build/rereviewB13a2-clang-debug` (WSL Ubuntu-26.04, clang 22.1.2, `clang-debug`,
`-DCORE_CPP_WITH_IMAGES=ON`), built and run through `wsl.exe` against the worktree's `/mnt/c/...`
path. Baseline at `aae25fc`, unmodified: `core-cpp-tui-test` **4445 assertions in 1035 test cases,
all passing** — matches the report's fix-round-2 table exactly (was 3870/1033 before this round).
Every mutation below was applied, built, run, and reverted; the worktree's `git status` was clean
before removal, and `diff -q` against a saved pristine copy confirmed each revert.

## Item 1 — the third probe, and the new guard against it

**ADDRESSED.** `src/core/tui/GenericSyntaxHighlighter.hpp:74` (the `no_language_hides_above_Last`
reference) and the test itself at `src/core/tui/GenericSyntaxHighlighter_test.cpp:106-149`.

I reproduced the third probe exactly: appended `Rust` after `LanguageId::Last`
(`GenericSyntaxHighlighter.hpp:75`, `Rust,` inserted before the closing brace) and gave it a case
dispatching to a real highlighter, no table row (`GenericSyntaxHighlighter.cpp:2342a`,
`case LanguageId::Rust: return highlightCpp(line, state);`). Ran it:

```
GenericSyntaxHighlighter_test.cpp:1130: FAILED:
  CHECK( highlightLine(probe, language).first == HighlightMap(probe.size(), HighlightCategory::Default) )
with expansion:
  { 1, 1, 1, 0, 0, '\t', 0, 7, 7, 7, 7, 7, 7, 7 }
  ==
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
with message:
  LanguageId value 15
```

Build was clean (no `-Wswitch` diagnostic, since the case exists), and the full suite went red on
exactly this one case: `test cases: 1035 | 1034 passed | 1 failed`, `assertions: 4445 | 4444 passed
| 1 failed`. Both the failing value (15, one past `Last`) and the expansion match the implementer's
report verbatim.

Doc check: `GenericSyntaxHighlighter.hpp:62-74` states all three measured outcomes in proportion —
`-Wswitch` demands a case (H1), the `static_assert` fires only once the table row exists, "which is
to say once the mistake is half corrected" (H2), and `no_language_hides_above_Last` "refuses a
hidden language that *works*" (H3, scoped with "that works" rather than claiming to catch every
hidden enumerator). It does not repeat the false claim from fix round 1, and it does not claim more
than what I measured. No overclaim.

## Item 2 — the guard's own stated limit

**ADDRESSED, and real.** Comment at `GenericSyntaxHighlighter_test.cpp:125-127`: "an enumerator
above `Last` whose switch case only breaks and which no table names" is invisible to the guard.

I built exactly that shape: `Rust` appended after `Last`
(`GenericSyntaxHighlighter.hpp:75`), and `case LanguageId::Rust: break;` added beside
`case LanguageId::None: case LanguageId::Last: break;` in `highlightBuiltin()`
(`GenericSyntaxHighlighter.cpp:2342-2344`) — no table row, no name, no dispatch. This falls through
to the same `return { HighlightMap(line.size(), HighlightCategory::Default), state }` that `None`
and `Last` themselves produce. Built clean, ran the full suite:

```
All tests passed (4445 assertions in 1035 test cases)
```

Exit 0, identical to the untouched baseline. The comment is accurate, not modest: this exact shape
is undetectable by the new guard, precisely because it behaves like "no language" in every
observable way `highlightLine()`, `name()`, and the three tables expose.

## Item 3 — the two Minors

**Streaming sites — ADDRESSED, both halves confirmed.**
`src/core/tui/MarkdownRenderer.cpp:831` (the highlight call, `highlightLine(...)`) and `:855` (the
fence lookup, `detectLanguageFromFenceTag(...)`), tested by
`MarkdownRenderer_test.cpp:258-309` (`the_fence_lookup_consults_the_registry` and
`the_highlight_call_consults_the_registry`).

- Broke the lookup alone (`:855`, `_highlighters` → `nullptr`): both cases red —
  `test cases: 2 | 2 failed`, with `the_fence_lookup_consults_the_registry` failing on the
  foreground-color check and `the_highlight_call_consults_the_registry` failing on the span split
  (falls back to the unregistered/plain path since the id was never resolved).
- Reverted, then broke the highlight call alone (`:831`, `_highlighters` → `nullptr`): only
  `the_highlight_call_consults_the_registry` red — `test cases: 2 | 1 passed | 1 failed`.

That is exactly the asymmetry the report claims: the pair names which site broke (both red → the
lookup; only the second → the dispatch).

**`renames.json` — ADDRESSED.** `python -c "json.load(...)"` parses the file at `aae25fc` without
error, and `ctest -R migrate-renames` **passes** (`1/1 Test #22 ... Passed 12.25 sec`) in the
worktree's build tree, which is also the check that every removed symbol is actually gone from the
tree. Both notes now carry code a consumer can paste:

- `core::tui::LanguageId::Endo`: `registerLanguage({.name="endo", .extensions={".endo"},
  .fenceTags={"endo"}, .highlight=highlightEndoLine})`.
- `core::tui::registerEndoHighlighter`: the five-call sequence (construct the registry, register,
  `MarkdownRenderer{output, theme, &highlighters}`, `StyledText::fromMarkdown(text, width, &theme,
  &highlighters)`, `detectLanguageFromPath(path, &highlighters)`,
  `highlightLine(line, detected, state, &highlighters)`).

I checked the second note's five calls against the real signatures
(`MarkdownRenderer.hpp:69-71`, `StyledText.hpp:56-59`, `GenericSyntaxHighlighter.hpp:139-140,153-157`):
parameter order, types (`MarkdownTheme` by value at the constructor vs. `MarkdownTheme const*` at
`fromMarkdown`, hence `theme` then `&theme`) and the trailing `SyntaxHighlighterRegistry const*`
line up in every call. The `LanguageDefinition` field names in both notes
(`.name/.extensions/.fenceTags/.highlight`) match the struct at `GenericSyntaxHighlighter.hpp:199-205`.
Both notes are pasteable and correct, not just present.

## New findings

**None at Critical or Important.** I additionally checked, and found clean:

- `python scripts/clang-format.py --check` on the three touched C++ files (`GenericSyntaxHighlighter.hpp`,
  `GenericSyntaxHighlighter_test.cpp`, `MarkdownRenderer_test.cpp`): "3 file(s) are formatted with
  clang-format 22.1.8".
- `git grep -rn "a_registered_fence_tag_highlights_its_code_block"` (the old streaming test name)
  finds only the two unrelated, still-correct siblings (`MarkdownRenderer.a_registered_fence_tag_...`
  and `StyledText.fromMarkdown.a_registered_fence_tag_...`) — no dangling reference to the removed
  name.
- `renderHighlightedLine`'s call at `MarkdownRenderer.cpp:833` passes `currentTheme()`, matching
  exactly what `the_fence_lookup_consults_the_registry`'s assertion computes
  (`categoryColor(HighlightCategory::Default, currentTheme())`) — the new test's oracle is not
  independently guessed, it mirrors production's own color source.

### Nit (no action expected)

The header doc for `LanguageId::Last` states the residual gap only implicitly (by scoping its claim
to "a hidden language that *works*"); the explicit sentence — "an enumerator whose case only breaks
and which no table names" is unreachable, unnamed and inert — lives only in the test file's comment
(`GenericSyntaxHighlighter_test.cpp:125-127`), not in the header. That is a reasonable division
(the header is terse by convention, the test comment carries the measured detail) rather than a gap;
not asking for anything here.

## The extra question

**I agree with the implementer's framing.** The guard closes exactly the harmful case — a hidden
language that dispatches and works, invisible to `name()` and the three tables — and by
construction cannot see an enumerator whose switch case only `break`s: that enumerator produces
observably identical behavior to `None`/`Last` themselves (plain text, empty name, absent from
every table), so no runtime probe, however constructed, can distinguish "unreachable enumerator that
exists" from "no such enumerator." Only a source-level scan (walking the `enum class` declaration
itself, e.g. via a code-generation step, reflection once C++26 static reflection lands, or an
external tool) could enumerate the enumerators and catch that shape.

Whether that scan belongs in Task B13's gate: I would **not** require it there. The residual gap is
inert by the guard's own definition — unreachable, unnamed, contributes nothing to behavior — so it
is not a defect a user or consumer can trigger; it is a dead enumerator sitting in source, which
`clang-tidy` or a reviewer reading the three-line diff of a future PR is far better placed to catch
than a generated scan would be. The behavioural guard added this round is the right level of
investment for what the task is actually protecting (a language that silently works but is
invisible to lookup): it catches the case that would ship a defect a user could hit. Chasing full
enumerator-set closure here would be spending a source-scanning tool on a shape that, if it ever
occurred, does nothing — the same "measure what's cheap and matches the actual risk" trade-off the
rest of this task has made throughout. I'd leave it as a documented limitation (which it already is)
rather than a follow-up ticket.
