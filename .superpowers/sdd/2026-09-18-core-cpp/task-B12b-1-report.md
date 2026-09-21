# Task B12b, part 1: the upstream catch-up checker, and the stable rows

`scripts/check-upstream-drift.py` + `scripts/check-upstream-drift-selftest.py`, registered under
`ctest -L hygiene`. Commits `8514854` (the checker and its self-test) and `fa7b569` (the
registration and the preamble pointer).

## Your zero-drift measurement: confirmed at row granularity

You measured `git log <pin>..origin/master` over contour's `src/crispy` and endo's `src/tui`,
`src/platform`, `src/testing` and got zero commits, and asked me to confirm or refute it per row.

**Confirmed.** After `git fetch origin` in both checkouts, all 328 rows are up to date:

| upstream | rows | drifted |
|---|---|---|
| `contour-terminal/contour` | 108 | 0 |
| `contour-terminal/endo` | 220 | 0 |

A row-level run can see what a directory-level one cannot — a file that moved, was deleted, or was
touched from outside those directories — and there was nothing of the sort to see. Your one-liner
was right, and now it is right for a reason that is checked rather than sampled.

One caveat that makes the confirmation weaker than it looks: **it only proves those rows' upstreams
have not moved, not that the rows are correct.** A row can name the wrong upstream file entirely and
still report clean if that file has not been touched since. Catching that needs a content
comparison, which is not this task and which the preamble's "a `-` is not byte-identical" paragraph
makes harder than it sounds.

## `cmake/portable/CompileCache.cmake`: drifted, and the gate is real

**The verbatim claim holds against the pin.** Both files are byte-identical to fastcached
`5a9dca04`, so nothing has been edited here.

**But fastcached has moved, so `CompileCache.cmake` is one commit behind:**

```
DRIFT  cmake/portable/CompileCache.cmake
       1 commit(s) since 5a9dca0498f4
       f6ec49f3 build: the released Linux daemon links yaml-cpp statically, and a failed start says why
```

`+67/-3`, and **behavioural, not cosmetic** — it reworks host system/processor detection behind a new
`_fc_auto_install_host()` and changes which prebuilt fastcache-cc row is selected for a given host.
That decides whether the compiler cache is found on a given machine, which is not a comment change.
`cmake/FetchTransferBound.cmake` is unchanged upstream. Not ported, per your instruction; the
decision and the work are yours to route.

**Is `downstream.yml`'s drift diff real, and does it work? Yes to both — it has already caught this
twice.** The job is not decorative:

| run | trigger | result |
|---|---|---|
| `35334305010` (18 Sep) | manual | success |
| `35430889453` (19 Sep) | nightly | **failure** |
| `35499910731` (20 Sep) | nightly | **failure**, with the full diff of `CompileCache.cmake` |
| `35517631664` (20 Sep) | manual | success |

The shape tells the story: the nightly failed, someone re-synced (`b505db8 build: re-sync
CompileCache.cmake from fastcached master`), and the manual run confirmed it green. That is the gate
working end to end — caught, acted on, verified. **And it is about to fire a third time**, because
fastcached moved again after that re-sync, which my run above found independently.

So the answer to "a gate nobody has watched fail is a gate nobody has watched" is that this one has
been watched, has failed, and was acted on. Two notes on it anyway:

- It is nightly and `workflow_dispatch` only, and not a required check — by design, per its own
  header comment.
- It compares against fastcached's **master**, while the provenance row records a **pin**. So it
  answers "are we verbatim with master today", not "are we verbatim with what the row claims". Those
  differ for exactly as long as a re-sync is outstanding, which is the state we are in right now.
  My checker answers the second question, from the row.

## What the checker found that nobody was looking for

Running it over the whole table surfaced two defects in the table itself. **Neither is mine to fix**
and both belong to live lanes, so they are reported here rather than touched.

**1. A malformed row, and it is on master** — `provenance.md:90` (async lane, committed):

```
| `src/core/async/ThreadPoolExecutor.hpp` | LASTRADA-Software/fastcached |
  `src/FastCache/Async/ThreadPoolExecutor.{hpp,cpp}` | 0708dd54... |
```

`{hpp,cpp}` is a brace pattern, not a path. `git log -- <path>` cannot read it, so the row silently
stops being checkable — the exact failure the table exists to prevent, in the one file that is meant
to be read mechanically. The preamble already prescribes the fix: *"A file adapted from more than one
upstream file (a merge) names its primary upstream in the table and lists the others in notes."* The
row's own notes already say the `.cpp` was inlined into the header, so the fix is one token —
`ThreadPoolExecutor.{hpp,cpp}` becomes `ThreadPoolExecutor.hpp` — and nothing else moves.

**This is the one thing that makes `core-cpp.upstream-drift` red on landing.** I am flagging that
plainly rather than weakening the checker to make my own gate green on day one: the row is wrong by
the table's own documented rule, and a checker that shrugged at it would not be worth registering.

**2. Thirteen stale rows** naming `src/core/net/` files the IoBackend lane has deleted in its
rewrite (`EventSource.hpp`, `DefaultEventSource.*`, the three backends, `ScriptedEventSource.hpp`,
…). Transient and expected mid-rewrite, and **reported without reddening** — see the design note
below.

## Design decisions worth their own line

**Drift never fails; a row wrong about its own upstream does.** Exit 0 on drift, 1 on malformed,
77 when an upstream could not be read at all. A nightly that reddens because upstream moved gets
muted, and then the next drift goes unseen.

**A deletion upstream is drift, not malformedness.** The row was true when written; "it is gone at
master" is the news this exists to carry.

**A row whose *core-cpp* file is gone is stale, not malformed — reported, not failed.** I had it as
a failure first. Then I checked, and `check-cmake-hygiene`'s `provenance` rule already refuses
exactly that, in almost the same words (`names 'src/core/net/DefaultEventSource.cpp', which does not
exist`). Two gates reddening for one defect gets both ignored — and it would have put my gate in the
red for any module mid-rewrite, which is the state it is least useful in. So it prints `STALE ROW`,
names the gate that owns the rule, and leaves the exit status alone. Without that, those 13 rows
would have been 13 false alarms on my gate for something already covered.

**One cell, one file.** Added as a checked rule and written into the preamble, because finding 1
showed the table could stop being machine-readable without anything saying so.

**A SHA cache, after measuring.** ~390 rows name **four** distinct synced SHAs between them — an
import syncs a whole module at one commit — so validating the SHA per row asked git the same two
questions 390 times. That was most of the runtime: **92s → 58s on WSL, 24s → 11s on Windows.**

## Self-test: the cases, and their REDs

Fourteen cases, each building throwaway git repositories with real history. Real repositories rather
than a stubbed `git`, because the checker is a wrapper around `git log`, `git cat-file` and
`git merge-base` — a stub would prove only that the stub agrees with itself.

**Every case was shown to fail.** I mutated the checker one way at a time and recorded which case
caught it; every mutation was caught by exactly the case that should catch it, and the checker was
restored byte-identically afterwards:

| mutation | caught by |
|---|---|
| drift reddens (exit 1 on drift) | `..._moved_is_drift_and_not_a_failure`, `..._deleted_upstream_is_drift_marked_deleted` |
| ancestor check removed | `..._sha_that_is_not_an_ancestor_is_malformed` |
| brace-pattern check removed | `..._brace_pattern_is_malformed_and_says_what_to_do` |
| path-at-synced-SHA check removed | `..._path_absent_at_its_own_synced_sha_is_malformed` |
| core-cpp path check removed | `..._core_cpp_path_that_no_longer_exists_is_stale_not_a_failure` |
| missing checkout exits 0 | `..._unreachable_upstream_skips_rather_than_passing` |
| repo filter matches by substring | `..._repo_matches_the_whole_name_or_the_bare_name_but_not_the_owner` |
| unknown-SHA check removed | `..._sha_the_checkout_does_not_have_is_malformed` |
| short SHA accepted | `..._short_sha_is_malformed` |
| column count unchecked | `..._an_unparseable_line_is_malformed` |

Two of those assert an **exit status of 0** — drift and a deleted upstream must not redden — which
no assertion about output would have caught. That is the same lesson as the A12 sentinel: assert the
thing happening, not the reaching of the end.

The self-test also earned its keep during development: one of my edits to `report()` silently did not
apply (a `str.replace` with no assertion on the match), and the stale-row case failed within seconds.
The in-tree measurement it protects would otherwise have reported `346 row(s) checked` while
printing no stale rows at all.

**The repo filter bug is worth recording** because only a row count exposed it: `--repo contour`
matched **328** rows rather than 108, because `contour` is a substring of the `contour-terminal`
owner that endo shares. Matching is now the whole `owner/name` or the bare `name`, and a case pins it.

## Bounds, measured

| test | Windows | WSL | bound | headroom |
|---|---|---|---|---|
| `core-cpp.upstream-drift` (`--no-fetch`) | 11.3 s | 53–58 s | 600 | ~10x |
| `core-cpp.upstream-drift-selftest` | 4.2 s | 1.8 s | 120 | ~28x |

WSL is five times slower than Windows on the same work: ~1200 git invocations against `/mnt/d`. The
bound is sized from the slower platform, as R72's were.

**It runs with `--no-fetch` under ctest.** A test that reaches the network fails for reasons that
have nothing to do with the tree; answering "has upstream moved" for real means fetching first,
which is what a person or a nightly does. What the registration gates is the part needing no
network: a row wrong about its own upstream. On CI, where the checkouts are absent, it skips and
names the upstream it could not read.

## Then

- `ruff format --check`: clean on both new files (and the whole tree is clean but for the async
  lane's in-flight Python, which I did not format).
- `ctest -L hygiene`: my two gates behave as designed — the self-test passes, the checker reports the
  drift and the malformed row above. The suite as a whole is red on other lanes' work
  (`check-cmake-hygiene` on 36 violations, all `src/core/async/` and `src/core/net/`).
- `mkdocs build --strict`: not run — nothing under `docs/` was touched.
- **No CHANGELOG entry, and saying so rather than omitting it silently**: a maintainer script is not
  something a consumer observes, and `.agent/rules/library-hygiene.md` ties entries to public
  headers and dependencies.

## A trap in the shared-checkout procedure, found the hard way

The dispatch says `git commit --only` for files only I touched, and a private index for hunks of a
shared file. I used the private index for **everything**, including my own new files. That was
over-application, and it had a consequence I did not anticipate:

**a private-index commit moves HEAD without updating the shared index, so the other lane's index
then encodes "delete impl's new files".** `git status` showed `D scripts/check-upstream-drift.py`
staged — and had the IoBackend lane committed with that index, my two scripts would have been
reverted along with their own work.

Repaired with `git reset -q -- <my four paths>`, which restores HEAD's index entries for those paths
alone and leaves everything else in the index untouched. Verified after: my four paths clean in both
index and worktree, and their 13 staged deletions plus the `EventSourceParity_test.cpp ->
BackendParity_test.cpp` rename all still staged.

**The rule this suggests**: use the private index only for a file whose *other* hunks are not yours.
For a file that is wholly yours, `git commit --only -- <path>` keeps the shared index consistent,
which is the safer outcome when someone else is holding staged work in it. Worth putting in
`global-constraints.md` beside the existing half, since the failure is silent and lands on the other
lane rather than on you.

## CI

Commits `8514854..fa7b569`, pushed. Per R73 I am watching for the first **completed green** Build
whose head contains `fa7b569`, checking that it executed a non-zero number of jobs so a superseded
run cannot be mistaken for a pass. **No green run covers these commits yet**; I will say so plainly
if that is still true at close.

For the record, the previous task closed properly: Build `35541190425` on `9d0bbc38` — **24 jobs
executed, 0 non-success** — which is what closed A12's R72.

## The overwritten commit message (Ruling R83)

`8514854`'s message was overwritten by another lane's `git commit --amend`: HEAD had moved between
that lane's `read-tree HEAD` and its amend, so the amend rewrote my commit instead of its own. The
commit is `438f40b` on master now, wearing a duplicate of that lane's message.

**Verified independently rather than taken on report**, the same way I would check any claim about
my own work:

| check | result |
|---|---|
| `git diff 8514854 438f40b` | empty — identical trees |
| parents | both `6dfb8c54` |
| `scripts/check-upstream-drift{,-selftest}.py` on `origin/master` | both present |
| the `git note` on `438f40b` | present, my full 38-line message verbatim, with the explanation above it |
| `refs/recovered/upstream-drift-checker-message` | resolves to `8514854` |

**And one thing the incident report did not cover, which I checked because it was the likelier
casualty: the other lane's own work survived too.** Their NetError change is intact at `6dfb8c5`,
and master's `NetError.hpp` carries it. So the amend did not replace their commit with mine — it
added a second commit with their message and my tree. Nothing was lost on either side; the damage is
exactly one duplicated subject line.

That duplicate is the only residue worth knowing about: `git log --oneline` shows two adjacent
commits with the same subject, and the second of them is the drift checker. The note explains it,
but only to someone who has fetched `refs/notes/*`, which is not the default.

I am not rewriting published history for a message, and I agree with the controller that nobody
should. The pointer to the note goes in the next commit I genuinely need to make on this task —
not in one invented to carry it, which would be a second cosmetic edit to fix a first.
