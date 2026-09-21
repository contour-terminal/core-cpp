# Task C0 report: migration tooling in core-cpp (`tools/migrate/`)

**Status:** DONE_WITH_CONCERNS
**Commit range:** `a13070c..ddddc67` (round 1) and `ac9e2e2` (round 2, ruling R71)

- `ddddc67 tools: rename table and codemods for consumer migration`
- `ac9e2e2 build: ruff is pinned, and the Python is formatted with it`

---

## What was built

| File | What it is |
|---|---|
| `tools/migrate/renames.json` | The rename table: 438 rows, 4 profiles |
| `tools/migrate/renames.py` | The one loader and schema validator every tool reads the table through |
| `tools/migrate/rewrite.py` | The mechanical codemod, `--profile contour\|endo\|tuidu\|fastcached` |
| `tools/migrate/semantic_rename.py` | The libclang pass over one or more `compile_commands.json` |
| `tools/migrate/check-renames.py` | The drift gate (R68) |
| `tools/migrate/{rewrite,check_renames,semantic_rename}_test.py` | 51 cases, stdlib `unittest` (R67) |

Registered in `tests/CMakeLists.txt` as two ctests with label `hygiene`:
`core-cpp.migrate-renames` (the gate) and `core-cpp.migrate-codemods` (the suite). Both are
registered whether or not a Python interpreter is found: without one they are a test that *reports*
`SKIPPED` and says why, rather than a test that silently does not exist.

---

## RED, then GREEN

Every case was written and run before the implementation existed.

### The first RED — nothing implemented

```
$ python -m unittest discover -s tools/migrate -p '*_test.py'
EEE
======================================================================
ERROR: check_renames_test (unittest.loader._FailedTest.check_renames_test)
ImportError: Failed to import test module: check_renames_test
    import renames
ModuleNotFoundError: No module named 'renames'
======================================================================
ERROR: rewrite_test (unittest.loader._FailedTest.rewrite_test)
ModuleNotFoundError: No module named 'renames'
======================================================================
ERROR: semantic_rename_test (unittest.loader._FailedTest.semantic_rename_test)
...
```

### The falsifiability RED — the anchor and the literal mask neutered

That first RED only proves the modules were absent. So after the tools were green, the two
properties the named cases exist for were deliberately broken and the suite re-run:
`(?<![\w:]){source}::` was replaced with `\b{source}::`, and `_maskLiterals` was made the identity.

```
$ python -m unittest discover -s tools/migrate -p '*_test.py'
FAIL: test_std_net_is_untouched (rewrite_test.TheNamespaceRegexIsAnchored...)
AssertionError: 'std::core::net::Socket s;\n' != 'std::net::Socket s;\n'
FAIL: test_another_projects_net_is_untouched (rewrite_test.TheNamespaceRegexIsAnchored...)
AssertionError: 'endo::core::net::Thing t;\n' != 'endo::net::Thing t;\n'
FAIL: test_a_second_run_changes_nothing (rewrite_test.RewriteIsIdempotent...)
AssertionError: '...core::core::net::EventLoop& loop, core::core::n[155 chars]n}\n' != '...'
FAIL: test_a_half_converted_tree_converges (rewrite_test.RewriteIsIdempotent...)
AssertionError: '...ore::core::net::EventLoop& loop, core::core::c[652 chars]n}\n' != '...'
FAIL: test_a_string_literal_is_data_and_is_left_alone (rewrite_test.TheNamespaceRegexIsAnchored...)
AssertionError: 'auto const s = "core::net::EventLoop";...' != 'auto const s = "net::EventLoop";...'
Ran 51 tests in 0.790s
FAILED (failures=5)
```

`mynet::` stayed correct under `\b` (the preceding `y` is a word character), which is why the case
set distinguishes `std::net`, `endo::net` and `mynet::` rather than testing one of them.

The implementation was then restored verbatim from the backup taken before the neutering.

### GREEN

```
$ python -m unittest discover -s tools/migrate -p '*_test.py' -v   # Windows, libclang installed
Ran 51 tests in 0.780s
OK
(51 ok, 0 skipped)
```

### The four cases the plan names

| Case | Where |
|---|---|
| `rewrite.py` is idempotent | `RewriteIsIdempotent.test_a_second_run_changes_nothing`, over a contour fixture whose replacements cover all five categories, asserted by `test_the_fixture_exercises_every_category`; plus `test_a_half_converted_tree_converges` |
| `crispy::cli::command` → `core::cli::Command` | `TheCliTypesAreRenamed.test_crispy_cli_command_becomes_core_cli_Command`, with all ten drifted spellings in `test_every_drifted_cli_type_has_a_row` and a negative case (`cli::parse`, `cli::helpText` are left alone) |
| `std::net` is untouched | `TheNamespaceRegexIsAnchored`, five neighbours (see below) |
| `sock.Read(` only where `sock` is a `FastCache::ISocket` | `ItRenamesByDeclarationNotBySpelling.test_only_the_call_on_the_declaring_class_is_renamed`, fixture of two classes that both have `Read`, only one under `--decl-paths` |

### The `net::` neighbours, and which are left alone deliberately

| Neighbour | Rewritten? | Why |
|---|---|---|
| `std::net::Socket` | no | `(?<![\w:])` refuses a `:` before the name |
| `endo::net::Thing` | no | same |
| `mynet::`, `SUBNET::` | no | `(?<![\w:])` refuses a word character before the name |
| `"net::EventLoop"`, `R"(net::x)"`, `'n'` | **no, deliberately** | a codemod may change what the code *says*; it must never change what the program *sends*. String and character literals are masked out before any non-include row is applied. A `net::` in a log message therefore survives the migration and is a hand edit. |
| `// net::EventLoop drives it.`, `/* net::ISocket */` | **yes, deliberately** | a comment documents the code beside it; leaving it would make every comment in six repositories describe a namespace that no longer exists |
| `namespace net { ... }` (a *definition*) | **no, deliberately** | a consumer's own namespace and the moved one are the same token. The plan already lists endo's five as hand edits. `using namespace net;` *is* rewritten, because it is unambiguous |

The character-literal pattern refuses a `"` in its body, so C++'s digit separator (`1'000'000`)
cannot swallow a following string literal.

---

## The table: 438 rows, and where each category came from

Nothing was typed from prose that could be read off a tree. Throwaway generators (in the scratchpad,
not committed) derived the mechanical rows; the rest were hand-written from the design spec's §2 and
`.agent/guides/consumer-migration.md`, and every one of them is checked by the gate.

| Category | Rows | Source |
|---|---|---|
| `include` | 174 | 165 from the union of *(a)* the headers under `src/{crispy,coro,net,platform,testing,tui}` of contour @`6777ff05`, endo @`f774a210` and tuidu @`HEAD`, and *(b)* every `#include <crispy/…>`-style directive those trees actually contain — the second half matters because endo and tuidu *fetch* crispy and coro from contour, so those headers are in no tree of their own. Each is mapped onto the core-cpp header that exists, with 22 explicit relocations (`net/platform/Clock.hpp` → `core/platform/Clock.hpp`, `net/EpollEventSource.hpp` → `core/net/linux/EpollEventSource.hpp`, …). Plus 9 fastcached rows |
| `symbol` | 226 | 46 `crispy::<Name>` rows from the names those three trees actually spell, intersected with what `core`, `core::log`, `core::cli`, `core::testing`, `core::views` and `core::ranges` declare; 90 `platform::<Name>` / `endo::platform::<Name>` rows; 20 `testing::<Name>` rows; the rest hand-written (the `cli` PascalCase drift, contour's `net::` → `core::platform::` moves, endo's compatibility aliases, fastcached's clock and ranges names) |
| `namespace` | 11 | Hand-written from the spec's §2 |
| `member` | 10 | Hand-written; 8 `semantic`, 1 `text` (pending), 1 `manual` |
| `macro` | 17 | Hand-written (`CRISPY_*` → `CORE_*`, `FC_*` → `CORE_*`) |

Per profile: contour 147, endo 261, tuidu 177, fastcached 53. By application: 422 `text`,
8 `semantic`, 8 `manual`.

**At commit `ddddc67`: 410 rows carried a delivered core-cpp target and were validated on every
build; 10 were pending.** At the time of writing, master is at `438 rows, 412 with a delivered
core-cpp target, 8 pending, 0 failure(s)` — see "The gate has already been used by another task"
below.

Two grounding findings worth recording:

- **The unmapped headers came out exactly as the spec's "stays in contour" and "stays in endo"
  lists.** The generator's leftovers were `crispy/{Point,Ring,BufferObject,StrongHash,LRUCache,…}`
  (25) and `platform/{Process,Pipe,ProcessProvider,ProjectFileTree,InstallPaths,InterruptThrottle,
  WaitResult,…}` (15 for endo, 14 for tuidu). That agreement is the strongest evidence the include
  map is right.
- **`crispy::`, `endo::platform::` and `endo::testing::` are *split* namespaces, so none of them is
  a namespace row.** contour keeps crispy's renderer half, endo keeps `platform::Process` and
  `testing::InjectedShell`. A prefix swap would move code that stays. They are rewritten symbol by
  symbol instead — which is also why those three account for 156 of the 202 symbol rows.

### A discrepancy between the dispatch and the code

The dispatch names tuidu's cli drift as `optionList` / `flagStore` / `helpDisplayStyle`. **tuidu's
tree actually spells them `option_list`, `flag_store` and `help_display_style`** (`src/tuidu/Cli.cpp`,
through `namespace cli = crispy::cli;`). Resolved by carrying **both** spellings — ten rows, all
targeting the same eight `core::cli` types — so the codemod works whichever a consumer has. contour
already uses PascalCase and needs only the namespace swap, which is why these rows are tuidu's alone.

---

## `check-renames.py`: what it validates, and what it cannot

**Python, not a `cmake -P` script in `tests/cmake/`** (R68 left the choice open). Two reasons: it
reads the table through the same `renames.py` loader the codemod uses, so the gate and the tool
cannot disagree about what a row means; and CMake would need a hand-rolled JSON parser, which is a
second thing to keep correct.

For every **delivered** row that names a core-cpp target it asserts:

1. `src/<target.header>` exists — or `src/<target.header>.in`, so the configured `core/Config.hpp`
   is checked against the template it is delivered from;
2. that header is listed in a `FILE_SET HEADERS` of its module, unless the row says
   `"public": false` (which the private relocations do). The `HEADERS` list is read out of
   `core_cpp_add_module()`, including one held in a `set()` variable, so `CORE_CPP_WITH_IMAGES`'
   two headers are counted;
3. the header opens the namespace `target.symbol` names — the longest `::`-prefix that the file
   actually opens, tracked through brace depth, so `namespace core::net` and the nested form both
   work — and declares every remaining component. That last part is what makes an enumerator
   (`core::net::NetErrorCode::BadHandle`) and a member (`core::platform::IClock::now`) checkable;
4. an `include` row's `to` is that same header.

For every **pending** row it asserts the opposite: the symbol must be absent from `src/core/`. So a
row cannot rot in either direction — the day Task B3 lands `core::net::IoBackend`, the gate fails
with `core::net::IoBackend now exists in src/core/net/IoBackend.hpp, so mark the row delivered (it
waits on task B3)`.

Its own self-test (`check_renames_test.py`) proves each refusal by name: a missing header, a symbol
the header does not declare, a namespace the header does not open, a public target in no
`FILE_SET`, a pending row that has landed — and that the real table passes, and that the gate
validated more than 40 rows rather than quietly validating none.

**What it cannot check**, stated plainly because a gate's blind spots are the part nobody reads:

- **The source side.** contour's and fastcached's symbols are not in this tree, so a row whose
  `from` is wrong is caught only by the consumer's compiler. (The `from` sides were nonetheless
  derived from those trees, at the pinned SHAs, rather than invented.)
- **It is a text scan, not a compile.** It cannot see a declaration behind an `#if`, cannot check a
  signature, an overload set, a parameter type or a default argument, and cannot tell a
  `constexpr` variable from a function of the same name.
- **It reads the working tree**, not `HEAD`. In a shared checkout another agent's uncommitted work
  can make a pending row's symbol appear to have landed. That is a true report, and it is how the
  two drifts below were found, but it means a red gate here is sometimes somebody else's in-flight
  edit rather than a wrong table.
- **A `manual` row and a row with no `target`** are documentation only; nothing is asserted.

### The gate found two real drifts on its first run, before this commit existed

1. `core::async::syncRun` already exists in `src/core/async/SyncRun.hpp` — **impl-B1's untracked
   work**, not in `HEAD`.
2. `core::net::NetErrorCode::SystemError` already exists in `src/core/net/NetError.hpp` — **impl-A12's
   uncommitted work**; `HEAD` still says `Other`.

Both are in the table as rows, and in both cases the *gate* was right and my first draft of the
*table* was wrong. See the concerns below for how they are left.

### The gate has already been used by another task

Within minutes of this commit landing, **`748142f tools(migrate): the NetErrorCode::Other rows name
the target B2 delivered`** — another agent's commit — read the `note` on the two `SystemError` rows
and did exactly what it asked: added the `target` and dropped `"status": "pending"`. The gate is
green against a fresh `git archive HEAD` of master (`438 rows, 412 with a delivered core-cpp
target, 8 pending, 0 failure(s)`), and concern 1 below is now half closed without my touching it.
That is the whole mechanism working end to end on its first day, and it is better evidence than any
of my own tests that R68 does what it was asked to do.

---

## R69: which CI job runs the libclang test, and how it was verified there

**The `style` job of `.github/workflows/build.yml`**, which `ci-ok` already requires. Three steps
were added:

1. `Consumer rename table matches the delivered headers` — `check-renames.py`, before anything is
   configured, so a drifted table is the first thing a pull request is told (it also runs as ctest
   in every build).
2. `Install libclang's Python bindings` — `python -m pip install "libclang==18.1.1"`, the latest
   PyPI release, which ships `clang/cindex.py` and a bundled native library, so no apt package and
   no clang toolchain is involved.
3. `Migration codemod tests, with the semantic pass running for real` — prints where `clang.cindex`
   was imported from, then runs the suite through a small runner that **fails on a skip**, emitting
   a `::error::` annotation naming each skipped case.

**Verified there:** run **35539983375** (`Build`, commit `ddddc67`), job **106155774336**, `style` —
succeeded in 23s with all three steps green and **no `::error::` annotation**. Its log, which is the
evidence rather than the green tick:

```
check-renames: 438 rows, 410 with a delivered core-cpp target, 10 pending, over 145 public headers: 0 failure(s)
Successfully installed libclang-18.1.1
clang.cindex from /opt/hostedtoolcache/Python/3.12.14/x64/lib/python3.12/site-packages/clang/cindex.py
ran=51 failures=0 errors=0 skipped=0
```

`skipped=0` is the line that matters: the three libclang cases ran for real, and had any of them
skipped the step would have exited 1.

**Verified in both directions locally**, because a guard never seen to refuse is not a guard:

- Windows, libclang installed: `ran=51 failures=0 errors=0 skipped=0`, exit `0`.
- WSL, libclang absent: `ran 51 skipped 6`, one `::error::…libclang's Python bindings
  (clang.cindex) are not installed` per case, exit **`1`**.

Where the bindings are missing the module also prints, before any case runs,
`semantic_rename_test: SKIPPING every case -- python -m pip install libclang (no clang.cindex for
/usr/bin/python3)`, so a plain ctest run says so rather than showing six silent `s` characters.
Observed in the WSL ctest run below.

`libclang==18.1.1` is pinned **in the workflow**, not in `scripts/tool-versions.py`. That script is
for the tools the *tree* is held to (clang-format, clang-tidy); nothing in core-cpp is formatted or
analysed by libclang, and `tool-versions.py --check` must not start failing for a developer who has
no reason to install it. The reasoning is in a comment at the step.

---

## Local gates

| Gate | Result |
|---|---|
| WSL `clang-debug` (private tree `out/build/task-c0-linux`), `ctest -L hygiene` | `core-cpp.migrate-renames` **Passed** (5.90s), `core-cpp.migrate-codemods` **Passed** (7.29s, `OK (skipped=6)` with the loud skip line) |
| Windows `cl-debug` (private tree `out/build/task-c0-win`), `ctest -L hygiene` | `core-cpp.migrate-renames` **Passed** (1.19s), `core-cpp.migrate-codemods` **Passed** (2.83s, no skips — libclang is installed here) |
| `check-renames.py` against a `git archive HEAD` export | `438 rows, 410 with a delivered core-cpp target, 10 pending, over 145 public headers: 0 failure(s)` |

## CI

| Run | Workflow | Commit | Result |
|---|---|---|---|
| **35539983375** | `Build` | `ddddc67` | **success**, all 23 jobs, none skipped or cancelled (`ci-ok` green) |
| **35539983517** | `Docs` | `ddddc67` | **success** |

Both local `ctest -L hygiene` runs also reported two failures that are **not mine**: see concerns.

A private build tree was used on each host rather than `out/build/clang-debug`, because impl-A12 and
impl-B1 are building in this checkout at the same time.

### Python style

`python scripts/clang-format.py --check` does not cover Python, and **the repository pins no Python
formatter or linter** — there is no `pyproject.toml`, `setup.cfg`, `.flake8` or `ruff.toml`, and
neither ruff, flake8 nor black is installed here. So the Python was checked by hand:

- every file wrapped to **110 columns**, which is `.clang-format`'s `ColumnLimit`. *(Round 1 also
  claimed `scripts/*.py` already kept that limit. **That was wrong** — the `awk` that produced it
  was truncated by a `head -20` that the `tools/migrate` lines filled. `scripts/clang-format.py`
  had lines of 112, 111 and 114 columns and `scripts/tool-versions.py` one more. ruff found them in
  round 2, which is the argument for R71 made by the thing R71 was about.)*;
- `# SPDX-License-Identifier: Apache-2.0` on line one of every file, as `scripts/*.py` have;
- the repository's naming carried over where Python allows it (`camelBack` functions, `CamelCase`
  types), module docstrings, and type annotations throughout;
- the suite itself is the functional check, and it imports every module.

**Recommendation, not applied in round 1:** pin `ruff`. **Ruling R71 said yes; it is done in round
2 (`ac9e2e2`) — see the section below.**

`mkdocs build --strict` was **not run**: nothing under `docs/` that the site builds was touched.
The documentation change is `.agent/guides/consumer-migration.md`, which is not in `mkdocs.yml`'s
nav; the plan file under `docs/superpowers/` is not built either.

---

## Documentation and plan

- **`.agent/guides/consumer-migration.md`** gained a "The two commands" section: the exact
  `rewrite.py` and `semantic_rename.py` invocations a consumer pull request runs, what the codemod
  deliberately leaves to a human and why, and how the drift gate binds a task that renames a public
  symbol.
- **The plan's Task C0 section** now records **R67** (unittest, not pytest, and registered as a
  ctest — the `python -m pytest tools/migrate` line above it is marked superseded), **R68** (the
  drift gate, and the rule that every task renaming a public symbol updates `renames.json` in the
  same commit) and **R69** (the libclang job).
- **`CHANGELOG.md`**, one `Added` entry under `[Unreleased]`.

---

## Concerns

1. **Two rows carry no `target`, so the gate does not cover them yet, and two tasks must close
   that.** Both are symbols that exist in another agent's *uncommitted* work but not in `HEAD`, so
   neither "delivered" nor "pending with a target" could be committed without turning the shared
   tree red for work that is not mine. Each carries a `note` saying exactly what to add:
   - `FastCache::SyncRun` → `core::async::syncRun`: **Task B1** adds
     `"target": {"header": "core/async/SyncRun.hpp", "symbol": "core::async::syncRun"}` and
     `"status": "delivered"`.
   - ~~`net::NetErrorCode::Other` and `NetErrorCode::Other` →
     `core::net::NetErrorCode::SystemError`~~ — **closed by `748142f`**, which added the target and
     marked both rows delivered once B2's rename landed. Only the `SyncRun` row is still open.

2. **The gate will fail for whoever lands the eight pending rows, by design, and they need to be
   told.** When Task B3 lands `IoBackend` / `Interest` / `makeDefaultBackend`, B4 lands
   `PlatformLoop` / `TestLoop`, or B6 lands `IListener::boundPort`, `ctest -L hygiene` goes red
   with a message naming the row and the one-line JSON edit that fixes it. That is R68 working, but
   it is a new obligation on tasks already dispatched. **Suggest the controller adds "update
   `tools/migrate/renames.json` in the same commit" to the shared constraints file**, as R68
   anticipated.

3. **`ctest -L hygiene` is currently red in this checkout for reasons that are not mine.** On both
   hosts, `core-cpp.cmake-hygiene` fails with ten `provenance` violations, all of them impl-B1's
   untracked files (`src/core/async/{AsyncQueue,DetachedTask,IExecutor,ParkedWork,ResumeOn,SyncRun,
   ThreadPoolExecutor}.hpp` and three `_test.cpp`), and `core-cpp.async-link-smoke` reports
   `Not Run` because I built only `core-cpp-exit-code-fixture` rather than the whole tree.
   Reported, not fixed. CI's `style` job (which runs the same hygiene scan over the committed tree)
   is green, which confirms they are B1's.

4. **The shared index needed repairing, and the same hazard will recur.** When I went to stage, the
   shared `.git/index` already held **another agent's staged `CHANGELOG.md` work** (the
   `core::tui` language-registry Breaking entry, 46 insertions), on top of impl-A12's *unstaged*
   `NetError` entry in the same file. Committing from that index would have taken their work with
   mine. I committed through a private index (`GIT_INDEX_FILE`) containing only my 13 files —
   which then left the shared index stale relative to the new `HEAD`, so a plain `git commit` from
   another agent would have **deleted all of `tools/migrate/`**. I refreshed it (`git read-tree
   HEAD`) and re-applied their staged CHANGELOG hunk on top, so it is exactly as I found it,
   relative to the new `HEAD`. Verified: `git diff --cached --stat` shows only their
   `CHANGELOG.md | 48 ++++--`. **Whoever else commits here should check `git diff --cached` first**;
   three sessions sharing one index is a sharper edge than three sessions sharing one working tree.

5. **Two files were written with CRLF and had to be normalised.** `renames.json` and
   `rewrite_test.py` picked up CRLF from Python's default newline translation on Windows. Git would
   have normalised them on commit, but the `style` job's CR-byte check reads the **working tree**,
   so it would have failed. Fixed before the commit; worth knowing for anyone else generating files
   from Python on this host.

6. **`rewrite.py` applies a pending row rather than skipping it, and warns.** A consumer migrates
   after v0.1.0, when nothing is pending, so refusing would silently leave `listener.localPort()`
   in the migrated tree for a human to find. Instead it rewrites and prints
   `rewrite: warning: applied 1x member localPort -> boundPort, whose target task B6 still owes`.
   Run before B6 lands, the output names a symbol core-cpp does not have yet — deliberate, and
   said out loud.

7. **`--decl-paths` is trusted, not verified.** `semantic_rename.py` rewrites a cursor whose
   declaration lies under a path the caller names. If a consumer points it at a directory holding
   an unrelated class with a matching member and matching `scope`, it will rename that too. The
   `scope` match makes this unlikely, and the tool reports every file and row it touched, but it is
   not proof.

8. **fastcached's `Async/` and `Net/` include rows are mostly absent.** Nine of its includes have
   rows (the four `Core/` ones plus `Task`, `Cancellation`, `ISocket`, `IListener`, `NetError`);
   the other ~90 headers of `FastCache/{Async,Net}/` have no core-cpp counterpart until Phase B
   merges them, so a row for each would be 90 pending rows asserting 90 absences. **Task C3's
   PR-B should extend the table** once B1–B11 have landed; the spec's §2 list and the
   `.agent/guides/consumer-migration.md` fastcached table are the input, and the gate will then
   validate every row.

---

# Round 2 — Ruling R71: pin `ruff`

**Commit:** `ac9e2e2 build: ruff is pinned, and the Python is formatted with it`, 12 files,
+233 −23.

| File | What it is |
|---|---|
| `.ruff-version` (new) | The one place the release is stated: `ruff 0.16.8`, the current PyPI release, in the organisation's `# comment` + `key: value` format, the same shape as `.clang-tidy-version` |
| `ruff.toml` (new) | `line-length = 110` — `.clang-format`'s `ColumnLimit`, so one number governs both languages — and `target-version = "py312"` |
| `scripts/ruff-format.py` (new) | The wrapper, mirroring `scripts/clang-format.py` line for line: finds ruff by `--binary`, `$RUFF`, the pinned PyPI package, then `PATH`; **refuses any build but the pin**; formats every `*.py` git knows about |
| `scripts/tool-versions.py` | `TOOLS` gains `"ruff"`, so `--install` installs all three and `--check` refuses a mismatch, with no other change needed — `pinned()` already reads `.<tool>-version` |
| `.github/workflows/build.yml` | Two steps in `style`, beside clang-format's: install the pin, then `python scripts/ruff-format.py --check` |
| `CONTRIBUTING.md`, `AGENT.md` | The command a contributor runs, and item 1 of the workflow checklist |
| `CHANGELOG.md` | One line under `Added` |

No row in `cmake/CoreCppDependencies.cmake`, as ruled: that table is for what enters a consumer's
build, and this never does. The vendoring file set is an explicit named list, so `ruff.toml` and
`.ruff-version` are correctly absent from what contour vendors.

## The guard, proven in both directions

A version guard never seen to refuse is not a guard:

```
$ python scripts/ruff-format.py --check --binary python
ruff-format.py: python is ruff (unknown version), but .ruff-version pins 0.16.8.
Install the pin with: python scripts/tool-versions.py --install
$ echo $?
2

$ python scripts/ruff-format.py --check          # before formatting
6 files would be reformatted, 4 files already formatted
ruff-format.py: the files above are not formatted; run: python scripts/ruff-format.py
$ echo $?
1

$ python scripts/ruff-format.py --check          # after
10 files already formatted
ruff-format.py: 10 file(s) are formatted with ruff 0.16.8
$ echo $?
0
```

## It is `ruff-format.py`, not `ruff.py`, and that was not a hypothesis

The first version was `scripts/ruff.py`. It reported `no ruff found` on a machine with ruff
installed, in the same shell where `python -c "import ruff.__main__"` had just worked: a module
named `ruff.py` on `sys.path` **shadows the `ruff` package the script imports to locate the pinned
binary**, and the `ImportError` fell through to the "not installed" branch. Renamed, which also
matches its neighbour (`clang-format.py` never had the problem because of the hyphen) and says what
it does — the formatter, not the linter. The reason is in the module docstring, where the next
person to shorten the name will read it.

## What the reformat touched — mine, and what predates me

Ruled: fix my own files, fix `scripts/*.py` too, but say so separately. **Five files, 50 diff
lines, none of it semantic.** The suite (51 cases) and the drift gate were re-run after and are
unchanged.

**Pre-existing, not mine** — these predate Task C0 and are in the commit because nothing had ever
checked them:

- `scripts/clang-format.py` — three call sites wrapped; lines of 112, 111 and 114 columns.
- `scripts/tool-versions.py` — one `argparse` call wrapped, one `print` call wrapped. (Its other
  changes in this commit *are* mine: `TOOLS` and the docstring.)

**Mine, from round 1:**

- `tools/migrate/check-renames.py` — one `re.compile` re-wrapped, and `f"…\"public\": false…"`
  became `f'…"public": false…'`.
- `tools/migrate/rewrite.py` — two f-string fragments joined onto one line.
- `tools/migrate/semantic_rename_test.py` — two single-quoted strings became double-quoted.

**This is where round 1's own style claim turns out to have been wrong**, and it is worth stating
plainly because it is the argument for R71: round 1 reported that `scripts/*.py` already kept the
110-column limit. They did not — the `awk` that produced that claim was truncated by a `head -20`
that the `tools/migrate` lines filled, and four over-length lines in `scripts/` were never in the
output I read. A hand check that reads its own output through `head` is exactly the check that
drifts. The report's round-1 section is corrected in place.

## Local gates

| Gate | Result |
|---|---|
| `python scripts/ruff-format.py --check` | `10 file(s) are formatted with ruff 0.16.8` |
| `python scripts/clang-format.py --check` | `382 file(s) are formatted with clang-format 22.1.8` |
| `python scripts/tool-versions.py --check` | all three pins installed |
| `python -m unittest discover -s tools/migrate -p '*_test.py'` | `Ran 51 tests … OK` |
| `python tools/migrate/check-renames.py` | `438 rows, 412 with a delivered core-cpp target, 8 pending … 0 failure(s)` |
| Windows `ctest -L hygiene -R "vendor\|migrate\|platform-sources\|layering\|release"` | 6/6 passed |
| No CR byte in any file this round touched | none |

No preset matrix run: this round changes no CMake, no C++ and no ctest registration. The
`style` job is its gate, and the Windows hygiene subset above covers the checks a new root file
could plausibly disturb (vendoring, layering, platform sources, release).

## Two things left open, deliberately

1. **ruff's linter (`ruff check`) is not enabled.** Ruled scope was `ruff format --check`, and a
   rule set is a decision of its own with a much larger blast radius than a formatter. The wrapper's
   docstring and `.ruff-version` both say so, so the next person finds the answer where they look
   for the question.
2. **The Python's naming is inconsistent, and the formatter cannot see it.** `scripts/*.py` is
   `snake_case` (PEP 8); `tools/migrate/*.py`, which I wrote, is `camelBack`, following the C++
   guidelines rather than its Python neighbours. `ruff format` does not touch identifiers, so this
   round did not change it and would not have caught it. It is one `ruff check --select N`
   (pep8-naming) away from being decidable, which is a reason to keep point 1 in mind rather than a
   reason to act now. Flagging it because I introduced it.

## CI

| Run | Workflow | Commit | Result |
|---|---|---|---|
| **35540789314** | `Build` | `ac9e2e2` | **success**, all 24 jobs, none skipped or cancelled |
| **35540789310** | `Docs` | `ac9e2e2` | **success** |

The `style` job's log, which is the evidence rather than the green tick:

```
clang-format.py: 372 file(s) are formatted with clang-format 22.1.8
Successfully installed ruff-0.16.8
ruff-format.py: 10 file(s) are formatted with ruff 0.16.8
```

The round-1 steps are still green in the same job, so the libclang guard
(`ran=51 failures=0 errors=0 skipped=0`) and the drift gate ran too.

---

# Addendum: the drift gate has now fired three more times

Since round 1 landed, `check-renames.py` has spoken four times about live work, all of it correct:

| What it said | Whose | Outcome |
|---|---|---|
| `core::async::syncRun` exists | impl-B1, then untracked | Row left without a target; **B1 has since landed `src/core/async/SyncRun.hpp`** (`b21229d`), so the row is now theirs to complete — still open |
| `core::net::NetErrorCode::SystemError` exists | impl-A12, then uncommitted | **Closed by `748142f`**, another lane reading the `note` I left |
| `core::net::IoBackend`, `Interest`, `makeDefaultBackend` exist | Task B3, **currently untracked** | Open, and correctly *not* acted on — see below |

**The `IoBackend` case is the documented limitation behaving exactly as documented.** In this shared
checkout `ctest -L hygiene` is red right now:

```
check-renames: rows[422]: core::net::IoBackend now exists in src/core/net/IoBackend.hpp, so mark the row delivered (it waits on task B3)
check-renames: rows[423]: core::net::Interest now exists in src/core/net/IoBackend.hpp, so mark the row delivered (it waits on task B3)
check-renames: rows[424]: core::net::makeDefaultBackend now exists in src/core/net/IoBackend.hpp, so mark the row delivered (it waits on task B3)
```

`src/core/net/IoBackend.hpp` is **untracked** — the B3 lane's in-flight work. So the rows must stay
pending: flipping them now would pass in this working tree and fail CI against `HEAD`, which is the
opposite mistake. Verified against a fresh `git archive HEAD`: `438 rows, 412 with a delivered
core-cpp target, 8 pending … 0 failure(s)`. **CI is green; only this working tree is red, and only
because somebody else's file is in it.**

The three rows are B3's to flip, in the same commit as `IoBackend.hpp`, exactly as the message says
— which is the obligation R68 created and which concern 2 above asked the controller to write into
the shared constraints.

**One more thing the new ruff gate will touch:** the `scripts/check-upstream-drift.py` that another
lane has untracked in this tree **already passes `ruff format --check`**, so the pin does not ambush
them. Checked rather than assumed; their file was not formatted or otherwise touched.

## One consequence to rule on: the same true red is reported twice

With B3's untracked `IoBackend.hpp` in this tree, **both** hygiene ctests go red for one cause:

- `core-cpp.migrate-renames` — the gate itself. Correct, and the reason it exists.
- `core-cpp.migrate-codemods` — because `check_renames_test.py::test_this_tree_passes_the_gate`
  runs the gate against the **real tree** as its fixture.

Both are telling the truth, and both name the rows. But the second one is a *unit test of the
checker* using live, shared, mutable state as its fixture, which is the shape
`.agent/rules/testing.md` warns about from the other direction — and it makes a lane's in-flight
file look like my test suite is broken.

**Recommendation, not applied:** drop `test_this_tree_passes_the_gate` from the suite and leave the
real-tree assertion to `core-cpp.migrate-renames`, which runs the identical check under the same
label in every local and CI build. `test_the_gate_checked_something` (it validated >40 rows, not
zero) and the seven sandbox refusal cases would stay, so nothing is lost and the suite goes back to
testing the checker rather than the tree.

**Not applied on my own authority**, for the same reason ruff was not applied in round 1: removing
an assertion while it is red is exactly the change that needs somebody else to agree it is not
just making the red go away. Happy to do it in a one-line round 3 if you rule yes.

---

# Round 3 — Rulings R74 (enum-scoped symbols) and R75 (a `removed` kind)

**Commit:** `a8e5212 tools(migrate): a removed symbol is a row that must stay absent`, 8 files,
+355 −33.

## R74: the correction to the premise, and the part that mattered

**The pending arm already walked prefixes.** It was fixed during round 1, before `ddddc67` was
committed, with a comment in place saying why ("so that an enumerator or a nested class is found
where a `namespace core::net::NetErrorCode` test would never have matched"). That is also why B2's
`SystemError` rows resolved at all: had the shipped arm asked for a namespace called
`core::net::NetErrorCode`, `748142f` could not have closed them. Saying so plainly because the
ruling's premise was that the hole was live, and it was not.

**The ruling's substance was right, and it was the half I had missed.** Nothing tested the shape.
None of the 51 cases had an enum- or class-scoped symbol, so nothing would have stopped the arm
regressing, and nothing did stop the *same* hole being written into the removed arm a round later.
A fix with no adversarial case is a fix with a shelf life.

So this round did three things:

1. **One implementation, not three that agree today.** `declaresQualified(text, components)` is now
   the single prefix walk, and the delivered, pending and removed arms all read symbols through
   `findSymbol()` over it.
2. **Cases for the shape**, in `APendingRowScopedToAnEnumIsSeenWhenItLands`: an enum-scoped pending
   row that has landed, one that has not, a class-scoped one that has landed, and a case pinning
   that the delivered arm reads the same shape the same way.
3. **The RED that proves them.** `declaresQualified()` neutered to the whole-path namespace test:

   ```
   FAIL: test_a_pending_class_scoped_row_that_has_landed_is_refused
   FAIL: test_a_pending_enum_scoped_row_that_has_landed_is_refused
   FAIL: test_a_removed_symbol_that_came_back_is_refused
   Ran 66 tests … FAILED (failures=4)   # the fourth is B3's in-flight tree, below
   ```

**`test_the_delivered_arm_reads_the_same_shape` keeps passing under that neutering**, because the
delivered arm has its own prefix walk. **The test that proves the fix necessary is the one that
stays green under the mutation** — it shows that one arm was right all along, so the arm that was
wrong could only ever be caught by asking it directly. That is the argument for a single
`declaresQualified()` rather than three arms that happen to agree today.

And the reason that argument is not hypothetical: nothing tested the shape, so nothing would have
stopped the pending arm regressing — **and nothing did stop the same hole being written into the
`removed` arm one round later**, by me, with the pending arm's correct implementation visible on
screen. Three arms that happen to agree is a state that decays on its own.

## R75: the `removed` kind

A row whose `from` names a symbol core-cpp deleted. No `to`, no `target`; the gate asserts the
symbol is **absent** from `src/core/`, the opposite direction from every other row.

**That no rewrite tool can see one is structural, not conventional** — three doors, each with a
case:

| Door | Refusal |
|---|---|
| The schema | a `removed` row carrying a `to`, a `target`, or any `apply` but `none` is a `TableError`; so is one without a `note`, or one marked `pending` |
| The loader | `textRows()` and `semanticRows()` filter on `apply`, which can only be `none`, so neither can yield one |
| The codemod | `rewrite._patternsFor()` raises on the kind |

Plus `test_a_removed_symbol_survives_the_codemod_untouched`, which runs the real table over all four
profiles and asserts the removed symbols come out unchanged.

**The first two rows are B13a's**, and B13a's reasoning for adding none is preserved rather than
overturned: `core::tui::LanguageId::Endo` and `core::tui::registerEndoHighlighter`. Both change the
*shape* of a call, so neither is rewritten; each carries a `note` with the `SyntaxHighlighterRegistry`
call that replaces it, which is where endo's C1 migration will read it, beside every other rename
that pull request applies.

`.agent/guides/consumer-migration.md` gains a table of what every kind matches and which tool
consumes it, and a paragraph on why `removed` is the one that runs backwards.

## Gates

| Gate | Result |
|---|---|
| `python -m unittest discover -s tools/migrate -p '*_test.py'` | **66 cases** (51 → 66), one failure — B3's in-flight tree, below |
| `check-renames.py` against a fresh `git archive HEAD` | `440 rows, 412 with a delivered core-cpp target, 8 pending, 2 removed … 0 failure(s)` |
| `python scripts/ruff-format.py --check` | `12 file(s) are formatted with ruff 0.16.8` |
| Other lanes' staged work after my commit | 13 paths, untouched — `git commit --only -- <paths>` did what the new rule says it does |

## The shared tree is red again, for the third time, and again it is not the table

Task B3's EventSource → IoBackend rename is in this working tree **partly staged and uncommitted**,
so `check-renames.py` reports ten failures here while `HEAD` is clean:

- seven include rows whose target headers B3 is deleting (`net/EventSource.hpp`,
  `net/DefaultEventSource.hpp`, `net/PollEventSource.hpp`, `net/EpollEventSource.hpp`,
  `net/KqueueEventSource.hpp`, `net/testing/EventSourceBackends.hpp`,
  `net/testing/ScriptedEventSource.hpp`);
- the three pending rows whose targets B3 is landing (`IoBackend`, `Interest`, `makeDefaultBackend`).

**All ten are B3's to resolve in the same commit**, and the messages say which row and what to do.
I did not touch them: flipping the pending rows would pass in this tree and fail CI against `HEAD`,
which is the opposite mistake. Verified green against `HEAD` before pushing.

This is the third time the duplicate-red problem has bitten (`syncRun`, `IoBackend`, now B3's
include rows), and it is the same one case each time: `check_renames_test.py::test_this_tree_passes_the_gate`
uses the live shared tree as a unit-test fixture, so a lane's in-flight file makes the *codemod
suite* red as well as the gate. **My recommendation from round 2 stands and is now better
evidenced:** drop that one case and leave the real-tree assertion to `core-cpp.migrate-renames`,
which runs the identical check under the same label in every build. Still not applied on my own
authority — it removes an assertion while it is red, which needs someone else to agree that is not
just making the red go away.

## CI

| Run | Workflow | Commit | Result |
|---|---|---|---|
| **35542004684** | `Build` | `a8e5212` | **success**, all 24 jobs |
| **35542004676** | `Docs` | `a8e5212` | **success** |
| **35542238141** | `Build` | `7838b53` (contains `a8e5212`) | **success**, all 24 jobs |

The `style` job's log on `a8e5212`:

```
ruff-format.py: 10 file(s) are formatted with ruff 0.16.8
check-renames: 440 rows, 412 with a delivered core-cpp target, 8 pending, 2 removed, over 152 public headers: 0 failure(s)
ran=66 failures=0 errors=0 skipped=0
```

`ran=66 … skipped=0` is worth reading twice: on a clean checkout **all 66 cases pass, including
`test_this_tree_passes_the_gate`**, which confirms the local red is B3's in-flight tree and nothing
else. The libclang cases ran for real, as R69 requires. (CI counts 10 Python files to this tree's
12: `scripts/ruff-format.py` lists everything git knows about, and this shared checkout has two
untracked `.py` files belonging to other lanes — both of which already pass the formatter.)

---

# Round 4 — Rulings R76, R77, R78, R79

(The controller called this round 3; numbered 4 here to follow this report's own sections.)

Three commits, deliberately separate where the reasoning is separate:

| Commit | Ruling | Shape |
|---|---|---|
| `1dba5fa test(migrate): the codemod suite stops using the working tree as a fixture` | R76 | 1 file, +10 −3 |
| `aee02ec build: the Python is snake_case, and ruff's default lints are on` | R77, R78 | 13 files, +239 −207 |
| `b528631 docs(migrate): proving a mechanical pass was mechanical` | R79 | 2 files, +55 −2 |

## R76 — the tree fixture is gone, in its own commit

`test_this_tree_passes_the_gate` removed; the class keeps a docstring saying why there is no such
case, so the next person does not helpfully add it back. What remains is hermetic: the sandbox
refusal cases, the enum- and class-scoped cases, the removed-kind cases, and
`test_the_gate_checked_something`, which reads only the table.

The suite is **65 cases** and no longer goes red when another lane has a half-written file in the
tree. The tree-level assertion is unchanged, in `core-cpp.migrate-renames` under `ctest -L hygiene`.

## R77 — snake_case, 59 identifiers across seven files

Renamed: the functions, helpers and locals this repository owns.

**Left alone on purpose**, and this is the part a blanket rename would have got wrong: unittest's
API (`assertEqual`, `subTest`, `skipUnless`, …) is not ours to rename, and neither are the C++ and
consumer symbols that appear in fixtures and prose — `localPort`, `boundPort`, `flagStore`,
`optionList`, `helpDisplayStyle`, `helpText`, `toInteger`, `errorLog`, `registerEndoHighlighter`,
`digitValue`, `onWindows`, `onLinux`, `writeSome`. Renaming one of those in a fixture would quietly
change what the fixture tests. The rename map was built from a scan of every camelBack identifier in
the directory, then split by hand into "ours" and "not ours", and a second scan confirmed nothing of
ours was left behind.

One mechanical near-miss worth recording: the first pass used a `(?<![\w.])` lookbehind, which
skipped every **attribute access** — `table.textRows(...)` stayed camelBack while `def text_rows`
was renamed. Caught by reading the result rather than the replacement count. The second pass used
`\b`, which is safe precisely because the map holds only identifiers this repository owns.

## R78 — ruff's default lint set: zero findings

`ruff.toml` now states `select = ["E4", "E7", "E9", "F"]` rather than inheriting the default, so a
future ruff cannot widen or narrow this gate by changing its mind about what "default" means.

**It found nothing** — on the tree before the rename and after it — so, as ruled, there is no lint
diff buried in this round. Nothing to stop and report.

**Proved it refuses**, because a gate never seen to refuse is not a gate. Planted
`return undefined_name_that_does_not_exist` in `renames.py`:

```
242 | def _planted() -> str:
243 |     return undefined_name_that_does_not_exist
    |            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Found 1 error.
python-style.py: the findings above are lint, not layout; fix them by hand
```

exit 1; removed it, exit 0.

**Enabling the linter retired all three `# noqa` comments**, which matters beyond tidiness: a
diagnostic-muting comment is the Python spelling of `NOLINT`, and this repository forbids those.
Two were inert once the real rule set was known (`F401` on a re-export that is genuinely used,
`BLE001` from a set that is not selected). The third was load-bearing — `E402` on `import renames`
sitting under a `sys.path.insert` — and is gone for a better reason than a suppression: Python
already puts a script's own directory first on `sys.path`, so the surgery was never needed. The
import moved to the top and the insert went away.

**`scripts/ruff-format.py` → `scripts/python-style.py`.** It now runs both halves, and both report
before either fails, so one invocation tells you everything. A name about one of ruff's subcommands
had become narrower than the job. It still cannot be `ruff.py`, for the shadowing reason recorded
last round.

## R79 — the byte-identity proof

In `.agent/guides/consumer-migration.md`, as a shell loop a consumer PR can paste: create a worktree
at the pre-image commit, run the codemod, then for every changed file re-derive the post-image from
the pre-image and `cmp`. Written with what it catches (an unintended rewrite; a hand edit smuggled
into a mechanical commit) and what it does not (a *correct* rewrite to a *wrong* target — the drift
gate's job), and the note that it pairs with idempotence: idempotence says running twice changes
nothing, this says running once changed nothing but what was intended.

`rewrite.py` already reports every row it fired and how often, per file, which is the substitution
list the check needs; the guide says to keep that log in the pull request.

## Gates

| Gate | Result |
|---|---|
| `python scripts/python-style.py --check` | `12 file(s) are formatted and lint clean with ruff 0.16.8` |
| `python -m unittest discover -s tools/migrate -p '*_test.py'` | `Ran 65 tests … OK` |
| `python tools/migrate/check-renames.py` | `0 failure(s)` — and green on a fresh `git archive HEAD` too |
| `python scripts/clang-format.py --check` | `391 file(s) are formatted` |
| `ruff check` over a fresh `HEAD` export | `All checks passed!` |
| Other lane's staged work after three commits | 13 paths, byte-identical before and after |

## Two things to know about the shared tree

1. **The private index was needed once more, and refreshed once more.** `CHANGELOG.md` again held
   another lane's hunks, so R79's commit went through a `GIT_INDEX_FILE` with a trimmed patch. The
   shared index was then stale against the new `HEAD` — it showed my two files as *deletions*, which
   is the hazard from round 1 — so it was refreshed and the other lane's 13 staged paths re-applied.
   Verified identical: `13 files changed, 1447 deletions(-)` before and after.

   A wrinkle worth passing on: building that patch by piping `git diff --no-index` through
   `python -c` **corrupted it**, because Python's stdout translates `\n` to `\r\n` on Windows and
   `git apply` then refuses with "patch does not apply". Write the patch to a file in binary
   instead. That cost ten minutes and would cost anyone else the same.

2. **`scripts/python-style.py` with no arguments formats every Python file git knows about**,
   including another lane's in-flight ones. `scripts/clang-format.py` has had exactly this property
   since before me, and both accept explicit paths, so the "format what you touched" rule is
   supported — but in a four-lane checkout the default is a loaded gun. Not changed, because
   changing one and not the other would be worse; flagging it. (I checked: the other lane's
   `scripts/check-upstream-drift.py` was *not* touched by any run of mine — its diff is their own
   docstring — and it already passes both halves of the gate.

   **My CRLF claim about that file was wrong, and the correction is worth more than the claim.**
   `git ls-files --eol` reports `i/lf w/lf attr/text=auto eol=lf`: LF in the index, LF in the
   worktree, zero CR bytes on disk. What misled me was `git diff`'s
   *"CRLF will be replaced by LF the next time Git touches it"* — which describes what git would do
   to a blob, not what the file is now, and which was true when I first saw it and stale when I
   reported it. **Ask `git ls-files --eol`, not a warning line and not `grep`**: it answers the
   actual question and prints both sides plus the attributes in force. The controller's own
   `grep -c $''` suggested the same wrong answer, so this is a trap that catches two readers in a
   row.)

## CI

| Run | Workflow | Commit | Result |
|---|---|---|---|
| **35543400100** | `Build` | `b528631` | **success**, all 24 jobs |
| **35543400145** | `Docs` | `b528631` | **success** |

The `style` job's log, covering all four rulings' gates in one place:

```
clang-format.py: 383 file(s) are formatted with clang-format 22.1.8
python-style.py: 12 file(s) are formatted and lint clean with ruff 0.16.8
check-renames: 440 rows, 412 with a delivered core-cpp target, 8 pending, 2 removed, over 152 public headers: 0 failure(s)
ran=65 failures=0 errors=0 skipped=0
```

`lint clean` is the new half (R78), `ran=65` is the suite after R76 dropped the tree fixture, and
`skipped=0` still holds, so the libclang cases ran for real.

## A coordination note on R79

The ruling asked me to let the B2 lane write the byte-identity recipe, since they have the reasoning
fresh, and to coordinate rather than both editing a shared file. **That ruling crossed with my push:
the recipe was already written and pushed as `b528631` before I received it.**

Checked before reporting: `.agent/guides/consumer-migration.md` has no uncommitted changes from any
lane, and nothing touching it has landed on `origin/master` since, so **no duplicate work has
happened yet** — the collision is prospective, not actual.

What I wrote is second-hand: derived from the controller's description of the B2 review, not from
having done it. So B2 is better placed than I am to check two things, and should **edit the existing
section rather than write a second one**:

- whether the shell loop matches what they actually ran (mine creates a worktree at `HEAD~1` and
  `cmp`s each re-derived post-image; theirs stripped whitespace before comparing, which mine does
  not, and that difference matters for a codemod that can change indentation);
- whether "apply only the intended substitution" is better expressed as the row list from
  `rewrite.py`'s per-file report, which is how I wrote it, or as an explicit `sed` list, which is
  how their one-off worked.

The section sits directly after "What each row's `kind` means, and which tool consumes it" and
before "The table is checked against the delivered headers", so the three read in order: what the
rows are, how to prove the pass only applied them, and how the rows themselves are gated.

---

# Round 5 — Ruling R89, and two corrections

**Commit:** `6d92c54 build: a bare run of either formatter is an error, not "format everything"`,
8 files, +87 −22.

## R89 — both formatters refuse a bare run

`clang-format.py` and `python-style.py` each now take **either** the paths you touched **or** an
explicit `--all`. Every branch was checked rather than assumed:

| Invocation | Result |
|---|---|
| `python scripts/clang-format.py` | exit **2**, naming both ways to mean it |
| `python scripts/python-style.py` | exit **2**, same |
| `python scripts/python-style.py --all <path>` | exit **2** — `--all` and paths mean two different things at once |
| `python scripts/python-style.py --all --check` | 0 |
| `python scripts/clang-format.py --check src/core/Assert.hpp` | 0, `1 file(s) are formatted` |

CI passes `--all`; so do the workflow checklist in `AGENT.md`, `CONTRIBUTING.md`, the C++ rulebook
and the source map. Two second-order fixes the change forced: both scripts' *"run this to fix it"*
hints named a bare command that is now refused, and one of my generated `print` calls was an
f-string with no placeholder — which **the linter enabled last round caught**, on its first
outing against new code.

## The rulebook now bans the act, not the spelling

One bullet added beside the `NOLINT` rule in `.agent/rules/cpp-guidelines.md`: the ban covers
`# noqa`, `# type: ignore`, `// eslint-disable`, `#[allow(...)]` and any other language's
suppression comment. The case that carries it is the one from round 4 — three retired, none needed
to stay, and the load-bearing one went away for a **better reason than suppression**. The sentence
the rule ends on: *a suppression comment is usually a fix nobody looked for.*

## The failure class, named where it is defended against

`.agent/guides/consumer-migration.md` now names it beside the purity proof: **a mechanical check
believed from its summary rather than its output.** Three of this task's near-misses were the same
one — a rename reporting 59 replacements that had skipped every attribute access, an `awk`
reporting no over-long lines that a `head` had truncated, and a `git diff` CRLF warning read as a
present fact when it described a past one. Each was a clean result to the wrong question.

## Correction: the CRLF report was wrong

Recorded in round 4's section above. `git ls-files --eol` says `i/lf w/lf` for the file I reported;
the `git diff` warning I trusted describes what git would do to a blob, not what the file is. **Ask
`git ls-files --eol`.** The controller's `grep` gave the same wrong answer, so it catches two
readers running.

## The R79 coordination resolved itself, and B2 corrected me

`7ddbe1d docs(migrate): the purity proof is independent of the tool it checks` landed on top of
`6d92c54`. B2 rewrote the recipe with their real method, and on the one detail I had flagged as
uncertain they were right and I was wrong: I had said to derive the substitution list from
`rewrite.py`'s per-file report. **That is circular** — it would confirm the tool did what the tool
said it did. Their version has you write the substitutions yourself, strips whitespace before
comparing, and carries a measured result against core-cpp's own rename (`8a88ce0`, 53 call sites):
`pure: 16   not pure: 2`, naming the two files that carried hand work. My framing paragraph on the
failure class survived alongside it, so the section reads as one piece.

Flagging the uncertainty in the report rather than picking a side is what let that land as a
correction instead of a conflict.

# Fix round 1 — 1 Critical, 7 Important, R91–R95

Four commits: `c973592`, `6d93980`, `4bde234`, `a4d91f5`. Every fix has a case that fails before it.

## The round was not split, and nothing was dropped

The offer to split was real and I did not take it. The reason is that the round divides cleanly by
*artefact*, not by finding: three of the four commits touch files nothing else in this round
touches, and the fourth is prose. Carrying it whole cost one extra RED/GREEN cycle, not a merge.

| Finding | Commit | Landed |
|---|---|---|
| I1 raw string, I2 comment mask, I3 non-UTF-8, I4 CRLF, M6 | `c973592` | yes |
| R91 override chain, I6 parse failure, I7, M7 | `6d93980` | yes |
| R92 pending target, R93 both claims, R94 scope key, R95 both arms | `4bde234` | yes |
| I5 purity proof, M2 ruff names, M3 row count, M4 ruff bullets, M5 aliases | `a4d91f5` | yes |

## R92 — a pending row carries its target

RED: `test_a_pending_row_without_a_target_is_refused` and
`test_a_pending_row_whose_target_names_no_symbol_is_refused`, both `FAIL` (the loader accepted both
rows). GREEN after `_row()` requires `target.symbol` when `status` is `pending`.

**The real table needed no change.** All four pending rows already carry a target with a symbol —
including `FastCache::SyncRun`, which another lane marked delivered when B1 landed `SyncRun.hpp`.
So this is a door closing on a gap that had already been walked through once and repaired, not a
live drift. That is worth saying because the review named the SyncRun row as the case to fix.

## R93 — the two structural claims, made true rather than reworded

**Claim one: the `removed` kind has two doors.** `rewrite.py`'s docstring said the loader refuses to
hand a removed row to a rewrite tool *and* `_patterns_for` raises on one. Only the second was a
door. `text_rows()`/`semantic_rows()` filtered on `apply`, and the schema forces a removed row's
`apply` to `none` — so they excluded one, but as a consequence of the schema. That is the schema's
door seen from downstream.

RED: `test_the_loader_is_a_door_of_its_own` builds `renames.Row(kind="removed", apply="text")`
directly and asserts both accessors return `[]`. It failed: the row came back from `text_rows`.
GREEN after `Table._rewritable()` tests the kind.

**Claim two: every arm reads a symbol through one walk.** `check-renames.py`'s docstring said so;
`_check_delivered` carried its own copy of the prefix walk. The R74 fix therefore reached two arms
of three and read as reaching all three.

RED: `TheDeliveredArmUsesTheSharedWalk` patches the module-level function to return `"PATCHED"` and
asserts the delivered arm's verdict changes. It failed — the delivered arm did not notice. GREEN
after extracting `qualified_failure(text, components) -> str | None`, which returns the *reason*
rather than a bool so the delivered arm can use it as it stands; `declares_qualified()` is now
`qualified_failure(...) is None`.

A patch-the-function test is the only kind that can distinguish "both arms are correct" from "both
arms share one implementation". The five existing delivered-arm cases all passed against the copy.

## R94 — `scope` in the duplicate key

RED: `test_two_members_of_different_classes_may_share_a_name` — two `member` rows, `ISocket::Close`
and `AsyncQueue::Close`, same profile. `ERROR`: the loader refused the table as a duplicate.
`test_two_members_of_the_same_class_are_still_refused` passed already and holds the other side.
GREEN after keying on `(profile, kind, scope, source)`.

Read, Write, Stop and Close are exactly the members Phase B renames, several classes each, so this
would have been hit by the first B-phase lane to add a second one.

## R95 — both arms, and the amendment was right

Arm (a), an unresolvable `${X}`, resolved to `[]`. Arm (b), a `${X}` a `list(APPEND)` extends,
resolved to whatever the `set()` beside it held — an answer that is confidently short, which an
"unresolvable reference" check passes because the reference does resolve.

RED, five cases in `TheGateSaysWhenItCouldNotLook`:

```
FAIL:  test_a_literal_header_list_resolves            (public_headers returned a set, not a pair)
ERROR: test_a_list_append_is_resolved_too
ERROR: test_an_unknown_variable_is_refused_not_read_as_empty
ERROR: test_a_variable_mutated_by_an_unmodelled_list_command_is_refused
FAIL:  test_the_gate_reports_an_unreadable_header_list_as_a_failure
```

GREEN after `public_headers()` returns `(headers, problems)`; `_cmake_variables()` follows
`list(APPEND|PREPEND|INSERT)` and records any other `list()` sub-command as unfollowable; and
`_header_tokens()` reports a `${...}` it cannot resolve in full rather than answering short.

`validate()` now reads the tree **before** the table and reports its problems whatever the table
says, because a table that fails to load must not swallow a fact about the tree — the same silence
one level up. That is what the fifth case pins.

**Arm (b) had already cost a workaround.** `src/core/async/CMakeLists.txt` carried a comment saying
it reaches HEADERS through a `set()` of its own "rather than a `list(APPEND)` over the whole list"
because "a `list(APPEND)` is invisible to it, and the row would then read as private". Somebody had
already shaped a build file around this gap. The reason to keep that list literal stands on its own
(a reader sees what the module publishes in one place); the claim about the tool does not, so the
comment is corrected in the same commit.

Header count before and after, over this tree: **155 both times.** The change removes a blind spot
rather than filling one here — no module currently reaches HEADERS through an appended variable,
because the one that would have was written around the bug.

## I5 — the proof failed on every consumer pull request, and the repository could not tell

This is the one that mattered most, and the reason it survived review is worth recording: run
against `8a88ce0` — the commit the guide cites, eighteen files, **all modified** — the snippet
works and prints the answer the prose quotes. The defect only appears on a commit that adds or
deletes a file, and a consumer migration is mostly deletions.

RED, the guide's snippet verbatim against `e7963de` (adds, deletes, renames under `src`):

```
NOT PURE: src/core/net/AsyncBufferedReader_test.cpp
fatal: path 'src/core/net/BackendParity_test.cpp' exists on disk, but not in 'e7963de~1'
NOT PURE: src/core/net/BackendParity_test.cpp
NOT PURE: src/core/net/CMakeLists.txt
fatal: path 'src/core/net/DefaultEventSource.cpp' does not exist in 'e7963de'
NOT PURE: src/core/net/DefaultEventSource.cpp
...
```

Nine `fatal:` lines among the verdicts, and every added and deleted path counted NOT PURE.

GREEN, the corrected snippet:

```
$ ... 8a88ce0
NOT PURE: src/core/net/NetError.hpp
NOT PURE: src/core/net/NetError_test.cpp
pure: 16   not pure: 2

$ ... e7963de
pure: 0   not pure: 11        # 11 modified files, no fatal:
```

Three changes, and the third was not in the finding:

1. `--diff-filter=M`. An added file has no pre-image and a deleted one no post-image; neither is a
   transform this can check.
2. `-z` with `while IFS= read -r -d ''`. `for file in $(...)` word-splits and globs, and
   `--name-only` shell-quotes a non-ASCII path into a string git then cannot find.
3. **The snippet quoted an answer it did not print.** The prose says it answers
   `pure: 16   not pure: 2`; the loop only echoed failures. It counts now — and reads its file list
   from a file rather than a pipe, because a `while` on the right of a pipe runs in a subshell and
   brings both counters back as zero. That would have printed `pure: 0   not pure: 0` on a clean
   run and on an empty one alike, which is this guide's own subject with the guide's name on it.

What the filter leaves out, the guide now says to review as itself. In a consumer migration that is
most of the commit, and a deletion has no bytes to be subtly wrong about.

## M2, M3, M4, M5 — stale claims

- `ruff.toml` and `.ruff-version` named `scripts/ruff.py`, **never committed under that name** and
  never committable: a module called `ruff.py` on `sys.path` shadows the `ruff` package the script
  imports to find the pinned binary. `.ruff-version` also said the linter is not run, which
  `ruff.toml` two files away contradicts. Both now name `scripts/python-style.py` and say both
  halves run.
- The CHANGELOG's `438-row` count is gone. It was 438 at C0 and is **484 today**, from four lanes,
  and no lane will think to update a number in a prose bullet. A count in prose is a claim with a
  maintainer nobody appointed.
- The two contradictory `[Unreleased]` ruff bullets are one bullet.
- The CHANGELOG described the purity proof as re-deriving the post-image "by applying only the rows
  the profile reported" — the circularity `7ddbe1d` removed from the guide, still sitting in the
  changelog.
- The guide lists a namespace **alias** beside a namespace definition as a hand edit. The codemod
  leaves both alone; only the definition was written down.

## Gates

| Gate | Result |
|---|---|
| `python -m unittest discover -s tools/migrate -p "*_test.py"` | `Ran 90 tests ... OK`, **0 skipped** (the libclang cases run for real) |
| `ctest -R core-cpp.migrate` | 2/2 passed |
| `ctest -L hygiene` | 9/10 passed; see below |
| `python scripts/python-style.py --check --all` | `12 file(s) are formatted and lint clean with ruff 0.16.8` |
| `python tools/migrate/check-renames.py` | `484 rows, 454 with a delivered core-cpp target, 4 pending, 8 removed, over 155 public headers: 0 failure(s)` |
| `mkdocs build --strict` | not run: neither `docs/`, `mkdocs.yml` nor a public header's comments changed |

`ctest -L hygiene`'s one red is `core-cpp.async-link-smoke` **(Not Run)**:
`Unable to find executable: .../src/core/async/core-cpp-async-link-smoke.exe`. My tree is configured
but its C++ targets are not built. It is a state of my build directory, not a failure of the tree —
but it is a red, and a red I am explaining away, so it is written down rather than filtered out of
the table.

## Shared-tree discipline

Both commits used `git commit --only -F <file> -- <paths>` and were checked with `git show --stat`.
`tools/migrate/renames.json` was modified in the working tree throughout this round by the B13a
lane (a `removed` row's `note`) and is **not** in either commit. `src/core/async/CMakeLists.txt` was
clean before I touched it and its whole diff is mine. `CHANGELOG.md` was clean; I read the full
diff before staging.

One note for the message-file habit: `git commit --only -- <paths> -m <msg>` does not work — the
`--` swallows `-m` and its argument as pathspecs, and git reports them as unmatched pathspecs. `-F`
*before* the `--` is the form that does.
