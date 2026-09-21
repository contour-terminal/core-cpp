# Task B13a: `core::tui` stops shipping endo's language (core-cpp#24)

Pulled out of Task B13 and dispatched early, because it touches only `src/core/tui/` — free right
now — while the rest of Phase B rewrites `core::net` and `core::async`. It must land **before the
v0.1.0 tag**: it removes public API, and B13's gate will check that no consumer-specific language
remains.

Read core-cpp#24 (`gh issue view 24 -R contour-terminal/core-cpp`) first. It is the requirement.

## What is there

`src/core/tui/GenericSyntaxHighlighter.{hpp,cpp}` carries one consumer's language in four places:

| Where | What |
|---|---|
| `GenericSyntaxHighlighter.hpp:53` | `LanguageId::Endo` — public API, a closed `enum class` |
| `GenericSyntaxHighlighter.hpp:146`, `.cpp:2346` | `registerEndoHighlighter(HighlightFunction)` — public API, and already process-wide mutable state |
| `GenericSyntaxHighlighter.hpp:231` | the `.endo` row in the extension table |
| `GenericSyntaxHighlighter.hpp:305` | the `endo` row in the Markdown fence-tag table |
| `GenericSyntaxHighlighter.cpp:2372` | the `case LanguageId::Endo:` dispatch |

Task A7's fix round removed the other four endo-named strings; these four survived because they
are API and `constexpr` table data with no registration seam. That seam is what you build.

## What to build

A consumer teaches `core::tui` its own language, instead of core-cpp shipping it:

- A way to register a highlighter under a language **name**, together with the file extensions and
  Markdown fence tags that select it.
- The built-in languages keep working — either through the same seam, or unchanged beside it. Say
  which you chose and why.
- `detectLanguageFromPath()` and the Markdown fence lookup consult registered rows as well as the
  built-in table.
- `LanguageId::Endo` and `registerEndoHighlighter()` are gone.

Three binding constraints, from `AGENT.md` and `.agent/rules/design-principles.md`:

1. **No consumer-specific concept enters core-cpp.** After this task, `git grep -i endo
   src/core/tui` finds nothing but history. That includes comments that use endo as the example.
2. **Inject every ambient resource; a constructed object is usable.**
   `registerEndoHighlighter()` is process-wide mutable state today, and the seam must not add
   more of it. If the existing call sites (`Buffer.cpp`, the Markdown renderer, `Terminal.cpp`)
   cannot reach an injected registry without a wider change, say exactly what stands in the way
   and what the bounded alternative costs — do not quietly install a second global.
3. **`enum class` over `bool`, and behaviour is a table.** A registered language needs an identity
   the closed `LanguageId` enum cannot give it. Pick one — a `Custom` discriminator plus an index,
   a `std::string_view` name replacing the enum at the seam, a reserved id range handed out by the
   registry — and record the choice with its cost. This is public API and free to get wrong only
   until v0.1.0.

## Tests, first

- A registered language highlights: register a toy highlighter under a name, with one extension
  and one fence tag, and assert all three paths select it — `detectLanguageFromPath("x.toy")`, a
  ```` ```toy ```` fence in the Markdown renderer, and a direct highlight call.
- A **second** registration does not clobber the first, and re-registering the same name is
  defined (replace or refuse — decide and assert it).
- An unregistered extension and an unregistered fence tag fall through to `None` exactly as they
  do today.
- Every built-in language still detects from its extension and its fence tag. There are fifteen;
  write it as a table-driven case so a new built-in cannot be added without an answer.
- A case that fails if a consumer-specific name comes back: assert the built-in language list and
  the extension and fence tables against a fixed expected set, so adding `.endo` again is a red
  test and not a code review.

Write the case, run it, capture the RED verbatim, then implement, then the GREEN.
`GenericSyntaxHighlighter_test.cpp`, `MarkdownRenderer_test.cpp` and `MarkdownImage_test.cpp` name
endo today — they are yours to update.

## Then

- `CHANGELOG.md` under `[Unreleased]` → **`Breaking`** (the file's preamble says every break goes
  there, not `Changed`), with the migration spelled out: what endo writes instead, in the exact
  shape Task C1 will paste. `endo` is the only consumer of these four rows.
- `docs/modules/tui.md`: the registration seam, with the example using a made-up language, never
  a consumer's.
- `tools/migrate/renames.json` if it exists by the time you land (a concurrent task is creating
  it) — `registerEndoHighlighter` and `LanguageId::Endo` are removals, so a row only if the table
  has a shape for that; say in the report what you did.
- Close core-cpp#24 in the commit message (`Closes #24`) and say in the report what B13's gate
  should check, so the gate is written against what you built.
- `python scripts/clang-format.py --check` on what you touched, the `clang-tidy` preset clean,
  `ctest -L hygiene`, `mkdocs build --strict`.
- Local presets: WSL `clang-debug`, `gcc-release`; Windows `cl-debug` and `clangcl-release`.
  Build `clangcl-release` before every push, not only `cl-debug`. `core-cpp.tui` is the slowest
  binary in the suite (~11 s under a sanitizer); be patient with it rather than narrowing the run.

## Concurrency

Other agents share this checkout and this branch: `src/core/net/`, `src/core/async/` and
`tools/migrate/`. Yours is `src/core/tui/`. One working tree, one index, one local `master`.

- **Never `git pull --rebase`** — it refuses with another session's unstaged work, and stashing
  would take their edits. `git fetch origin`, then push; it is a fast-forward.
- For `CHANGELOG.md`, `docs/` and `.agent/reference/provenance.md`, read every hunk with
  `git diff -- <file>` and stage only your own with `git apply --cached` from a trimmed patch
  (`git add -p` is interactive and unavailable). Confirm with `git diff --cached -- <file>` and
  check `git show --stat` before pushing. Pathspecs protect against the wrong file, not the wrong
  hunk.
- Never run a formatter over a file another session is editing.
- Report anything of theirs that looks broken; do not fix it.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B13a-report.md`: RED/GREEN per
test, the identity choice and its cost, whether the registry is injected or global and why, the
exact migration text endo will paste in Task C1, what B13's gate should check, and the CI run ID.
Return only status, the commit range, a one-line test summary, and concerns.

Push, watch `Build` to green.
