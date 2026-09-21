## Global Constraints

- C++23, `CXX_EXTENSIONS OFF`, CMake `3.25...3.31`.
- Compilers: clang 22, GCC 14, AppleClang (Xcode 16), MSVC (VS 2026 locally), clang-cl 22.
- Every source file starts with `// SPDX-License-Identifier: Apache-2.0`. License is Apache-2.0.
- Namespace equals directory under `core`.
- Naming: types CamelCase, functions/methods/variables camelBack, private members `_x`, no `k`/`g_` prefixes, constants CamelCase, namespaces and module directories lowercase (`core::async` in `src/core/async/`; CMake targets stay `core::<module>`).
- No `NOLINT`, no diagnostic-muting pragmas (an allowlist row with a reason is the only exception), no C-style `for(;;)` index loops (use ranges/`std::views::iota`), no raw owning pointers.
- Recoverable errors return `std::expected<T, E>`, with monadic chaining preferred. Exceptions are only for unrecoverable conditions (for example a failed wakeup-channel construction on descriptor/handle exhaustion, or a test fixture that cannot set up), plus `core::async::OperationCancelled` for cancellation (user direction, 2026-09-18; see `.agent/rules/cpp-guidelines.md`).
- Use DI through constructors and data-driven tables. Use `enum class` over `bool` in API surfaces. Put `[[nodiscard]]` on value-returning functions and Doxygen `///` on public API.
- CMake: no global state unless top-level; `CORE_CPP_*`/`core_cpp_*` prefixes; no PUBLIC compile or link flags; explicit source lists (no glob).
- Dependencies are limited to the Part I §3 table. Adding one requires an option, a table row and a CHANGELOG entry.
- **Upstream sync discipline** (user request, 2026-09-18). Every import or port reads the upstream at its current `origin/master`, fetched at task start. It records the synced SHA per file in `.agent/reference/provenance.md` (`core-cpp path | upstream repo | upstream path | synced SHA`). Nothing is deleted from a consumer while an applicable upstream commit is missing from core-cpp (see Tasks B12b and the C-task delta checks).
- The WebAssembly subset (Part I §1) must build and pass tests under single-threaded Emscripten with emsdk **3.1.56** and latest.
  - No `std::thread`, `std::jthread`, blocking waits or `Threads::Threads` in that subset.
  - Code there sticks to what libc++ 17 (emsdk 3.1.56) supports: check `__cpp_lib_*` before using newer library features.
  - Every task that touches a subset module runs the `emscripten` CI job green before it is done.
- Imports read git blobs: `git -c core.autocrlf=false -c core.eol=lf show <sha>:<path>`. Refuse CR bytes.
- Import pins:
  - contour `6777ff05` (`D:\contour`)
  - endo `f774a210` (`D:\endo`)
  - fastcached `origin/master` after `git -C D:\fastcached fetch origin` (record the SHA; `cc8992b0` or later)

  Record every pin in `CHANGELOG.md` and `NOTICE`.
- **Worktrees only; never touch a main checkout.**
  - **fastcached (user requirement):**
    - All work happens in a worktree under `D:\fastcached-worktrees\<name>` (the repo's existing convention), on a `claude/<issue#>-<slug>` branch.
    - `D:\fastcached` itself is never modified: no pull, checkout, build or commit there. `git -C D:\fastcached fetch origin` and `git worktree add` are the only commands run against it.
    - Phase A/B reads of fastcached sources use `git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show origin/master:<path>`, never the working tree (which is behind origin).
  - **Other consumers:** the same rule, with worktrees at `D:\<repo>-worktrees\core-cpp[-<suffix>]` (e.g. `D:\endo-worktrees\core-cpp`), created from `origin/master`.
- Commits are small and semantic, each ending with the trailer `Signed-off-by: Christian Parpart <christian@parpart.family>`. core-cpp's default branch is `master`.
- Local build environments:
  - Windows: `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`, then `cmake --preset clangcl-debug` / `cl-debug`.
  - Linux: `wsl -d Ubuntu-26.04 -- bash -lc 'cd /mnt/d/core-cpp && cmake --preset clang-debug && cmake --build --preset clang-debug && ctest --preset clang-debug'` (cmake 4.2.3, clang 22.1.2, gcc 14.3).
  - macOS: CI only.
- Pinned tools: `python -m pip install --user clang-format==22.1.8 clang-tidy==22.1.8`, via `scripts/tool-versions.py`.
- Compiler cache: core-cpp builds through **fastcache-cc whenever it is available** (`cmake/portable/CompileCache.cmake`, verbatim from fastcached). On this machine fastcache-cc is installed on Windows (`%LOCALAPPDATA%\fastcache-cc\bin`, daemon at `127.0.0.1:6674`) and in WSL (`~/.local/bin/fastcache-cc`). Never set `USE_COMPILER_CACHE=OFF` locally, except for coverage.
- Every task is done only when the local Windows **and** WSL build+tests are green, pinned clang-format is clean, and (after Task A2) the pushed CI is green.

- **WSL is shared with other sessions** (user, 2026-09-18). Other Claude sessions build and test in the same WSL at the same time. Only core-cpp's `out/build/<preset>` trees and processes this task started are yours. Never kill, clean or reuse another build directory or process (e.g. `~/bld/*`), and don't read their load or failures as your own.
- **Every task ends with a `/code-review` pass** (user, 2026-09-19), run by the controller over the task's commits once CI is green. Its findings join the task's fix round. Implementers do not run it themselves.
- **Never `git pull --rebase` in a shared checkout.** The other sessions always have unstaged work in the tree, so rebase refuses outright ("cannot pull with rebase: You have unstaged changes"), and stashing would stash their work, not yours. You all share one working tree and one local `master`, so your commits already sit on top of theirs: `git fetch origin` and push. A push is a fast-forward and succeeds; if it is ever rejected, fetch and look before doing anything else, and never force-push.
- **Do not run a formatter over a file another session is editing.** Format what you touched.
- **When several agents share this checkout** (the controller says so in the dispatch): explicit pathspecs are not enough for files every task edits — `CHANGELOG.md`, `.agent/reference/provenance.md`, `docs/modules/*.md`. Staging such a file takes another agent's in-flight hunks with it. Before staging a shared file, run `git diff -- <file>` and read every hunk; if any are not yours, stage only yours with `git apply --cached` from a trimmed patch (`git add -p` is interactive and unavailable), then confirm with `git diff --cached -- <file>`. Check `git show --stat` against what you expected before pushing.
- **A public symbol you rename updates `tools/migrate/renames.json` in the same commit** (from Task C0 onward). Six consumer migrations read that table; a table that drifts from the API turns one edit here into six debugging sessions there. `tools/migrate/check-renames.py` validates the target side under `ctest -L hygiene`, so a missed row fails your own build, not someone else's.
- **And the same hazard from the other side.** Sooner or later one of your hunks in a shared file reaches master inside another lane's commit, because they staged the file whole while your edit sat in the tree. Check that your content survived their restructuring — entries can be moved between sections, reworded, or merged — and then leave it. Their commits are not yours to rewrite, and a revert to re-attribute a changelog line costs more than the wrong attribution does.
- **Three sessions share one `.git/index`, which is sharper than sharing one working tree.** Two failure modes, both seen: your `git add` stages another lane's hunks, and — worse — a bare `git commit` against an index another lane left staged against an older `HEAD` can commit a tree that *deletes* files they have since added. So:
  - **Never run a bare `git commit`, `git commit -a`, or `git add`** in this checkout.
  - For files only you touched: `git commit --only -- <pathspecs>`. It takes content from the working tree for exactly those paths, disregards whatever is staged, and leaves everyone else's index entries alone.
  - For a shared file where you need only some of the hunks (`CHANGELOG.md`, `NOTICE`, `.agent/reference/provenance.md`, `tools/migrate/renames.json`, `docs/`): commit through a **private index** — `export GIT_INDEX_FILE=$(mktemp)`, `git read-tree HEAD`, `git apply --cached` your trimmed patch, `git commit`, then unset it. Never `git add -p`; it is interactive and unavailable.
  - Before every commit, `git diff --cached` and `git show --stat` afterwards. If the stat lists a file you did not touch, stop and look rather than pushing.
- **Since `74308e7` (R105), a push to master gets a concurrency group of its own and is never superseded — so watch your own SHA.** This rule previously said the opposite and it was right to, until it was not: `build.yml`'s group was `${{ github.workflow }}-${{ github.ref }}`, and `cancel-in-progress: false` protects a run that has already *started* while doing nothing for a **pending** one, so every head queuing behind a running build was cancelled before executing a single job. **Seven heads went unverified in one evening that way, two of them the fixes for the other five.** The group is now per-SHA for pushes, PRs keep the old behaviour, and `docs.yml` keeps its shared ref group deliberately — *"a push is a record" is an argument about verification runs, not deployment ones.*
  - **The zero-job diagnostic is still worth knowing**, for reading history and for PR runs: a superseded run reports `conclusion: cancelled` with **zero jobs executed**, which in a run list is indistinguishable from a real failure. `gh run view <id> --json jobs --jq '.jobs | length'` separates them in one line.
  - **Closing against a run on a commit that CONTAINS yours remains valid** — `master` is linear, so a green Build at head H proves everything in H's history builds and passes together. It is simply no longer forced. And if you close with no completed green covering your commits, **say so explicitly** rather than citing a run that was cancelled.
- **Never act on a git object you did not choose. Two traps, opposite halves of one hazard.** The private index protects the **index** from other lanes but leaves **HEAD** free to move under you; an amend trusts **HEAD** but is blind to other lanes' commits. In both, *the object you act on was chosen after you last looked*.
  - **Banned: anything that moves HEAD or the worktree.** No `git commit --amend`, no `git reset <commit>`, no `git reset --hard|--soft|--mixed` without pathspecs, no `git rebase`, no cherry-pick onto a moving branch. A lane once ran `git read-tree HEAD` and `git commit --amend` seconds apart; another lane committed in between, and the amend rewrote *that lane's* commit with this one's message. Content survived (same tree, same parent); a careful 38-line message did not, and it was already pushed. **Get the message right the first time.** If it is wrong, say so in the report and in your next commit, and leave the commit alone — a wrong message costs a reader a minute; a rewrite costs another lane their work and cannot be undone once pushed.
  - **Permitted, and sometimes required: `git reset -- <paths>`** for your own files' index entries. It is index-only: it moves no HEAD, rewrites no history and touches no worktree file, so it chooses nothing. It is the only clean repair when the shared index has gone stale (see the private-index rule below), and a blanket ban would leave a dangerous index with no remedy — a rule people must break routinely stops being read.
- **A private-index commit needs a fourth step, and it is not optional: snapshot, reset, diff.** Committing through a `GIT_INDEX_FILE` moves `HEAD` without telling the **shared** index, which goes on caching the pre-commit blob. The staleness it leaves *points the wrong way*: `git status` reads `MM <file>` — index-versus-`HEAD` differing by **your own change, inverted** — so it looks like another lane staged a revert of what you just landed, and a bare `git commit` from any lane would revert it for real. So: take `git status --porcelain` **before** committing, `git reset -- <your paths>` **after**, and **diff the two snapshots** — they must match. This is the same hazard as the two rules around it in a third costume; the first left the shared index deleting your new files, the second staging a reversal of your whole commit, this one reverting a single hunk.
- **Use the private index only for a file whose *other* hunks are not yours.** For a file wholly yours, use `git commit --only -- <paths>`. Over-applying the private index has a silent failure mode that lands on *someone else*: a private-index commit moves HEAD without updating the shared index, so another lane's index then encodes "delete the files this lane just added". It happened — `git status` showed one lane's two new scripts staged for deletion, and had the other lane committed with that index, those files would have gone out inside their commit. Repair is a path-scoped `git reset -q -- <your paths>`; then verify the other lane's staged work is still intact before doing anything else.
- **For a shared file, build your commit's content from `HEAD`'s blob plus your own transform — never from the worktree copy.** In a checkout where lanes commit through private indexes, **the worktree is not a base; it is one lane's stale copy.** Seen: the worktree `CHANGELOG.md` was 13 lines behind `HEAD` because another lane had committed to it through a private index without updating the worktree. A patch built from `git diff` of that worktree would have staged those 13 lines as *deletions* and silently reverted their commit. Read the base with `git show HEAD:<path>`, apply only your change, and stage the result with `git hash-object -w` plus `git update-index --cacheinfo`. Then `git show --stat` and confirm the line counts are what you intended.
- **Write patches to a file in binary, never through a pipe that touches text.** Piping `git diff --no-index` through `python -c` corrupts the patch on Windows — Python's stdout translates `
` to `

`, and `git apply` then refuses with *"patch does not apply"*, which reads as a conflict rather than as an encoding fault. It costs ten minutes the first time and the same ten minutes to everyone after.
- **Ask `git ls-files --eol` about line endings, not `grep`.** It prints the index's and the worktree's endings (`i/lf w/lf`) and the attributes in force, which is the actual question. A `grep` for CR in Git Bash answers something subtly different and has already produced one false CRLF report in this checkout — and nearly a second, from me, checking the first.
- **Normalise line endings before committing a text file you edited.** `.gitattributes` (`* text=auto eol=lf`) keeps CRLF out of the stored blob, so nothing is at risk in history — but a CRLF worktree file makes every other lane's `git diff` of it unreadable, which in a shared checkout costs them more than it costs you.
- **Assert the expected failure count; never read an empty output as a pass.** Silence is what a passing scanner prints *and* what a scanner that never ran prints. A lane ran a hygiene scanner over a table it had deliberately seeded with two defects, got no output, and for a moment had "the rule works and the table is clean" — the real cause was `cmake: command not found`, because Git Bash has no `cmake` on PATH (every other cmake run that session went through WSL or the VS dev shell). It was caught only because the lane had **predicted two refusals and got zero**. So: before running a verification step, say how many failures you expect and from which rows; if the answer is zero, the step proved nothing about itself. A verification step that cannot fail loudly is not a verification step.
- **Read a status inside the script that produced it — never through a nested shell, and never after a pipe.** `wsl -d Ubuntu-26.04 -- bash -lc '...'` expands `$VAR` and `$?` in the **outer** shell, so a probe can run an empty command against a filename and then report the outer shell's success; and `cmd | tail -6; echo $?` reports `tail`'s status, not `cmd`'s. Three mis-measurements in one session came from this, one of which was reported as a defect in working code. The remedy for anything whose answer you will act on: **write the probe to a file and run the file**, print the values you are about to use (`echo "B is: [$B]"`), and check your result against any existing gate that already asserts the same thing before you report it.
- **Capture a RED by running the new test against `HEAD`'s code, not by disabling your own.** Checking out the pre-change scanner (or building against `git show HEAD:<path>`) and running the new case against it proves the case discriminates between the old behaviour and the new one — and it cannot leave a mutation behind in the tree, which disabling your own code can. Use a mutation when the defect is in data or in a branch you cannot reach from `HEAD`; use `HEAD` whenever the change is to code the test exercises directly.
- **`git checkout -- <file>` is not a single-hunk undo.** It discards *every* uncommitted change in that file, including the ones you made an hour ago and the ones another lane made. A lane used it to remove one probe row and lost its whole fix round in that header — caught immediately, redone, nothing shipped wrong, but it is a whole-file weapon with an undo-shaped name. To drop one change, edit it out; to recover one file's committed text, read it with `git show HEAD:<path>` and write what you want.
- **Deleting a public header means grepping the whole tree, not `src/`.** `tests/`, `docs/`, `.agent/` and `tools/migrate/renames.json` all name public headers, and `tests/consumer-*` compiles only in CI — so a green local run is no evidence about it. Removing `DefaultEventSource.hpp` left the consumer smoke fixture including a file that no longer existed, and master went red on the leg whose entire job is proving a consumer can build against us.
- **Structure is decided by structure, never by content.** A table's header is the row whose *first cell* is the column name — not any line containing the column's words. A drift checker recognised its header by testing the whole line for the substring `core-cpp path`, so a data row whose **notes** read *"a row keys on its core-cpp path"* was swallowed as a second header: not checked, not counted, not reported, while the run printed clean. Prose in a notes column cannot decide whether a row is data. The same applies to any parser here — decide on position, delimiter or shape, never on a phrase that free text may also contain.
- **Exercise a gate the way CI invokes it, not the way you invoke it.** A registered ctest and a direct script run are different programs: the registration may add flags (`--no-fetch`), set `SKIP_RETURN_CODE`, or bound a `TIMEOUT`, and a gate can pass one form while failing or silently skipping the other. Run `ctest -R <name>` before reporting a gate green, not just the script.
- **Mutate in a private detached worktree, never in the shared checkout.** To prove a test can fail you must break something; breaking it here breaks it for three other lanes, and a mutation left behind is indistinguishable from a defect. `git worktree add --detach <scratchpad>/wt-<name> <sha>` gives you a tree at a known commit with its own build directory, mutate freely, and remove it when the round closes. This is also how you run a mutation against a commit that is no longer `HEAD`.
- **A dirty-file marker is not evidence about a specific change.** ` M path/to/file` in `git status` says the file differs from `HEAD`; it does not say *whose* difference it is, or that the change you are looking for is missing. A lane read that marker on `provenance.md` and concluded another lane's fix was uncommitted — the fix had landed an hour earlier, and what the worktree held beyond `HEAD` was a third lane's 25 uncommitted rows. To ask whether a specific change is on the branch, read the branch: `git cat-file -p origin/master:<path>`, or `git merge-base --is-ancestor <commit> origin/master`.
- **The pattern behind several of the rules above, worth knowing as one thing: trusting a shorthand instead of asking for the thing itself.** In one evening this produced four separate wrong reports from one careful lane — the **worktree** read as a base (it is one lane's stale copy), a **variable** read as expanded (it expanded in the outer shell), an **exit status** read as the command's (it was the pipeline's), and a **dirty-file marker** read as a claim about a particular change (it says only that the file differs from `HEAD`). Each shorthand was close enough to the truth to pass a glance. Before you report a fact you have not established, ask what the thing you looked at *actually says*, and whether a command exists that answers your question directly — `git cat-file -p origin/master:<path>`, `git ls-files --eol`, `git merge-base --is-ancestor`, a probe written to a file. They are all a few characters longer than the shorthand that misleads.
- **It is only a check if you commit to the answer first.** Deciding in advance what the output should be — how many failures, which rows, what `git status` will print — is what turns looking into testing. Without it, silence reads as success, a count reads as complete, and a clean run reads as a clean tree: three separate defects found tonight, each caught only because someone had written the expected answer down before looking. This is the companion to the shorthand rule below: that one is about **what you look at**, this one is about **whether looking was a test at all**.
- `mkdocs build --strict` writes `site/`, which is gitignored and rebuilt from scratch each run. Harmless, but a directory appearing unannounced in a shared checkout costs someone five minutes.
- **`tools/migrate/renames.json` is generated as well as hand-edited, so base every change on `HEAD`, never on the worktree copy.** A lane's flow reads the whole file, parses it, inserts its rows and writes it back — so any edit committed between its read and its write is silently reverted, whoever made it and however. That has already dropped one lane's hand edit and, separately, the generating lane's own rows. The fix is not "hand-edit, do not generate": a hand edit has the identical failure mode when the editor's buffer predates someone's commit. Re-run your transform against the **current** `HEAD` blob immediately before staging, produce a `HEAD → yours` patch in a scratch tree, and `git apply --cached` that. Regeneration is fine; reading a stale base is not.
- **A `removed` row names the fully qualified symbol — `core::net::FdToken`, never `FdToken` or `net::FdToken` — and since `4919c79` the schema enforces it.** The danger is **not** that a bare row tells consumers to delete a live type: `removed` rows carry `apply: none` and tell a consumer nothing. **The danger is inertness.** The gate reads a one-component name as a macro lookup, finds no `#define`, reports absent and **passes** — so a misspelled row reads exactly like a working guard and guards nothing, forever. Worse, the surrounding convention teaches the broken form: `removed` is the only kind whose `from` is a core-cpp name, so a fresh agent pattern-matching its neighbours writes the consumer's spelling. The collision it guards against is live until Task B12 — `core::tui::runtime` still declares its own `FdInterest`, `FdToken`, `WaitOutcome`, `FdRegistration`, `FdRegistry` and `EventSource`.
- **A local green is not evidence about something your local run cannot reach.** Two defects reached master from one task, sharing one shape: `tests/consumer-shared/ConsumerSmoke.hpp` compiles **only in CI** (the CPM, vendored and WebAssembly legs each build it), so every local preset was green while the job whose whole purpose is proving a consumer can build against us was failing; and a `poll(2)` divergence appeared only on macOS, which none of us runs. Before reporting a change green, name what your run **could not** reach — the CI-only fixtures, the platforms you do not have — and say which run covers each. `Portability` dispatches FreeBSD on demand (`gh workflow run portability.yml --ref master`); macOS runs on every Build.
- **When a list is handed to you, ask what question it was answering** — it was probably not the question you are about to use it for. A dispatch naming four places to change was describing *the API surface to remove*; the lane read it as *the inventory of tables in the file*, and the table it did not name went unguarded. The scope statement and the completeness statement look identical on the page and are different claims. If you need an inventory, derive one from the tree (`git grep` for the construct, not the name) and say in your report how you derived it — the method is what the next reader checks, not the count.

- **A red build has two causes that look identical, and `git log -- <the file in the error>` tells
  them apart in one command.** Either a lane is mid-edit (blameless, wait or work in a worktree) or
  **master is broken and CI missed it** (urgent, and nobody may be looking). The first version of
  this rule collapsed the second into the first; B1 found master red on every POSIX toolchain while
  the rule told it to assume someone's uncommitted mess.
- **A shared checkout's working tree is not a buildable state, and was never promised to be one.**
  Five lanes edit it concurrently, so at any moment some file's header is ahead of its source. A red
  build in the shared tree is **not evidence about your change** until you have looked at whose
  files are in the errors. Build in a throwaway `git worktree` at `origin/master` plus your own
  files: one configure, complete isolation, a green you can believe. Remove it afterwards. No blame
  is owed to the lane whose partial edit you tripped over — a partial edit mid-task is what a shared
  checkout is. (B3, on B4's half-landed `EventLoop.hpp`/`.cpp`.)
- **When an arm-removal RED comes back green, that is data, not a broken case.** Ask whether the
  property has a SECOND defence before concluding the case is weak — and if it does, pin the
  property rather than either defence. B3 removed WfmoBackend's multi-chunk sweep and the suite
  stayed green because the rotation reaches the late chunk on the next `wait()`; pinning the
  rotation and keeping the sweep also stayed green. Only removing both reddens. The case that
  resulted pins the property, and the finding — **`nextWaitRotation` is not redundant, it is the
  second of two independent guarantees** — is what stops someone deleting it later with a passing
  suite as their evidence.
- **A control that fails on its first run has earned its place.** Most never fire at all. When the
  control fails alongside everything else, the case is telling you the CASE is broken, not the code
  — read it that way before hunting the implementation. (B3 found WfmoBackend's level-triggered
  precondition this way: `WaitForMultipleObjects` consumes an auto-reset event, so the detector eats
  it and the rescan dispatches to nobody.)
- **"Assert what distinguishes" removes cases as well as demanding them.** A case dropped with its
  reason stated is worth more than a kept one that asserts nothing. (B3 wrote a Wfmo starvation case
  and dropped it: `collectSignalled`'s rescan is global, so it distinguished nothing the boundary
  case does not.)
- **Arm a CI monitor on the commit you want covered, never on an ancestor.** A monitor watching an
  ancestor can fire green on a run that does not contain your later work, and you will report a
  green you do not have. (A12, on its own instrumentation, unprompted.)

---

## An enumeration standing in for a rule

This is the dominant failure mode of this project, not a run of bad luck. Four instances tonight,
in four different documents, written by four different lanes — and the fourth was written *in
direct response to* the third.

**Measured 2026-09-21; every pointer below has since been repaired, and the counts have moved (the binary table is now 16 registrations over 9 modules). The rows record what was found, not the tree today.**

| Where | What it said | What was true |
|---|---|---|
| `CHANGELOG.md`'s Imported table | these five files came from SHA `eb9c9c68` | true for three of them; two had moved to `f6ec49f3` |
| `.agent/rules/testing.md:13`, `AGENT.md:130` | one binary per module, and a second for a definition | 13 registrations over 8 modules; `net` has four, and only one extra is a definition |
| `task-B1-fixround1.md`'s title | "Rulings R97–R99" | R97 and R98 existed; R99 never did |
| `WfmoBackend.hpp`'s precondition | a WSAEVENT and the wakeup channel's are manual-reset | true, and `platform::Wakeup` is a third today, the console handle a fourth at B12 |

**The structure is always the same.** Someone knows a general truth and writes down the instances
they can see. Every instance is correct. The set grows, because a library's whole job is to grow.
The person who adds the instance is never the person who wrote the enumeration, so **nobody owns
the sentence** — and it reads as authoritative precisely *because* it is specific. A vague sentence
invites checking; a list of four exact things does not.

**You cannot gate prose, so shape it not to make claims it cannot back.** When you find yourself
writing "the X are A, B and C":

1. **State the property** that makes A, B and C qualify. That is what you actually know.
2. **Give A, B and C as examples**, not as the list.
3. **Add the instruction an enumeration cannot give** — *when you add a new X, check it against
   this property* — because that is the sentence the next lane needs and the one a list never
   provides.

A count, a range, and a parenthetical naming "the one exception" are all enumerations wearing
other clothes. So is a table row whose subject is a list.

- **A rule that is visibly not followed stops being read as a rule, and takes the steps around it
  down with it.** Do not state a rule more strongly than the practice it describes: AGENT.md's
  checklist read as "every change edits `CHANGELOG.md`" and four same-day commits did not, including
  the one that wrote the words. State the practice (*an entry for what a release-notes reader would
  care about*) and the steps beside it keep their authority. (B3.)
- **A condition you cannot check is more expensive than the check it guards.** `mkdocs build
  --strict` stays unconditional not because every change touches the site but because it costs under
  a second, while its condition — *did I touch one of the three snippet-included files outside
  `docs/`?* — cannot be evaluated without reading `mkdocs.yml`.
- **When a rule's justification has two halves and one turns out false, check whether the other
  stands alone before discarding the rule.** Step 4 survived its own correction that way.
- **Ask what the real users of a mechanism need before choosing between two ways to make it
  uniform.** `onError` looked like a portability defect in two backends; asking what a reader and a
  dial actually depend on (being woken, then `SO_ERROR`) showed neither needs it to be correct — so
  the two "broken" backends were right and the promise was the defect. Uniformity is not a goal in
  itself; it is a goal because callers depend on it, so find out what they depend on first.

### …and it is born stale, not merely decayed

The first version of this rule said lists rot because the set grows and nobody owns the sentence.
B3 found the sharper mechanism in its own work: `WfmoBackend.hpp`'s precondition named two handle
classes while **`HandleKind::Waitable`'s doc in the same file had named all three since the commit
that created it** — a narrower list than the author's own file two hundred lines away, in a comment
whose entire purpose was to record a general property.

So the failure happens **at the keystroke, not over months**. When you write *"the X are A, B and
C"*, you are not recording what you know — you are recording **what came to mind just then**. The
set does not have to grow for the list to be wrong; it was already wrong.

That is why the remedy is to state the property rather than to keep the list current. A list you
promise to maintain is a list you will write from memory again.

**And check that your replacement states the property rather than a correlate of it.** B3's draft
replaced the enumeration with two categories — manual-reset, or "signalled by state". A semaphore
is signalled by state (its count) and `WaitForMultipleObjects` **consumes** it; so does a mutex, so
does a synchronization waitable timer. The property is **non-consumption**, and "state" merely
correlates with it. A classification by correlate is the same defect one level down.

- **A review is a statement about a commit; forwarding it as a task makes it a statement about
  now.** Re-check a forwarded finding against `HEAD` before making someone act on it. The controller
  handed C0 four I5 defects from a review written against `b528631`; two had been closed by B2 in
  `7ddbe1d` before C0's round began, so C0 was given a list describing a repository that no longer
  existed — and found two real defects the list did not name.
- **A true conclusion resting on a false argument is more fragile than no argument**, because the
  reader who checks the argument discards both. C0's own verdict on its unconditional step 4: the
  false premise "every change edits the CHANGELOG" invited the next reader to throw out the step
  along with it. When one half of a justification fails, either rewrite the argument or drop the
  rule — do not leave the rule standing on the half that failed.
- **A measurement whose answer cannot come out wrong is not a measurement.** `git log -20
  --name-only -- CHANGELOG.md` *filters* the log to commits touching that file, so it reports
  "20 of 20" whatever is true; the real rate was 3 of 20. Before reporting a number, ask what input
  would have made it come out differently.

- **A reason recorded twice is a reason that can disagree with itself.** Record why a thing is the
  way it is beside the thing, and only there. `.agent/rules/testing.md` stated the ground for a
  second test binary in a rule bullet AND in a mechanism paragraph eight lines below, and the two
  disagreed; the fix records it only in the module's `CMakeLists.txt`, beside the split it explains.
- **Prefer removing what a gate would have to watch over building the gate.** A gate keeping two
  lists in agreement is worse than one list: it makes the duplication permanent and calls the
  arrangement safe. When a document's claim cannot be checked, **change the claim** rather than
  build a checker for the old one. (A12, twice: the Imported-table split, and the second-binary
  rule.)
- **If the only mechanical form of a check is a count, you are about to re-introduce the
  completeness claim you were deleting** — and it will fire the first time someone legitimately
  adds one. That is a gate on a true negative, which this project has now refused four times.
- **A rule that must be revisited by every lane touching something else will be wrong again by
  Friday.** State the grounds, not the instances, so that a lane adding an instance owes the rule
  nothing.

- **R87 applies to ANY source file another lane has open, not only to `CHANGELOG.md` and generated
  tables.** Build shared-file content from `git show HEAD:<path>` plus your own transform, never
  from the worktree copy — and **assert in your script that the other lane's markers are absent**,
  rather than inspecting carefully. **The failure mode in this direction is worse than the one R87
  was written for:** reverting someone's work is visible and repairable; publishing their
  half-finished work inside your commit, under your name, is neither. B3 copied `IoBackend.hpp`
  from the shared tree and picked up B4's uncommitted `virtual setPump`, its `IHostScheduler`
  include and its doc edit; only a missing `override` at `HEAD` made the Linux build fail and catch
  it. On Windows alone it would have shipped.
- **A new file gets built on a second platform before it is pushed, however small it is.** Not
  "run everywhere" — that rule does not survive an urgent moment, and **urgent is exactly when the
  shortcut gets taken.** B3 shipped `.handle = nullptr` in a new test file with WSL available and
  unused, because the fix was unblocking another lane and Windows was green. Four minutes to find
  once looked at.
- **Where two backends genuinely differ, assert that the property holds, not which one does it.**
  B3's hangup-with-buffered-data case pins `reader.readable >= 1` on every backend, because that is
  the contract; its companion case, a hangup with nothing buffered, asserts only `total() >= 1`,
  because which callback fires there is a real divergence and pinning it would pin the divergence
  instead of the property.

- **A check whose failure mode is INERTNESS is worse than one whose failure mode is error.** A
  false positive is noisy and gets fixed; a guard that silently guards nothing is indistinguishable
  from a working one and never gets looked at again. When you add a check, ask what a *misspelled*
  instance of it does — if the answer is "passes", the check needs the spelling enforced, not
  documented.
- **Beware a format where the surrounding convention teaches the broken form.** In
  `renames.json`, `removed` is the only kind whose `from` is a core-cpp name; every other kind's
  `from` is the consumer's spelling. So a fresh agent pattern-matching its neighbours writes
  `net::FdToken`, which is inert. That is not a discipline problem — **it is a design that punishes
  the way the rows actually get written.**
- **"True by discipline, not by construction" is a finding, not a reassurance.** 8 of 8 `removed`
  rows were correctly qualified and nothing enforced it. Cross-lane discipline is exactly what
  failed four times in one evening, in four documents, written by four lanes.
- **Report the wrong intermediate answers, not only the final number.** C0's first extractor said
  83 (it matched any `identifier(`, promoting member calls to namespace scope) and its second said
  22 (it asked the table about `core::net::X` when a row's `from` is the consumer's `net::X`). Both
  were over-reports, so the safe direction — but the first number it could have sent was wrong by
  an order of magnitude. **A lane that reports only its final number is a lane whose final number
  has to be taken on faith.**

- **The row that differs is the row to check.** A12 verified R96 by counting labels and by a
  worktree experiment, and never checked what the `sanitizers` leg actually ran — **because it was
  the one matrix row with an extra variable in it.** The row you had most reason to check, skipped
  for the very thing that made it interesting. Generalises well past CI matrices.
- **`ctest` ANDs repeated `-LE` options: a test is excluded only if it matches EVERY one.** So
  `-LE tree-level -LE no-tsan`, with nothing carrying `no-tsan`, excluded **nothing** — 28 tests
  selected where 16 were intended. Keep one `-LE` per invocation and extend its regex; never add a
  second flag. The trap is a property of the tool and outlives any label we write.
- **When reporting that a gate ran the wrong set, say which DIRECTION it broke.** Running more than
  intended wastes time and falsifies a claim, but hides nothing and makes no green false. Running
  less is a coverage hole. A reader six months later should not have to derive which one happened.

- **A new gate's first red decides whether anyone believes it.** Buy the evidence that a failure is
  not yours *before* you land it: A12 ran the header self-check clean at pristine `HEAD` in a
  throwaway worktree, and ten minutes later that was the only reason it could say "not mine"
  instantly when an unrelated lane's commit reddened its run. Without it, it would have been
  arguing from plausibility on a leg it cannot reproduce, defending a gate one commit old — and a
  gate nobody believes gets disabled by whoever is tired at the time.
- **An implementer naming where it "reasoned rather than tested" is the strongest signal a reviewer
  gets.** Aim there first. It is also a legitimate finding in its own right: "correct by argument,
  untestable here" is a different verdict from "verified", and saying which one you have is the
  whole job.

- **Read the whole record before reporting on it — a record's fields are not equally load-bearing,
  and the one you skip is the one written by someone who anticipated you.** C0 read a row's `kind`,
  `to` and `target` while surveying and skipped its `note`; the `note` said precisely the thing it
  later derived from the source after its check wrongly failed two correct rows. Its own verdict:
  *"the answer was inside the row I was reporting on — the same failure as the rest of the evening,
  one field to the right."* This is distinct from writing an enumeration from memory: it is
  treating part of a record as the whole record.
- **When you deliberately choose the looser rule, pin the looseness.** Otherwise the next person
  tightens it and calls it a fix. C0's macro check asserts the header *consults* the macro rather
  than *defines* it, and carries a case asserting the two real rows — which document a macro a
  consumer defines and core-cpp only tests — still pass.
- **Do not invent a way to disagree in order to enforce an agreement that has never been broken.**
  Closing the `to`/`target.symbol` gap needed a schema field meaning *these differ on purpose*
  before any check could exist. Record that it holds by discipline and tell the task downstream, so
  nobody reads 237 of 238 as a guarantee.

- **A correct explanation that covers most of the symptom is the most expensive kind of wrong,
  because it stops the search.** Sixteen failing jobs had the cause the controller had diagnosed;
  the seventeenth had a different one, and the ready-made explanation nearly buried it.
- **A forecast that does not state its scope will be read as covering everything.** When you tell
  someone a failure is not theirs, say which legs, files or cases your explanation accounts for and
  that it may not account for the rest. Three extra words. (Controller's failure; A12 recovered it
  by reading the log after agreeing.)
- **clang-tidy resolves its configuration from the `.clang-tidy` nearest the SOURCE FILE.** A unit
  generated into the build tree is analysed under the root config and can never see a
  per-directory one, so a suppression living beside the real sources cannot reach it. Do not fix
  that by copying configs into the build tree — that is a reason recorded twice, which is a reason
  that can disagree with itself.

- **The count is not the measure.** Counting occurrences of a stale-looking term says nothing until
  you ask what each occurrence is *doing*. In the plan, all 41 sat in positions where the old name
  is the correct name: the record of a completed task, the left side of a mapping arrow, a
  consumer's own symbol, an upstream artefact. A mechanical pass would have rewritten *"Rename
  `core::coro` to `core::async`"* into *"Rename `core::async` to `core::async`"* — **erasing the
  evidence that the thing it was enforcing had ever been decided.**
- **Before proposing a mechanical pass over a document, ask what the document is ABOUT.** A
  document about consumers legitimately contains consumer spellings; a gate forbidding the old
  names there forbids the document from describing its own subject. The proposed one would have
  demanded 197 rewrites of correct text across 36 delivered rows.
- **A dispatch is the document with no reader who can contradict it.** It is read once, by a fresh
  agent, with no way to check its claims against the tree — so a dispatch asserting that something
  *exists* is far more dangerous than the same error in a plan or a rulebook. `task-B6-dispatch.md`
  said `core::async::asTask` existed; it does not, and the plan says *"Add"* it. The agent would
  have hunted for it or concluded the previous task under-delivered.

- **Read the failure before agreeing with an explanation of it, however good the explanation.**
  (A12's, and the load-bearing one: it does not depend on the other party having phrased their
  claim carefully, so it still works when they have not.) Its companion, owed by whoever makes the
  claim rather than whoever receives it, is that **a forecast must state its scope** — both are
  true, and the first survives a careless colleague.
- **"A push is a record" is an argument about VERIFICATION runs, not deployment ones.** A
  verification run superseded is coverage lost; a deployment run superseded is correct, because the
  newest push is the one whose site should win. `build.yml` needed a per-SHA concurrency group and got one in `74308e7`;
  `docs.yml` keeps its shared one.
- **Find the existing proof before producing a new argument.** A12 validated the R105 ternary by
  noting the same `A && B || C` idiom is already used twice in that workflow, on those runners,
  green today — stronger than any reasoning about GitHub's expression evaluator. Then it named the
  idiom's one trap (a falsy middle operand collapsing to `C`) and showed the middle operand cannot
  be falsy here.
- **When told to report before landing, establish whether the worst case is worse than the status
  quo.** A12 showed neither failure mode of the concurrency change could cancel *more* than the
  defect already did, so landing was correct and reporting first would have been ceremony.

- **A correction is a measurement too, and deserves the scrutiny you just gave the thing it
  corrects.** The controller wrote "seven consecutive heads at zero jobs", doubted it, re-checked
  with a narrower query window, and "corrected" it to six — reading the smaller set as the whole
  set. The truth was **seven, not consecutive**: the original number was right and the correction
  was wrong. **Doubting a number is not the same as verifying its replacement**, and the pleasure
  of catching yourself is exactly when you stop checking.
- **An absent filename is not an absent function.** C0 listed a directory for files matching
  `connect|listen`, found none, and for a moment read that as the claim being false; both are
  member declarations in `Sockets.hpp`. Its diagnosis generalises past the instance: **you asked a
  question shaped like the one you wanted answered, and the shape was wrong.**
- **Run the control before the conclusion.** C0 was three for three on first-cut probes that over-
  or under-reported tonight, and none reached the controller as a finding — not because the probes
  were right but because the control ran first. A lane with no wrong first cuts either got lucky or
  is not probing hard enough.

- **Do not pre-write a dispatch more than one task ahead of the code it describes.** A dispatch
  asserts what exists, and a dispatch written before the code exists asserts it from the plan —
  which says *"Add `core::async::asTask`"* where the dispatch said it *exists*. B5 through B8 were
  written ahead to use idle time; one of them carried a false existence claim for exactly that
  reason, and it survived until another lane happened to grep for it. Write the next dispatch when
  the previous task reports, and spend idle time auditing instead.

- **A line reference into another repository at a pinned SHA is the safest kind of citation; a
  reference into our own moving tree is the one that rots.** This inverts the intuition. A12
  expected B7's `IocpSocket.cpp:555-561` and `:327-334` citations into fastcached to be the most
  decayed things it audited and found them exact, because the SHA freezes them — while paths and
  line numbers into `src/core/` drift under every lane's edits.
- **Report what held, not only what failed.** A survey listing only defects leaves the reader
  unable to distinguish a thorough audit from a shallow one. A12's dispatch audit named the ten
  presets, two open issues, the fastcached sources and the FreeBSD dispatch trigger it had verified
  as correct, which is what made its six findings believable.
- **A dispatch that asserts ANOTHER lane has already delivered something is worse than one that
  asserts a thing merely exists.** The failure mode is not a fruitless search, it is a defect
  opened against a lane that did nothing wrong. `task-B7-dispatch.md` claimed B3 had declared
  `completionPort()`; it had not, and the interface's own comment said so.

- **A case written by the author to cover a hazard the author identified is the weakest kind of
  evidence against that author's own blind spot.** It is worth having; it does not settle the
  question. A reviewer told to break a fix should be told when such a case lands, and told that its
  existence changes what a clean result means.
- **"Right conclusion, wrong reason" is worth reporting and is often the more important half.**
  B1 predicted the pre-existing join would show no race on the refcount; it showed fourteen,
  because `Parked::resume()`'s local `ParkedWork` is destroyed on the pool thread — so the
  *decrement* was already concurrent and only the *increment* was uncovered. The conclusion stood
  and the reason inverted what the question was about: the ordering hazard predated the refcount
  rather than arriving with it. **A prediction that is wholly right teaches nothing except that you
  already knew.**
- **A compiled-out case reports nothing, where a skipped case says so.** `#ifdef`-ing a test out
  for a platform silently changes the count — 81 against 84 with nothing explaining the gap. A
  `TEST_CASE` whose body on that platform is one `SKIP("reason")` costs three lines and makes the
  absence visible, which is what `.agent/rules/testing.md` asks for.

- **A caveat is the first thing lost when a claim is restated — and propagating a ruling into
  another document IS restating it.** B6's R101 section carried *"after B3's fix round 2 lands"*
  because it was written while the ruling was fresh and provisional; B7's and B8's were written
  from memory of the **conclusion**, and a conclusion does not carry its own preconditions. When
  you copy a ruling forward, copy the condition first and the conclusion second.
- **A brief that loses its reader's trust in the first five minutes is worse than one that says
  less.** `task-B6-dispatch.md` claimed its agent would be the first code to set
  `ReadinessHandler::onError`; four places already do, one of them deliberately with a comment. An
  agent greps, finds four, and doubts everything after it.
- **"True only after task N lands" is a distinct verdict from "true" and from "wrong", and it is
  the one that matters most in a dispatch.** A document read once by a fresh agent cannot
  distinguish a ruling from its implementation unless it says which it is describing.

- **A false premise handed to a searcher does not come back as a refutation; it comes back as
  noise.** "The premise is false" and "I could not locate it, so: inconclusive" look identical from
  inside a search. **Retract a claim to whoever is acting on it the moment you doubt it** — do not
  wait for their negative result, because you will not be able to tell which kind it is.
- **Do not re-attribute a property of a fix to the code the fix replaced.** The controller took
  "concurrent decrement was already covered" — true of a case written twenty minutes earlier in the
  same round — and inferred the hazard predated R97. It did not: before R97 there was no counter at
  all (`git show ac0ff76^:ParkedWork.hpp` matches it zero times). **Calling the counter's ordering
  "pre-existing" merges the fix with the defect it removed**, which makes the fix look like it
  inherited a problem it eliminated.
- **"Existing" means the pre-task tree unless you say otherwise.** A report using it for something
  written minutes earlier in the same round hands the reader a wrong inference for free.
- **An UNCOMMITTED dependency is worse than an unlanded one, not merely earlier.** An
  unlanded-but-committed dependency is a scheduling fact; an uncommitted one lives in a working
  tree that several lanes share and can be lost to a stray checkout, a clobbered file, or a lane
  reverting a shared file from the worktree rather than from `HEAD`. **Lateness is recoverable;
  loss is not.** Order a hold list by that, not by dependency depth.
- **Check the claim you are most confident is wrong.** That is the one whose answer teaches you
  something — the same instrument as "commit to the answer first", pointed at your own confidence
  rather than at the result. A prediction only teaches when it can be surprised.

- **A measured figure wants its date or its tree beside it.** Numbers in a long-lived document age
  silently and read as present tense: "3 of 20 commits touch the CHANGELOG" is now 4 of 20, a
  `ctest` selection count is per-preset, "197 rewrites across 36 rows" moves as the table grows.
  None becomes *wrong* — but a reader who recomputes one and gets a different answer learns to
  distrust the file rather than the number. Write the date, the SHA or the preset beside it.
- **When a record of a past defect ages, date the row; do not chase the numbers.** A
  "what it said / what was true" table is correct as history and misleading as present tense. Its
  pointers will land on repaired text, which is the point of having repaired them.

- **`global-constraints.md` is TRACKED (`d44e2d6`) and the shared-file rules apply to it.** Build
  edits from `git show HEAD:<path>` plus your transform, not from the worktree copy; commit it
  through a private index if your hunks are not the only ones. It was gitignored until a lane found
  that **none of the three shared-file rules stated in this file could be applied to this file** —
  no index, no diff, no history, no collision detection — while every dispatch cites it as binding
  and several lanes write to it. A document that cannot obey its own rules is a special case, and
  special cases are where rules stop being read.
- **A working control proves the instrument, not the question.** C0 checked six SHAs against
  core-cpp, got two "DOES NOT EXIST", and had a control (a fake `deadbee`) pass — so the probe was
  working perfectly, on the wrong repository. The SHAs were fastcached's. Run the control *and* say
  which question the probe's shape asks.
- **A probe that succeeds at the wrong question hands you a confident number — and the wrong
  answer is often the more interesting one.** Two bad SHAs in the lead's file; a tool claim of the
  lead's shown false; a dispatch caught asserting something absent. Those are the results that
  travel fastest, get reported first, and are least likely to be re-checked before they are
  believed. **Be slowest with the finding you most want to be true.**
- **A citation into a pinned tree cannot rot; a count of your own tree rots before the document is
  read.** Both halves came out of one audit: B7's `IocpSocket.cpp:555-561` into fastcached at
  `0708dd54` is still exact, while B3's "contour's `EventSourceParity_test` (1243 lines)" was wrong
  when written (contour's is 1131; 1243 was *our* copy) and is 1588 today.
- **Say which claims you did not reach.** A12 marked the `wake()` coalescing claim *unverified, not
  confirmed false*, because asserting it from the call chain would have been the plausible-narrative
  move. Naming what you did not verify is what makes what you did verify worth anything.
- **A document written before a ruling cannot misstate the ruling; only documents written after it
  can.** `task-B3-dispatch.md` had zero "true only after task N lands" defects because it predates
  R101 entirely — it describes the interface B3 was told to build, and B3 built it. The defects of
  that class were all in dispatches written *after* the ruling, from memory of its conclusion.

- **A dispatch cannot hold a state; it can only tell you to check one.** Write the instruction, not
  the fact. The controller wrote *"R101 is not true at `HEAD` yet"* into three dispatches; it was
  true when written and false thirty minutes later, when `7cd86fc` landed. **A stale "not yet" is
  exactly as misleading as a stale "already"** -- and the fix for one produced the other inside an
  hour. The durable form names the file to read and asks for the answer in the report.
- **A run's conclusion is a summary of its jobs; a job list is a summary of a log.** Two lanes hit
  this an hour apart: a monitor fired on a run that *completed* having executed nothing (cancelled,
  zero jobs), and a lane read a job list, saw `clang-tidy` named, and inferred from the job names
  whose failure it was -- the log said its own file was in it too. **Exit a monitor on the job's
  terminal state, not the run's; attribute a failure from the log, not the list.**
- **A control is built from the same misconception as the probe.** It shares an author and a
  premise, so it only ever tests the half you already doubted -- C0's fake-SHA control proved the
  checker could tell real from fake and could not possibly reveal that it was asking the wrong
  repository. **The control covers the instrument; the question needs its own check, and the check
  for a question is naming what a wrong answer would look like** before you run it.
- **Be slowest with the finding you most want to be true -- the re-check is driven by
  dissatisfaction, which is backwards.** A confident wrong answer that is *boring* gets re-checked
  because it is unsatisfying; one that is *interesting* gets reported.
- **A parked patch's verification is evidence about a tree that has since moved.** "It applied
  cleanly" is not the same claim as "it still does what I measured". Re-run the gates before
  landing, however trivial the rebase looked.
- **Skipping a gate is judgment only if you can name what it would have told you.** B3 skipped ASan
  and TSan for a pure function's branch reorder -- neither can speak to it -- and ran the pinned
  clang-tidy directly instead, which was the gate that had caught it before. The same lane's
  earlier skip, under time pressure, could not be justified that way and put a defect on master.

- **A general justification applied to one instance is the narrowest form of this session's
  defect.** `d44e2d6` carved `global-constraints.md` out of the SDD `.gitignore` with a comment
  whose every clause applied equally to 131 other files. **When you write a reason, check what else
  it covers** -- and if the answer is "most of the directory", you have written a rule and applied
  it as an exception. `8c7daa5` extended it by pattern, not by listing the three filename shapes
  that existed that day.
- **"Regenerable" means the SOURCE does not move.** `review-*.diff` stays ignored because
  `git diff A..B` reproduces it exactly from two immutable commits. Task briefs look equally
  derivable -- `scripts/task-brief` extracts them from the plan -- but **the plan moves**, so a
  brief regenerated today is not the text the agent actually received. A derivation from a moving
  source is a record, not an artifact.
- **An audit that reads current text can find a claim that disagrees with the tree; it cannot find
  a claim that was deleted.** A12, bounding its own five dispatch audits: every "wrong when
  written" versus "drifted since" dating was inference from surrounding evidence, because the
  documents had no history to verify against. **Say what your method could not have detected** --
  it is a stronger result than the findings.
- **With per-SHA CI runs, a red leg is more often an older SHA than a new defect.** That is R105's
  cost side and it is the right trade. `git merge-base --is-ancestor <fix> <run SHA>` settles it in
  one command -- check that before spending an hour on a failure someone already fixed.
