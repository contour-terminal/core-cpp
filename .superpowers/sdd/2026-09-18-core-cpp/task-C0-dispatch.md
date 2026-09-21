# Task C0: migration tooling in core-cpp (`tools/migrate/`)

Phase C migrates six consumers (contour, endo, tuidu, fastcached, Lightweight, morph) onto
core-cpp. Each rewrites hundreds of files' namespaces and includes. This task builds the tools
that do it and the table they read, so every consumer PR is a mechanical, reviewable, idempotent
transform rather than six hand edits of the same map.

It is dispatched **out of plan order**, during Phase B, because it touches only the new
`tools/migrate/` directory and nothing in `src/`. Two other agents are working `src/core/net/`
and `src/core/async/` in this same checkout right now.

Read the plan's Task C0 (`docs/superpowers/plans/2026-09-18-core-cpp.md`, the "Task C0" section)
and the design spec's §2 **Rename map** and §7 **Consumer migration**
(`docs/superpowers/specs/2026-09-18-core-cpp-design.md`). Those are your requirements; this file
adds what they cannot know.

## What exists today, and what does not

Phase A is finished; Phase B is in flight. The table you seed must describe **what core-cpp
actually delivers**, not only what the spec drafted:

- `core::coro` **does not exist**. Task A5b renamed the module and namespace to **`core::async`**
  (`src/core/async/`, headers `<core/async/…>`). Every spec sentence that says `core::coro` means
  `core::async`; the plan's own text was amended, the spec's §2 code block was not everywhere.
- `Generator` is **`core::Generator`** in `<core/Generator.hpp>`, part of `base` — not
  `core::async::Generator`. Task A5b moved it: it needs only std, and `core::async::Generator`
  would read as an asynchronous, `co_await`-able stream, which it is not.
- The TUI completer is **`core::tui::completer`** (Ruling R57, core-cpp#30), not `core::tui`.
- `core::net`'s vocabulary is mid-rewrite: Phase B replaces `EventSource`/`FdInterest`/
  `makeDefaultEventSource` with `IoBackend`/`Interest`/`makeDefaultBackend`, and renames much of
  the socket surface. Seed those rows from the spec's rename map — they are the target — and see
  the drift gate below.

Derive every row you can from the tree rather than from prose: the delivered names are in
`src/core/**/*.hpp`, and each module's public API is its `FILE_SET HEADERS` in
`src/core/*/CMakeLists.txt`.

## Three rulings

**R67 — the codemod tests run under stdlib `unittest`, not pytest.** pytest is installed neither
on this machine nor in the CI images, and these tests are plain assertions that need nothing it
provides. Invoke them as `python -m unittest discover -s tools/migrate -p '*_test.py'`, and
**register that as a ctest** with label `hygiene`, so they run in every local and CI build instead
of being a command nobody types. Keep the file naming `*_test.py`, matching the C++ convention in
this repository. The plan's `python -m pytest tools/migrate` is superseded; say so in the plan's
C0 section when you edit it.

**R68 — `renames.json` is validated against the delivered headers, by a checker you write.**
A rename table that silently drifts from the API turns six consumer migrations into six
debugging sessions of the same table. Write `tools/migrate/check-renames.py` (or a CMake
checker in `tests/cmake/`, your choice — say which and why): for every row whose **target** is a
core-cpp symbol, assert that the symbol and its header exist in `src/core/`. Register it with
label `hygiene`. It validates the target side only; fastcached's and contour's source symbols
are not in this tree. From now on, every Phase B task that renames a public symbol updates
`renames.json` in the same commit — I am putting that in the shared constraints file, and your
gate is what makes it true rather than aspirational.

**R69 — `semantic_rename.py`'s test must actually run somewhere.** It needs libclang's Python
bindings, which are optional. Skip loudly and by name when `clang.cindex` cannot be imported —
never pass silently; "a gate that does not report reads as passed"
(`.agent/rules/build-and-toolchain.md`). Then make **one** CI job install the bindings and run it
for real, and say in the report which job and how you verified it there. A tool whose only test
always skips is untested.

## The four tests the plan names, and what they have to prove

- `rewrite.py` is **idempotent**: running it twice changes nothing the second time. Prove it over
  a fixture containing at least one row from every category in the map (namespace, include,
  symbol, member), not a single line.
- `crispy::cli::command` → `core::cli::Command` — the map is not only a namespace prefix swap;
  tuidu's PascalCase drift (`command`/`option`/`optionList`/`flagStore`/`value`/`verbatim`/
  `helpDisplayStyle` → `Command`/`Option`/`OptionList`/`FlagStore`/`Value`/`Verbatim`/
  `HelpDisplayStyle`) is real work the table must carry.
- `std::net` is untouched: the anchored regex `(?<![\w:])net::` must not match inside a longer
  qualified name. Test the neighbours that will actually appear — `std::net`, `endo::net`,
  `mynet::`, a `net::` inside a string literal and inside a comment — and say in the report which
  of those you chose to leave alone deliberately.
- `semantic_rename.py` renames `sock.Read(` only where `sock` is a `FastCache::ISocket`. The
  fixture is two classes that both have `Read`; only one is under `--decl-paths`.

Write each case, run it, capture the RED, then implement. A test that was never seen to fail
proves nothing.

## `rewrite.py` shape

Mechanical and anchored, per the plan: `--profile contour|endo|tuidu|fastcached` selects which
subset of the map applies (contour keeps `crispy::` for its renderer half and rewrites only the
generic headers; endo has no `crispy::` at all; tuidu's snapshot is `endo::coro`). It must be
safe to run twice, safe to run on a partially converted tree, and it must report what it changed
per file so a reviewer can read the diff. It edits files in place under a path given on the
command line; it never walks outside that path.

`semantic_rename.py` uses libclang over one or more `compile_commands.json`, rewrites only
`DeclRefExpr`/`MemberRefExpr`/`TypeRef` cursors whose **declaration** lives under the given
`--decl-paths`, and unions the edits from several compile databases (fastcached's Windows and
Linux builds see different files, so one database alone misses the other platform's call sites).
Apply edits back-to-front within a file so earlier offsets stay valid.

## Then

- `python scripts/clang-format.py --check` does not cover Python. Say in the report how you
  checked the Python for style, and whether the repository should pin a Python formatter — do not
  add one without saying so; a new tool is a decision, not a detail.
- `ctest -L hygiene` green locally (WSL `clang-debug` is enough for a Python-only change; you do
  not need the whole preset matrix, but you do need the hygiene label to pass on one Linux and
  one Windows preset, since your new tests run there).
- `CHANGELOG.md` under `[Unreleased]`: the tooling is not public API, so a short `Added` line.
- `docs/`: the consumer-migration guide (`.agent/guides/consumer-migration.md`) should point at
  these tools and show the two commands a consumer PR runs. `mkdocs build --strict` if `docs/`
  changed.
- Edit the plan's Task C0 section to record R67 (unittest, not pytest) and R68 (the drift gate),
  so the plan and the tree agree.

## Concurrency

**Two other agents are working this checkout**: impl-A12 in `src/core/net/`, impl-B1 in
`src/core/async/`. Yours is `tools/migrate/`, plus `tests/cmake/` if you put the checker there.
You share one working tree, one index and one local `master`.

- **Never `git pull --rebase`** — it refuses outright with another session's unstaged work, and
  stashing would take their edits. `git fetch origin`, then push; it is a fast-forward.
- Stage with explicit pathspecs, and for `CHANGELOG.md`, `docs/` and the plan file read **every
  hunk** with `git diff -- <file>` first and stage only your own with `git apply --cached` from a
  trimmed patch (`git add -p` is interactive and unavailable). Confirm with
  `git diff --cached -- <file>` and check `git show --stat` before pushing. Pathspecs protect
  against the wrong file, not the wrong hunk.
- Never run a formatter over a file another session is editing.
- Report anything of theirs that looks broken; do not fix it.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-C0-report.md`: RED/GREEN per test,
how many rows the table has and where each category came from, which CI job runs the libclang
test and how you verified it, what `check-renames.py` validates and what it cannot, and the CI run
IDs. Return only status, the commit range, a one-line test summary, and concerns.

Push, watch CI (`Build`) to green.
