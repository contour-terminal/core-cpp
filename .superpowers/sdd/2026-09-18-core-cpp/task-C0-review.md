# Task C0 review: migration tooling in `tools/migrate/`

**Reviewer:** review-C0
**Scope:** `ddddc67`, `748142f`, `ac9e2e2`, `a8e5212`, `1dba5fa`, `aee02ec`, `b528631`
**Baseline used for every experiment:** a clean `git archive b528631` export, because the shared
working tree carries another lane's in-flight `src/core/net/` rewrite (440 rows at `b528631`, 472 in
the tree today). Nothing in the repository was edited; all probes ran in the scratchpad.

---

## Verdicts

**Spec compliance: PASS.** Every item the dispatch names is delivered, and the three rulings are
real rather than claimed. Detail in §1.

**Task quality: CHANGES REQUESTED.** One Critical and seven Important findings. The mechanical
codemod is well built and genuinely anchored; the semantic pass is not yet fit for the one job it
exists for (Task C4), and three of `rewrite.py`'s stated boundaries do not hold under inputs six
consumer trees will contain. Detail in §2 onward.

**Verdict line: Changes requested.**

---

## 1. Spec compliance

| Dispatch item | Verdict | How I checked |
|---|---|---|
| R67 — stdlib `unittest`, registered as a ctest with label `hygiene` | met | `tests/CMakeLists.txt:110-139`; both tests registered, and registered *with a reporting skip* when no interpreter is found |
| R68 — `check-renames.py`, registered, validates the target side | met | ran it against the clean export: `440 rows, 412 with a delivered core-cpp target, 8 pending, 2 removed, over 152 public headers: 0 failure(s)` |
| R69 — one CI job installs libclang and refuses a skip | met | `.github/workflows/build.yml`, `style` job: `pip install "libclang==18.1.1"` then a runner that emits `::error::` per skip and `sys.exit(0 if result.wasSuccessful() and not result.skipped else 1)`. Read, not run; the report's run/job IDs are consistent with it |
| Four named tests exist and were seen RED | met | reproduced two of the REDs myself (§4, §5) |
| `rewrite.py` shape: `--profile`, in place, path-bounded, idempotent, per-file report | met | `rewrite.py:133-193`; `test_it_never_walks_outside_the_given_path`, `test_dry_run_writes_nothing` |
| `semantic_rename.py`: unions several compile databases, filters by `--decl-paths`, applies edits back to front | met | `semantic_rename.py:145-211`; verified by running the suite with libclang installed locally |
| Python style question answered; a formatter is a decision, not a detail | met, and then some | `ruff.toml`, `.ruff-version`, `scripts/python-style.py`, `scripts/tool-versions.py` |
| `CHANGELOG.md` under `[Unreleased]`, guide, plan edited | met | plan records R67/R68/R69 at `docs/superpowers/plans/2026-09-18-core-cpp.md:933-937` |
| Concurrency: `tools/migrate/` plus `tests/cmake/` only | met | `git show --stat` over the seven commits touches no file under `src/` |

The three table facts the dispatch called out are all correct in the data: `coro`/`endo::coro` →
`core::async` (no `core::coro` row anywhere), `endo::Generator` → `core::Generator` with target
`core/Generator.hpp` (base, not async), and the six `tui/completer/*.hpp` include rows targeting
`core/tui/completer/`. The gate proves each of those against the delivered headers on every build,
so they cannot rot silently.

Local gate runs on the clean export: suite `Ran 65 tests … OK`; `check-renames.py` `0 failure(s)`.
I did not configure a build tree, so I did not run the two ctests as ctests — I ran the exact
commands `tests/CMakeLists.txt:121-126` registers.

---

## 2. Critical

### C1. `semantic_rename.py` does not follow virtual overrides, so Task C4 produces a tree that does not compile

`semantic_rename.py:170` keys a row by `(qualified_name(declaration.semantic_parent), spelling)`.
A declaration that *overrides* the row's `scope` has a different semantic parent, so it is never
matched — neither the override's own declaration nor any call site typed to the derived class.

Verified by running, with libclang 18 installed locally:

```cpp
// input
struct ISocket  { virtual int Read(char*, int) = 0; };
struct TcpSocket : ISocket { int Read(char*, int) override; };
int viaBase(FastCache::ISocket& s)    { return s.Read(b, 4); }
int viaDerived(FastCache::TcpSocket& s) { return s.Read(b, 4); }
```

```cpp
// output of collect_edits(..., scope="FastCache::ISocket") + apply_edits
struct ISocket  { virtual int read(char*, int) = 0; };   // renamed
struct TcpSocket : ISocket { int Read(char*, int) override; };  // NOT renamed
int viaBase(...)    { return s.read(b, 4); }   // renamed
int viaDerived(...) { return s.Read(b, 4); }   // NOT renamed
```

The result is an `override` of a virtual that no longer exists — a hard compile error — plus every
call site typed to a concrete socket left behind. `FastCache::ISocket` is an *interface*; the
existence of implementations is the reason a semantic pass was commissioned at all. The rows at
`renames.json` rows[414]-[421] are exactly this shape: `IClock::Now/Refresh`, `ISocket::Read/Write/
Close`.

**Why the fixture cannot see it:** `semantic_rename_test.py:32-54` uses `FastCache::ISocket` and
`Other::Reader`, two *unrelated* classes. The dispatch asked whether that fixture is strong enough
for C4. It is not: it tests the one axis (same spelling, different scope) and not the axis fastcached
actually has (same scope-by-inheritance, same spelling).

**Fix:** accept a declaration whose own scope matches the row *or* any of whose transitively
overridden declarations' scope matches — `cursor.get_overriden_cursors()` in libclang's Python
bindings, walked to a fixed point. Add a fixture with a base, a derived override, and calls through
both.

---

## 3. Important

### I1. An `#include` inside a raw string literal is rewritten — the one boundary the tool declares absolute

`rewrite.py:99-104` applies every `include` row to the **raw text**, before `_mask_literals()` runs.
The comment says why ("the quoted form of the directive is not a string literal"), which is true of
`#include "x.hpp"` and false of a directive that happens to sit at the start of a line inside a raw
string. Verified:

```cpp
// in
static char const* Source = R"(
#include <net/EventLoop.hpp>
)";
// out
static char const* Source = R"(
#include <core/net/EventLoop.hpp>
)";
```

`rewrite.py:13-15` and the guide both state "A codemod may change what the code says; it must never
change what the program sends." Embedded C++ snippets in raw strings are ordinary in compiler,
parser and highlighter test suites — core-cpp's own `src/core/tui/GenericSyntaxHighlighter_test.cpp`
has fifty raw strings. This is precisely the failure the dispatch warns about: a consumer's test data
silently changes and nothing fails until something unrelated does.

**Fix:** mask raw strings before the include pass (ordinary `"..."` strings cannot contain a
line-initial directive, so masking raw strings alone is enough), or run one combined tokenising scan.

### I2. The literal masker also runs over comments, so an unterminated `R"(` in prose masks real code

`LITERAL` (`rewrite.py:51-54`) is applied to text in which comments are still present, and its
raw-string alternative is the one rule that is not line-confined (`re.DOTALL`, `.*?`). A comment that
mentions `R"(` without a matching `)"` therefore opens a mask that runs forward to the next `)"`
anywhere in the file. Verified:

```cpp
// a raw string starts with R"( and ends with the mirror
net::EventLoop loop;          // <- NOT rewritten
int f() { return 1; }
auto s = R"(hello)";
net::ISocket* p;              // <- rewritten
```

The two lines between the comment and the real raw string were swallowed. The blast radius is a
*missed* rewrite, which the consumer's compiler will catch, so this is not silent corruption — but it
defeats the "mechanical and reviewable" property and makes the per-file replacement counts wrong,
which is the input the R79 proof depends on.

Related and smaller: the character-literal rule means an apostrophe pair in a comment can hide a
rename on that line (`// don't rename net::EventLoop, it's prose` is left alone — verified). Harmless
in a comment; the `R"(` case is the one that reaches code.

**Fix:** one scan whose alternation is `comment | rawstring | string | char`, where comment matches
are passed through to the rename patterns and the other three are masked. `check-renames.py:53-73`
already gets the ordering right for its own purpose (`STRING.sub('""', COMMENT.sub(" ", text))`);
`rewrite.py` needs the same lexer shape but with comments kept.

### I3. A non-UTF-8 source aborts the run mid-tree, after partial writes, with an unhandled traceback

`rewrite.py:139` — `path.read_text(encoding="utf-8")` is unguarded. Verified over a three-file
directory containing one latin-1 byte:

```
Crlf.cpp:  written
Latin.cpp: UnicodeDecodeError, traceback, process dies
Plain.cpp: never processed
```

No summary line, no exit status a script can act on, and the tree left half-converted. Idempotence
means a re-run recovers, but only if the operator knows that; the message they get is a stack trace.
Across six repositories one latin-1 copyright sign or umlaut is likely (fastcached in particular).

**Fix:** try/except per file, print `skipped <path>: not UTF-8`, count it, and return non-zero at the
end so a CI-run migration fails loudly rather than half-succeeding.

### I4. Every file the codemod changes is silently converted from CRLF to LF

`rewrite.py:139` reads with universal newlines and `rewrite.py:150` writes `newline="\n"`. Verified:
a file whose bytes were `#include <net/EventLoop.hpp>\r\nnet::EventLoop loop;\r\nint untouched = 1;\r\n`
came out entirely LF — including the line the codemod did not touch.

On a Windows checkout with `core.autocrlf=true` git hides this at staging time; on a repository that
pins `eol=crlf` for any path, or a checkout with `autocrlf=false` and CRLF in the index, the
"mechanical" commit rewrites every line of every changed file. That destroys exactly the
reviewability this task exists to provide, and the R79 byte-identity proof cannot see it because both
sides of the comparison pass through the same tool.

**Fix:** read with `newline=""` and write the same way (or work in bytes), so the file's existing line
endings survive. Note in the guide that the tool preserves line endings.

### I5. The R79 byte-identity recipe cannot be run on a real migration commit

`.agent/guides/consumer-migration.md:101-112`. I ran it verbatim against a throwaway repository whose
codemod commit does what the guide's own step 4 tells a consumer to do — convert, delete the
superseded copy, and add a file:

```
--- the log the guide tells the PR to keep: ---
.../src/New.cpp: 1 replacement(s)
        1  namespace net -> core::net
rewrite --profile contour: 1 file changed, 1 replacement(s) from 1 of 144 rows
--- the loop ---
NOT PURE: src/New.cpp
NOT PURE: src/Old.cpp
```

Three separate problems, all deterministic:

1. **The substitution log is captured from the wrong run.** By the time `git worktree add ../before
   HEAD~1` is possible, the codemod commit is already `HEAD`, so `rewrite.py --profile endo src |
   tee ../rewrite.log` is a no-op over the converted tree. The log that reaches the pull request
   describes nothing the commit did. The log the check needs is the one from the *original* run and
   has to be kept then, which the guide does not say.
2. **It mutates the tree it is verifying.** That same command rewrote `src/New.cpp` — a file written
   by hand after the codemod ran. A verification step must not write.
3. **Every added and every deleted path reports `NOT PURE`.** `git diff --name-only HEAD~1` includes
   both; a deleted file has no post-image and an added file has no pre-image, where `rewrite.py`
   exits 1 with `no such path` and `cmp` then fails. A consumer PR that deletes `src/tui` gets
   hundreds of false `NOT PURE` lines on its first run — a wall of red that says the codemod
   misbehaved when it did not, which is the worst kind of false alarm to hand six teams.

Smaller, same recipe: `git diff --name-only` C-quotes unusual paths and `while read -r file` strips
leading and trailing whitespace from each line, so a path with either breaks silently.

**Fix:** `git diff -z --name-only --diff-filter=M HEAD~1 -- src | while IFS= read -r -d '' file`,
drop the second `rewrite.py` invocation over `src` entirely, and say that the substitution log is an
artefact of the original run to be pasted into the PR body.

On the dispatch's three questions about this section: it **does** say what to substitute (the profile,
and `rewrite.py` does report every row it fired and how often, per file — that part is genuinely
good), it does **not** address the whitespace/quoting strip, and it **does** state clearly what it
does not catch (a correct rewrite to a wrong target, which is the gate's job) — that paragraph is
well judged and worth keeping verbatim.

### I6. `semantic_rename.py` never inspects `unit.diagnostics`, so a stale compile database yields a confident partial rename

`semantic_rename.py:160` parses and walks whatever comes back. Verified: a translation unit with a
missing `#include` still produced and applied edits, and printed nothing about the failure. Where the
missing header is the one that *defines* the class, `cursor.referenced` is null, the call site is
skipped, and the summary line reports a confident count of the edits it did make.

`.agent/rules/build-and-toolchain.md`: a gate that does not report reads as passed. For C4 this is
the difference between "53 call sites moved" and "53 of 80 call sites moved, and you will find the
other 27 on the platform you did not build".

**Fix:** collect `unit.diagnostics` at severity >= error, print them with the TU name, and refuse to
write unless `--allow-parse-errors` is given.

### I7. libclang's host default target decides which `#ifdef` branches are live, and nothing says so

Verified on Windows: a `#ifdef _WIN32 / #else` pair had the `_WIN32` branch renamed and the `#else`
branch left alone, from a compile database whose command line named no target. The design note at
`semantic_rename.py:15-16` and the guide both say to pass every platform's compile database — but a
database does not carry a target triple, so parsing fastcached's Linux database on a Windows host
activates Windows branches for *both* databases and misses the POSIX call sites entirely. The union
only works if each database is parsed on its own host, or `--target=` is supplied.

Also silently missed, and inherent to libclang rather than to this tool: a member call on a dependent
type inside a template (`template <typename T> … s.Read(...)`) — verified untouched.

**Fix:** document that each compile database must be parsed on the host that produced it (or accept a
`--target` per database), and print a count of unresolved/dependent member expressions whose spelling
matches a row, so the operator knows how many the tool could not decide.

---

## 4. The three doors of the `removed` kind — verified, and there are two

I built the row the report says is structurally impossible (`kind: removed`, `apply: text`, with a
`to`) and ran it through three builds of the tools, each with two of the three mechanisms disabled:

| Configuration | Result |
|---|---|
| only the schema alive (loader + `_patterns_for` disabled) | **refused** — `TableError: rows[0]: a removed row has no 'to'` |
| only the loader alive (schema + `_patterns_for` disabled) | **`'SOMETHING(f);'` — REWRITTEN** |
| only `_patterns_for` alive (schema + loader disabled) | **refused** — `TableError: no pattern for kind 'removed'` |
| all three alive | refused at the schema |

`Table.text_rows()` / `semantic_rows()` (`renames.py:87-98`) filter on `apply`, not on `kind`. They
exclude a `removed` row only *because* `_row()` forces its `apply` to `"none"` — so it is not an
independent door, it is the schema door seen from downstream. The protection is real, but it is two
mechanisms, not three, and the report, `renames.py:22-25`, `rewrite.py:22-25` and the guide all say
three.

Severity **Minor**, because the two surviving doors are genuinely independent and both refuse. Two
one-line changes would make the claim true and are worth taking:

- `renames.py:91` and `:98`: add `and row.kind != "removed"` to each comprehension.
- `rewrite.py`: `_patterns_for()`'s `TableError` escapes `rewrite_tree()` as an unhandled traceback —
  `main()` only wraps the `renames.load(...).text_rows(...)` call. It fails closed, which is right,
  but it should fail with a message.

There is no fourth path: `semantic_rename.py` reads rows only through `semantic_rows()`, and
`rewrite_test.py:180-200` covers both surviving doors by name.

---

## 5. `declares_qualified()` — the RED reproduces, and one arm still bypasses it

Reproduced the report's RED exactly. Neutering `declares_qualified()` to the whole-path namespace
test gives three failures — `test_a_pending_class_scoped_row_that_has_landed_is_refused`,
`test_a_pending_enum_scoped_row_that_has_landed_is_refused`,
`test_a_removed_symbol_that_came_back_is_refused` — while
`test_the_delivered_arm_reads_the_same_shape` passes.

Then the harder question the dispatch asked. **Yes: the delivered arm bypasses it.**
`_check_delivered()` (`check-renames.py:246-258`) carries its own copy of the prefix walk and never
calls `declares_qualified()`. The docstrings say otherwise, twice and emphatically:

- `check-renames.py:25` — "Every arm reads a qualified symbol through `declares_qualified()`"
- `check-renames.py:121-123` — "Every arm of the gate -- delivered, pending and removed -- reads a
  symbol through this one function, so none of them can drift into that hole on its own"

R74's stated remedy was "one implementation, not three that agree today". What landed is two
implementations and a comment asserting there is one.

I checked whether the two can disagree, and they cannot: `_check_delivered()` takes the longest
namespace prefix that the header opens, which is the weakest of the requirements
`declares_qualified()` ORs over, so the two accept the same inputs. The duplication exists for a good
reason — the delivered arm reports *which* component is missing, which a boolean cannot — and there
is a case that fails if the delivered arm regresses (I neutered it: `test_the_delivered_arm_reads_
the_same_shape` fails, and the gate over the real tree reports 13 failures including
`core::net::ISocket::read`, `core::platform::ManualClock::setNow`).

So: **Minor** as a defect, but the comments must be corrected. They are load-bearing — the next
person who hardens `declares_qualified()` will believe the delivered arm came along for the ride.
Either extract a shared walk that returns the failing component, or delete the "every arm" sentences
and say plainly that the delivered arm duplicates the walk in order to name what is missing.

---

## 6. Minor

| # | Where | Finding |
|---|---|---|
| M1 | `renames.json` rows[429], `check-renames.py:262-266` | A `pending` row with **no `target`** (`FastCache::SyncRun` → `core::async::syncRun`) is asserted by nothing: `_check_pending` returns `[]` on `not target`, and the delivered arm never sees it. `core/async/SyncRun.hpp` has since landed and the gate is still green. The schema should require a `pending` row to carry a `target` with a `symbol`, which is the only thing that makes a pending row a gate rather than a comment |
| M2 | `ruff.toml:3`, `.ruff-version:4,8` | Both point at **`scripts/ruff.py`**, a name that was never committed (it became `ruff-format.py` in `ac9e2e2`, then `python-style.py` in `aee02ec`). `.ruff-version:8` also states "ruff's linter is not run", which `aee02ec` made false — and `ruff.toml` two files away now carries `[lint] select = ["E4","E7","E9","F"]`. These are the first two files a contributor opens |
| M3 | `CHANGELOG.md:268` | "the 438-row rename table" — 440 at `b528631`, 472 today. A count in an `[Unreleased]` entry that every Phase B task changes is guaranteed to be wrong at release. Drop the number; `check-renames.py` prints it |
| M4 | `CHANGELOG.md:282-290` vs `:340-347` | The same `[Unreleased]` section says `scripts/ruff-format.py --check` is the gate and "enabling ruff's linter is a decision of its own", then later says the script is `python-style.py` and the linter is on. Both bullets are true of their own commit and contradictory as a released changelog. Consolidate before `v0.1.0` |
| M5 | `rewrite.py:72-76`; guide "what is therefore yours" | A namespace **alias** is not rewritten and is not listed as a hand edit. Verified: `namespace cli = crispy::cli;` and `namespace coro = endo::coro;` survive all four profiles untouched, while `cli::command` in the same file becomes `core::cli::Command`. tuidu reaches the cli types exclusively through that alias (`src/tuidu/Cli.cpp`), so the alias line is a mandatory hand edit for the very profile whose PascalCase drift the table was built for. The compiler catches it; the guide should name it |
| M6 | `rewrite.py:47` | `SKIPPED_DIRECTORIES` is lowercase and exact: `Build`, `_deps`, `cmake-build-debug` are walked. Low risk while the guide's commands point at `src`, but a consumer that points at the repository root will rewrite CPM-fetched third-party sources. Case-fold the comparison and add `_deps` and a `cmake-build*` prefix |
| M7 | `semantic_rename_test.py:200-201` | `test_the_availability_probe_answers` asserts `isinstance(bindings_available(), bool)` — a case that cannot fail. The class docstring promises "a missing dependency is reported, never worked around"; nothing tests that `collect_edits()` raises or that `main()` returns 1 without the bindings. Easy to make real with a patched `bindings_available` |
| M8 | `rewrite.py:95-111` | Roughly one regex pass per row per file (257 rows for the endo profile). Measured 9.2 s over 377 files, so a 2000-file consumer tree is about a minute. Fine, noted only so nobody is surprised |

---

## 7. What I verified by running, and what by reading

**By running:** every probe in §2 I1-I4 and C1, I6, I7; the three-door matrix in §4; both neuterings
in §5; the byte-identity recipe end to end in a throwaway git repository (§I5); the suite
(`Ran 65 tests … OK`) and the gate (`0 failure(s)`) on a clean `b528631` export; the four-profile
dry run over core-cpp's own `src/`; the anchoring table below.

**By reading:** the CI `style` job (I did not push or dispatch a run; the steps are present and
correctly ordered, and `sys.exit(0 if result.wasSuccessful() and not result.skipped else 1)` is the
fail-on-skip the ruling asked for), the ctest registration, and the plan's C0 section.

The anchoring property the dispatch asked about, confirmed against the real table, contour profile:

| Neighbour | Result |
|---|---|
| `std::net::Socket`, `endo::net::Thing`, `mynet::Thing`, `SUBNET::x` | untouched |
| `"net::EventLoop"`, `R"(net::EventLoop)"`, `R"xy(...)xy"`, a multi-line raw string, `'n'` | untouched |
| `auto s = "#include <net/EventLoop.hpp>";` | untouched |
| `1'000'000` followed by a real string and then code | correct |
| `// net::`, `/* net:: */`, `/// @see net::`, a URL in a comment | rewritten, deliberately |
| `namespace net { }` | untouched, deliberately |
| `using namespace net;`, `#include <net/…>`, `#include "net/…"` | rewritten |
| Markdown, CMake, JSON files | not opened (`SOURCE_SUFFIXES`) |
| a line-initial `#include` **inside** a raw string | **rewritten — finding I1** |
| an unterminated `R"(` in a comment | **masks the following code — finding I2** |

---

## 8. The one thing most likely to bite the first consumer migration

**The byte-identity proof in `.agent/guides/consumer-migration.md` will fail on the first pull
request that runs it, and it will fail in a way that looks like the codemod is broken.**

It is not the subtlest finding, but it is the only one that is *certain*. The same guide tells a
consumer to delete its superseded copy in the same pull request (step 4), and `git diff --name-only
HEAD~1` lists deletions and additions alongside modifications — so `NOT PURE:` prints once per
deleted file, hundreds of times, on a commit that is in fact perfectly mechanical. The author will
either spend an afternoon finding out why, or conclude the check is noise and stop running it. And
the log they are told to attach to the pull request will describe nothing, because it was captured
from a re-run over the already-converted tree.

Everything else in this review is conditional on what a particular consumer's source happens to
contain. This one fires on every consumer, on the first attempt, and the fix is three words in a
`git diff` invocation plus one deleted line.

Close seconds, in order: the CRLF normalisation (I4), which makes the diff unreviewable on a Windows
checkout and which nothing in the tooling can detect; and the `UnicodeDecodeError` abort (I3), which
turns one odd byte into a half-converted tree and a stack trace.

---

## 9. What I would ask the controller to rule on

1. **C1 blocks C4, not C0.** The override gap is a fitness failure against the *plan*, not a breach
   of the dispatch, which only asked for `DeclRefExpr`/`MemberRefExpr`/`TypeRef` under
   `--decl-paths`. Decide whether C0 lands with a documented limitation and C4 owns the fix, or
   whether C0 re-opens for it. My recommendation: fix it in C0 — it is `get_overriden_cursors()` and
   one fixture, and discovering it in C4 means discovering it mid-migration.
2. **Whether a `pending` row may omit its `target`** (M1). Right now one row is unchecked in both
   directions and has quietly gone stale. Making the schema require it costs one commit and closes
   the hole permanently.
3. **The "three doors" and "every arm" claims** (§4, §5) are in shipped docstrings, the guide and the
   CHANGELOG. Either make them true (two small edits) or correct the prose. Leaving a false
   structural claim in a comment is worse than having no comment.
