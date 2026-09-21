# Audit: the checkable claims in `global-constraints.md`

**Seven wrong claims** — inside your 3–8 and at the top of my own 4–7, stated before I looked.
**One fixed in place** (it was mine). **Six reported**, because correcting them changes meaning.

Everything else checked out: 7 SHAs, 3 tool behaviours, 6 file/path claims, 5 version pins, and the
numbers I contributed except one.

**Your four examples are not in this file.** `posix/PollBackend.cpp:107`, `platform/Types.hpp:26`,
`IoBackend.hpp:176-190` live in `progress.md`, `task-B3-fixround2.md`, `task-B3-report.md` and
`task-B1-report.md`; `platform/Types.hpp:48` appears in no file at all. This file carries far fewer
path:line claims than you remembered — two, both in one table row, and both stale.

## Fixed in place

| Line | Was | Now | |
|---|---|---|---|
| 310 | *"all **43** sat in positions where the old name is the correct name"* | **41** | mine |

41 is the sum over `core::coro` (11), `src/core/coro` (7), `EventSource` in every form (17),
`FdInterest` (1), `IReactor` (3), `NetErrorCode::Other` (1), `NetErrorCode::BadFileHandle` (1). I
wrote 43 by adding up my own report's four class counts instead of counting the document — which is
your prediction landing exactly, and on the one number in the file that was mine to get right. The
conclusion is unchanged: all 41 are in positions where the old name is correct.

## Reported — correcting the fact would change the meaning

### 1. Line 52's rule has lost its premise. This is the loud one.

> *"Your commit will usually never get a CI run of its own, and that is expected. `build.yml`'s
> concurrency group sets `cancel-in-progress: false` for pushes… GitHub always supersedes a pending
> run in a group… watch the newest head rather than your own SHA."*

`74308e7 ci: a push to master gets a concurrency group of its own, as the comment promised` landed.
`build.yml:24-27` now reads: *"a push to master above all — groups by SHA, which means it shares a
group with nothing and is **never superseded**."*

So the mechanism the rule describes is gone. A lane reading line 52 today will expect its run to be
cancelled, will not look for one, and will close against someone else's head when it now has its
own. The advice underneath ("close against a completed green run on any commit that CONTAINS your
commits") is still *valid* — `master` is linear — but it is no longer *forced*, and the paragraph
presents it as forced.

This is also the basis of the standing instruction to watch the newest head rather than your own
SHA. That instruction is now optional rather than necessary, which is a ruling, not an edit.

### 2. Line 330–333 asks for something already done

> *"`build.yml` needs a per-SHA concurrency group; `docs.yml` should keep its shared one."*

The first half landed in `74308e7`. The second half is correct and in force —
`docs.yml:44` groups by `${{ github.workflow }}-${{ github.ref }}`. As written the bullet reads as
an outstanding recommendation.

### 3. Line 78's justification is false, and the file contradicts it 160 lines later

> *"A `removed` row names the fully qualified symbol… A bare row would tell every consumer to
> delete a type that is still there."*

A bare row tells no consumer anything. `removed` rows carry `apply: none`, so no rewrite tool is
ever handed one, and the gate reads a single component as a macro lookup, finds no `#define`,
reports the symbol absent and **passes**. The danger is that the row is *inert* — which is exactly
what line 237 says, correctly, in the same file:

> *"A check whose failure mode is INERTNESS is worse than one whose failure mode is error… ask what
> a **misspelled** instance of it does — if the answer is 'passes', the check needs the spelling
> enforced."*

Two statements of one reason, disagreeing — which is line 201's own rule (*"a reason recorded twice
is a reason that can disagree with itself"*). The rule at 78 is right; its justification is the one
that was true before I measured it.

Line 78 is also **an enumeration**: it names `core::tui::runtime::FdToken`, `FdInterest` and
`WaitOutcome`. That namespace declares **six** — `FdInterest`, `FdToken`, `WaitOutcome`,
`FdRegistration`, `FdRegistry`, `EventSource`. Three of six, in the file whose longest section is
*"An enumeration standing in for a rule"*. Extending the list would repeat the defect; the property
is *every name `core::tui::runtime` still declares until B12*.

Line 78 should also now say the spelling is **enforced by the schema** (`4919c79`), not merely
required — that is the remedy its own line 237 prescribes.

### 4–6. The table at line 123–128 has aged into an instance of its own section

| Cited | Claim | Today |
|---|---|---|
| `.agent/rules/testing.md:13` | *"one binary per module, and a second for a definition"* | line 13 is *"Tests sit next to their sources"*; the sentence is gone from the file |
| `AGENT.md:130` | same | line 130 now holds the **correction**: *"A module has as many test binaries as it has things to link, not one — count them rather than assume"* |
| — | *"13 registrations over 8 modules; `net` has four"* | **16** registrations; `net` has **5** |

I did not touch these. The table's columns are *"What it said / What was true"*, so it is a record
of a fixed defect and the numbers were right when written. But the pointers now land on the repaired
text, so a reader following them cannot tell whether the record is accurate — and the counts, read
as present tense, are wrong by three and one. The table is evidence for the section it sits in, and
it has become an instance of it.

The cheap repair is to date the row (*"as of `<sha>`"*) rather than to chase the numbers; chasing
them re-commits the file to maintaining a count.

## Confirmed correct

| Claim | Checked by |
|---|---|
| contour `6777ff05`, endo `f774a210`, fastcached `cc8992b0` | exist, on `origin/master` in each repo |
| `eb9c9c68`, `f6ec49f3` (Imported table) | exist in fastcached; both in `CHANGELOG.md:1098-1099` |
| `b528631`, `7ddbe1d` | exist, on `origin/master` |
| **`ctest` ANDs repeated `-LE`** | measured: 27 tests total, 17 with `-LE tree-level`, **27** with `-LE tree-level -LE no-tsan` |
| **`git log -N --name-only -- <path>` filters** | `git log -20 -- CHANGELOG.md` → 20 of 20; only **4** of the last 20 commits touch it |
| clang-tidy resolves config from the `.clang-tidy` nearest the source | documented tool behaviour |
| `.gitattributes` = `* text=auto eol=lf` | line 1 |
| CMake `3.25...3.31`, emsdk `3.1.56`, clang-format/tidy `22.1.8` | `CMakeLists.txt:2`, `build.yml:635`, `scripts/tool-versions.py` |
| `CompileCache.cmake`, `check-renames.py`, `ConsumerSmoke.hpp` | present |
| VS dev-shell path, `%LOCALAPPDATA%\fastcache-cc\bin` | present |
| 237 of 238 symbol rows agree `to` == `target.symbol` | recomputed at `HEAD` |
| 8 `removed` rows, all `core::`-rooted | recomputed at `HEAD` |
| `docs.yml` keeps a shared ref group | `docs.yml:44` |

## Two things about method

**Numbers in this file age silently, and three already have.** *"3 of 20"* (line 198) is **4 of 20**
today; the ctest counts *"28 where 16 were intended"* (line 262) are from a preset that is not this
one — my tree gives 27 and 17; *"197 rewrites across 36 rows"* moves as the table grows. None is an
error: each was right when measured. But every one reads as present tense, and a reader who
recomputes and disagrees learns to distrust the file rather than the number. A measured figure wants
its date or its tree beside it.

**I ran three malformed probes during this audit and caught all three before reporting.** I asked
core-cpp for `eb9c9c68` and `f6ec49f3` when they are fastcached SHAs and briefly had "two bad SHAs";
I put `--format` after a pathspec and got "0 commits" from `git log`; and I read an empty
`core_cpp_add_test` directory listing as a count. Same shape each time — a question shaped like the
one I wanted answered. The only reason none reached you is that each had an expected answer written
down first.

## One hazard

`global-constraints.md` is **not** in git (`.superpowers/sdd/` is ignored), so there is no index, no
diff and no protection against a concurrent write. It changed on disk between my read and my
one-character edit — the edit applied cleanly, but nothing would have told either of us if it had
not. If more than one agent is going to edit this file, it needs to be tracked, or edits need to be
serialised through you.
