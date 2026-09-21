# Task C0: fix round 1 (Rulings R91–R93)

Spec compliance **PASS** — every dispatch item and all three rulings really delivered, and the
table's `core::async`, `core::Generator`-in-base and `core::tui::completer` rows are correct and
gate-checked. Task quality **Changes requested**: 1 Critical, 7 Important, 8 Minor, nearly all
verified by running.

Read `task-C0-review.md` in full before starting. What follows is the rulings and the order.

## Ruling R91 — the Critical is fixed here, not deferred to C4

`semantic_rename.py:170` does not follow virtual overrides. The reviewer ran it with libclang:
`ISocket::Read` renamed, `TcpSocket::Read` **override** and every call typed to the derived class
left behind. The migrated tree does not compile.

The dispatch only asked for `DeclRefExpr`/`MemberRefExpr`/`TypeRef` under `--decl-paths`, so this
is a fitness failure against the **plan** rather than a breach of the brief — which is why it is a
ruling and not simply a defect. It is fixed here anyway: `ISocket` is an interface, and renaming
its methods across an override hierarchy is the *exact job Task C4 commissioned this tool for*.
Discovering it in C4 means discovering it mid-migration, in a repository where a broken rename
costs a benchmark gate and a full preset matrix.

`get_overriden_cursors()` plus a fixture. **The fixture must have the defect's shape**: an
interface and a deriving class that overrides, with call sites typed to *both*, and at least one
call through a base reference. Your current fixture is two **unrelated** classes, which
structurally cannot see this — a fixture that cannot fail for the bug is the thing this session
keeps finding, in your own tool this time.

## Ruling R92 — a `pending` row must carry its `target`

Right now a `pending` row with no `target` is asserted by **nothing** in either direction, and one
has already gone stale (`FastCache::SyncRun`). A row that cannot be checked is a comment wearing a
row's clothes, and the pending list is the one part of this table whose entire job is to be
checked later. Make the schema require it, with a self-test case. `FastCache::SyncRun`'s target is
owed by the async lane; if it is still missing when you land, leave the row and say so, but the
**schema** must refuse a new one.

## Ruling R93 — make the two structural claims true; do not merely correct the prose

Both claims are in shipped docstrings, the guide and the CHANGELOG, and both are false:

- **The `removed` kind has two doors, not three.** With schema and `_patterns_for` disabled, the
  loader alone rewrote the row — `text_rows()` filters on `apply`, not `kind`, so it is the schema
  door seen from downstream. Fix: `and row.kind != "removed"` in the two comprehensions. Then
  re-run the reviewer's adversarial check: **disable any two doors and the third must still
  refuse.**
- **`declares_qualified()` is bypassed by the delivered arm**, which carries its own copy of the
  prefix walk — so `check-renames.py:25` and `:121-123` assert something untrue, and R74's "one
  implementation, not three" was not delivered. The two walks happen to accept the same inputs
  today, which is exactly the state your own decay argument says rots on its own: *nothing stopped
  the same hole being written into the `removed` arm a round later, by you, with the correct
  implementation on screen.*

Making them true costs two small edits. Correcting the prose instead would leave the structure
that produced the defect. Leaving a false structural claim in a comment is worse than no comment —
but a true claim is better than either.

## The Important seven

**I5 first — it is the one that fires on every consumer, on the first attempt.** The byte-identity
recipe at `consumer-migration.md:101-112`, run verbatim against a codemod commit that does what
the guide's own step 4 describes (convert, delete the superseded copy, add a file), reports
`NOT PURE:` for **every added and every deleted path**, produces an **empty** substitution log
(it re-runs over the already-converted tree), and — worst — **writes to the working tree**. The
reviewer's fix is `--diff-filter=M -z` plus deleting one line. A verification step that mutates
the thing it verifies is not a verification step. Coordinate with the `net_types` lane, which
wrote the current text, rather than both editing that guide.

Then, in the reviewer's own order of damage:

- **I4** — every changed file is silently CRLF→LF converted, **including untouched lines**, and
  neither the tests nor the purity proof can see it. On a Windows checkout that makes the diff
  unreviewable, which is where most of this project's review happens.
- **I3** — one non-UTF-8 byte aborts mid-tree with a traceback, *after partial writes*, with no
  summary and no usable exit status. A half-converted tree is worse than a refused one.
- **I1** — a line-initial `#include` **inside a raw string** is rewritten, because include rows run
  before literal masking. That breaks the tool's own stated absolute boundary.
- **I2** — the masker runs over comments, so an unterminated `R"(` in prose masks arbitrary real
  code until the next `)"` → silent under-conversion.
- **I6** — `unit.diagnostics` is never read, so a stale compile database yields a confidently
  reported partial rename.
- **I7** — libclang's host default target silently decides which `#ifdef` branches are live, so
  "union the Linux and Windows databases" only works per host. Document it at minimum; say what
  C4 must do.

## The Minors

Take them or argue them, each in a sentence. Two are worth naming: `ruff.toml` and
`.ruff-version` point at `scripts/ruff.py`, **a name that was never committed**, and say the
linter is off when R78 turned it on — a config that describes a tool that does not exist. And
namespace **aliases** (`namespace cli = crispy::cli;`) are untouched and unlisted as hand edits,
which a consumer will hit.

## Then

- Every fix gets a case that fails before it. For I1, I2, I3 and I4 the case is a fixture file
  with the hostile content; for I5 it is running the recipe against a commit with an add and a
  delete in it.
- `python -m unittest discover -s tools/migrate -p '*_test.py'`, both ctests, `ruff` both halves,
  `ctest -L hygiene`, `mkdocs build --strict`.
- **Assert the expected failure count before you run a verification step** — if you expect zero
  failures, the step proved nothing about itself.
- R87: build shared-file commits from `HEAD`'s blob plus your transform, never the worktree.
  `git show --stat` after each, and check the counts.
- Append "Fix round 1" to `task-C0-report.md` with RED/GREEN per finding.
