# Running core-cpp work as a team

How a manager session and two or three developer sessions work on core-cpp at the same time,
so a new session, or a different account, can take a run over without reconstructing the setup.
It is adapted from
[fastcached `.agent/guides/team-run.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/guides/team-run.md),
where each rule below was learned; the lanes there were fastcached's components, and here they
are core-cpp's modules.

This file deliberately holds **no board state**. What is done, in progress and left lives in
GitHub issues and pull requests; a copy here would go stale the moment a pull request merges.

## Roles

- **One manager** owns the board, the assignment of tickets, merging, and bug intake. It does
  not write feature code: a manager that writes code can no longer review it, and reviewing its
  own work is how a wrong finding gets ratified twice.
- **Developers** each work in their own git worktree, on their own branch, one ticket at a
  time. Nobody works in the main checkout.

## Lanes are modules

Tickets are assigned by **module**, not by priority. Priority decides the order; the module
decides who, because that is what keeps branches from colliding.

| Lane | Owns |
|---|---|
| **base** | `src/core/*.hpp`, `src/core/log/`, `src/core/cli/` |
| **platform** | `src/core/platform/`, `src/core/testing/` |
| **async** | `src/core/async/`, `src/core/net/` |
| **tui** | `src/core/tui/` |
| **build** | `cmake/`, `CMakePresets.json`, `tests/cmake/`, `scripts/`, `.github/`, `docs/` |

A ticket that spans two lanes is split into two tickets with an explicit order, or held until
the blocking lane lands. The manager sequences them; developers never negotiate an interface
between themselves, because two developers agreeing on an interface is two developers writing
it twice. The module table (`cmake/CoreCppModules.cmake`) is the dependency order: a change to
a lower module's public header is sequenced before the lanes above it.

## Which session works on which ticket

- **The branch is the claim.** A ticket is in progress if and only if an open branch or pull
  request references it. No label, title prefix or file records assignment: those bind a ticket
  to a session, and a session is the least durable thing in the run.
- **The claim is not the work.** A branch shows what was pushed, not what sits uncommitted in a
  worktree. A session that goes quiet gets its working tree looked at
  (`git -C <worktree> status --porcelain`), not only its branch. Push early, including
  half-finished work: a pushed branch is recoverable, an unpushed worktree is not.

## Coordination

- **Nobody touches the main checkout** (`D:\core-cpp`, or whichever path holds it): other
  sessions hold work in it.
- **A closing keyword in the pull request body closes the issue; a trailer on a commit does
  not.** Put `Closes #N` in the body for every ticket the pull request delivers, and write the
  keyword before each number: `Closes #A, closes #B`. `Closes #A and #B` closes only `#A`.
- **`origin/master` moves under you without you fetching**, because worktrees share one
  `.git` and any session's fetch moves the ref for all of them.
- **Compare against the base with three dots:** `git diff --stat origin/master...HEAD`. Two dots
  compares against master's tip, so every commit that landed since shows up as your deletion.
- **Never `git reset --soft origin/master` to squash after the base moved**: the index still
  holds the old tree, so everything that landed in between becomes a deletion in your commit.
  Rebase instead, and read `git diff --diff-filter=D` before pushing.
- **Read the left-hand SHA of every force-push.** `--force-with-lease` compares against your
  remote-tracking ref, and a rebase fetches first, so the lease can match another session's
  commit and overwrite it silently. Pin the lease to the SHA you last pushed
  (`--force-with-lease=<branch>:<sha>`), and never put two sessions on one branch.
- **Rebuild after a rebase in a fresh build directory, or with `--clean-first`.** A build tree
  reused across a rebase can link objects of two vintages into one binary, and the failures
  then name innocent files; a `clangcl-*` tree also has the header-dependency caveat in
  [`../rules/build-and-toolchain.md`](../rules/build-and-toolchain.md).
- **Prefix every scratchpad file with your lane.** The scratchpad may be shared, and two
  sessions writing `pr-body.md` publish each other's descriptions.
- **Pairwise clean is not serially clean.** Two branches that each insert at the same place
  merge cleanly onto `master` one at a time and conflict in sequence. Before ordering two pull
  requests that touch one file, merge-tree the second onto a synthetic `master + first`. Judge
  `git merge-tree --write-tree` by its exit status *and* whether it printed a tree: it exits 1
  both for a conflict (tree printed) and for a ref it cannot resolve (nothing printed).
- **Green and mergeable are different facts.** Checks describe a SHA; mergeability describes
  that SHA against the current base. Ask for both.
- **"No pending checks" is not "CI ran".** Checks that depend on other jobs are created only
  when those finish, so a watcher that stops at zero pending can stop before the matrix exists.
  `ci-ok` is the one check whose result settles a core-cpp pull request.

## Review gates

Every developer, every pull request, scoped explicitly to `origin/master...<branch>`:

- at the end of each phase within a pull request: `/simplify`, then `/code-review medium --fix`;
- before handing the pull request to the manager: `/simplify`, then `/code-review high --fix`.

`/simplify` runs first because it shrinks the change, so the correctness review examines less
code and its findings land on lines that will ship. The explicit scope is not optional: a skill
that silently reviewed the wrong tree and came back clean is worse than no review.

## Documentation and consumers are part of every pull request

- **No pull request merges leaving the documentation describing behaviour it changed**:
  `docs/`, the module page, `AGENT.md` when it establishes a constraint, and `.agent/rules/` when
  it fixes something that was a bug.
- **A public header change has a CHANGELOG entry and a "Consumer impact" section** naming what
  each consumer must change ([`../reference/consumers.md`](../reference/consumers.md)).

## Bug intake and reports

- A developer who finds a defect outside its ticket either fixes it inline (trivial and in its
  lane) or reports it to the manager with `file:line` and a failure scenario. Nothing is
  silently deferred, and nothing is silently widened.
- **A report is a set of claims about state, and every one of them decays.** Re-read somebody
  else's state before writing it down (`git merge-base --is-ancestor <sha> origin/master` answers
  "has it merged" without asking anyone), recount from the list rather than carrying a number
  beside it, and give CI results as settled, unsettled and failed with the time of the sample.
- **A retraction lands in the artefact.** A message is corrected by the next message; a comment,
  a header or a script is not, and it goes on being read by people who never saw the retraction.
- **Hand over the checkable form**, such as the SHA or the query, rather than the conclusion.

## Verification

Reproduce a finding before fixing it; a developer who cannot reproduce one reports back rather
than "fixing" it. Then: tests next to the code, the pinned clang-format and clang-tidy, the
local presets, and `ci-ok` green. Prove a regression test fails without the fix, not only that
it passes with it ([`../rules/testing.md`](../rules/testing.md)).
