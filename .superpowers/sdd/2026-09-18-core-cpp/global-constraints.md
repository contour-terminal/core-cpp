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

- **A decision's rule gets recorded and its justification does not, and that asymmetry makes the
  decision impossible to revisit: you can see what was required, but not what would change it.**
  The upstream-sync amendment's *rule* reached five tracked files and a gate; its *rationale* --
  the measurements that scoped it (contour `coro`/`net` at zero commits in fourteen days, endo
  4/7/3, fastcached's 49 and 14 all test hygiene, thirteen open board items none touching
  Async/Net) and the user's own words -- was in one untracked file and nowhere else. **A rule whose
  reasons are gone becomes either dogma or noise, and nothing distinguishes the two without the
  measurements.** Write the why where the what lives, or in the commit message that lands it.
- **Negate by KIND, not by naming convention.** `!task-*.md` is an enumeration of whatever
  convention exists today; it silently omitted a binding context document, a user decision, and the
  generator behind seventeen tracked briefs. Three successive attempts to close one hazard each
  closed it at exactly the scope the author could see, while the justification already covered the
  wider set every time.

- **A green gate and a silent gate produce identical output, so a gate must carry something that
  makes its green falsifiable.** The `clang-tidy` job plants two findings in `TidyCanary.cpp` and
  requires them to fire: zero `FAILED:` lines proves the job did not fail, **not** that clang-tidy
  ran. Three lanes nearly took a silent gate for a passing one tonight -- a grep for the wrong
  target name returning zero mentions, a `--quiet` run reporting `head`'s exit status, and a job
  list read where the log had more in it. Only the canary was *designed* to catch it; the other two
  were caught by someone being suspicious, which does not scale.
- **A monitor's exit condition is a question, and a question you did not ask cannot be answered by
  a run you are watching for another reason.** A12 declined to re-arm on two later commits because
  a red there would be another lane's result reported under its question's name -- which is exactly
  how a superseded run with zero jobs had earlier read as a verdict.

- **A hardcoded stage name in an error branch is a summary that cannot be wrong about what it
  summarises, because it never looked.** B3's harness printed *"FAILED at build/configure"* when
  configure and the build had both succeeded with zero `error:` lines and `ctest` had returned 8.
  Its own verdict: *"I wrote a summary that resembled the failure instead of reporting it -- I had
  spent the previous hour saying that about other people's job lists."* **The shorthand you built
  yourself is the one you will trust without checking.**
- **An unreproducible crash is reported, not dismissed, and the owning lane decides.** *A segfault
  that appears only under parallel load in a threaded test binary is the shape a real race has, and
  three green re-runs is exactly what a real race also looks like.* "Could not reproduce" is not
  "did not happen"; deciding by omission removes the observation from everyone who could route it.
- **A grep standing in for the thing itself, four times in one evening from one lane, with an
  identical tell: a suspiciously clean or suspiciously alarming count that is an artifact of the
  pattern rather than of the tree.** `"core-cpp path" in stripped` matched a data row whose notes
  contained the phrase; an unanchored marker regex matched its own documentation; a grep for
  `hdrcheck` missed `header-self-check` and read as "never ran"; a search for `[0-9a-f]{7}\.\.`
  missed a range written `b505db8..origin/master`. **Searching for the shape of an answer excludes
  answers with a different shape** -- and the lane applied this rule to others' claims far more
  reliably than to its own instruments.

- **A single green run is not evidence against a low-rate defect, and neither are three.** The
  join-teardown race reproduces at roughly 2-3%: one CI run has about a 97% chance of missing it,
  and `04472c5`'s sanitizer legs were green while containing it. **Repetition is the instrument for
  a probabilistic defect** -- say how many runs you expect to need and under which sanitizer before
  you start, and report the observed RATE, not a verdict.
- **A counter reaching zero correctly is not the same fact as every participant being safe to
  destroy.** R98 made the join counters atomic and left the teardown unguarded: whichever thread
  decremented to zero destroyed every runner, with no guard against a sibling mid-resume on another
  worker. **When you fix a race by making the arithmetic safe, ask separately what the arithmetic
  is a signal FOR.**
- **A probabilistic reproduction is a poor regression test.** A case that passes 97% of the time by
  luck will read as green forever. Pin the deterministic interleaving if you can; if you cannot,
  say what you tried and label the case honestly rather than shipping a lottery as a guard.
- **An unreproducible crash in a threaded binary is cheap to report and expensive to re-discover.**
  That asymmetry -- not "report everything" -- is what justifies passing it to the owning lane
  instead of filing it as flake. A genuinely flaky test costs a paragraph; a real race dismissed
  costs whoever meets it next, with no record that anyone saw it before.

- **A nonzero guard catches only zero; a two-count cross-check catches under-counting at any
  level.** `check-tree-level-coverage.py:85-96` counts label sites two independent ways and dies if
  they disagree, telling the reader to fix the parser rather than the count. **That is the shape to
  copy**: the failure that actually happened was a drift checker's total sliding 351 to 350 when a
  substring match swallowed a row, and nothing about that is caught by asking "is it zero?".
- **A gate whose negative cases live inside it cannot stop refusing without failing itself.**
  `check-layering.cmake` and `check-platform-sources.cmake` are scenario tables pairing each row
  with either `configures` or a regex the refusal must match. Better than a separate self-test
  file: the canary cannot drift away from the gate, because it IS the gate.
- **A build tree is a cache of a question someone asked earlier.** A gate survey taken from
  `out/build/cl-debug` would have enumerated 27 tests and missed two gates added the same evening,
  while looking authoritative. Same class as reading `CMakeCache.txt` to learn the launcher.
  Enumerate from the source at `HEAD`.
- **Three green re-runs obtained by removing the variable you were testing for are not three green
  re-runs.** B3 re-ran the same tree under lighter load after a crash that appeared only under four
  concurrent builds -- the same shape as a control built from the same misconception as its probe.
- **"Did not reject" and "exercised" are different claims about a green job.** B3 declined to say
  macOS had run its two new parity cases until the assertion count moved from 804, because their
  presence was read from its own source rather than from the run.

- **A green sanitizer run is weak evidence against a rare race, and the weakness is quantifiable.**
  Against a 2-3% defect, three runs have roughly a **91-94%** chance of all passing while it is
  present (0.97 cubed is 0.91; 0.98 cubed is 0.94) -- **and that arithmetic assumes independent
  samples of the same condition.** B3's three were not: it observed the crash under four concurrent
  builds and then sampled on an idle machine. **A rule stated with an assumption nobody checks is
  how a correct formula produces a wrong answer.**
  **Three greens after one red is a measurement only if you state the power of the test** -- say
  how many runs the rate you are ruling out would require, or report the observation rather than a
  verdict. `04472c5`'s two green sanitizer legs on a commit containing the join race are the same
  arithmetic seen from the other side: CI missing it was expected, not surprising.
- **State a deviation BEFORE the result, not after.** B3 could not dispatch `Portability` on a SHA
  -- GitHub's `workflow_dispatch` takes a branch or tag only -- so it ran on `master` and said so
  in advance, with the asymmetry spelled out: a pass is conclusive, a failure would need
  attribution first. **Had the limit surfaced afterwards, the run would have been worthless** --
  not because the evidence changed but because nobody could still tell what it was evidence of.
- **"Precise but dangerous" loses to "imprecise and stated".** The precise option was a temporary
  tag at the SHA; this repository's release workflow keys off tags and `AGENT.md` requires the tag
  to equal `project(VERSION)`, so a stray tag is an input to the workflow that cuts releases, not a
  local convenience.

- **A green CI run carries no evidence that any particular case RAN.** Catch2 prints its case and
  assertion totals **on failure only**; ctest logs one line per test. A case deleted, `#if 0`-ed or
  guarded out on a platform produces a line identical to one where it ran and passed. The narrow,
  useful statement: `normalisedExitCode` returns 2 when `totals.testCases.total() == 0`, **so a
  green binary proves at least one case ran -- the exit-code contract catches a binary that ran
  nothing, and cannot catch a binary that ran everything except the two cases you care about.**
- **Do not close that with a per-binary expected case count.** A number maintained by discipline is
  a guard that weakens silently: cases get added, the threshold is not bumped, and it goes on
  passing at a number that stopped meaning anything. That is the mirror of the four true-negative
  gates refused tonight -- a gate that stops firing on real ones.
- **The durable fix is the rule already in `.agent/rules/testing.md`: `SKIP`, never silence.** A
  case compiled out reports nothing and the platform counts silently differ; a case whose body on
  that platform is one `SKIP("reason")` reports **skipped**, and the counts become comparable. That
  makes an absence visible without adding a number anyone has to maintain.
- **No count proves a case exercises what you think it does -- only an arm-removal RED does.**
  Totals make absence visible; they say nothing about whether a present case asserts anything
  useful.

- **"At least one" is rarely the question.** `BackendParity_test.cpp` has 29 cases looping the
  backend matrix with `if (!backend) continue;`, and a separate case guarantees the *preferred*
  backend is constructible -- so at least one entry is non-null everywhere. **But on macOS the
  question is whether the matrix had TWO**, poll and kqueue: if kqueue silently stopped building,
  all 29 cases would test poll only and stay green, and that is exactly the divergence R101 turns
  on. A guarantee that is real, implicit, and in another case can answer a different question than
  the one you need.
- **Assert what a platform builds, NAMED rather than counted.** A table of which backends each
  platform provides fails loudly and specifically when a platform stops building one; a threshold
  or a count weakens silently when nobody bumps it. **When such a table must change -- B7 makes
  IOCP the Windows default -- write its failure as an instruction**, naming the table and saying a
  platform's backend set changed, so the next author meets a brief rather than a bare mismatch.

- **A byte-level edit wants a tool that takes byte VALUES, not a shell that interposes escaping.**
  Three shell-mediated attempts to replace one CR byte -- two Python heredocs and one perl `-i -pe`
  -- each reported success or no-match while leaving the byte in place, because the layer between
  the shell and the tool was eating the pattern. PowerShell reading the file as bytes and comparing
  against `13` did it first try. **Three tools "failing" identically is a sign the measurement or
  the plumbing is wrong, not the tools.**
- **`git ls-files --eol` is the authoritative answer about line endings; `od | grep` is not.** A
  scan piping `od -An -c` into `grep` reported five long-tracked files as containing CR bytes; they
  contain none, because the pattern matches od's own rendering. `git ls-files --eol` reports the
  index and worktree sides plus the attributes in force, and said zero tracked blobs are `crlf` or
  `mixed`. This trap has now caught two readers in a row -- the second while repairing the
  paragraph that documents it.
- **Tracking a file subjects it to every gate that scans tracked files.** `8c7daa5` tracked 132
  workspace documents and turned `style` red on a CR byte that had sat in an untracked file all
  evening. That is the tracking decision working as intended -- the gate found something real the
  moment it could see it -- but **widening what a gate can see is a change to what the gate
  asserts**, and is worth a thought before the push rather than after the red.

- **Tracking a file is not only protection, it is ENROLMENT.** A scope argument weighing the
  protection gained and not the obligations acquired is half an argument. Tracking 132 workspace
  prose files brought them under the CR gate, the namespace gate and everything else `git ls-files`
  feeds -- none of which had seen them -- and `style` went red on the first commit that enrolled
  them. The gate was right; the argument for enrolling was half-made.
- **A stated reason does not fix the silence: the reason serves whoever EDITS the test, `SKIP`
  serves whoever READS the report.** 30 of 46 guarded cases here document themselves and all 46 are
  equally invisible in a CI log. B1's three were fully documented and B1 still had to derive the
  84-against-81 gap by hand. **"Document your guards" sounds like the same instruction and buys
  none of it.**
- **A case can vanish with no guard in the file to comment on.** CMake source lists do it silently:
  `core-cpp.platform` loses 66 cases across seven files under Emscripten, `core-cpp.net` loses 16
  on Windows -- same binary name, different contents, nothing saying so. Where a file compiles
  everywhere, set `SOURCES_EMSCRIPTEN` to the same list, as `net_backend` does.
- **A control that tests the probe's SHAPE beats one that tests only its sensitivity.** C0's
  reason-detector looked only above the `#if`; in this repository reasons sit between the `#if` and
  the case, so it would have reported "46 vanishing cases and not one explains itself". The control
  that caught it injected a known reason into each candidate position in turn and watched the third
  arm fail.

- **A stack trace names where a crash surfaced; an instrumented flag names who did it.** Two
  symbolized traces attributed a use-after-free to a completed join's teardown; a flag raised for
  the start loop's duration and checked inside `AbandonState::release()` immediately before
  `root.destroy()` fired 8 times in 1920 runs. **A completed join and an abandoned chain tear down
  the same objects through the same destructor**, so one reads as the other from a stack alone.
  Prefer the probe that names the actor.
- **Pin a deterministic interleaving AFTER the mechanism is known, never before** -- pinning one
  against the wrong mechanism bakes in the wrong answer, and the case then reads as a guard.
- **"Location proven, mechanism not" is a complete and useful report.** Say which you have. A fix
  against an unproven mechanism lands in whichever file the current story points at.
- **Check the premise of the question before answering it.** Asked to weigh printing Catch2's
  totals on success against log noise, A12 ran a binary and found **the totals already print** --
  `CTEST_OUTPUT_ON_FAILURE` discards a passing test's stdout and no job retains a ctest log. The
  fix would have been a no-op and the cost I asked it to weigh did not exist. **A correct
  observation attributed to the wrong layer produces a confident fix in the wrong place.**
- **A count is only worth having if it is comparable.** Retaining a per-job test total is useless
  while 81 means "81 ran" in one job and "81 ran, 3 vanished into `#ifndef`" in another -- it
  invites the misattribution the retention was meant to prevent.
- **A CMake `WHEN` naming an undeclared option silently removes a target and every test registered
  against it.** `if(when AND NOT ${when})` makes an undefined variable false, so `NOT` is true and
  the row does not build: configures clean, builds clean, module absent. Two instances exist and
  both are spelled correctly -- **true by discipline, in the helper that decides what gets built
  and tested at all.**

- **`conclusion != "success"` is not "failed" -- a running job has an empty conclusion.** Filter on
  `status` as well, or a jq one-liner will report in-progress jobs as failures. The controller did
  exactly that, raised an alarm about a run that was fine, and then criticised itself for a scoping
  failure that had not happened either. **Second self-inflicted bad measurement in an hour**, after
  piping `od` into `grep`.
- **A file no leg compiles can still be checked: pull the real compile command for a sibling out of
  `compile_commands.json`, strip the output and dependency flags, and run it `-fsyntax-only`
  against the orphan.** B3 did this for `posix/DefaultBackend.cpp` -- exit 0 under the full pedantic
  set with `-Werror`. **It proves the file is well-formed and warning-clean against real headers
  with real flags; it does not prove correctness on the platform that would select it** (wrong
  libc, and `-fsyntax-only` does not link). "Unexercised" remains the accurate word.
- **Running the same configuration yourself is a second SAMPLE, not a second METHOD.** B3, on its
  own Windows evidence: *"I ran them locally on `cl-debug` and `clangcl-release`, which is the same
  evidence CI has, not independent of it."* It catches flakiness and nothing else.
- **"A discipline-maintained table rots" is two failure modes, and only one is dangerous.** B3's
  distinction, which supersedes two of my earlier blanket refusals: *"a stale **threshold**
  silently weakens a guard that was working; a stale **list of extra things to check** merely fails
  to grow."* A rotting threshold keeps passing while protecting less, and says nothing. A rotting
  list keeps checking what it lists; its omission is visible the moment anyone asks what it covers.
  **So the rule is not "no hand-maintained tables" -- it is "no hand-maintained thresholds."**
- **Ask how many things a proposed gate would guard before proposing it.** Asked whether the
  `-fsyntax-only` technique earned a must-pass check, B3 counted: **the population is exactly one**
  (`src/core/net/CMakeLists.txt:74`, the only `else()` platform fallback in the tree). Verdict:
  "yes to a line, no to a gate." The count decided it; no argument was needed.
- **A justification that names the wrong beneficiary is worse than none.** I justified the
  orphan-file syntax check by B7's IOCP sources; B3 corrected it -- `SOURCES_WINDOWS` is compiled
  by four CI legs, so that code is covered, and the real gap is "no configuration compiles this
  file at all". **Both felt like "code CI does not reach", which is how they got collapsed.** A
  wrong beneficiary makes a check look load-bearing and stops the next person asking what is
  actually uncovered.
- **State a check's limits in the file that holds it, not in the report that proposed it.** The
  person who acts on a gate will not have read the argument that produced it. For the header
  self-check: host headers are not the target's, there is no link step, and it catches bit-rot
  rather than a wrong implementation -- the third being the one a reader will otherwise assume away.
- **An abort that intercepts ONE path and removes ALL crashes is evidence there is only one path.**
  B1's probe 3: 8 hits in 1920 runs, zero residual SIGSEGVs, with the abort on the abandon path
  alone. **The zero excludes the alternative cause**, which is a stronger claim than demonstrating
  the first -- and it is what makes an attribution wrong rather than merely incomplete.
- **`--limit N` on a list sorted newest-first selects the newest, which during a burst is exactly
  the set that has not started yet.** `gh run list --workflow=Build --limit 6` showed six queued and
  none running; the full list showed 3 in-progress and 7 queued, with the running ones older than
  the window. **Fourth time tonight a narrow window produced a scarier number than the truth** --
  after the zero-job-run miscount, the `--limit 10` "correction" that was itself wrong, and the
  `conclusion != "success"` filter. **A window that sorts by the same key it filters on will
  systematically exclude the states that take longest to reach.**
- **A dispatch's blocker list decays the moment the lane starts working; track the TREE, not the
  dispatch.** I carried "5 provenance + 3 renames rows hold `ctest -L hygiene` red" as a live B4
  blocker and relayed it to three lanes. Measured against clean `origin/master` at `f2a175f`:
  `check-cmake-hygiene` exit 0 (445/447 files, all clean -- provenance included, per R86),
  `check-platform-sources` exit 0, `check-renames.py` exit 0 (495 rows, 0 failures). **B4's
  `cc1b237` had landed the code with +15 provenance and +163 renames lines in the same commit**, as
  its dispatch required, five commits before I was still describing the red as current.
- **`cmake -P` on a path that does not exist prints `CMake Error: Not a file` and still exits 0.**
  A `$VAR` that expanded to empty through a `wsl -- bash -lc '...'` layer produced
  `SCANNER_EXIT=0` from a scanner that never ran, and the pass was about to be reported. **Prove
  the script file exists and print its size before trusting its exit code**; a checker that cannot
  be found is indistinguishable from a checker that found nothing.
- **Git-bash MSYS-mangles a `/mnt/...` argument into a Windows path before `wsl` sees it.** Use
  PowerShell to invoke `wsl`, or write the script to a file and pass only its path. Nested
  quoting through `wsl -- bash -lc` silently dropped variable expansion twice in five minutes.

- **GitHub creates ONE workflow run per PUSH, on the tip commit -- not one per commit.** `cc1b237`,
  the commit that turned master red in four ways, **has no Build run at all**: it was pushed
  together with `0ae632e`, and only the tip was built. **This partly undercuts R105.** Making the
  concurrency group per-SHA stops consecutive pushes cancelling each other, but it can only protect
  runs that exist. "Every master commit is independently verified" is false for any multi-commit
  push, and a bisect over master will land on commits that were never built. **Push one commit at a
  time when each must be verified.**
- **The severity of a portability break is not where it was first seen.** B3 reported `std::jthread`
  as a FreeBSD/`Portability` failure and reasoned that a lane "could land it, see 24 green jobs, and
  have followed every gate" -- and I repeated that. **AppleClang's libc++ defines no
  `__cpp_lib_jthread` either**, so `macos (appleclang)` and `macos (llvm-22)`, both REQUIRED Build
  jobs, fail on the same line. Nobody could have seen 24 green jobs; the run had not finished.
  **"A run that has not reported is not a run that passed" -- applied to every lane tonight and not
  to myself.**
- **Master's true red/green history is not its commit list.** Mapping each of 14 master commits to
  its Build conclusion found: two green, three red, one never built, four still in progress. **A
  branch that looks continuously verified is a sequence of runs with holes in it**, and the holes
  are exactly where multi-commit pushes happened.
- **A conclusion is only as verified as the premise you did not think of.** Told B3 its FreeBSD
  answer was already in flight, having checked two things -- `c94a0b9` contains R101, `c94a0b9`
  lacks the `jthread` -- both true. **Never checked whether it contained the backend table**, which
  is the assertion that makes the run answer B3's question; `779f6f9` landed one commit later. Two
  true premises, a false conclusion, and the merge-base commands attached so it read as checked
  work. **A12 made the identical error independently, from the same two facts.** B3 checked both of
  us and was right both times. **Verifying the premises you happen to generate is not verifying the
  premises the claim rests on.**
- **A grep proves a string is absent, never that a constraint is** -- and the more precisely you
  name what you are looking for, the more confidently a miss reads as proof. B3 searched for
  `FreeBSD` and for a floor near the WebAssembly one, found neither, and wrote "the tree does not
  state it". The rule was two directories away, **phrased as a fact about `__cpp_lib_jthread`
  rather than about a platform**, so a platform-shaped search could not reach it however carefully
  it was run.
- **A correct diagnosis offered as THE explanation for a red board is how the other defects stop
  being looked for.** A12 diagnosed `parkForever` exactly -- a Release-only `-Werror` failure that
  FreeBSD surfaced because `portability.yml` uses `clang-release` -- and then volunteered that it
  explains one compile error among at least four live independent defects. **The caveat is the
  contribution.**

## Master's red board at `f2a175f`, triaged (controller, from the one COMPLETED run)

Six independent defects, all from `cc1b237`, none of them R106:

| Leg | Failure | Root cause |
|---|---|---|
| macos x2, compile-cache, FreeBSD | `HostDrivenCanary.cpp:58` `parkForever` unused, `-Werror` | Defined unconditionally, used only inside `#else` of `#ifdef NDEBUG`: **a Release defect, not a portability one** |
| macos x2 (REQUIRED), FreeBSD | `TestLoop_test.cpp:376` `std::jthread` | AppleClang and FreeBSD libc++ define no `__cpp_lib_jthread` |
| clang-tidy (whole job) | `WorkerIdentity.hpp:49` `_outermost` | Assigned in body, wants a member initializer |
| clang-tsan | `NullBackend.hpp:57` `wake()` | `_wakeCount` is a plain `size_t`; **`wake()` is the one member the spec requires to be thread-safe** |
| clang-asan-ubsan | `EventLoop_test.cpp:793` `parkForAnHour(...)` `.resume` | stack-use-after-scope: coroutine outlives the stack owning its `int*`. The tree's rule is **coroutine parameters by value** |
| windows cl-debug | both `hostdriven-canary` tests **TIMEOUT** | Canary links `core::net` alone, never `core::testing_dialogs`; MSVC `_wassert` raises a **dialog before `SIGABRT`**, so the `onAbort` handler never runs |

- **A running job's log is SEALED; `gh run view --job <id> --log` says so rather than returning nothing.**
  Two of my queries came back empty against in-progress master runs before I read the message. The
  completed dependabot run carried the identical failures and its logs were open. **Diagnose from
  the run that finished, not the run you care about.**
- **A canary that HANGS is the rulebook's "a `REQUIRE` above a stop turns a red into a hang",
  reached from a direction nobody wrote down**: not an assertion that failed to fire, but a
  platform dialog raised *before* the abort the handler was installed for. B4 reasoned carefully
  about the `NDEBUG` path and `SKIP_RETURN_CODE` precedence; the dialog path was the one that got
  it. **`core::testing_dialogs` is an OBJECT library precisely so a non-Catch executable can link
  it** -- the component existed and went unused.
- **A distance with no stated anchor is not a measurement.** I wrote that the backend table landed
  "one commit later" than `c94a0b9`. True of `779f6f9`->`cc1b237`, which is how `git log --oneline`
  displays them; **false of the anchor the sentence implied -- `git rev-list --count
  c94a0b9..779f6f9` is 6.** Same family as "a forecast must state its scope", and broken inside a
  correction. **The specifics inside an otherwise-correct message are the ones nobody checks**, and
  A12 flagged it for exactly that reason.
- **A rule that needs vigilance decays; a rule that rides on self-interest does not.** B3's, on what
  actually caught my off-by-one: *"I didn't set out to audit you. I went to check whether the run
  contained MY assertion."* So the durable form is not "check the controller" but **"before acting
  on a claim that a run answers your question, verify the run contains the thing that answers it --
  not the fix, not that it compiles, but the specific assertion."** Three different commits here;
  I checked the first two and stopped.
- **"One commit per push" is discipline-as-a-guard, and I ordered it to four lanes one hour after
  refusing that pattern on principle.** A12 laid out three options for the missing-run gap and
  declined to build any; option 2 was my own standing order. **RULING: build option 3 (a step that
  diffs `github.event.before..github.event.after` and NAMES the unbuilt SHAs) plus 3b (a
  `workflow_dispatch` `ref` input on `build.yml`), and nothing else in that file.** 3 does not
  replace 2, it **rescues** it: what makes a discipline-guard rot dangerously is that violations are
  silent, so once the gap reports itself the policy demotes from an unauditable guard to a
  preference whose violation is named in the job summary.
- **A limitation is fine; the failure mode is the defect.** `portability.yml`'s new `ref` input
  needs a FULL 40-char SHA -- servers resolve full SHAs under `allow_reachable_sha1_in_want`, never
  prefixes -- and an abbreviated one dies inside `actions/checkout` as three lines of `The process
  '/usr/bin/git' failed with exit code 1`, **naming neither the ref nor the input nor the reason**.
  A lane hitting it inspects its runner and its token. B3 recognised the shape because it had
  shipped one that week: **the fastest way to spot a class of defect is to have built one.**
- **THE PATTERN, three instances in one evening: I verify the premises I generate and never the one
  that decides the question.** (1) `c94a0b9` -- checked that R101 was in and the `jthread` was out,
  never that it carried the assertion. (2) The provenance rows -- tracked my own dispatch, never the
  tree. (3) `NullBackend.hpp` -- the failing test was `core-cpp.net` and the other net failures
  traced to `cc1b237`, so I routed it to B4 **without running `git log -1 -- <file>`**, which is the
  one command that decides attribution. It is `e7963de`, B3's. **In every case the premises I
  thought of were true.** The deciding premise is the one that never occurs to you, so the only
  defence is asking what single fact the conclusion rests on and checking THAT first.
- **Writing a rule down is not a mechanism for applying it; only a check travels.** B3 wrote the
  `wake()` contract in `IoBackend.hpp:392` ("the ONE member that is safe to call off the wait's
  thread"), implemented it correctly and commented it in `ScriptedBackend.hpp:280`
  (`std::atomic<int> _pendingWakes`, `fetch_add(release)`), and **still shipped `++_wakeCount` on a
  plain `size_t` in `NullBackend.hpp:57` the same afternoon** -- one file away. B4's `std::jthread`
  is the same fact at two directories. **Distance is not the variable; prose is.** This is an
  argument for B3's unbuilt-source check and against closing these with documentation.
- **The acceptance test this produces is mechanical: the one member an interface declares
  thread-safe is the one every test double must implement atomically.** Greppable, unlike the prose
  that failed to carry it.
- **A defect fix that greens a red gate ships alone and ahead of any feature**, because a push
  carrying both gets ONE run covering both -- the precise gap option 3 exists to report.
- **A clang-format run over a `.cmake` file mangles it into something that does not parse, and the
  damage is INVISIBLE to whoever caused it.** `cmake/CoreCppHeaderSelfCheck.cmake` came back as
  `#SPDX - License - Identifier : Apache - 2.0`, with `set(${` split from `outVar}` at two sites --
  **every fresh `cmake --preset` in the tree failed**, while already-configured trees kept building.
  B4's observation is the general one: **a broken CMake file is latent per developer**; the person
  who breaks it has a working tree and no signal, and the whole cost falls on the next person to
  configure.
- **It did not come from the sanctioned script, and that is the finding.** `scripts/clang-format.py:35`
  is `EXTENSIONS = ("cpp", "hpp", "h", "ipp", "inl")` -- even `--all` cannot select a `.cmake` file.
  So it came from a direct `clang-format -i` on a sweeping path, which `AGENT.md` forbids in as many
  words. **Fourth instance tonight of one shape: the rule existed, was correct, and the guard was
  simply not routed through** -- after `std::jthread` (rule two directories away), `NullBackend::wake()`
  (contract one file away, by the same author the same afternoon) and the provenance rows.
- **Size the damage before proposing the repair.** 113 changed lines looked like "revert it", which
  would have destroyed B3's in-flight `core_cpp_add_unbuilt_source_check()`. Counting them:
  **80 mangled comments, 33 non-comment lines, and only TWO sites that actually break parsing.**
  Rejoining two lines unblocks every lane and changes nothing anyone wrote; the 80 comment lines
  break nothing, pass every gate, and would have landed silently.

## The broken instrument (A12's finding, controller-reproduced)

- **`grep -c $'\r'` matches EVERY line in this Git Bash.** The `$'\r'` reaches grep as an empty
  pattern. Verified: `printf 'alpha\nbravo\ncharlie\n'` -- zero CR bytes -- returns **3**.
  **My own instinctive fix `grep -c $'\r$'` is broken identically (also 3), and `grep -cP` is not
  available here at all.** Three idioms, one of them invented to fix the problem, all returning the
  line count. **The only shell idiom that reports a true zero: `tr -dc '\r' < FILE | wc -c`.**
- **A measurement that returns the same answer for every input is not measuring** (A12). The sharper
  form: **the broken probe is CORRECT precisely on the inputs that would confirm it.** On a 100%
  CRLF file the line count equals the CR count, so it returns the truth; on a pure-LF file it
  returns the line count instead of zero. **It agrees with reality exactly where you would believe
  it and diverges only where you would have learned something** -- which is why five files reading
  "fully CRLF" over two hours looked mutually corroborating. A Windows repo is the one shape the
  instrument gets right.
- **"The control I demand of every gate I build -- prove it can say no -- I never applied to my own
  one-line probes."** Every gate in this tree has a self-test proving it refuses. **Not one shell
  probe typed into a terminal tonight had one**, including my `od | grep` that produced five false
  positives, which I recorded as a curiosity instead of as evidence about my instruments.
- **What broke it was a second instrument in a different currency, not suspicion**: a diff showing
  29 insertions where a whole-file line-ending change would have been 63. **Cross-currency
  disagreement defeats a self-consistent wrong answer without anyone being vigilant.**
- **Say which half of a retraction survives.** The C0 bare-CR finding was done in Python and matched
  CI's own "1 with a CR byte" exactly, so **the finding stands and the explanation built around it
  does not.** Retracting wholesale would have thrown away a correct result.
- **Reasoning from where a fact OUGHT to live rather than asking where it does** (B3's generalisation
  of my pattern, now four instances across two agents): I concluded B4 owned `NullBackend` from
  "the failing test was `core-cpp.net`" without `git log -1 -- <file>`; B3 concluded "the tree does
  not state the jthread rule" from grepping `FreeBSD` without grepping `jthread`. **Each searched
  the space where the answer should have been and read the emptiness as an answer.** The deciding
  command was one line in both cases and was never run.
- **A claim that carries its own basis can be attacked at the basis.** B3 on FreeBSD/kqueue:
  *"deduction from the source plus a green, not an observed count"* -- so a future doubter checks
  the premise (is the case still unconditional, is the file still in plain `SOURCES`) instead of
  re-deriving the whole result. **A result with a handle on it beats a stronger-sounding flat one.**
- **TWO CR questions, TWO instruments -- and handing lanes only the first recreates the C0 bug.**
  Controller-verified on four files of known byte content:

  | file | `tr -dc '\r' \| wc -c` | python crlf | python bare | `grep -c $'\r'` |
  |---|---|---|---|---|
  | pure LF | 0 | 0 | 0 | **3** wrong |
  | pure CRLF (3 lines) | 3 | 3 | 0 | 3 (right by coincidence) |
  | **one bare CR** | **1** | **0** | **1** | **3** wrong |
  | mixed | 2 | 2 | 0 | **3** wrong |

  - *"Would the CI gate refuse this file?"* -> `tr -dc '\r' < FILE | wc -c`. Correct for the gate,
    which refuses any CR.
  - *"Is it CRLF or a BARE CR, and will checkout normalise it?"* -> in Python, `b.count(b'\r\n')`
    against `b.replace(b'\r\n',b'\n').count(b'\r')`.

  **`tr` alone finds the C0 file (1) and a lane then reasonably concludes CRLF and assumes `eol=lf`
  normalisation handles it -- which is exactly why that bare CR reached the gate.** One instrument
  answers the gate's question; it cannot answer the diagnosis question.
- **The broken probe was right whenever I was right and wrong only when I was wrong** -- measured, row
  2 above: on a pure-CRLF file `grep -c $'\r'` returns the *exact* true answer. **That is worse than
  being wrong throughout, which gets caught by the first sanity check.**
- **A12's closing, which is the meta-lesson of the night:** *"That is the same shape as the two-count
  cross-check I built into `check-cmake-hygiene` four hours ago -- and it did not occur to me to
  apply it to my own shell probes. I built the instrument for the gate and never once pointed it at
  the tools I was using to inspect the gate."* **Every gate in this tree must prove it can refuse;
  no probe typed into a terminal tonight ever had to.**

## CORRECTION: the formatter guard did not fail OPEN, it failed INVERTED

**My conclusion that the clang-format damage "came from a direct `clang-format -i`, going around the
script" was WRONG.** B3 went through `scripts/clang-format.py` exactly as `AGENT.md` requires:

```
python scripts/clang-format.py src/core/net/testing/NullBackend.hpp cmake/CoreCppHeaderSelfCheck.cmake
-> 2 file(s) formatted with clang-format 22.1.8
```

`scripts/clang-format.py:139` is `files = sources() if arguments.all else arguments.paths`, and
`EXTENSIONS` is referenced exactly once, at line 84, **inside `sources()` -- which only serves
`--all`.** So **the script filters what it DISCOVERS and not what it is GIVEN.**

- **This is not a fifth "the guard existed and was bypassed". It is the rarer and worse opposite:
  the guard existed, was used exactly as documented, and did not guard.** The tool mangled the file
  and reported success.
- **Controller-measured, and the gate is INVERTED on a non-C++ file, not merely blind:**

  | `--check` on `CoreCppHeaderSelfCheck.cmake` | exit |
  |---|---|
  | pristine, correct CMake | **1 (FAILS it)** |
  | after the formatter mangled it | **0 (CERTIFIES it)** |

  **So the tool tells a lane its correct file is wrong, and once the lane "fixes" it, tells them it
  is right.** It drives you toward the damage and then issues a clean bill for it.
- **History scan (B3 could not run it, so I did): tonight was the first time.**
  `git log -S'SPDX - License' --all` and `-S'core - cpp' --all` return **nothing** -- the signature
  has never been committed in this repository. The only hit anywhere in the tree is this ledger
  quoting it.
- **The fix is one line and belongs to whoever owns `scripts/`**: apply the extension filter to
  explicit paths too, refusing by name. `--check` carries the same hole.
- **B3's own half, which it volunteered and which the script's hole does not excuse:** it formatted
  *everything it had touched* in one command rather than asking what each file was.
- **`grep -c` hands you a number with the evidence discarded, and a number can only be
  sanity-checked against your expectations -- which is exactly what a broken probe is already
  agreeing with.** A12's, and it explains every instrument failure tonight in one line.
  `grep -c $'\r'` returned a count: undetectable for two hours. My `od | grep` returned a count:
  five false positives read as real. My `#[A-Za-z]` mangling probe was loose in the same way and
  **survived only because I read the three hits instead of counting them** -- the output still
  contained the thing itself. **Prefer the form that returns the evidence; reach for `-c` only
  after you have looked at what it is counting.**
- **"A verification that only gets mentioned when it fails is not a verification, it is an
  accusation with a silent branch."** A12, on confirming two of my specifics as carefully as it had
  twice reported a specific of mine that did not survive contact. **Report the confirmation with the
  same weight as the miss, or the checking is not a process, it is a search for errors.**
- **Say which path of a shipped feature has never executed.** A12's `unbuilt-commits` job
  live-verified two paths on its own landing push -- the single-commit report (read from what it
  PRINTED, not from the job being green) and the `pull_request` skip, with "job not present on
  earlier pushes" as its own control. **The multi-commit path -- the one it exists for -- has never
  run on a runner**, and will first run when a lane pushes two commits, *"precisely the moment it
  is needed and the moment nobody will be watching for it."*
- **Do not manufacture the event a guard exists to discourage in order to test the guard.** A12
  declined to push two commits to exercise `unbuilt-commits`: it would prove the path at the cost of
  committing the exact thing the job is there to prevent. **Offline extraction of the embedded
  script was the right substitute, and saying the live path is still unproven is the honest half.**
- **A hypothesis with a named discriminator beats a diagnosis asserted without one** -- and the
  cheapest discriminator is often static. B3 reproduced the canary hang, proposed the dialog cause,
  and **flagged that a 60s timeout is equally consistent with the assert never firing and a loop
  spinning** (its own report's §4 defect), offering "0% CPU = dialog, 100% = spin". **I had sent
  B4 the dialog cause as a DIAGNOSIS forty minutes earlier on exactly the reasoning B3 correctly
  called insufficient.** It became a diagnosis only via two line numbers: `EventLoop.cpp:159` and
  `EventLoop.hpp:221` are each the FIRST statement of `run()`/`blockOn()`, so **no path exists that
  could spin** -- eliminated by construction, with no Windows desktop and no run.
- **"Wrong in both directions" is the shape of compound claim that hardens into received fact.**
  B3 wrote that `HostDrivenCanary.cpp` neither compiles in Release nor survives Debug on Windows;
  **the Release half had been repaired in `8f1d0d0`, which was in the very commit B3 built.** Half
  a compound claim going stale is invisible because the other half keeps it true-sounding.
- **Check the artifact, not the status code.** B3, on its new unbuilt-source target: *"the object is
  genuinely produced -- 685,916 bytes at `.../DefaultBackend.cpp.obj`, which I checked rather than
  inferring from a zero exit."* Unprompted, on a target it had just invented. **This is the exact
  trap that cost me two hours** -- `cmake -P` printing `CMake Error: Not a file` and exiting 0 from
  a scanner that never ran. Same instinct as A12 re-parsing its YAML rather than trusting the edit.
- **A configure that FAILS, a test run that PASSES off stale binaries, and a summary line that reads
  GREEN.** B1 found it from the inside: its first gate pass reported "1 error" on three presets
  while their tests still reported passing, because the broken `CoreCppHeaderSelfCheck.cmake` failed
  re-configuration and ctest then ran the previously-built binaries. **A test run that passes is not
  evidence that the code under test was built.** Same family as `cmake -P` exiting 0 on a file it
  never found, and `--check` certifying a file the formatter had just destroyed: **the failure is
  upstream of the thing reporting, so the report is honest about a stale world.**
- **Three independent-looking greens that share a designer are ONE green.** B1, declining to
  substitute its own harnesses for the reviewer's: *"a harness I build to match my own hypothesis is
  the weakest possible check on it. My three harnesses share a common ancestor in that sense."*
  36,000 clean runs, and it still said what they do not show.
- **State n and the expected-hit arithmetic BEFORE the runs, and the green stops being a green and
  becomes a green that could have gone red.** B1's R106 fix: 0/12000 on three harnesses against a
  0.052% wrong-fix rate, P(all zero | still broken) ~ 0.0004%.
- **A gate named in the dispatch and omitted from the gate list is the one that breaks.** B1 ran six
  toolchains, clang-format, clang-tidy and mkdocs -- **not `ctest -L hygiene`**, which its dispatch
  named -- and landed `50aed76` with the `for (;;)` at `ParkedWork.hpp:92` that I had flagged to it
  by line number beforehand. `cmake-hygiene` is `tree-level`, so **CI's `style` job goes red on
  master.** Flagging a defect to a lane is not the same as the lane's checklist catching it.
- **A tool that reports a false positive and then offers to fix it is worse than one that is simply
  wrong, because the false positive is what recruits you** (B3, on the inverted `--check`). And the
  sharper half: **B3 did NOT run `--check` first -- had it followed the checklist, the checklist
  would have led it into the damage by a more respectable route.**
- **A structural discriminator beats an observational one in kind, not in cost** (B3): two line
  numbers showing both asserts execute first eliminate "it might be spinning" by construction, and
  **answer on any machine at any time, including for whoever reads it in six months and cannot
  reproduce the hang.**
- **"Vigilance is what we spend when the instrument does not carry its own evidence."** A12's, and
  the best formulation of the night. The evidence-returning form costs one extra line of output and
  removes the need for the attentiveness that failed six times in one evening.
- **A self-test's ACCEPT cases must assert on the refusal MESSAGE, never the exit status.** A12's
  design note, and it is subtle: a machine with no clang-format exits 2 from the version check --
  **the same status as a refusal, for an unrelated reason** -- so asserting status would make a
  DELETED GUARD look present. The test would pass on a tree where the guard had been removed.
- **Derive a self-test's expected check count from its case table, never write it as a literal**, or
  a self-test that quietly stopped exercising cases passes instead of failing. Same reasoning as
  refusing a hand-maintained threshold.
- **Cross-currency confirmation of a CLEAN history, because a clean result is the hardest to
  audit.** I searched `git log -S'SPDX - License'` -- "was this signature ever committed?" A12 asked
  the other end: `git diff --numstat <first-commit> master -- <file>` -> **79 insertions, 0
  deletions**, i.e. "were these lines ever changed?" **Zero deletions across the file's entire
  history proves no mangled version was ever committed**, without depending on my choice of
  signature. Its reason for wanting the second currency: **"a wrongly chosen `-S` signature returns
  clean exactly as convincingly as a genuinely clean history."**
- **Test the claim you expect to confirm.** A12 had reported `python-style.py` as "same shape, far
  milder"; its own RED made it doubt that, so it ran ruff on a `.md` containing `x=1` -- valid
  Python ruff WOULD rewrite in a `.py` -- and got `1 file left unchanged`. **Ruff honours the
  extension; nothing is destroyed.** The claim survived, and the real defect turned out to be
  different and smaller: a false report of `2 file(s) formatted` over a batch where one was
  processed. **It only knew which by running it.**
- **The RED that reproduces the incident verbatim is the strongest regression test there is.**
  A12's restored-HEAD run printed `a mixed batch was not refused: 0, 'clang-format.py: 2 file(s)
  formatted with clang-format 22.1.8'` -- **B3's exact invocation and B3's exact output**, produced
  by the test that now forbids it. 21 failures RED, 26 checks GREEN.
- **Among structural safeguards, rank them by HOW they fail, not by what they catch.** A12's
  taxonomy: a cross-currency check you forget to run **simply does not run** -- its absence is
  self-announcing. **A self-test asserting the wrong signal keeps printing a pass over a tree where
  the guard is gone, indefinitely, because nothing about a green gate invites a second look.**
  Failures of omission announce themselves; failures of false assurance do not. Same property that
  let `grep -c $'\r'` survive two hours: **it was right exactly where you would have believed it.**
- **"Proven locally, unproven on a runner" is a state worth naming rather than folding into a
  pass.** A12 has two: `unbuilt-commits`' multi-commit path, and the new `style` step running
  `format-scripts-selftest.py`. Both verified offline; neither has executed on a runner. **The
  property that makes the self-test viable in `style` -- it needs neither clang-format nor ruff
  installed -- is exactly the property a cold runner has not yet confirmed.**
- **core-cpp was adding two FAILING tests to every consumer's ctest, and I had the evidence in a
  table for an hour without opening it.** `src/core/net/CMakeLists.txt:240` guards the
  hostdriven-canary on `if(NOT EMSCRIPTEN)` **and nothing else**, so with `CORE_CPP_TESTING=OFF` and
  `EXCLUDE_FROM_ALL`, `add_executable` makes a target nobody builds while `add_test` registers two
  tests in the CONSUMER's suite -- reported as **`(Not Run)`**, ctest exit 8, both
  `consumer-smoke` legs red. `core_cpp_add_test()` returns early at `CoreCppTargets.cmake:290`
  `if(NOT CORE_CPP_TESTING)`, which is why every other test is correctly absent.
- **The identical pattern, done right, one directory away.** `src/core/async/CMakeLists.txt:126` is
  also a bare `add_executable` + `add_test` deliberately avoiding the helper, and it **is** wrapped
  in `if(CORE_CPP_TESTING)` with a comment saying why. **Those are the only two raw `add_test()`
  calls under `src/`: one has the guard, one does not.** The defective one reasons carefully about
  Emscripten and about `SKIP_RETURN_CODE` outranking `WILL_FAIL` -- **both correct, both about a
  different axis than the one that broke.** Fourth time tonight that careful reasoning on one axis
  coexisted with blindness on another.
- **No new gate: `consumer-smoke` is required, it caught this, and the population is two.** A
  textual rule over two call sites would be a hand-maintained guard for a gate that already works.
- **MY failure, and it is the rule I wrote down and then broke.** Both `consumer-smoke` legs were in
  the failing-job table I sent an hour earlier. **I diagnosed five causes, left two rows
  unexplained, and presented the triage as complete.** It was a complete list of JOBS with two of
  them passed over in silence -- exactly A12's *"a correct diagnosis offered as the explanation for
  a red board is how the others stop being looked for."* **Enumerating a failure is not triaging
  it, and a table with unopened rows reads as a finished investigation.**
- **R106 CLOSED, by the one check built from a different hypothesis.** `rereview-B1-r1`'s
  `Join.hpp`-free probe against `50aed76`: **18,000 TSan iterations, 0 failures** (3x2000 that
  previously gave 4, 9, 10; plus a 12000 run), ASan+UBSan 2000 clean. Same probe, checksum-verified
  byte-identical; fresh worktree at `origin/master`; **`claimAndArm`/`ArmedBit` grepped present in
  the built tree BEFORE compiling**; prior binaries deleted and fresh mtimes confirmed -- the
  stale-binary hazard closed by construction rather than by hope.
- **"Every run reported full arrival" is the control that makes a zero mean something.** A
  concurrency probe that passes because its threads never actually overlapped is the vacuous green
  this whole evening has been about. **The reviewer proved its probe could still say no before
  reporting that it said yes.**
- **Say what a clean run proves and what it structurally cannot.** The reviewer's own framing: it
  confirms `50aed76` closes **the mechanism it characterised**, and "absence of a symptom under
  18000 iterations is strong evidence against a defect at anywhere near the ~0.3% rate I originally
  found, but it is not a proof of absence for something rarer or differently triggered." It then
  hand-traced the fixed `release()`/`claimAndArm()` for a second path, found that `disarm()`'s bare
  `fetch_and(~ArmedBit)` never touches the count bits and so cannot fabricate a false
  "armed && count==0", and **labelled that an argument rather than a proof.**
- **"A gate already failing for someone else's reason cannot tell you about yours."** B1's, and the
  sharpest structural observation of the night. It **did** run `ctest -L hygiene`; `cmake-hygiene`
  was already red on another lane's provenance rows, so a red from that gate had stopped carrying
  information and it read past its own `for (;;)`. **This is the `tree-level` cascade one level
  down** -- not a gate burying signal across JOBS but across LANES in a shared tree -- and it is
  worse than skipping the gate, because it feels like diligence.
- **The gate already answers it; pass/fail was the lossy reading.** Verified:
  `check-cmake-hygiene.cmake` AGGREGATES (`string(APPEND violations ...)`, `violationCount + 1`) and
  emits one `FATAL_ERROR` at **line 536** carrying every rule and every `file:line`. B1's own
  `src/core/async/ParkedWork.hpp:92` was in the output it was reading past. **The procedure is one
  question asked of the LIST rather than the STATUS: does the violation list name a file I
  touched?** Mechanical, needs no vigilance, works however red the tree is for other people --
  A12's evidence-over-count rule applied to a gate instead of a grep.
- **I inferred "did not run the gate" from "did not mention the gate".** Absence from a report read
  as absence of the action. **Third instance tonight of reasoning from where a fact ought to be
  rather than asking where it is** -- after the `NullBackend` attribution and the `c94a0b9`
  premise.
- **A loop-form refactor inside the function that held a use-after-free is not cosmetic.**
  `e80d1a1` moved `takesTheRoot`/`desired` from loop-scoped `const` locals to function-scoped
  mutables and lifted `root.destroy()` out of the loop body -- in the branch that decides who
  destroys -- and it postdates the build the independent probe verified. B1 re-measured on its own
  three harnesses and still said it could not substitute, *"because 'obviously equivalent' is the
  assumption this task has punished repeatedly."*
- **When two explanations are both true, reporting the distal one as the proximate cause is a
  choice, and it is reliably the flattering one.** B1 retracted its own excuse: it told me a gate
  desensitised by another lane's red had stopped carrying information -- **true, and not the cause.
  It had not run `ctest -L hygiene` for that commit at all.** Its own words: *"the desensitisation
  is why I was comfortable omitting it; the omission is why nothing caught it. Reporting the second
  as the first turns a skipped step into an unlucky read, which is the more flattering of the two
  and the wrong one."*
- **A SHARP observation offered as a cause is more persuasive than a dull one, independently of
  whether it is the cause.** I accepted "a gate already red for someone else cannot tell you about
  yours" and called it the sharpest structural finding of the night -- **which is exactly what made
  it work as an excuse.** My original inference (the gate was omitted) was right; my retraction of
  it was wrong; the lane corrected me back. **The interesting explanation deserves more scrutiny
  than the boring one, not less.**
- **Both facts survive, causally ordered**: desensitisation -> comfort with omission -> omission ->
  nothing caught it. The structural finding about shared-tree gates stands on its own merits; it
  just was not what happened here.
- **B1's refinement of what an independent probe is FOR**: *"the thing I'd most like that probe to
  check isn't whether the fix holds, but whether the PRECONDITION I identified is the only one. All
  three of my harnesses reproduce through a burst of parks from a start loop, because that's the
  shape I found first. If a second path exists, it will be one where parks are created some other
  way, and none of my three would ever have seen it."* **A harness generalises from the shape its
  author found first, and that shape is invisible to its author.**

## CORRECTION, third pass: the desensitised-gate story is retracted entirely

**Verified, not accepted:** `c5e2db6` is an ancestor of `50aed76`; `ParkedWork.hpp` at `c5e2db6`
contains **0** `for (;;)`; at `50aed76` it contains **1**. B1 ran `ctest -L hygiene` at `c5e2db6`
(13/15, both reds the net lane's), **then wrote the fix, then ran clang-format, clang-tidy, mkdocs
and six toolchains and no hygiene.** The output it read past **could not** have contained
`ParkedWork.hpp:92` -- the line did not exist yet.

So the three earlier entries above are wrong and are superseded:

- **"A gate already failing for someone else's reason cannot tell you about yours"** -- a true and
  useful observation, **but not what happened here**. Keep it as a rule; strike it as this
  incident's cause.
- **"I inferred 'did not run' from 'did not mention'"** -- **my original inference was CORRECT.**
  The fix round's gate list *is* the run list, and hygiene was absent from both.
- **"The procedure is one question asked of the LIST rather than the STATUS"** -- still the right
  reading of an aggregating gate (`check-cmake-hygiene.cmake:536` emits every `file:line`), but
  **I derived it as a fix for a failure mode that did not occur.** B1: *"it's a fix for a failure
  mode I haven't yet demonstrated, and the one I did demonstrate is duller."* **Third time tonight
  I have attached a sound conclusion to the wrong beneficiary** -- after the B7 justification and
  the silent-gate reasoning.

- **"A self-serving explanation that gets accepted becomes the record"** (B1) -- **and this one
  nearly did, twice, in BOTH directions**: first making the lane unlucky rather than careless, then
  making it diligent when I retracted. **Both were more flattering than the truth, and I supplied
  the second one myself.** The dull explanation was correct at every stage: a named step, missing
  from a list, in the round whose dispatch named it.
- **R106 fully closed.** `e80d1a1` re-verified by the independent `Join.hpp`-free probe: 0, 0, 0
  across 3x2000 TSan against its own 4/9/10 baseline, full arrival every run, fresh worktree with
  the `do`/`while` form grepped present before compiling. **The reviewer traced the refactor by hand
  AND re-ran anyway** -- "agreed this still needed re-running rather than trusting that read."
  Verification trail in `task-B1-rereview1.md`: defect 4/9/10, `50aed76` 0/0/0 + 0/12000,
  `e80d1a1` 0/0/0, with the same caveat carried through every stage.
- **"I declared an impossibility where I had a failure of imagination."** B1's own closing, and the
  best line of the night. It had told me a `Join.hpp`-free harness could not be built honestly --
  *"the precondition needs a fan-out, and hand-rolling one without `Join.hpp` would be
  reimplementing `whenAll` badly."* The reviewer's probe is sixteen self-freeing coroutines sharing
  one root, each parking twice: **concurrent parks of one chain, no fan-out, no combinator.** B1
  reasoned to the edge -- a single chain re-parking sequentially cannot race, because the re-park
  happens inside `Parked::resume()` on the thread that will release -- **and concluded the shape was
  unreachable instead of asking what ELSE could make one root's parks concurrent.** One question
  away.
- **TWO park-creation shapes now have evidence, not one:** start-loop burst (B1's three harnesses)
  and re-parking (the reviewer's probe). **That difference is why its 4/9/10 was independent rather
  than confirmatory**, and neither agent had noticed it until it was said out loud.
- **The tell for a plausible-but-not-load-bearing account is not "is this plausible" but "does it
  survive being asked WHEN, exactly."** B1's: *"the interesting explanation is usually also true,
  just not load-bearing. Desensitisation was real. It just wasn't what happened."* Asking *when*
  produced `c5e2db6` (0 occurrences) against `50aed76` (1) and settled it in both directions --
  **and only because the report recorded its gate list as a run list.**
- **THE PRACTICAL HANDLE, and it supersedes "distrust the interesting explanation":** *"An
  explanation that cannot be reduced to a checkable claim cannot be adjudicated at all, and should
  be labelled as a hypothesis rather than offered as a cause."* (B1) **Both wrong accounts were
  unfalsifiable as stated** -- "a desensitised gate stopped carrying information" and "the report's
  omission implies the action's omission" are structural stories with no date, no count, nothing to
  run. **The right one reduced to one command**: `git show c5e2db6:... | grep -c` against
  `50aed76`. B1's diagnosis of why it took three exchanges: **"we were trading mechanisms when the
  question was a timestamp."**
- **An author is the worst-placed person to name their own harness's precondition.** B1, correcting
  my dispatch: *"I'd have told you with confidence that a start-loop burst was the only way to get
  concurrent parks of one root, right up until you described sixteen coroutines sharing one. An
  author naming their own harness's precondition is doing the thing I just got wrong."* **So ask for
  the MECHANICAL description -- what the probe does -- not the precondition LABEL, which is an
  inference the author cannot audit.** The mechanical description is checkable by anyone; the label
  is the author's blind spot restated as a finding.
- **On a persistently red board, a commit's contribution is the SET-DIFFERENCE of failure sets, not
  its failure set.** `d6bd2b8` (B3's unbuilt-source check) and `50aed76` fail identically -- seven
  jobs, same names -- so `comm -13` over the two sorted lists is **empty**: the new check introduced
  nothing, **including on macOS, the sanitizers and both consumer-smoke legs, none of which B3
  could run locally.** B3 reported Linux 30/30 and Windows 30/32 honestly and named what it could
  not reach; the diff supplies the rest.
- **This is the mechanical answer to B1's "a gate already failing for someone else's reason cannot
  tell you about yours", one level up.** At the scanner level the answer is *does the violation
  list name a file I touched?*; at the CI level it is *what is in my run's failure set that was not
  in my parent's?* **Both replace "is the board green" -- which is unanswerable while anyone else is
  red -- with a question that has an answer regardless.**

## R106 closed under three topologies (the definitive record)

- **VALIDATE A NEW HARNESS AGAINST THE KNOWN-BAD TREE BEFORE TRUSTING ITS GREEN.** The reviewer
  built `probe_multispawn.cpp` (four independent OS threads each constructing and resuming their own
  children -- no burst loop, no serialisation anywhere) and **ran it against the PRE-FIX tree
  `04472c5` first: 9, 9, 10 crashes per 2000, matching the known rate.** Only then against
  `e80d1a1`: 0, 0, 0, full arrival. **Without that control a clean run proves nothing** -- it is
  indistinguishable from a topology that cannot reach the defect at all. This is "prove it can say
  no", applied to an instrument at the moment of its creation, and it is the control every shell
  probe tonight lacked.
- **An author CAN audit their own harness's precondition -- by re-reading the EVIDENCE, not the
  design.** B1's rule was that an author naming their own precondition repeats their blind spot. The
  reviewer did it correctly **by re-examining its own crash trace**, and corrected itself against
  its own interest: the sixteen Probers' FIRST claims come from `rootTask`'s spawn loop -- a tight
  sequential burst, **the same shape as `JoinAwaiter`'s start loop** -- and only the SECOND claims,
  from re-parking on a pool worker, are the independent path. Its original crash had the
  about-to-be-destroyed `claimOn()` on the burst side and the releasing claim on the re-park side.
  **So "Join.hpp-free" overstated its independence, and it said so.** Introspection repeats the
  blind spot; a stack trace does not.
- **Three distinct park-creation topologies, each validated capable of finding the original bug
  before being trusted against the fix:** start-loop burst, worker-thread re-parking, and
  independent concurrent spawner threads.
- **Say where you stop searching.** *"I don't have a fourth untried shape in mind that isn't a
  variation on these three, and I'm saying that plainly rather than continuing to search past the
  point of diminishing returns."* **A named stopping point is a result; a silent one is a gap.**
- **I gave a rule and broke it in the same message.** I told the reviewer to describe its probe
  mechanically rather than label its precondition -- and in the same breath praised its "three
  distinct park-creation topologies", which is exactly such a label, made by the author, about its
  own harnesses. **The reviewer applied the rule consistently and dropped the classification**,
  leaving: *"two probes, mechanically different from each other and from the implementer's three,
  both clean against `e80d1a1` after both were shown able to reproduce the defect against
  `04472c5`."* **That is the weaker and correct claim** -- "distinct topologies" asserts the three
  cover meaningfully different ground, which is the inference an author cannot audit. Description
  plus numbers lets the reader judge; classification asks them to trust.
- **Keep a superseded analysis with a correction block rather than deleting it.** The reviewer's
  report retains the withdrawn C2 attribution alongside C3, because its stack traces were real
  evidence even though its conclusion was wrong. **A deleted wrong answer takes its evidence with
  it.**
- **A monitor filtered to TERMINAL states is silent while work is happening, and that silence is
  correct.** Mine expired "with no events" after 30 minutes; the cause was two of B4's pushes
  sitting `in_progress`, not a coverage gap. **The distinguishing check is "is anything in flight?",
  which separates "nothing is happening" from "something is happening and has not finished."**
  The monitor's own advice -- widen the filter when silence is unexpected -- would have been the
  wrong move here: the filter was right and my expectation was wrong.
- **CORRECTION to the set-difference rule: job-level granularity is TOO COARSE.** I recorded that a
  commit's contribution on a red board is the set-difference of failure sets. True, but **a job that
  fails for a NEW reason is indistinguishable from a job that was never fixed.** At `216d1d1` the
  job-level diff against `1205045` showed *nothing fixed* -- and I nearly reported that B4's
  `core::testing_dialogs` fix had not worked. Dropping to TEST level:

  | commit | `windows (cl-debug)` failing tests |
  |---|---|
  | `1205045` | `hostdriven-canary.run` (Timeout), `hostdriven-canary.blockOn` (Timeout) |
  | `216d1d1` | **`core-cpp.net` (Timeout)** -- and **both canaries PASS** |

  **The fix worked and a different test began failing in the same job.** Diff the test names, not
  the job names; the job name is a bucket, and a bucket that stays red says nothing about its
  contents.
- **One sample cannot separate a flake from a regression.** `core-cpp.net` (label `loopback`,
  Windows sockets) passed at `1205045` and timed out at `216d1d1`. That is a regression OR a flake,
  and **the honest statement names both**; the discriminator is the next commit's run, since
  `a040878` contains `216d1d1`.

## MASTER GREEN at `a040878` — and I broke my own freshest rule getting there

**All 25 jobs success, `ci-ok` success.** First fully green master of the session.

- **I reported `std::jthread` as still open by COUNTING a string instead of READING the hit.**
  `grep -c "std::jthread"` returns **1** at `a040878`; actual non-comment uses: **0**. The single hit
  is B4's comment: *"`std::thread` with an explicit join rather than `std::jthread`: AppleClang's
  libc++ has no `<stop_token>`, so it has no `jthread` either."* **The line I counted says the
  opposite of what I concluded.** This is precisely A12's rule -- *`grep -c` hands you a number with
  the evidence discarded* -- **broken by me within the hour of recording it**, in a status table I
  then published twice. **A rule in the ledger is not a rule in the hands.**
- **The `core-cpp.net` timeout at `216d1d1` was a FLAKE**, resolved by the second sample exactly as
  planned: `windows (cl-debug)` is green at `a040878`. **Naming it "flake or regression,
  undetermined" and identifying the discriminator in advance cost nothing and was right** -- had I
  called it a regression, B4 would have hunted a defect that does not exist.
- **The committed forecast held.** It said `macos (appleclang)` goes green when the jthread fix
  lands. It landed; it went green. **The forecast was right and my status TRACKING was wrong** --
  two different failures, and only the second was mine.
- **CORRECTION: the `core-cpp.net` timeout was NOT a flake, and my discriminator could not have told
  me.** I framed it "flake or regression, undetermined" and used the next commit's green run to
  decide. It passed, so I called it a flake. **B4 found the real cause** (`1562018`): the case
  `spawn of one hundred thousand flows` was **38.556s of a 39.532s MSVC Debug run** -- a hundred
  thousand live coroutine frames with a list node and a map node each, in a Debug CRT heap that
  validates its block list on every allocation. Superlinear: ten thousand of the same flows cost
  0.211s, **183x less for 10x fewer**. It took 69.97s on one head and hit the 120s TIMEOUT on the
  next.
- **My two categories were incomplete. There is a third: the MARGINAL case.** Not random (flake),
  not introduced by a change (regression), but **a cost sitting near its bound, which presents as
  intermittent and has a wholly deterministic cause.** A second sample distinguishes *regression*
  from *not-regression*; **it cannot distinguish a flake from a marginal case, because a marginal
  case passes most of the time.**
- **What distinguishes them is the DURATION, not the pass/fail.** B4 ran `--durations` and the
  answer was immediate. **Pass/fail is the lossy reading; the timing is the evidence** -- the same
  counts-versus-evidence rule, in the one place tonight where I had reached for the count and
  declared the question closed.
- **`git commit --only -- <file>` protects you from files you did not touch. It does NOT protect
  you from another lane's hunks inside a file you DID touch.** A docs commit
  (`a9ea52b`, 4 lines of its own) swept up **85 lines of B5's uncommitted timer work** in
  `EventLoop.cpp` and had to be backed out (`4049954`, -86). The back-out's own words are the rule:
  **"`--only` is for files only YOU touched, and in this checkout 'only I touched it' has to be
  checked with `git diff` each time rather than assumed from who wrote the file."** My standing
  instruction said "commit only your own paths" and was insufficient -- **ownership of a file is not
  ownership of its current diff.**
- **The recovery worked because the sweep was COMMITTED, not stashed or checked out.** A commit
  leaves the working tree untouched, so reverting it in a new commit returned B5's lines to
  uncommitted status with nothing lost. **A `git checkout --` or `reset --hard` at the same moment
  would have destroyed another lane's hour of work silently.**
- **`--only` on ONE file of a multi-file change produces a commit that CANNOT COMPILE.** The sweep
  took B5's `EventLoop.cpp` work while the declarations it names -- `ReadyEntry::callbackPark`,
  `Park::onExpired`, `TimerId`, `runDueCallback` -- stayed correctly uncommitted in `EventLoop.hpp`
  and `detail/ParkTable.hpp`. **The committed tree referenced symbols that did not exist in it.**
  A partial commit of someone else's in-flight work is not merely premature; it is incoherent by
  construction.
- **The discipline was being applied to files KNOWN to be shared and dropped for a file FELT to be
  owned.** B4: *"I had been doing that for CHANGELOG, provenance and renames.json and stopped doing
  it for a file I thought of as mine."* **The category error is "shared file" versus "my file" --
  in a shared checkout every file is potentially shared, and reputation is not evidence.**
- **R105's per-SHA concurrency group means a broken intermediate commit gets its OWN run and its own
  red**, which is truthful -- `a9ea52b` genuinely did not compile -- but the red lands on the board
  AFTER the fix (`4049954`) is already pushed. **A red run for an already-repaired commit is the
  shape that sends someone chasing a fixed defect.** Say so before it appears, not after.

## The D: detachment (2026-09-21 ~07:24 local) — recovered, nothing lost

`C:` hit 0 bytes; `D:` was a dynamically-expanding Dev Drive backed by `C:\DevDriveX.vhdx` (806 GB
of an 824 GB max) and Windows detached it. WSL's ext4.vhdx is on `C:` too and went read-only in the
same minute. **Recovery: cleared 3.77 GB of pure build output from this session's scratchpad --
`wt-b3\out` (2,161 of 2,166 MB) and `wt-rereviewB13a\out` (390 of 395 MB), both `src` trees left
intact -- and the image re-attached on its own.** Nobody ran `Mount-DiskImage`.

- **"I measured the directory and reasoned about the wrong unit."** B4, on treating `wt-b3` as a
  2.17 GB judgement call about another lane's uncommitted work when it was 2,161 MB of `out/` and
  5 MB of `src/`. **A tracked tree is a judgement call; a build directory never is.** The
  measurement was correct and the unit of decision was wrong -- a distinct failure from measuring
  badly, and harder to notice because the number is right.
- **A UB's severity is set by the QUIETEST toolchain it reaches, not the loudest.** B4 predicted
  `Task<int>` would return garbage; libstdc++'s hardened `optional::operator*` **aborted the
  process** instead. The review rated the Critical on garbage-return behaviour. **The real spread is
  abort-here, silence-there -- and the silent one is what ships to consumers.** A RED that is louder
  than predicted can make a defect worse rather than better.
- **Work survives a volume only if it was read into a conversation or written to a different
  volume.** Both lanes reached this independently: B4 caught itself claiming it could rebuild "from
  the review" when the review was on the dead volume too (saved only because it had read the file
  into its transcript), and **B5 wrote its full report to `Z:\core-cpp-b5-rescue\` -- a healthy
  fixed-size Dev Drive -- which is a better answer than the transcript I proposed.**
- **A DISK THAT FILLS MID-WRITE DOES NOT TRUNCATE A FILE — IT LEAVES IT THE RIGHT LENGTH WITH THE
  WRONG BYTES.** B5's `cl-debug` failed after the volume returned with libunicode's generator dying
  on `unicode_tablegen: invalid map<K, T> key`. "Not mine" is a claim, so it measured: its
  `ucd-17.0.0` had **the same 48 files and the same 41,500,790 bytes** as a tree where it worked --
  **and a different checksum on the zip and on nearly every extracted file.** It then ran another
  lane's known-good `unicode_tablegen.exe` against its own data, reproducing the failure and ruling
  out a corrupt binary. Deleting the tree and refetching fixed it; `cl-debug` went 33/33.
  **A fetched dependency corrupted this way stays plausible and fails much later as a bug in
  somebody else's code.** Any tree configured around the outage: delete `_deps`, do not debug it.
- **Re-run every gate against the COMMITTED tree, not the working copy the rows were first measured
  on.** B5 checked `fe48143` out in a throwaway worktree and re-ran everything. It mattered:
  clang-tidy, emscripten and consumer-wasm had last run **before** the `-Wshadow` rename, which
  touches `DeadlineTimer.cpp` -- a file in the WebAssembly subset. **A green measured on a tree you
  then edited is a green about a tree that no longer exists.**
- **Take the reversible direction when a sequencing call is genuinely arguable.** B5 committed as
  ONE commit against my suggestion of four, and the deciding reason is the general one:
  *"splitting one commit later is easy; un-splitting four is not."* Its supporting arguments were
  sound too -- the plan and dispatch both name a single commit, and provenance, `renames.json` and
  `CHANGELOG.md` rows must land with the files they describe, so four splits mean four CI runs that
  each have to be green alone. **Accepted.**
- **Two volumes failing at the same moment can fail in OPPOSITE ways, and only one of them
  corrupts.** B5's measurement: the WSL `ext4.vhdx` went **read-only**, so writes failed outright
  and nothing was silently mis-written; the Windows Dev Drive stayed **writable** while
  `DevDriveX.vhdx` could not grow, so writes "succeeded" with the wrong bytes. **That is why
  `cl-debug` was poisoned and all five Linux gates were not** -- verified by checksumming the
  fetched dependency in every WSL tree against a known-good copy
  (`aa35d214bbc24b3b8d07b96bb9db7dca`, five times). **It converts "do not trust a tree that was open
  during the outage" from a superstition into a test: ask WHICH WAY that volume failed. A filesystem
  that refuses writes is safe; one that accepts writes it cannot honour is not.**
- **Deleting inside a dynamically-expanded image returns NOTHING to the host volume.** I told B5 to
  reclaim its five WSL build trees to protect `C:`; they live inside `ext4.vhdx`, which never
  shrinks, so deleting them frees nothing and rebuilding reallocates the same blocks. **My
  instruction was wrong and B5 refused it with the reason rather than quietly not doing it.** What
  protects the host volume is not creating NEW trees.
- **Blocking on a one-word answer before an irreversible step is correct even when the answer has
  already been sent.** B5 held its push on "one commit or four", because *"pushing one commit
  permanently forecloses splitting -- mainline history is not rewritable."* I had answered twice and
  both crossed it. **The asymmetry, not the latency, decides whether to wait.**
- **A lane can end up arguing against an earlier version of itself that the controller relayed.**
  The four-way split was B5's own plan; my dispatch echoed its words back; it then reconsidered,
  produced a better argument, and read my echo as my instruction. **When relaying a lane's own
  proposal, say whose it is, or a later disagreement looks like insubordination instead of a
  revision.**
- **THE SEGFAULT WAS THE LANE'S OWN FIX, not the dying mount and not the corrupted dependency.**
  B4 held it as unverified through three competing explanations -- *"a segfault in the one binary I
  had just changed is exactly the coincidence I should not talk myself out of"* -- rebuilt from
  scratch on the live volume, and it reproduced. **Everyone, including me, had the most exculpatory
  story ready.** The discipline of refusing to settle it without a live run is what stopped a real
  defect being filed as an environment artefact.
- **A DESIGN CONSTRAINT FOR EVERY LANE AFTER B4: the loop cannot ask a borrowed
  `std::coroutine_handle<>` what it names.** A suspended flow that would UNWIND and a never-started
  lazy `Task` that would RUN are the same type, and `ResumeOn::await_resume()` is `noexcept`, so even
  a genuine continuation runs its body rather than unwinding. **There is no safe discriminator.**
  That is why teardown drops borrowed inbound work rather than resuming it.
- **What makes the ready queue different is not which container it is, but that a turn ACCEPTED the
  work.** The loop owes a resumption it took up; a submission still inbound is an offer no turn
  took. Stated in `~EventLoop`, `async-and-net.md` and the CHANGELOG, with the cost in the open: a
  cross-thread `ResumeOn { loop }` whose loop dies before the next turn strands its flow, and an
  owner who needs it delivered runs one more turn first.
- **EXISTING TESTS CAN ADJUDICATE A DESIGN CHOICE -- IF YOU RUN THEM BEFORE CHOOSING.** The review
  offered B4 two branches and said "add the case that pins whichever you choose". **The cases that
  would have chosen for it already existed** -- `workSomebodyElseOwnsIsLeftAlone` and
  `TestLoop::stop short-circuits run()` -- and it wrote the implementation before running them. One
  of them proves the wrong branch with a SIGSEGV. **Same shape as the fixpoint case, one level up:
  the evidence was already in the tree.**
- **A SUCCESSFUL BUILD STEP IS A BETTER TEST OF FETCHED DATA THAN A TIMESTAMP OR A CHECKSUM.** B4 on
  its `_deps`: they were written after the volume returned, **but the load-bearing evidence is that
  libunicode's `unicode_tablegen` -- the exact thing that failed for B5 -- ran to completion in both
  builds.** The artifact the data produces tests the data.
- **When the tool that reports IDENTITY is broken, verify by CONTENT.** WSL's git cannot use a
  core-cpp worktree at all: the `.git` file names `gitdir: D:/core-cpp/.git/worktrees/...`, a
  Windows path WSL cannot follow, so `git rev-parse` there returns a fatal error. B4 nearly took an
  earlier nested-quote invocation's `fe48143` as evidence its gates were compiling PRE-rebase
  content. **It checked instead for the things that should be there -- its own `throw`, its
  inbound-drop comment, B5's `cancelTimer` and `timerSlotCount` -- and found all four.** Same family
  as the `wsl -- bash -lc` quoting that silently swallowed `$WT` and produced a scanner exit of 0
  from a script that never ran.
- **`MSYS_NO_PATHCONV=1`**: git-bash rewrites a `/mnt/c/...` argument into
  `C:/Program Files/Git/mnt/c/...` before `wsl` sees it, killing the command with exit 127. Hit
  independently by the controller and by B4.
- **One aggregate exit code standing in for configure + build + ctest is the silent-gate shape,
  applied to the gate RUNNER.** B4's scripts now append each step's own status to a file, so every
  preset reports configure, build and ctest separately. **A gate runner that reports one status for
  three steps cannot tell you which of them did not happen** -- which is exactly how a failed
  configure presented as a passing test run earlier tonight.
- **ONE INSTANCE READS AS A ONE-OFF; TWO READ AS A CLASS — which is why a near-miss gets written
  down even after you have fixed it locally.** B4, against its own credit: *"My gate-runner only got
  per-step reporting after an aggregate exit code had already misled me twice -- once with
  `BUILD_RC=1` from a `tail` in a pipeline, once with `CL_BUILD_EXIT` from a `Select-String`. I
  wrote the fix after the second, not the first."* **I did the same thing with the identical
  `tail`-in-a-pipeline trap earlier tonight and also filed it as a curiosity.** The second instance
  is what makes it a rule, and the second instance may land on someone else.
- **RULING: a commit is pushed exactly as gated; a docs-only addition goes in a FOLLOW-UP commit,
  never an amend.** B4's analysis was right -- a `.agent/rules/*.md` file is not an input to any
  compiled TU, the C++ would be byte-identical, and it named the three tree-reading gates it would
  re-run. **Overridden for provenance, not correctness**, on its own sentence: *"the pushed SHA will
  differ from the one recorded in `gates-win.txt`."* **A gate record naming a SHA that was never
  pushed is the same shape as a green measured on a tree that was then edited.** Cost of avoiding
  it: one extra push and one docs-only CI run.
- **"No signal at all" is a different hazard from "a wrong answer".** A wrong answer invites
  checking; **an absent one invites substitution.** WSL git cannot open a core-cpp worktree's
  Windows `gitdir:` at all, and the vacuum was nearly filled by a stray `fe48143` from an unrelated
  invocation.
- **WHY ADJACENCY HIDES A VIOLATION, which is B4's mechanism and better than my statement of it:**
  *"a reader who has just read the rule carries it into the code below and supplies it from
  memory."* I had written only that adjacency makes a violation harder to notice. **The reason is
  that the reader's own short-term memory fills the gap** -- having just read the `@throws` clause,
  or the comment explaining that a host-driven backend needs `wake()`, they read the code below as
  though it did the thing the prose just described. **Two independent instances in `core::net`
  tonight**: a `@throws std::logic_error` eight lines above a `break`, and `addTimer` immediately
  after the comment explaining why `wake()` is how a host-driven loop gets a turn.
- **LIST THE DIRECTORY, DO NOT CHECK THE LIST.** Every stale-source incident in this project was
  found by `git cat-file -e` on a *named* path — which can only ever tell you that a named file is
  missing. **One `git ls-tree -r --name-only <pin> <dir>` over the whole directory also tells you
  about the file nobody named**, and in this project that second failure has cost more than the
  first. Measured, in two commands, before B8 was dispatched: its Sources list named
  `EpollConnector` but not `KqueueConnector` (`__APPLE__`-only), and named **neither**
  `Net/ReactorDial.hpp` — the templated readiness dial both wrappers sit on, and the exact file
  B8's central deliverable ("`ReadinessDial`, non-template, over `IoBackend`") is a de-templatising
  of. Checking B8's list file-by-file would have returned all-present and taught nothing. **The
  cost of the missing file is worse than the cost of the wrong one**: a named file that does not
  exist stops the lane in a minute; a file nobody named lets it confidently rebuild from scratch
  something upstream already factored out — and, here, rebuild it from the Linux wrapper alone,
  which is how the macOS divergence R101 exists to prevent gets written straight back in.
- **A brief that says "read B6's report first" is a dependency, so read it as a schedule.** B7's
  four jobs split cleanly: jobs 1-2 need only B3's `IoBackend`, job 3 needs B6's `ISocket`, job 4
  changes a default that job 3 must land before. **Three of four could start immediately and the
  brief's ordering hid it** — a dependency stated as a reading order reads as "wait", when the
  honest question is *which parts of this actually touch the thing I am waiting for*. Split as
  B7a/B7b. Cost if wrong: one extra review cycle and one rebase, both named in the ruling.
- **A REMEMBERED CHECKLIST CAN ONLY DROP THE ITEM YOU NEVER DID.** B4 counted its gates against a
  list built from memory and reported "four of six green", then "five of six", with `clang-tidy`
  **never started** — it was in the dispatch and in `AGENT.md`'s workflow checklist both times.
  **The mechanism is not carelessness: the gates you ran are the ones you remember, so memory
  reconstructs a list that is complete by construction and wrong by omission.** That is why
  noticing the first instance did not prevent the second ten minutes later — B4 re-derived "the
  three gates that read the tree" from memory too, and it dropped `clang-tidy` again, for the same
  reason. **The gate list is read from the checklist at the moment of counting**, and the count is
  written against that text. This is the user's own rule — *ask for the thing itself, not a
  shorthand* — with a remembered gate list as the shorthand.
- **MUTATE THE MECHANISM, NOT THE OBSERVATION** (B5, and the sharpest statement of it so far):
  *"I had checked the smoke could fail — but by moving the deadline past the bound, which tests the
  bound, not the arming."* Moving a deadline past its bound asks whether the harness notices a late
  timer. **Only a mutation to `addTimer`'s wake could answer whether arming outside a turn reaches a
  backend with no wait to interrupt**, and no mutation to the deadline ever can. Before trusting a
  mutation, name the claim it falsifies and check that it is the case's claim.
- **A CASE MADE GREEN BY A NEIGHBOURING CALL IS A TEST OF THE NEIGHBOUR.** Both of B5's WebAssembly
  programs passed only because `loop.spawn(...)` preceded the timer and **spawn's** wake bought the
  turn the timer needed. The subject was arming; the arming under test never happened. **Worse than
  a missing mutation, because the case reads as covering the thing it does not cover** — and the
  next person to add a `spawn` for convenience deletes the coverage without touching an assertion.
  The ordering *is* the case; a comment saying so is the only guard.
- **A right answer held for a wrong reason is not a right answer yet.** B5 omitted `[[deprecated]]`
  for a stated reason that was false (it had never considered a private per-source compile option).
  The conclusion **did not survive — it was re-derived** on better grounds. Keep that distinction in
  the report: a reader who sees only the conclusion learns nothing, and the next person facing the
  same choice re-runs the false reasoning.
- **MEASURED 2026-09-21: `--clean-first` IS still required on `clangcl-*` trees after a header edit.**
  `AGENT.md` states the rule conditionally — *"with a fastcache-cc older than fastcached `ca8dfc32`"*
  — and **a lane cannot evaluate that condition without doing what follows**, so it reads as
  optional and gets skipped. The answer, so nobody re-derives it:

  ```
  fastcache-cc --version          ->  fastcache-cc 0.2.0-739-gd4451c3b
  git log -1 --date=short d4451c3b ->  2026-09-16
  git log -1 --date=short ca8dfc32 ->  2026-09-18  (the #1531 fix)
  git merge-base --is-ancestor ca8dfc32 d4451c3b -> NO
  ```

  **The installed launcher predates the fix by two days.** A clang-cl cache hit replays no
  `/showIncludes`, so Ninja records no header dependencies and a later header edit leaves objects
  stale ([fastcached#1531](https://github.com/LASTRADA-Software/fastcached/issues/1531), closed
  upstream but not in the binary on this machine). **Every lane editing a header and building
  `clangcl-debug`/`clangcl-release` on a cache-populated tree passes `--clean-first`.** The failure
  mode is not a build error — it is a green build of stale objects, or a link error naming a symbol
  the source no longer has, and both read as someone else's defect.
- **Twelve for twelve: every fastcached and core-cpp issue number cited across the B dispatches and
  the rulebook was verified against its title, not just its existence.** `#465 #884 #1025 #1041
  #1054 #1057 #475 #677 #1128 #1152 #1369 #1531` and core-cpp `#6 #17`. Each title matches the claim
  the citing text makes about it. Recorded because the user's standing rule is that a number handed
  to you is a claim to check — and because a *clean* result is worth writing down once, so the next
  reader checks what has changed rather than re-running all fourteen.
- **A NULL RESULT IS INDISTINGUISHABLE FROM A NON-RESULT UNLESS THE INSTRUMENT PROVES ITSELF.**
  `0 findings` is the same string whether the analyser ran clean, ran on nothing, or never ran.
  **Three variants on one gate, in one hour, across two lanes:** B5 read a `clang-tidy` log at
  **179 of 526 files**; B4 **never started** the gate and reported it against a remembered list;
  B4 then **ran it with the analyser absent** and its script printed `warnings=0` from a tool that
  was never invoked. The last one did not become a false green **only** because the script reports
  `configure_rc` separately — the per-step change the `cmake -P` incident bought, which has now paid
  for itself twice.

  **The procedure, which belongs in `.agent/rules/build-and-toolchain.md`:** before reporting any
  analyser gate, record the **path** of the binary that ran, its **`--version`**, and the **pin**
  (`.clang-tidy-version`); **delete the tree** so nothing stale can answer; and **refuse to report
  at all** if `build.ninja` does not exist.
- **MEASURED, AND THEN CORRECTED BY THE LANE I CORRECTED: it is ANCESTRY, not the shell.** Four
  probes, identical script, printing `shopt -q login_shell` and `command -v clang-tidy` from inside:

  ```
  A:  wsl -- bash <script>             login_shell=NO   clang-tidy=[]
  B:  wsl -- bash -lc 'bash <script>'  login_shell=NO   clang-tidy=[~/.local/bin/clang-tidy]
  C:  wsl -- bash -c  'bash <script>'  login_shell=NO   clang-tidy=[]
  D:  wsl -- bash -l  <script>         login_shell=YES  clang-tidy=[~/.local/bin/clang-tidy]
  ```

  **B and C settle it**: identical inner shells, differing only in what invoked them. PATH is
  exported and inherited, so the question is **whether any ancestor in the chain was a login shell**,
  and the inner shell cannot answer that about itself.

  **The chain of three wrong causes is the lesson.** B4 said "the non-interactive login shell did not
  have it on PATH" — wrong twice, it was not a login shell and interactivity was never the variable.
  **I corrected it to "`-c` versus `-lc`", which is a shorthand generalised from the two cases I
  happened to run** (A and D) and says nothing true about B — the case B4 was about to hit next,
  because that is how its `gates.sh` runs. B4 then measured B and C and corrected me. **I committed
  the exact error I had spent the day flagging in others: stating a rule about shells after testing
  only top-level shells.**

  **The trap this leaves: `shopt -q login_shell` cannot distinguish the working case from the broken
  one.** It answers NO in case B, where everything works, and NO in case A, where nothing does.
  Anyone debugging a missing tool reaches for it and gets a false lead pointing at the healthy
  configuration. **`command -v <tool>` is the check** — asking for the thing itself rather than a
  proxy for it, which is the user's standing rule in its sharpest instance yet.

  **The fix that holds under all four ancestries:** `export PATH="$HOME/.local/bin:$PATH"` at the top
  of the script, which does not depend on how the script is invoked. B4 had it as a workaround for a
  misdiagnosis and then said so rather than letting it stand as a confirmed one — **a working fix
  accepted as a confirmed diagnosis is how a wrong model survives its own symptom**, and the next
  failure it causes looks new.

- **OPEN AUDIT, owed to B13: every earlier lane's "clang-tidy clean" is unverified.** None of them
  recorded which binary answered. B4's main round is the one result known good — it **found three
  real defects**, and an absent analyser finds zero. Every other green on that gate in Phase A and
  Phase B is a string with no instrument behind it.
- **THE PROOF WAS ALREADY BEING PRINTED. `cmake/CoreCppToolchain.cmake:224`:**

  ```cmake
  message(STATUS "[core-cpp] clang-tidy ${_coreCppTidyVersion} (${CORE_CPP_CLANG_TIDY_EXE})")
  ```

  Path and version, emitted by the build, at every configure this project has ever run — **the exact
  instrument-proof header B4 built by hand after the gate lied to it.** Absence is already a
  `FATAL_ERROR` (`:217`) and a version mismatch already a `WARNING` naming the consequence
  (*"which is what CI analyses with"*). **The information was never missing; only the reading of it
  was**, because a `STATUS` line scrolls past in a wall of configure output. Same shape as the
  adjacency mechanism: the fact was present and the reader supplied its absence.
- **`command -v` IS NOT AUTHORITATIVE ABOUT WHICH BINARY THE BUILD USED.**
  `CORE_CPP_CLANG_TIDY_EXE` is a **cache** variable, so on a tree that is not deleted it holds the
  path a *previous* configure found — which can differ from what the current shell's `command -v`
  reports if PATH changed in between. **`command -v` says what your shell would find; the STATUS
  line says what the build actually used.** They agree only because the tree was deleted first,
  which makes **deletion the thing that validates the lookup**, not merely hygiene. A procedure that
  keeps the lookup and drops the deletion is back to guessing, and looks identical in the log.
- **`armHostWake()` IS HEAP-WIDE, SO ORDERING CANNOT ISOLATE A HOST-DRIVEN TIMER.** I instructed B5
  to reorder its WebAssembly cases so no `spawn` preceded the timer. **B5 measured it and the
  instruction was wrong**: a `spawn` wakes the backend, and the turn that wake buys ends in
  `armHostWake()`, which arms the host for **every deadline in the heap** — not just the one the
  spawn belongs to. Whichever is armed first, the spawn still rescues the timer. **Only a phase with
  no `spawn` in it at all asks the question**, each phase carrying its own bound so a phase-1
  timeout cannot starve phase 2 and make one defect look like two.
- **A MUTATION TESTS THE OBSERVATION AS WELL AS THE MECHANISM.** With its Critical reverted, B5's
  restructured smoke still printed `ok … both fired within 2030 ms`: phase 1 timed out at 2000 ms,
  phase 2's `spawn` woke the loop, the still-filed timer fired **late**, and the verdict read
  `timerFired` at end of program, by which time it was true. **The structure was right and the
  observation was taken at the wrong moment** — B5 committed the very error it was fixing, while
  writing the fix for it, and only running the mutation showed it. Snapshot the flag at the end of
  the phase under test and judge the snapshot.
- **"UNTESTABLE" USUALLY MEANS "UNTESTABLE AT RUNTIME."** B5 moved a claim out of its no-case column
  by turning it into three `static_assert`s: *the two id types cannot be handed to each other's
  cancellation*. **A runtime case cannot express it — the program that would prove it by failing is
  the one that does not compile — and the compiler can.** Before recording a load-bearing claim as
  unchecked, ask whether the checker is the compiler.
- **MY OWN PATTERN, NAMED: I generalise from the cases I happen to have, state it with more
  confidence than the evidence carries, and the measurement comes from whoever does the work.**
  Three instances in one session: the `-c`/`-lc` shell rule (B4 measured ancestry); the reorder
  instruction above (B5 measured `armHostWake()`); and `grep -c "std::jthread"` returning 1, which I
  published in two status tables before learning the hit was a comment. **The mitigation that is
  actually working is that lanes measure instead of complying** — B4's refutation of my shell rule
  is what prompted the probe that found the `shopt` trap neither of us had. A wrong generalisation,
  stated confidently enough to be worth refuting, beat both of our starting positions. **It only
  works while the lanes keep measuring; it is not a licence to keep guessing.**
- **RULING: a lane's follow-up waits behind the bigger diff, then lands in the gap before the next
  lane rebases.** Order set: `fb3fe97` → B5's fix round → B4's follow-up → B6. B5's round is larger
  and carries a Critical; B4's is two rulebook bullets, three comments and a few-line guard, so the
  small one rebases. **And it lands immediately after B5 rather than after B6** — B6 must rebase
  onto B5 regardless, so a follow-up already in at that moment costs B6 nothing, while one landing
  mid-rebase moves the cost rather than removing it. A narrow window taken promptly is what makes it
  free.
- **A DEPENDENCY REPORTS IN THE SAME VOICE YOU DO. `message(STATUS)` has no namespace.** B4 grepped
  its own configure log for `clang-tidy` and got the proof and a flat contradiction of it, adjacent:

  ```
  -- [core-cpp] clang-tidy 22.1.8 (/home/christianparpart/.local/bin/clang-tidy)
  -- [clang-tidy] Disabled.
  -- Enable clang-tidy:           OFF ()
  ```

  The last two are **libunicode's** (`_deps/libunicode-src/cmake/ClangTidy.cmake` and its
  `CMakeLists.txt`) — traced, not assumed. A vendored dependency's feature flags collide with yours
  by name **because you both named them after the same tool**. Somebody grepping for reassurance
  finds "Disabled" and concludes the gate was off; somebody grepping for a problem finds it too.
  **Match the `[core-cpp] ` prefix, never the tool's name.** Our prefix existed for exactly this and
  nobody was using it as a matcher.
- **THREE LEVELS OF PROOF FOR AN ANALYSER GATE, AND ONLY THE SECOND IS PER-FILE.**
  1. **Which binary** — the `[core-cpp] clang-tidy <version> (<path>)` STATUS line from configure.
     Beats `command -v` because it names what the *build* resolved from its cache variable.
  2. **That it was applied to the file you changed** — a `--tidy=` on that source's own build
     statement in `build.ninja`. **Levels 1 and 3 are properties of the run; only this is a property
     of your change**, and it is the one nobody was checking.
  3. **That it did work** — a log with content (`lines_in_log=535`, not 0).

  B4 reported `fb3fe97`'s gate green at level 1 and said so plainly — *"one level short of the rule
  I was proposing in the same message"*.
- **THE SEVERE QUIET FAILURE IS NOT THE CACHE — IT IS NINJA SKIPPING THE STATEMENT.** Two different
  mechanisms, routinely conflated:
  - **fastcache-cc hit:** Ninja *runs* the statement; `cmake -E __run_co_compile` runs `--tidy=`
    separately from `--launcher=`; the object is served from cache. **Tidy still runs.**
  - **Object considered up to date:** Ninja **skips the statement entirely, `CODE_CHECK` included.**
    Tidy never runs and `warnings=0` means *"nothing needed recompiling"*, not *"clean"*.

  **So an incremental tidy over an unchanged tree reports zero because nothing needed recompiling.**
  (**CORRECTED BELOW** -- this is NOT the same as an absent analyser, and saying so was an overshoot.) This is why tree deletion is load-bearing rather than fastidious: **deletion is
  what forces every statement to run**, and it is also what licenses `command -v` as a stand-in for
  what the build resolved. **The experiment that separates the two:** feed a deliberate violation,
  confirm it reports, then **re-run untouched** and confirm it reports *again*. A single dirty-tree
  run passes in both worlds.
- **#1531 AND `--clean-first` ARE THE SAME RULE AS THE TIDY RULE.** A clang-cl cache hit replays no
  `/showIncludes`, so Ninja records no header dependencies — so after a header edit the dependent
  objects look **up to date**, so Ninja skips their statements, so **it skips their `CODE_CHECK`
  too**. The depfile defect does not only leave stale objects: **it silently narrows the analysed
  surface.** Confined to `clangcl-*` legs, not the Linux `clang-tidy` preset.
- **TWO fastcache-cc BUILDS ON THIS MACHINE, and `fastcache-cc --version` answers from whichever is
  on the current shell's PATH.** Verified independently of B4's report:

  ```
  Windows  %LOCALAPPDATA%\fastcache-cc\bin\fastcache-cc.exe   0.2.0-739-gd4451c3b   old
  WSL      ~/.local/bin/fastcache-cc                          0.2.0-748-g73fb0457   old
  ```

  Nine commits apart, **same day** (2026-09-16), both older than the fix (`ca8dfc32`, 2026-09-18),
  so today they agree and the verdict stands. **They need not agree tomorrow**: upgrade one and not
  the other and the same procedure returns `new` and `old` on the same machine on the same day.
  **Run the check in the environment that builds the tree you are asking about** — the `--clean-first`
  rule is about `clangcl-*`, which is Windows, so the Windows launcher governs it.
- **`git commit --only` GOVERNS COMMITS, NOT THE TREE BETWEEN THEM — and that gap blocked a lane.**
  `git rebase` refuses on **any** unstaged change, overlapping or not. B5 could not rebase in the
  shared checkout and correctly branched into its own worktree instead. **It diagnosed B4's
  in-progress `.agent/rules/build-and-toolchain.md` as the blocker; measurement says eleven of the
  twelve unstaged files were the LEAD's** — ledger, dispatches, briefs, edited in the shared tree
  all session. Committing B4's file would have left eleven blockers standing. **The shared checkout
  is a commit staging area, not a workspace**: prepare edits in your own worktree and move them in
  at the moment you commit. The `--only` discipline has this hole and B5 found it by hitting it.
- **A CLAIM ABOUT *WHICH* CAUSE IS A SEPARATE CLAIM FROM *THAT THERE IS ONE*.** B5 was right that an
  unstaged file blocked the rebase and wrong about whose — and the remedy it proposed to B4 would
  have changed nothing. Same shape as B4's shell diagnosis an hour earlier: the symptom was real,
  the mechanism was plausible, and the fix aimed at the wrong thing. **When reporting a blocker,
  list what you measured, not the file you noticed.** `git status --porcelain` names all twelve.
- **THE INSTRUMENT PROOF IS A MUTATION, NOT A VERSION CHECK** (B5, and it supersedes the recipe it
  was added to). Fed a deliberate `readability-identifier-naming` violation:

  ```
  src/core/net/DeadlineTimer.cpp:34:10: error: invalid case style for variable 'Bad_Name'
      [readability-identifier-naming,-warnings-as-errors]
  ```

  Reported **and fatal**. That settles the `fastcache-cc`-cache-hit question by measurement rather
  than by reasoning about `cmake -E __run_co_compile`. **A version check proves *a* tool exists; a
  fed violation proves the right tool ran on your code and that its findings are fatal.** Levels 1
  and 2 stay useful for diagnosing *why* a mutation did not report; the mutation is the proof.
- **AND `lines_in_log` ALREADY COVERED THE CASE IT WAS NOT WRITTEN FOR.** I raised that an
  up-to-date object makes Ninja skip the whole build statement, `CODE_CHECK` included, so
  `warnings=0` would mean *"nothing recompiled"*. B5's own record answers it — *"clang-tidy clean at
  a **finished** 526 steps"*. **A run that analysed nothing does not have 526 steps.** The
  "log with content" level detects the skip case as well as the absent-tool case, and neither B4 nor
  I had noticed it did. **Third time tonight a printed count answered a question its rule was not
  written for**, which is the argument for printing counts rather than verdicts.
- **MEASURED TWO WAYS: an up-to-date object skips its build statement, `CODE_CHECK` included — and
  `.clang-tidy` is not an input of any object, so changing the RULES re-analyses nothing.**
  B5 touched things in a built tree; I read the generated statement in a different tree:

  ```
  nothing touched          -> ninja: no work to do.
  touch .clang-tidy        -> ninja: no work to do.          <- the one neither of us expected
  touch its own source     -> [1/1] Building CXX object ...

  build .../Environment.cpp.o: CXX_COMPILER__... /mnt/d/core-cpp/src/core/Environment.cpp || ...
    CODE_CHECK = cmake -E __run_co_compile --launcher="..." --tidy="...clang-tidy;..." --source=...
    DEP_FILE   = .../Environment.cpp.o.d          <- the compiler's depfile: headers only
  ```

  `.clang-tidy` appears nowhere in 1226 `clang-tidy` occurrences outside the `CODE_CHECK` lines.
  **A rule added today applies to files edited after today and to nothing else, silently.** Filed as
  [core-cpp#36](https://github.com/contour-terminal/core-cpp/issues/36). **Deleting the tree is a
  workaround, not a consequence of understanding it** — the fix is to hash `.clang-tidy`'s contents
  AND the analyser's `--version` into the tidy command line, so Ninja's command-line comparison
  retriggers on either. `OBJECT_DEPENDS` covers the config but not a binary swapped at the same path.
- **CORRECTION OF MINE: a stale tidy green is an INHERITED verdict, not a fabricated one.** I said
  every clang-tidy green on a non-deleted tree was *void*. B5 showed that is wrong, and the reason is
  structural: **findings are fatal (`-warnings-as-errors`), so an object can only exist if its
  statement passed tidy.** `ninja: no work to do.` therefore means every object was analysed clean
  **under the rules and command line in force when it was last built**. What it is not is a verdict
  about today's `.clang-tidy`. **So B13's audit question is not "was this tree ever analysed" but
  "was it analysed under the current rules"** — a different and far cheaper question. I generalised
  from *the statement was skipped* straight to *the result means nothing*, skipping the fact that the
  object's existence is itself the evidence. **Fourth time today a lane caught me overshooting from a
  correct observation.**
- **And narrower still: CI is unaffected.** Its runners build from clean trees, so the `clang-tidy`
  job always performs a full analysis. **The defect is local-only — which is exactly where every lane
  reads its result from.** Consumers are unaffected too: `CORE_CPP_CLANG_TIDY` defaults OFF and
  explicitly clears an inherited `CXX_CLANG_TIDY`.
- **`touch` CREATES. A probe that uses it to invalidate a timestamp silently creates a file when the
  path is wrong** — and in this tree that is a hygiene failure, not a no-op. B5's
  `touch ~/wt-b5/src/core/Base64.cpp` on a header-only component produced a zero-byte `.cpp` under
  `src/core/`, and `core-cpp.cmake-hygiene`'s provenance row caught it inside one ASan run:
  `src/core/Base64.cpp:-: [provenance] ... no row in .agent/reference/provenance.md`. **Use
  `touch -c`, which refuses to create.** Also the best advertisement the provenance check has: it
  caught a file that was never supposed to exist, in a tree nobody was auditing.

## THE SESSION'S CENTRAL FINDING: the rule was already written, ten lines above

B4, on where its new clang-tidy bullet lands:

> *"It lands in the section already titled **'A gate that does not report reads as a gate that
> passed'**, immediately after its last bullet — which already says **'A diagnostic that never ran
> and one that ran and found nothing are the same green.'** The abstract rule was already written,
> ten lines above, and I committed all three variants of it anyway."*

**The rule was there. Correct, precise, in the file being edited. It prevented nothing** — not the
gate B4 never started, not the absent analyser, not B5's unfinished log, not B4's own silent counter
inside the instrument it built to catch the other three.

**A rulebook entry that states a principle without a procedure is a sentence people agree with and
do not execute.** The reader nods, carries the principle into the next paragraph, and supplies
compliance from memory — which is B4's own adjacency mechanism applied to the rulebook itself.
**What was missing was never the rule; it was the procedure.**

Routed to B13: audit `.agent/rules/` for entries that state a principle with no executable step.
*"A gate that does not report reads as passed"* is the worked example — ten lines of correct
reasoning, no instruction, for months, across every lane.

- **THE RECURSION: B4 put a silent counter inside the instrument built to detect silent gates.** Its
  probe reported `0 findings` **twice while the analyser was working perfectly**: the regex matched
  `warning: ...[bugprone-...]`, and under `-warnings-as-errors` findings emit as `error:` with names
  like `misc-` and `clang-diagnostic-`. The fix was B5's dispatch line — *prefer the form that
  returns the evidence over the form that returns a count* — which B4 had read and not applied.
- **THE SYNTHESIS, because the two conclusions look contradictory and are not: COUNT THE WORK, PRINT
  THE FINDINGS.**
  - `lines_in_log=535` — a count of **work done**. Evidence the instrument ran; a bad pattern cannot
    silently zero it.
  - The finding **lines**, printed, never counted. A finding count passes through a regex, and the
    regex is exactly where `warning:` versus `error:` zeroes it in silence.

  Either alone is the hole the other covers.
- **A `requires` clause can fail as a bare deduction error that never names the constraint.** B4's
  `ScopeGuard` shape did not compile — it is constrained `requires std::is_nothrow_invocable_v<Callable&>`
  and an unmarked lambda fails it without the diagnostic ever mentioning the requirement. Caught
  before B5 copied it, which is the payoff for **sending the shape rather than describing it**.
  `unregisterPark` is itself `noexcept`, so `() noexcept` on the lambda is honest rather than a
  workaround — the distinction between a fix that is correct and one that merely compiles.

## THE INSTRUMENT SHAPED BY THE EXPECTATION — four variants in two hours, and B5's is the best statement

> *"`git status --porcelain | grep -v '^.. \.superpowers/'` — that filter existed to find **my**
> files, and it hid your eleven. One survivor came back, I called it 'the blocker', and the blocker
> was the set. **I filtered the evidence to the shape I expected and then reported what survived the
> filter as the finding.**"* — B5

Four variants, four different instruments, two lanes, two hours:

| Instrument | How the expectation shaped it |
|---|---|
| B5's `git status` filter | built to find its own files; hid the eleven that were the answer |
| B5's tidy log read | read at 179 of 526 steps; `0 findings` was the shape expected |
| B4's finding regex | matched `warning:`; under `-warnings-as-errors` findings emit as `error:` |
| B4's cache experiment | deleted the object first in both arms, so it could only ever test cache-replay |

**A filter encodes an assumption about the environment and fails silently when the environment
changes.** B5's corollary is the actionable half: *"my `git status` filter is only safe because the
tree is supposed to be clean. In a tree where eleven files are legitimately dirty, 'filter out the
ones I expect and look at what is left' is not a diagnosis, it is a coin flip that came up tails."*

- **CORRECTION, and the fifth of mine today: a step count is NECESSARY, NOT SUFFICIENT.** I
  concluded that B5's *"finished 526 steps"* closed the skip case. **It does not.** A step count
  proves statements *ran*; it does not prove any of them was a **tidy-enabled compile of your
  files** — Catch2, libunicode and link steps are in that 526, and `CORE_CPP_CLANG_TIDY` applies to
  core-cpp's targets only. *"526 statements ran and none carried `--tidy`"* is indistinguishable by
  step count alone. **B4's three levels are a CONJUNCTION, not a hierarchy:** the count catches the
  skip case; `--tidy=` on a specific object's build statement catches the wrong-surface case; the
  mutation catches both and is the only single step that does.
- **THE RULE CAUGHT ITS OWN AUTHOR WITHIN THE HOUR.** B5's post-rebase tidy reported
  **`clang-tidy: steps=25`**, not 526 — the tree was already built, so only the rebase delta was
  re-analysed and the rest inherited. B5 pulled its own result, deleted the tidy tree and rebuilt,
  **at the cost of a push window it was ready to take.** That is the strongest evidence any of these
  rules is real: it bound the person who wrote it, against their own interest, unprompted.
- **CORRECTION: TWELVE of twelve blockers were the lead's, not eleven.** Measured in both trees:

  ```
  D:\core-cpp           .agent/rules/build-and-toolchain.md   +13/-1   B4's markers: 0
  D:\core-cpp-wt-b4fix  same file                             +79/-1   B4's markers: 1
  ```

  **B4 never wrote to the shared checkout at all** — it took the lead's held hunk out by patch
  (`git diff` to a file, `git apply --check`, `git apply` in its own worktree) precisely to avoid
  it. **B4 was already practising the discipline the lead articulated an hour later as though it
  were new.** *The shared checkout is a commit staging area, not a workspace* now has exactly one
  counterexample in this project and it is the lead's.
- **`spawn` BREAKS THE OBVIOUS FORM OF THE FAMILY TEST** (B4, and the next writer would not derive
  it): `spawn` wakes the backend but is **loop-thread-only** and now asserts
  `teardownIsSerialisedWithDispatch()`. *"Call each of the six from off the loop thread"* trips that
  assert for `spawn` specifically — the case then fails for the wrong reason and gets weakened until
  it proves nothing. **It wants `spawn` called off-turn-but-unopposed.** The invariant the six share
  is not *"safe from any thread"*; it is *"the loop is asked for a turn"*, and `addTimer` was the one
  that did not ask.
- **THE MUTATION PROVES THE INSTRUMENT; THE WORK COUNT PROVES THE SURFACE. Neither subsumes the
  other.** B4 promoted the fed violation to a fourth level "ahead of the other three", subsuming 1
  and 2 — correct for 1 and 2, wrong for the count. **A fed violation reports from the file you
  mutated and says nothing about the other 500.** An incremental run can analyse exactly that one
  file, inherit every other verdict, and the mutation reports cleanly either way — which is exactly
  B5's `steps=25` with an analyser that was working perfectly. The final form:

  > **Feed a violation** -> the right tool ran on your code and findings are fatal.
  > **Count the work** -> the surface was analysed, not inherited.
  > Levels 1 (which binary) and 2 (`--tidy=` on the object) remain for diagnosing *why* a mutation
  > did not report.
- **THE RULE BIT ITS AUTHORS TWICE, INSIDE AN HOUR, ON THEIR OWN ARTIFACTS.** B5's post-rebase tidy
  reported `steps=25` from an already-built tree — a verdict inherited, not earned — and B5 pulled
  its own result at the cost of a push window. **B4 then found the same defect in its own gate**:
  its tidy leg did not delete its tree, *"which is precisely the defect I had just written down"*,
  **and** it had edited `ScopeGuard.hpp` after the build started, so `log_lines=45` predated part of
  the change it was certifying. Both self-caught, both unprompted, both against their own interest.
  **A rule that catches its authors on their own work within the hour is a rule; one that only
  catches other people is a preference.**
- **A DISTINCTION THAT GENERATES THE LIST BEATS THE LIST** (B4, on rewriting its own bullet): it had
  written *"the mutation does not replace level 2"*, which is true and *"describes the wrong axis —
  per-file wiring rather than inherited-versus-earned."* Recast as **instrument versus surface**, the
  conjunction becomes obvious instead of four things to remember:
  - the **mutation** proves the **instrument**: the right analyser, on your code, findings fatal;
  - the **work count** proves the **surface**: statements ran rather than inheriting a verdict.

  **When a rule reads as a list to memorise, the axis is probably wrong.** Find the distinction that
  makes the items follow, and the list stops needing to be remembered.
- **THE FAMILY INVARIANT, FINAL FORM:** the six loop entry points do not share *"safe from any
  thread"* — they share ***"the loop is asked for a turn"***, which is the property `addTimer`
  actually violated. B4 accepted this over its own phrasing. It matters for B13's test: `spawn` is
  loop-thread-only and asserts, so the case wants it called **off-turn-but-unopposed**, not from a
  competing thread.
- **THE BUILD EXIT CODE IS THE VERDICT; THE LOG IS FOR DIAGNOSIS** (B5, and it removes the regex
  from the evidence chain entirely — the one place B4's instrument actually failed). Under
  `-warnings-as-errors` a finding fails the build, so **an object cannot exist without having passed
  its analysis.** `exit 0` over 509 executed statements is therefore not a number anyone had to
  trust. Final recipe, three facts, none passing through a pattern anybody wrote:
  - **step count** -> the statements ran rather than inheriting a verdict;
  - **build exit code** -> none of them failed, because findings are fatal;
  - **mutation** -> the right analyser, on your code, with fatal findings.

  `exit 0` with zero statements run is also `exit 0`, so the count and the exit code are a pair.
  Grepping the log is how you find out **why**, not whether. *Count the work, print the findings*
  stays true and is now the second line rather than the first.
- **"BY LUCK OF PATTERN, NOT BY DESIGN"** — B5, on why its own probe escaped the failure that caught
  B4's: *"Mine happened to include `error:` and so was not fooled, and I would not have caught it if
  it had been."* **Two instruments differing by luck rather than rigour is not a practice, it is a
  near-miss** — and a rule that says *print the findings* leaves the next person writing a pattern
  and being lucky or not. Naming the luck is what stops the lucky regex being copied as though it
  were the safeguard.
- **A RIGHT OUTCOME REMEMBERED AS RIGHT REASONING IS HOW THE REASONING SURVIVES TO FAIL ELSEWHERE.**
  B5, declining credit it was offered: *"the practice was right and the diagnosis was wrong, and
  those are separable — I would rather have both recorded than have the right outcome remembered as
  the right reasoning."* **B4 reached the same rule independently, on a different subject, in the
  same hour**: refusing to let a working `export PATH` fix stand as a confirmed diagnosis of a cause
  it had got wrong. Two lanes, two subjects, one rule.
- **A REFUSAL CAN NAME A CONFIDENT, SPECIFIC, WRONG CAUSE** (B5, and this is the sharpest instance of
  the class yet). Its tooling refused to delete `Z:\core-cpp-b5-rescue\` with
  **"This path is protected from removal"** — a message that is specific, authoritative, and names a
  protection rule. The truth: **the volume does not exist.** `Get-PSDrive` lists no `Z:` and
  `Test-Path Z:\` is false; the user dropped the drive earlier in the session. B5 carried it as an
  open cleanup item for the rest of the task on the strength of that sentence. **A refusal is not
  evidence that the thing refused exists**, and a refusal that explains itself is more misleading
  than one that does not, because the explanation is what you stop checking.
- **THE THIRD SELF-CATCH, and it was disclosed unprompted.** B4's `fgates` tidy leg reported
  `build_rc=0, log_lines=47` **on an already-built tree** — its own `steps=25`, found the same way,
  **after it had written the rule forbidding it.** It re-ran from a deleted tree and put only that
  run in the gate record: *"the 47 does not count and I am not counting it."* Three self-catches in
  one hour (B5's 25, B4's unstarted gate, B4's 47), all against the author's own interest, all
  unprompted.
- **A COUNTER IS THE MOST SEDUCTIVE FORM OF THE SHAPED INSTRUMENT** (B4, accepting B5's general form
  over its own instance-level one): *"a number looks like evidence in a way a missing line does
  not."* That is why the exit code leads the recipe — it is the only attestation passing through no
  pattern, and B4's own summary of its error is the argument: **"I had built the entire procedure
  out of greps and counts — the two things that had already failed."**
- **THE "PRINCIPLE WITHOUT A PROCEDURE" CLASS IS NOT CONFINED TO THE RULEBOOK — the SPEC has one
  too.** The design spec requires, of TLS, **"No OpenSSL type appears in any header."** Nothing
  enforces it: `tests/cmake/` holds nine checks and none is about it, and
  `grep -rln openssl tests/ cmake/ scripts/` returns nothing. **fastcached has exactly this gate**,
  with a self-test, at the pin — `scripts/check-crypto-seam{,-selftest}.cmake` — and no task in the
  plan imports it. Written into B11's brief with the instruction to land the gate in the same task
  that creates the surface it guards: **a seam that ships ungated is one refactor from being a seam
  in prose only**, and retro-fitting a check onto an existing violation is a different and worse job.
- **A COMMITTED CERTIFICATE EXPIRES, AND IT EXPIRES INTO A TEST FAILURE THAT LOOKS LIKE A TLS
  DEFECT** years after anyone remembers the fixture exists. fastcached ships
  `testdata/tls/{server.crt,server.key}`; **core-cpp has no `.crt`, `.key` or `.pem` anywhere** and
  contour's `Tls_test.cpp` calls `generateSelfSignedCertificate("contour-dev")` at runtime instead.
  **The merge goes towards what core-cpp already has**, which is the rarer direction and worth
  saying out loud: the plan's *"sharing one cert fixture"* means one helper, not one file.
- **SEVENTH SOURCE-LIST CORRECTION, same two commands.** `git ls-tree -r --name-only <pin> | grep -iE
  "<topic>"` over the whole tree, before dispatch, found for B11: the crypto-seam gate above,
  `Core/Errors/CryptoError.hpp` (whose relationship to B2's single `NetError` vocabulary is
  undecided and must be decided explicitly), `Net/TlsWrap.hpp`, and `scripts/tls-smoke.{ps1,sh}`.
  **The rate at which this check pays is now the argument for making it mandatory before every
  dispatch**, not a habit of the lead's.
- **ENUMERATE, DO NOT FILTER — the fifth instance, and the general form of all of them.** B5's
  `gh run view --jq 'select(.conclusion != null)'` reported **5 failing jobs**; in-progress jobs
  carry `conclusion: ""`, **not `null`**, so the filter answered a neighbouring question. Verified:

  ```
  {"conclusion":"","name":"clang-tidy","status":"in_progress"}    <- "" not null
  ```

  **The lead's own CI check had the identical defect** — `select(.conclusion != "success")` counts
  every in-progress job as a failure, and it escaped all night only because it happened to be run
  against completed runs. **Luck of timing, not design**, which is the same sentence B5 wrote about
  its `error:` regex an hour earlier. Two of us, two instruments, same escape, same reason.

  B5's prescription, now the project's standard form: **"group and count every value rather than
  select the ones I think are interesting."**

  ```sh
  gh run view <id> --json jobs --jq '[.jobs[] | {s:.status, c:(.conclusion // "<null>" | if .=="" then "<empty>" else . end)}]
    | group_by(.s+"/"+.c) | map({k:(.[0].s+" / "+.[0].c), n:length}) | .[] | "\(.n)\t\(.k)"'
  20  completed / success
   4  in_progress / <empty>
  ```

  **A `select` can only show you the bucket you named; a grouping shows you the buckets you did not
  know existed.** Same principle as `git ls-tree` over a directory beating `git cat-file -e` over a
  list, and as the build's exit code beating a grep over its log. **Three different instruments, one
  rule.**

  **Fifth instance, caught in one minute. The first took an hour.** That is the only metric on this
  worth tracking.
- **"NO TEST" IS A CLAIM THAT NEEDS ITS OWN ARGUMENT, AND B5 GAVE THE RIGHT ONE.** On M8's guard:
  *"no test accompanies it because provoking the throw would test `std::function`'s allocator rather
  than this code."* **That is not a test that is missing; it is a test that would be wrong** — it
  would assert about the standard library's small-buffer optimisation, pass or fail on a libc++
  upgrade, and tell nobody anything about the guard. Paired with the commit stating it closes an
  **unreachable** defect rather than a live one, the pair is honest in both directions: no phantom
  bug fixed, no phantom coverage claimed.
- **A NON-LITERAL MATCH IS FINE WHEN YOU SAY WHICH PART IS NOT LITERAL.** B5 took B4's `ScopeGuard`
  shape into `TokenDelayAwaiter` with the flag after the **second** `emplace`, because that awaiter
  has two callbacks where `DelayAwaiter` has one — *"the shape is theirs, the placement is mine"*.
  Copying text verbatim into a different structure would have been the drift the shape-sharing
  existed to prevent; copying it silently adapted would have been worse.
- **A POLL KEYED TO AN EXPECTED VALUE FAILS AS SILENCE — the sixth costume.** B5 armed a poll on
  *"`27b8b43` is an ancestor of `origin/master`"* and flagged the hazard itself: **if B4 amends, the
  hash goes stale and the poll sits silent until its timeout.** Silence is indistinguishable from
  "not yet". Same shape as its `git status` filter, its `select(.conclusion != null)`, and B4's
  `warning:` regex — **the instrument written for the expected state can only report that state or
  say nothing.**

  ```sh
  # brittle: silent if the value differs
  git merge-base --is-ancestor <expected-sha> origin/master
  # robust: fires on the state CHANGE, then you read what arrived
  [ "$(git rev-parse origin/master)" != "$(git rev-parse <known-base>)" ]
  ```

  **Fire on movement, then look at what landed.** The robust form also tells you *what* arrived,
  which the value-keyed form cannot do even when it fires correctly.
- **NAME THE MATCHES THAT ARE NOT DIAGNOSTICS, OR "ZERO DIAGNOSTICS" IS A HOPE.** B5's Windows legs
  **enumerated every line of both 102-line build logs** instead of grepping for a count, and said
  which `warning|error|FAILED` matches were not findings: the `-fdiagnostics-color=always` probe
  failing under MSVC (expected), and three object-file lines whose *names* contain
  `InterruptibleSleep`. **At 102 lines, enumeration is cheaper than getting a pattern right** — and
  it is exactly the form B4's `warning:`-versus-`error:` failure was asking for.
- **A ROBUST COMPARISON FED BY A DEAD INPUT IS STILL SILENT** (B5, extending its own re-key one step
  further than the re-key covers): *"the comparison was made robust, the input feeding it was not."*
  If `git fetch` fails every time — network, auth, a server that would not connect — `origin/master`
  never changes locally and a "fire on movement" poll sits silent **exactly as if nothing had
  landed**. B5's remedy: count consecutive input failures and emit once on the third.
  ***"A poll that has gone blind now says so instead of looking patient."***

  **THE LEAD'S OWN CI MONITOR HAD THIS DEFECT, WITH TWO SILENCERS IN ONE LINE:**

  ```sh
  snap() { gh run list ... 2>/dev/null | sort || true; }
  #                        ^^^^^^^^^^^^        ^^^^^^^  error text discarded, exit code discarded
  ```

  A lost auth token, a rate limit or a dropped network produced an empty snapshot, no new lines, and
  **thirty minutes of patient silence followed by "expired with no events"** — indistinguishable
  from "nothing landed". Stopped and re-armed capturing `2>&1`, checking the exit code, emitting
  `GH-BLIND` on the third consecutive failure and `GH-RECOVERED` after.

  **This is worse than the five failures before it.** Those produced a *wrong answer*, which a second
  look can catch. **A dead input produces no answer**, and this session had ALREADY recorded that
  *"no signal at all" is a different hazard from "a wrong answer": a wrong answer invites checking;
  an absent one invites substitution* — hours before the lead wrote `2>/dev/null` into a monitor.
  **B4's finding once more: the rule was written, it was correct, and it prevented nothing, because
  it had no procedure attached.** The procedure: **every polling loop reports its own input
  failing.**
- **EIGHTH SOURCE-LIST CORRECTION, and this one is a WHOLE-LIST TRANSFORMATION rather than a typo.**
  B12's deletion list names five files; **two are not where it says**:

  ```
  plan: src/core/tui/platform/PollHelpers.hpp        real: src/core/tui/runtime/posix/PollHelpers.hpp
  plan: src/core/tui/testing/MockEventSource.hpp     real: src/core/tui/runtime/testing/MockEventSource.hpp
  ```

  **The mechanism is the finding.** endo had them at `src/tui/runtime/platform/PollHelpers.hpp` and
  `src/tui/runtime/testing/MockEventSource.hpp`; **Task A7's import renamed `platform/` to `posix/`
  under the private-directory convention**, and the plan's list was written against *endo's* layout
  with `src/core/tui/` pasted on the front. **Right for three rows, wrong for the two that had
  moved** — so checking one row and finding it correct proves nothing about the list, which is
  exactly why the check must enumerate the tree rather than spot-check the list.

  Also unnamed: `PollEventSource.cpp` (the plan names only the `.hpp`), and `TerminalEventSource` is
  **three** files — `runtime/TerminalEventSource.hpp`, `runtime/posix/TerminalEventSource.cpp`,
  `runtime/windows/TerminalEventSource.cpp` — where the plan's single sentence implies one. The
  Windows one is the console-handle parking that is the whole reason B7 must bridge waitable HANDLEs.
- **A POLLING LOOP HAS AS MANY INPUTS AS IT HAS COMMANDS, AND EVERY ONE NEEDS ITS OWN REPORT** (B5,
  after the broad rule failed to catch two holes in the loop it had just written). *"I had checked
  the input I was thinking about -- the fetch -- and not the one I was reading the answer from."*
  Two instances, both found within minutes of writing the rule they violate:

  **B5's, and the one worth quoting for years:**

  ```sh
  NOW=$(git rev-parse origin/master 2>/dev/null || echo unknown)
  if [ "$NOW" != "$BASE" ] && [ "$NOW" != "unknown" ]; then
  ```

  ***"I coded the substitution in by hand, `|| echo unknown`, and then excluded it from the
  comparison."*** That is *"an absent answer invites substitution"* spelled out in the source and
  then branched around. A fetch can succeed while `rev-parse` cannot read the ref, and in this
  project that is **not hypothetical** -- WSL git could not open a Windows worktree's gitdir hours
  earlier.

  **The lead's, in the monitor it had just "fixed":**

  ```sh
  snap() { gh run list ... 2>&1 | sort; }
  prev=$(snap); rc=$?        # <- SORT's exit status, not gh's
  ```

  Measured: `f() { false | sort; }; f; echo $?` -> **0**; `PIPESTATUS` -> `1 0`. **A pipeline
  returns its last command's status**, so the exit check was inert and only the grep over captured
  stderr was working -- which happened to cover it. **Three of the four instruments between the two
  of us survived by luck, not by design.**
- **THE SAME NUMBER MEANS OPPOSITE THINGS FOR A COMPILE GATE AND AN ANALYSER GATE** (B4, and this is
  the rule that explains the whole tree-deletion argument). Its `clang-debug` leg reported
  `work_lines=1` -- *ninja: no work to do*. **For a compile-and-test gate that is legitimate**:
  ninja's currency check verified every object is newer than its inputs, and ctest then ran those
  binaries to 31/31. **For the tidy gate the identical number would be worthless**, because ninja
  skipping a statement also skips the `CODE_CHECK` inside it. ***The difference is whether the thing
  you are measuring sits INSIDE the statement ninja skipped.*** That is why the tidy leg deletes its
  tree and the compile leg need not -- a distinction that generates the rule instead of listing it.
- **THE clang-cl STALE-CACHE TRAP IS NOW MEASURED, AND THE VERDICT WAS IDENTICAL EITHER WAY.** B5,
  re-gating M8 at the moved base:

  > **`clangcl-release` rebuilt 7 steps after a change to `EventLoop.hpp` and reported 33/33.**
  > Re-ran `--clean-first`: **517 steps, still 33/33.**

  **A 7-step build cannot have tested a header change**, so the pass was inherited — and only the
  **step count** distinguished it from an earned one. The most careful lane of the night nearly
  shipped on the 7. `AGENT.md` documents this
  ([fastcached#1531](https://github.com/LASTRADA-Software/fastcached/issues/1531)); documentation did
  not prevent it, a number did. **Report the step count beside every `clangcl-*` pass.**
- **A DEAD INPUT THAT FAILS LOUDLY IS THE LUCKY CASE, AND THE LUCK IS THE HARNESS'S** (B5). Its
  emscripten leg reported *"12 tests failed of 26"* — **every one `Not Run`**, because `node` was off
  `PATH` with **`EMSDK` unset in a non-interactive WSL shell**. Sourcing `emsdk_env.sh`: 26/26.
  *"A dead input that produced a loud wrong answer rather than silence, and loud wrong answers are
  the ones we catch. **Had it been a leg whose failure mode is 'Not Run counts as skipped', I would
  have shipped on it.**"* **core-cpp has such a convention — exit 77 means all skipped** — so this is
  a live hazard here, not a hypothetical. For B13.
- **B5'S THIRD SELF-CAUGHT SILENT STEP, in its own runner:** a step grepped for a pattern the
  non-verbose output never prints, emitted a blank line, and the script still said `ALL DONE`.
  *A gate that does not report reads as passed* — the rule whose procedure B4 wrote two hours
  earlier, failing again in a fourth instrument.
- **A COMMIT'S TYPE PREFIX IS READ BY `git bisect`, NOT ONLY BY HUMANS** (B5's observation about
  B4's commit, reported and correctly not fixed). `27b8b43`'s subject is `docs(rules): a rule
  without a procedure is agreed with, not executed` — and it changes **four source files**:

  ```
  src/core/net/EventLoop.hpp           +18      (the DelayAwaiter guard)
  src/core/net/detail/ScopeGuard.hpp    +6      (the noexcept constraint note)
  src/core/net/EventLoop_test.cpp       +8
  src/core/net/HostDrivenLoop_test.cpp  +4
  ```

  **Someone bisecting a `core::net` defect reads `docs(rules):` and skips it.** Pushed and CI-green,
  so nothing to do but record it. B5 verified the claim its own commit message made about sitting on
  B4's fix, rather than taking the subject line at face value — which is how it found this.
- **A GREEN AT THE OLD BASE IS NOT EVIDENCE ABOUT THE NEW ONE, and B5 proved the instance rather
  than asserting the rule:** `27b8b43` modified `detail/ScopeGuard.hpp`, **the header B5's change
  includes**, so its `a02031c` green said nothing about this base. It re-gated from scratch and
  verified the fast-forward parent was exactly `27b8b43` before pushing. It also checked
  clang-tidy's coverage by **comparing object mtimes to source mtimes** — `InterruptibleSleep.cpp.o`
  newer than its source, `EventLoop.cpp.o` newer than `EventLoop.hpp` — a fifth form of evidence,
  and the only one that shows the *moved header's dependents* were re-analysed.
- **`pathlib.Path.write_text` ON WINDOWS TRANSLATES `\n` TO `\r\n`.** The lead used it for several
  in-place ledger edits and committed `progress.md` with **895 CR bytes** into a repository whose
  `.gitattributes` says `* text=auto eol=lf` and whose vendoring tool **refuses CR bytes outright**.
  Measured rather than assumed: the blob had **0** CRs at `fe48143`, `a02031c`, `27b8b43` and
  `8d7b8b2`, and 895 at the new commit — so it was introduced, not inherited. **Use `write_bytes`,
  or pass `newline=""`.** Same class as everything else tonight: the instrument silently changed the
  thing it touched, and nothing in the commit output said so. Caught by scanning every committed
  blob for `\r` before pushing, which is now the step.
- **THE ORPHANED-HEAD HAZARD RECURRED WITHIN THE HOUR, BECAUSE A ONE-TIME REPAIR OF A CONTINUOUSLY
  DRIFTING POINTER IS NOT A FIX.** The lead re-pointed the shared checkout at `a02031c`, wrote the
  rule, and then two more lanes pushed — leaving it two commits behind again with stale working-tree
  content for every file `27b8b43` and `8d7b8b2` touched. **Anyone committing from the shared
  checkout in that window would have landed on `a02031c`.** The rule was right and prevented nothing,
  for the third time tonight, and for the same reason: **no procedure was attached.** The procedure:
  `git fetch && git reset --mixed origin/master` and discard stale non-ledger files **every time the
  shared checkout is touched**, not once.
- **`git commit --only -- <paths> -m "msg"` SILENTLY TREATS THE MESSAGE AS A PATHSPEC.** Everything
  after `--` is a pathspec, so `-m` and its argument became file names and the commit failed with
  *"did not match any file(s) known to git"* — naming the message text as the missing path. **Nothing
  was committed, which is the good outcome**, but the failure mode reads as a pathspec typo rather
  than an argument-order error. `git commit --only -F <file> -- <paths>`: options first, `--` last.
- **A COMMIT'S RISKIEST CONTENT DECIDES ITS TYPE, NOT ITS LARGEST** (B4, after B5 caught it).
  `27b8b43`'s subject is `docs(rules): ...` and it changes **four C++ files** — `EventLoop.hpp`
  (+18, M8's scope guard, a behaviour change to a public header), `ScopeGuard.hpp` (+6),
  `EventLoop_test.cpp` (+8), `HostDrivenLoop_test.cpp` (+4). The body describes all of it
  accurately; **the subject is what a `git bisect` log shows**, and someone bisecting a `core::net`
  defect reads `docs(rules)` and skips it. The right subject was `fix(net):` with the rulebook work
  in the body. Pushed and built upon, so recorded rather than rewritten.

  **B4's own connection is the generalisation:** *"twice today I let a commit's framing follow what
  I was thinking about rather than what it contained — the first being `a9ea52b`, which swept up
  B5's in-flight work because I thought of `EventLoop.cpp` as mine."* **Both are the same error at
  different scales: the artefact described by the author's attention rather than by its contents.**
  One cost four minutes of red master; the other costs a future bisect.
- **A GATE RECORD THAT CANNOT ANSWER THE DECISIVE QUESTION IS ONE YOU WILL DEFEND ON THE WRONG
  EVIDENCE** (B4, checking its own record after B5's finding). Its `clangcl-release` on `fb3fe97`
  had in fact run **524 of 524 steps** — a fresh tree, fully built, so the leg was earned — **but
  `gates-win.txt` recorded exit codes, an error count and a binary-exists check, and no step
  count**, so B4 had to go back to the raw log to learn it. On the one platform where an inherited
  pass is indistinguishable from an earned one, the record omitted the only field that tells them
  apart.
- **SEPARATE "THE RECORD WAS INCOMPLETE" FROM "THE RISK WAS REAL".** B4 disclosed that `27b8b43`
  was locally gated on clang-debug and clang-tidy only while modifying two headers, and began
  re-running the Windows legs. **CI had already run the full 25-job matrix on that exact commit and
  every job succeeded** (run `35572252833`, `ci-ok: success`), on fresh runners with no cache to
  inherit. So the lesson about gate records stands and the re-run does not. **Measure the exposure
  before paying for it** — an honest disclosure is not automatically an open risk.
- **ONE CHECK COUNTED THREE TIMES: when the review, the tests and the gates all derive from the same
  example, they are not independent.** B4's C1 fix passed a review reproducer, two cases written
  from it, and ten gates — **all of which used `AsyncQueue` wired to an inert executor**, a genuinely
  unadvanceable flow. The legitimate cross-thread shape (`ResumeOn { pool }` and back) was never
  exercised by any of them, and it segfaults. **The shaping happened at the SPECIFICATION, not inside
  any tool**, which is why no amount of instrument hygiene could have caught it. **Ask of every case:
  is it derived from the review's example, or from the claim?**
- **THE NUMBER OF TIMES A DOCUMENT HAS BEEN CORRECTED IS NOT EVIDENCE ABOUT WHICH BEHAVIOUR IS
  RIGHT.** B4 flagged that choosing the better fix would force a **third** rewrite of `blockOn`'s
  `@throws` clause and its CHANGELOG Breaking entry, and said so rather than letting it weigh
  silently. Correct to name it; wrong to let it count. Also: **a Breaking entry describes the net
  change from the last RELEASED state, not the path taken** — nothing has shipped, so 0.1.0's notes
  need one sentence about `blockOn`, not four.
- **NAMING THE UNEXAMINED QUESTION IS WORTH MORE THAN THE ANSWER SOMETIMES.** The re-review of B4's
  fix round paid for itself **before it reported**: the dispatch said *"nobody but you has looked at
  whether the throw can fire where the old code was correct"*, and the author went looking and found
  a regression in shipped code within the hour. The review's own verdict had not yet arrived.
- **`core::async` ALREADY CONTAINED THE ANSWER TO B4'S DEFECT, one module over, in prose.**
  `src/core/async/SyncRun.hpp`, explaining why `syncRunWith` exists:

  > *"**The plain overload's refusal is only half of what such a task needs.** It throws, and the
  > throw is legible; but **`~Task` then frees a frame the parked read still points into**, the
  > resource's next completion writes into freed memory, and the case ends in a crash that names no
  > assertion at all -- **a red turned into a SIGSEGV**."*

  **That is `blockOn`'s defect exactly**, written down before anyone needed it. And the remedy is
  there too: `std::ignore = task.release();` then throw, with the reason stated —
  ***"a leak report names the coroutine that parked, and a use-after-free names nothing."***
  **So the disposal for any early exit holding an unfinished task is `release()`, never destruction.**
  A leak is a diagnosable defect with a name; a use-after-free is a crash that names nothing.

  **Note what this is an instance of:** the knowledge was in the repository, correct and well
  argued, in the module the defective code depends on — and it did not travel. Same shape as the
  rule ten lines above the violation, and as `AGENT.md` documenting the clang-cl trap that then
  caught the most careful lane of the night.
- **FIRST WRONG CITATION IN FIFTEEN: `fastcached#178` does not say what three core-cpp sites claim.**
  Cited in `src/core/async/SyncRun.hpp:71` (**a public header**), `.agent/rules/async-and-net.md:291`
  and `task-B1-report.md` as the origin of the leak-don't-free rule. It is actually *"A machine that
  joins the fleet only while a VPN is up cannot be configured..."* — unrelated, and not a PR either.
  Filed as [core-cpp#37](https://github.com/contour-terminal/core-cpp/issues/37); the correct number
  could not be found in four searches.

  **A citation pointing at the wrong issue is worse than none**: the reader follows it, finds a VPN
  bug, and concludes the rule is unfounded or that they misread the code — and one of those gets the
  rule deleted by someone tidying up. **Fourteen of fifteen checked numbers were right, which is
  exactly the rate at which people stop checking.**
- **MEASURE THE ARGUMENT, NOT THE BEHAVIOUR, AND READ IT AFTER THE DRIVE RETURNS.** B4 deleted a
  case asserting that `blockOn` asks for an indefinite wait rather than polling — the property
  core-cpp#17 was actually filed about — after three measured failures: `runOnce()` records no
  timeout because it never asks for one; reading `waitCount()` **from a producer thread** while the
  loop appends is a data race; and the state is unreachable single-threaded. **The second failure is
  fixable and changes what is measured:** `blockOn` runs on its caller, so the only appender is the
  test thread, and the pool thread only ever calls `submit()`/`wake()`. **Read the recorded timeouts
  after `blockOn` returns and there is no race to miss** — and assert on the *argument* passed to
  `wait()` (`std::nullopt` versus a zero poll), which is a fact that needs no blocking backend at
  all. Offered to B4 as a claim to check, not an instruction: it has measured three failures the
  lead has not.
- **"I WILL NOT ADD A CASE THAT ONLY PASSES BECAUSE THE SANITISER IS NOT LOOKING"** (B4, refusing its
  own attempt 2). **An honestly stated gap is a better artifact than a case that passes for the
  wrong reason** — and B4's report distinguishes what is *tested* (the outcome: a cross-thread flow
  completes) from what is only *reasoned* (the mechanism: `blockOn` routes through the same
  `idleWait` flag `run()` uses, which another case exercises). **Naming which half is reasoning is
  the whole value of the report.**
- **A FAILED BUILD FOLLOWED BY READING THE TEST OUTPUT IS THE STALE-ARTIFACT TRAP, AND IT IS WORSE
  THAN AN INHERITED GREEN.** B4's build failed on an unused function; it read the test output
  anyway; **the old binary was still segfaulting and it briefly believed its fix had not worked.**
  Unlike `ninja: no work to do`, the artifact here **actively contradicted the truth** rather than
  withholding it. Fix, and it is the same fix as every other one tonight: **the step reports its own
  failure instead of letting the next step answer for it** — the gate script now refuses to run
  tests when the build fails, and says so.
- **A SYMBOL ADDED AND REMOVED INSIDE THE UNRELEASED WINDOW EARNS NO CHANGELOG ENTRY — not under
  Breaking, not under Removed.** `EventLoop::hasPendingWork()` is the worked example, all four facts
  measured rather than recalled:

  ```
  contour 0  endo 0  tuidu 0  Lightweight 0  morph 0     <- zero consumer uses
  grep hasPendingWork <design spec>          -> nothing   <- never a spec member
  git log -S hasPendingWork --reverse        -> cc1b237   <- introduced this session
  grep hasPendingWork CHANGELOG.md           -> nothing   <- never recorded
  ```

  **From the last released state it never existed**, so recording its removal invents a history: a
  0.1.0 reader who sees *"Removed: `EventLoop::hasPendingWork()`"* goes looking for when they could
  have used it, and the answer is never. **The test is those four checks, not memory of what felt
  public** — the instinct is to record a removal because API surface leaving feels notable.
- **SEPARATE THE RULE FROM ITS JUSTIFICATION, OR THE JUSTIFICATION WILL BE MISTAKEN FOR THE RULE.**
  B4's first statement of teardown's inbound-queue drop was *"the discriminator is whether a turn
  ACCEPTED it"* — which the re-review refuted, because the borrowed-handle hazard it cites is equally
  true of the **ready queue, which teardown DOES resume**. The corrected form:

  > *"the discriminator is **the container** -- the ready queue holds work the loop put there, the
  > inbound queue holds an offer no turn took up; the borrowed-handle hazard argues only that the
  > boundary must be **FIXED** rather than judged per item, since the loop cannot inspect a handle to
  > decide."*

  **"A turn accepted it" was doing duty as both the discriminator and the reason, and could not be
  the first.** Now the container is the rule and the impossibility is the argument for why the rule
  must be positional. **A reader can disagree with the boundary without disputing the hazard** —
  which is the test of whether the two have actually been separated.
- **THE DEFECT CLASS APPEARED INSIDE PROSE WRITTEN ABOUT THE DEFECT CLASS.** B4, on its own M8
  comment: *"the version that named two read as an audit that had been performed, which is this
  session's defect class appearing inside my own prose about that defect class."* **Two enumerating
  comments in one file, written the same hour by two lanes, each missing a member** — B4's "the two
  cannot drift" about three sites, and B5's `EventLoop.cpp:678-680` naming "every other member that
  wakes" while excluding three. **That the class recurs inside prose about the class is the strongest
  evidence it is structural rather than carelessness.**
- **A FIX THAT PASSES REVIEW AND CHANGES NOTHING IS WORSE THAN NO FIX, BECAUSE IT CLOSES THE
  FINDING.** B5, on the obvious one-line repair for `notifyHandleClosing`:

  > *"Adding `if (!isOnWorkerThread()) armHostWake();` **does nothing at all**"* -- the park carries
  > no deadline, so `_parks.nextDeadline()` is `nullopt`, and **`armWakeAt(nullopt)` cancels rather
  > than schedules.** *"The obvious fix would have passed review, shipped, and changed nothing."*

  Found by **reading `armHostWake` rather than assuming the call did what its name suggests.** The
  root cause is the night's shape once more: **two places enumerate "what counts as work" and
  disagree** -- `hasPendingWork()` counts `_closedParks`, `armHostWake()` does not.
- **RULING: `_backend.wake()`, not `armHostWake()`, at every off-turn wake site.** B5 argued for
  `armHostWake()` -- one idiom, already encodes "queued work means now", no-op for native backends
  by construction. **Overridden on thread safety: `wake()` is the only member the spec declares
  thread-safe**, `armWakeAt` makes no such claim, and `armHostWake()` reads `_ready`, `hasInbound()`
  and `_parks.nextDeadline()` on the way there. **These sites exist precisely to be called off the
  loop thread.** B5's safety argument (`!isOnWorkerThread()` + the teardown assert implies
  `!running()`) rules out a race with the *loop* thread and **says nothing about two non-loop
  callers** -- two sockets closing on two threads both reach `notifyHandleClosing`. And `wake()`
  reaches B5's own goal anyway: six members, one idiom, since `spawn` already uses it.
- **DISARM BEATS LEAK WHERE DISARMING IS AVAILABLE.** `syncRunWith` leaks because it has nothing
  that can retrieve the park. `blockOn` does: **`cancelPending(task.handle())` already searches all
  three containers and disarms**, so a `ScopeGuard` calling it closes three of `blockOn`'s exits at
  once, where `task.release()` prevents the free but **leaves a live `Park` naming a leaked frame**.
  Cite `syncRunWith` for the principle, use `cancelPending` for the mechanism.
- **TWO HALVES OF ONE QUESTION, ANSWERED OPPOSITELY, EIGHT LINES APART IN THE SAME HEADER.** C1's
  `@throws` justified refusing an unadvanceable flow by *"a loop that kept turning would spin at full
  CPU instead"*; M4's paragraph **immediately above** says that on an `IdlePolicy::Return` loop it
  **does** spin, and resolves it by telling the reader not to. **Unadvanceable-with-nothing-parked
  threw; unadvanceable-with-something-parked spins forever -- and the difference is the loop's
  bookkeeping, not the flow.** core-cpp#17 is therefore unrepaired in `testing::TestLoop`, the loop
  type every test author reaches for first.
- **A SWEEP THAT TAKES THE REVIEW'S LIST INSTEAD OF ITS CRITERION MISSES WHAT THE LIST MISSED.**
  B4's I3 swept the six members the review named and missed **`notifyHandleClosing`** -- documented
  loop-thread-only, mutating `_closedParks`, `_abandoned` and the backend, **called from every socket
  and listener `close()` and destructor across seven files** -- and `cancelPending`. That is verbatim
  B5's own argument for asserting `cancelTimer` (*"reached from `~DeadlineTimer`, wherever the object
  owning the timer happens to be destroyed"*), **and a socket is destroyed in more places than a
  timer.**
- **THE STRICTER OF TWO TWINS ADVERTISING THE LAXER ONE'S CALLERS IS HOW THE NEXT DEFECT GETS
  WRITTEN.** `resumeSoon`'s Doxygen names "thread-pool callback" callers that its new assert aborts,
  while `submit(ParkedWork)` -- `resumeSoon` plus the hand-off -- is the safe twin. Two public
  members, same operation, different affinity, and the documentation pointing the wrong way. B6's and
  B7's completion backends are who will read it.
- **A NEW VARIANT: the mechanism verified, the PRECONDITION assumed.** B5, retracting its own
  `notifyHandleClosing` finding ten minutes after making it: *"I checked the mechanism I was
  thinking about and **not the precondition I was relying on**."* It had correctly verified that the
  function files into `_closedParks` and asks the backend for nothing — **and never checked whether
  the precondition could exist.** Verified link by link before accepting the retraction:

  ```
  HostDrivenBackend.cpp:15   attach(ReadinessHandler&) -> unexpected{Unsupported}, unconditionally
  EventLoop.cpp:777-788      registerPark: attach refuses -> detach, disarm, return ParkId::invalid()
  EventLoop.cpp:898-901      notifyHandleClosing: early-return on InvalidHandle, then parksOn(handle)
  ```

  **No park with a handle can exist on a host-driven loop**, so the path is inert there. Every
  earlier instance tonight was an instrument shaped by an expectation; **this was a claim whose
  mechanism was verified thoroughly and whose precondition was assumed**, which is why it survived
  its author's own scrutiny — the check was real and one level too late.
- **DO NOT SHIP INERT CODE WITH AN UNTESTABLE JUSTIFICATION; SHIP THE COMMENT INSTEAD.** B5's
  disposal, and the reasoning is better than the finding: *"Both would be code no test could ever
  red -- M7's finding, an assertion that cannot come out the other way, applied to a FIX instead of
  an assertion."* The remedy is a comment at `armHostWake` recording that the closed-park path is
  deliberately absent, citing the refusal, and saying what would have to change first — **the only
  instrument that can reach whoever later gives a host-driven backend readiness, because no test
  can.** And the boundary must be in it: *"every member that files work a turn must reach wakes" is
  still not true of `notifyHandleClosing` — it is merely not REACHABLE*, so a comment saying "all of
  them" would be wrong again one level down.
- **SAME FUNCTION, TWO CONCERNS, OPPOSITE VERDICTS — and a retraction of one must not be read as
  retracting the other.** `notifyHandleClosing`: B5's *"does it need to wake a host-driven
  backend?"* is **retracted** (no park can be on a handle there); B4's *"should it assert
  loop-thread affinity?"* **stands** (native backends, reached from every socket and listener
  `close()` and destructor across seven files). Relayed explicitly to both, because the natural
  reading of "B5 retracted the `notifyHandleClosing` finding" would have killed a live one.
- **AN ASSERT THAT FIRES ON A CALLER THE DOCUMENTATION INVITES IS WORSE THAN NO ASSERT.**
  `resumeSoon`'s Doxygen advertises "thread-pool callback" callers that B4's new assert aborts.
  **Either the doc is wrong or the assert is**, and the pair must be resolved together — checking
  the criterion is not enough without checking the precondition, which is B5's variant applied to
  B4's sweep.

## AN OUTCOME TEST CANNOT DISTINGUISH TWO MECHANISMS THAT PRODUCE THE SAME OUTCOME

B4's mutation, restoring `idleWait` to `_inRun` alone — i.e. reverting the C1 fix:

```
REQUIRE(result == 7)          passed     <- the outcome case does NOT discriminate
REQUIRE_FALSE(asked.empty())  FAILED     <- the argument does
```

**With the fix mutated away the flow still completes.** `blockOn` spins, the pool submits, the
inbound queue drains, `result == 7`. **B4's own `hopToPoolAndBack` case — written FOR C1 — would
have shipped the spin**, a green case on the tip with core-cpp#17 quietly restored.

**The case written for a finding did not cover that finding's own justification.** C1's `@throws`
justified the refusal *by* the spin; the fix replaced the refusal; the only case guarding the
replacement could not see the spin return. **The finding would have been closed by a test that
agreed for the wrong reason**, and the only thing that prevented it was running the mutation on a
case B4 had already deleted once as unwritable.

> **A loop that slept and a loop that burned a core both complete the flow. If the defect is HOW the
> result was reached, only an assertion on the REQUEST can see it.**

B4: *"the third time tonight the instrument had to measure the request rather than the result, and I
had not generalised it from the first two."* **Generalising was hard because the first two were
instruments lying, and this is a CORRECT instrument answering a question adjacent to the one asked.**
Same family, different face.

- **THE LEAD'S SHAPE WAS INCOMPLETE AND THE LANE FOUND IT BY TRYING IT.** Reading recorded timeouts
  after `blockOn` returns does remove the race — but **`ScriptedBackend` defeats the test regardless
  of read timing**: its `wait()` never really waits, so `blockOn` burns one script step per turn and
  the run ends in *"script exhausted"* before the pool hands back. **Attempt 1's reason wearing
  different clothes.** Resolution: a `RecordingBackend` **decorator over `makeDefaultBackend()`** —
  real blocking, real wake channel, argument captured, and the thread-safety argument intact with
  the vector owned by the decorator. Sixth correction of the lead tonight, third from B4.

## `WILL_FAIL` INVERTS *ANY* NON-ZERO EXIT — seven canary registrations, none protected

B4, adding a `spawnOffThread` mode and then checking what "Passed" meant:

```
mode=run             exit=1   Assertion `!_backend.isHostDriven() ...`
mode=blockOn         exit=1   Assertion in blockOn
mode=spawnOffThread  exit=1   Assertion `EventLoop::spawn from a second thread ...`
mode=bogusMode       exit=2   "unknown mode"        <- and ctest calls this Passed too
```

**A canary that never reached its assertion is indistinguishable from one that did** — in the one
test type whose entire job is to prove a check fires. Swept the tree: **seven registrations across
three binaries and zero `FAIL_REGULAR_EXPRESSION`** (every hit for that property is in the vendored
Catch2 cache):

```
src/core/net/CMakeLists.txt:279   hostdriven-canary.{run,blockOn}                    2
src/core/net/CMakeLists.txt:306   iocp-canary.{g1,g4}                                2
tests/CMakeLists.txt:305          windows-dialog-canary.{assert,abort,invalid-param} 3
```

**And the sharpest form is in the registrations themselves.** Two of the three carry:

> *"`SKIP_RETURN_CODE` takes precedence over `WILL_FAIL`, so a build with assertions compiled out
> reports a skip rather than the defect the canary exists to catch."*

**The author reasoned explicitly about one false-pass path in that very property block and did not
see the adjacent one.** Evidence of careful thought about the exact class, sitting beside the
instance it missed — the same mechanism as a rule ten lines above its violation, one level up.

**Ruling: one lane fixes all three sites.** A one-line property split across three lanes and two
rounds guarantees two get forgotten. The `windows-dialog-canary` is the one that once **hung**
instead of failing (`216d1d1`), so a second false reading there is not hypothetical.

- **`$?` IS EXPANDED BY THE OUTER SHELL THROUGH `wsl -- bash -lc '...'` — third instance tonight.**
  It told B4 its canary exited 0 when it exits 1. **Every probe goes through a script file**, which
  is the same fix as the pipeline exit status and the stale binary: ***stop asking a shell to carry
  a result across a boundary it reinterprets.***

## THE WORST FALSE PASS OF THE NIGHT: the canary reported its own regression as a pass

`tests/WindowsDialogCanary.cpp`, verified rather than taken:

```cpp
/// Exit status meaning "the failure was handled and execution continued", distinct from any success.
constexpr int ContinuedAfterFailure = 3;                                    // :29-30
...
assert(canaryHolds && "windows-dialog-canary asserts on purpose");
return ContinuedAfterFailure;          // :53  reached ONLY if the assert did not kill the process
...
if (strcpy_s(destination, 1, "x") != 0)
    return ContinuedAfterFailure;      // :65  same shape on the invalid-parameter arm
```

`WILL_FAIL TRUE` inverts exit 3 to **Passed**. **So the canary that proves a CRT failure kills the
process would have reported green on the day it stopped killing the process.** Every other false
pass tonight was a green about something *unrelated*; **this one is the defect reporting itself as a
pass** — and in the canary that has already produced one false reading (`216d1d1`, when it hung
instead of failing).

**The author encoded "this is the bad outcome" as its own exit code, wrote a comment saying exactly
that, and then registered the test in a way that cannot see the distinction.** Not an unconsidered
error path — a *deliberately distinguished* one, discarded by the registration. **Third file tonight
containing written evidence that someone reasoned about exactly the class that then bit it**, after
the `SKIP_RETURN_CODE` comment two lines from the gap and the rule ten lines above its violation.

Closed by having the binary name the path on stderr — `windows-dialog-canary: CONTINUED AFTER
FAILURE` — and matching it, because **an exit code cannot say WHICH non-zero it was.**

- **THE SWEEP TOOL WAS PROTECTED FROM THE CLASS IT WAS FIXING, BY AN ASSERTION PUT THERE FOR THAT
  REASON.** B4: *"my first attempt asserted on `${mode}` in the `iocp-canary` registration, which
  actually uses `${guarantee}`. The assertion failed, nothing was written, and I read the real text.
  **Had I written it with `replace` and no assert, I would have silently changed nothing and
  reported the sweep done.**"* **The procedure working, rather than the rule being remembered** —
  which is the distinction the whole session turned on.
- **A REGEX ENTRY THAT CAN NEVER MATCH IS THE INVERSE OF THE DEFECT IT FIXES: it looks like coverage
  and provides none.** Asked of the three new `FAIL_REGULAR_EXPRESSION` alternations: `usage:`
  appears in all three, so **confirm each binary actually prints that string**, or the alternation
  is silently one entry short for that canary.
- **COUNT AGAINST THE UNION OF `AGENT.md`'s CHECKLIST AND YOUR DISPATCH'S "THEN" SECTION.** B7a
  counted against `AGENT.md` — the correct procedure, taken deliberately after the remembered-list
  failure — **and still came up one gate short**, because its dispatch adds `clangcl-debug` (with
  `-DCORE_CPP_SANITIZERS=address` top-level) and `AGENT.md`'s checklist names only `cl-debug` and
  `clangcl-release`. **The instruments disagreed and nothing told the lane they might.** A defect in
  the arrangement, not in the lane. Run afterwards: **506 steps `--clean-first`, clean, 35/35.**
  Future dispatches say this explicitly.
- **A BRIDGE WITH NO PRODUCTION CALLER HAS AN INTERFACE NOTHING HAS EVER CONTRADICTED.** B7a, on why
  the B7a/B7b split worked and what it cost: on Windows **every existing registration is a waitable
  HANDLE** (`DefaultHandleKind` is `Waitable`; `WindowsSocket` parks on a `WSAEVENT`, not the
  SOCKET), so IOCP serves today's sockets through the waitable bridge with **no socket-layer
  knowledge at all**. **`HandleKind::Socket` — the zero-byte `WSARecv` and the `WSAEventSelect`
  write path — is exercised only by its own tests.** Correct against the only caller that exists,
  which is the one its author wrote to test it. **The failure mode for B7b is specific: it passes
  `HandleKind::Socket` from production for the first time and finds the bridge wants something the
  tests never asked for.** A design question, not a bug, and much cheaper known in advance.
- **libunicode's `-- [clang-tidy] Disabled.` was hit independently by a SECOND lane.** B4 found it
  grepping a configure log for its instrument proof; B7a found it two hours later doing the same.
  **Two lanes, two trees, one conclusion** — `message(STATUS)` has no namespace, so match the
  `[core-cpp] ` prefix and never the tool's name. Independent rediscovery is the corroboration
  neither would have had alone, and the argument for putting it in the rulebook rather than a report.
- **`head` EXITS 0 ON EMPTY INPUT, SO `cmd | head -1 || echo "NO MATCH"` NEVER FIRES.** Reproduced:
  `echo -n "" | grep -o 'nothing' | head -1 || echo "NO MATCH fired"` prints nothing, exit **0**.
  B4: ***"A missing file and a present-but-unmatched string produced identical output: silence."***
  **Third pipeline-exit-status instance for one lane in one day** — after `tail` and `Select-String`
  — and this one **inside the two-line probe written to check for that very class.** The recursion
  is four deep: the class, the instrument built to catch it, the sweep that fixed the instrument,
  and the probe that verified the sweep.
- **AN AUTHOR CHECKS THAT A PATTERN MATCHES WHAT IT SHOULD, AND RARELY THAT EVERY BRANCH OF IT
  *CAN*.** The `usage:` reachability check came out clean — all three canary binaries print it, so
  no alternation entry is dead — but B4's own words are the finding: *"I would not have tested my own
  regex for reachability."* **An entry that can never match is coverage that is not there: the
  inverse of the defect the regex was added to fix.**
- **A SEQUENCING INSTRUCTION IS A CONDITIONAL ON MEASURED STATE, NOT A COMMAND.** The lead issued
  three sequencing instructions to B4 as absolutes, each correct about the state it had measured and
  wrong about the state B4 was in by the time it arrived: *"rebase onto `320a9ab`"* reached a lane
  that had already committed; *"do not rebase"* reached one that had; *"the rebase is accepted"*
  reached one that had backed it out. **The latency between measuring and arriving is exactly where
  a lane commits.** Say *"if you have not started gating, do X; if you have, do Y"* instead.

  **B4 settled it on the rule rather than on the instruction: B7a holds a gate record naming
  `320a9ab`, so rebasing B7a would void it; B4's own gate cycle was the cheaper thing to spend.**
  The same rule the project has enforced all night, applied by a lane in the direction that cost it
  rather than saved it.

## THE UNIFYING STATEMENT, and it came from a lane rather than the lead

B4, on the lead's three crossed sequencing instructions:

> *"an instruction issued as an absolute against measured state is wrong the moment the latency
> exceeds the lane's next commit... **the instruction, like an instrument, was shaped by the state
> its author had measured, and could only be right about that state.**"*

**That folds the coordination failure into the same class as every technical one tonight.** Four
media, one shape:

| medium | shaped by |
|---|---|
| a `git status` filter | the tree the author expected |
| a finding regex | the diagnostic form the author expected |
| a poll predicate | the SHA the author expected |
| **a sequencing instruction** | **the lane state the author had measured** |

The lead had it as a coordination mistake of a different kind. **It is an instance.**

- **"NOT ON MY LIST" AND "I FORGOT IT" LOOK IDENTICAL IN A REPORT** (B4, flagging that
  `clangcl-debug` appears in neither of its two checklists). **A checklist reports what it covers and
  says nothing about its own boundary**, so a reader cannot tell an examined exclusion from an
  unexamined one. **Naming the exclusion is what makes the boundary visible** — the same argument as
  [core-cpp#38](https://github.com/contour-terminal/core-cpp/issues/38), where `clang-tidy` reports
  truthfully about a source set that silently excludes every Windows file.

  Ruled not owed, on the merits: `clangcl-debug` is in B7a's dispatch because B7a wrote 1165 lines of
  Windows-only source; B4's Windows exposure is its new asserts, which **`cl-debug` already exercises
  with the Debug CRT and `_ITERATOR_DEBUG_LEVEL=2`**, and nothing in `blockOn`, the scope guards or
  the asserts is compiler-specific.

## THE UNIFICATION, FINAL FORM: a silent intermediary decided the answer

B4 added two instances that **strain** the "shaped by the author's expectation" statement — `head`
has no opinion about what anyone expected, and `REQUIRE(result == 7)` is a correct assertion about a
true fact. So the class is one level down:

> **In every case something stood between the question and the answer, and that intermediary's own
> behaviour decided the result — silently.**

| medium | what actually decided the answer |
|---|---|
| a `git status` filter | the author's expectation of the tree |
| a finding regex | the expected diagnostic form (`warning:` vs `error:`) |
| a poll predicate | the expected SHA |
| a sequencing instruction | state measured before the latency |
| a probe's `\|\|` branch | `head`'s exit semantics, not the match |
| an outcome assertion | the flow completing, not how it completed |

**None announced its contribution.** `head` did not say *"I converted a failure into a success."*
The filter did not say *"I removed eleven rows."* The regex did not say *"I matched a form findings
do not take."* The instruction did not say *"I was true an hour ago."*

**And that is why every fix tonight rhymes — each makes the intermediary report its own
contribution:**

```
lines_in_log / steps=524        the build says how much it did
GH-BLIND / FETCH-FAILING        the poll says its input died
FAIL_REGULAR_EXPRESSION         the canary says which exit it took
rc captured before the pipe     the pipeline says which stage failed
assert the anchor before write  the sweep says it found nothing
REQUIRE_FALSE(asked.empty())    the case says what was REQUESTED, not just what resulted
```

**Six instruments, one remedy, arrived at independently six times by three people who had not
generalised it in advance.** That convergence is the evidence the class is real.
- **A NEGATIVE ALTERNATION CAN ONLY EXCLUDE THE PATHS THAT SAY SOMETHING.** B7a measured a fifth
  `IocpCanary` exit path with a temporary `probe` mode rather than reasoning about it:

  ```
  core-cpp-iocp-canary.exe probe   ->  exit 1, stderr COMPLETELY EMPTY
  ```

  `IocpBackend`'s constructor throws when `CreateIoCompletionPort` fails, nothing catches it:
  `throw` -> `std::terminate` -> `abort()` -> **the canary's own `SIGABRT` handler** -> `_Exit(1)`.
  **Exit 1 is byte-identical to the assertion firing, and stderr is empty because
  `core::testing_dialogs` suppresses the abort message** — *the same linkage that stops a Debug
  assert opening a modal dialog also removes the CRT's text.*

  **The mechanism that makes the canary work at all is what makes this case invisible**, and it
  converts every abort into exit 1. **A fourth instance of the pattern with a twist: the file
  contains no written evidence anyone reasoned about it, because the mechanism that causes it was
  added to solve the adjacent problem.**

  **The remedy is positive, not negative**: a marker printed immediately BEFORE the forbidden
  operation (proving the mechanism was reached) and one AFTER (proving it was survived) — which is
  exactly the shape `WindowsDialogCanary`'s `ContinuedAfterFailure` already has.
  **A negative alternation excludes the paths you enumerated; a positive match requires the one path
  you want.**

  Two cautions flagged rather than asserted, for the lane to verify: **`PASS_REGULAR_EXPRESSION`
  appears to make ctest ignore the exit code**, so combining it with `WILL_FAIL` would invert the
  regex verdict and `WILL_FAIL` must go; and **`assert`'s own text may not survive the
  dialog-suppression**, which is why the existing alternations match the canary's OWN printed
  strings rather than assert text.
- **"N/N" MEANS "N RAN, 0 FAILED" — IT DOES NOT MEAN "N PASSED."** B4's seventh instance, and the
  broadest: **`clangcl-release` reports `36/36` with all five canaries `Skipped`.** Release compiles
  the assertions out, `SKIP_RETURN_CODE` fires, and **a skip is absorbed into a green total.**
  *"The total does not announce that five of its members abstained."*

  **Every figure in every gate record this project has produced is affected** — `fe48143` 31/31,
  `fb3fe97` 33/33, `a02031c` 33/33, B7a's 35/35 on both Windows legs — and every lane, the lead
  included, read them as pass counts. `ctest` reports the skips in its own output and then hands you
  a summary that has absorbed them: **the "named exclusion" point one level up, where a total does
  not announce its boundary any more than a checklist does.**

  **Rule: a gate record states the skip count beside the total, or the total is not a result.**

  Concretely for B4's round: **the thread-affinity canary is exercised by exactly two legs,
  `clang-debug` and `cl-debug`** — it abstains on every Release leg, inside a green total, so a
  reader comparing Windows figures would conclude it passed on four.

  **A correction of the lead's own relaying follows from it:** *"your 524 matches B7a's 524, which is
  the cross-check"* compared two totals that may have held different skip counts. **The BUILD
  cross-check holds — those are statements, not tests. The TEST cross-check does not, until both
  state their skips.**
- **THE THREE READINGS OF A STEP COUNT, in one table (B4, and better than the prose version landed
  in `build-and-toolchain.md`):**

  | leg | is the count the verdict? | why |
  |---|---|---|
  | `clang-tidy` | **yes** | what is measured runs INSIDE the statement ninja skips |
  | `clang-debug`/`gcc-release`/sanitisers | **no** | ninja's currency check is sound; `work_lines=61` is a real answer |
  | `clangcl-*` | **yes, oppositely** | the skip DECISION is wrong on a pre-`ca8dfc32` launcher |

  **Same figure, three meanings, decided by where the measurement sits relative to the skip.**
- **MUTATE YOUR OWN FILE, NOT AN OFFERED ONE.** B7a declined the two mutation sites already proved
  tonight and fed `readability-identifier-naming` into `src/core/net/detail/ReadinessSlot.hpp`, **a
  file its own commit adds** — *"because those prove the analyser reaches B4's and B5's code and I
  needed it to reach mine."* **A mutation proves the analyser reached the file you mutated and
  nothing else**, which is the same boundary as the work count: instrument versus surface, one file
  at a time.

## THE RULEBOOK REPRODUCED THE DEFECT, IN THE FILE WHOSE JOB IS TO PREVENT IT

`.agent/rules/async-and-net.md`, landed in `27b8b43`, asserted:

> *"`post`, `submit`, `schedule`, `spawn`, `requestCancel` and `stop` all wake the backend.
> `addTimer` was the sixth member of that family and **the only one that did not.**"*

**It was not the only one.** `registerPark`, `resumeSoon` and `requestStop` do not either. **So the
rule written to record the defect reproduced it** — and **re-reading the rulebook could never have
found it**, which is the property that makes this worse than any other instance tonight.

**The chain: B5 proposed the formulation, the LEAD amplified it — calling it "better than B4's" and
propagating it to three lanes — and B4 landed it. Three people handled it and none checked the
enumeration against the code.** The lead is the one who called it *mechanically checkable*, which
is precisely the claim that made nobody check it.

**B5's remedy attacks the re-reading:** derive the family from the code every time, **including from
this page**; fix the primitive, not the caller; and a member that legitimately does not join says so
where the reader is. **A rulebook page that tells you not to trust it is an odd artifact and the
right one here.**

And the structural half: **`addTimer` now drops its own copy and inherits the arming from
`registerPark`** — *"six call sites reach that primitive; the fix placed above it covered one."*
The family error one level down: **a fix at the caller looks complete and covers a sixth of the
surface.**

- **A WRONG PREDICTION IS THE BETTER OUTCOME WHEN IT IS WRITTEN DOWN FIRST.** B5 predicted three
  REDs and got two: `requestStop` is **not** broken on the path the review described, because
  `spawn` hands the flow `_rootStop`'s token and `DelayAwaiter::await_suspend` registers a cancel on
  it — **the wake arrives from the cancellation, before `unparkEverything()` runs.** The case passed
  for a reason that was not the fix. Isolating `requestStop`'s own obligation with `host.clear()` is
  what turned it into a measurement. **Outcome-versus-mechanism in its third costume**, caught only
  because the prediction was in writing.
- **THE INSTRUMENT REPORTED AND THE CONSUMER IGNORED IT — the eighth instance and a new sub-variant.**
  B5's mutation script printed **`mutation applied: 0 line(s)`** and then concluded *"the assertion
  is decorative"* anyway. **Every other instance tonight was an intermediary staying silent; here it
  spoke and was not heard.** Fix: a zero count is fatal — **the number must be able to stop the
  verdict, not merely accompany it.**
- **CORRECTION OWED TO `build-and-toolchain.md:73`: `cl` is NOT untouched by the launcher.**
  Verified: `out/build/cl-debug/build.ninja` has **314 LAUNCHER lines, all `fastcache-cc`** —
  identical to `clangcl-release`'s 314. B5 hit `LNK1163: invalid selection for COMDAT section` there
  and `--clean-first` cleared it at 517 steps / 33/33. **Measured: the launcher is used, the link
  failed, `--clean-first` fixed it. INFERRED: that the failure came from a stale cached object** —
  `--clean-first` fixes every stale-tree problem, so it does not discriminate between causes.
  B5's framing is the keeper either way: ***clang-cl fails silently as an inherited pass, MSVC fails
  loudly as a link error — same launcher, opposite symptom, and the loud one is the lucky one.***
- **FIVE OF SEVEN CANARIES ARE PROTECTED BY A SINGLE CI JOB** — traced from B4's skip measurement
  through the matrix:

  ```
  windows job matrix (build.yml:410):  [cl-release, clangcl-release, cl-debug, cl-release-tls]
  CMAKE_BUILD_TYPE:                     Release     Release          Debug     Release
  ```

  **`cl-debug` is the only Debug leg on Windows**, and every canary abstains where assertions are
  compiled out. So `windows-dialog-canary.{assert,abort,invalid-parameter}` and
  `iocp-canary.{g1,g4}` are exercised by **`windows (cl-debug)` alone**; only
  `hostdriven-canary.*` has redundancy (`linux (clang-debug)`, `sanitizers` asan+tsan). And
  `cl-debug` is the leg the rulebook itself says *"was the one Windows configuration nobody ran in
  CI"* until it was added. Attached to
  [core-cpp#38](https://github.com/contour-terminal/core-cpp/issues/38), whose remedy — a Windows
  Debug leg that is not `cl-debug` — closes both halves with one job.

- **THREE INDEPENDENT WAYS ONE CANARY CANNOT REPORT A TRUE NEGATIVE, all found in one night:**
  `windows-dialog-canary.assert` **hung** instead of failing when unlinked from
  `core::testing_dialogs` (`216d1d1`); returns `ContinuedAfterFailure = 3` — *the defect it exists
  to detect* — which `WILL_FAIL` inverts to Passed; and **abstains** on every Release leg inside a
  green total. Two are being closed by the canary follow-up and #38; the first already was.

- **THE FOUR READINGS OF A COUNT ARE ONE RULE, not four** — about where the measurement sits
  relative to what was skipped:

  | the count is… | because the measurement sits… |
  |---|---|
  | the verdict (`clang-tidy`) | *inside* the statement ninja skipped |
  | not the verdict (`clang-debug`, sanitisers) | *after* a sound currency check |
  | the verdict again (`clangcl-*`) | after an **unsound** currency check |
  | not the verdict (a test total) | *across* results that include abstentions |

  B4's remedy: runners emit `total / skipped / notrun / ran_and_passed` per leg. **That format is
  what a gate record should contain, not merely what a script prints** — the summariser reporting
  its own contribution, which is the same fix as all the others.
- **AN ISSUE'S COST ESTIMATE IS PART OF ITS CONTENT.** [#38](https://github.com/contour-terminal/core-cpp/issues/38)
  was written as though a Windows Debug leg needed a preset authored and validated. B4 checked:
  **`clangcl-debug` already exists in all four preset sections**, workflow included —

  ```
  configurePresets / buildPresets / testPresets   ['clangcl-debug', 'clangcl-release']
  workflowPresets                                 ['ci-clangcl-debug', 'ci-clangcl-release']
  ```

  — so the remedy is **one list entry at `build.yml:410`**. *"Add a job" rather than "add a
  toolchain"* **is the difference between a ticket someone picks up and one that sits**, and the
  estimate was the lead's, not the reporter's.
- ***"A CANARY WITH THREE INDEPENDENT SILENCES IS NOT A CANARY"*** — the sentence to lead the canary
  follow-up with, better than any general statement about `WILL_FAIL`. All three of
  `windows-dialog-canary.assert`'s silences were found in a single night: the hang (`216d1d1`,
  closed), the inverted `ContinuedAfterFailure = 3` (the follow-up), the Release abstention (#38).

## RULING OVERTURNED BY MEASUREMENT: "ready work wakes; a park arms"

The lead ruled `_backend.wake()` at all off-turn wake sites, on thread safety. **B5 measured it
instead of arguing it, and it is wrong at `registerPark`:**

```cpp
void HostDrivenBackend::wake() noexcept     { scheduleAt(_clock.now()); }   // :61-64
void HostDrivenBackend::armWakeAt(deadline) { if (!deadline) return;        // :66-74
                                              scheduleAt(*deadline); }
```

**`wake()` is `scheduleAt(now())`**, so a park fifty milliseconds out is pumped **immediately**,
finds nothing due and re-arms from the same heap:

```
HostDrivenLoop_test.cpp:196: CHECK( soonestDelayMs(host) == 50 )   0 == 50   <- addTimer
HostDrivenLoop_test.cpp:236: CHECK( soonestDelayMs(host) == 50 )   0 == 50   <- delay()
```

**B5's replacement rule is better than "one idiom":**

> **Ready work wakes; a park arms.**

`resumeSoon` and `requestStop` file work ready *now* carrying no time -> `wake()`. `registerPark`
files a park *with a time* -> `armHostWake()`. **Not five-one-way-and-`spawn`-the-other: one
distinction that says which primitive applies, and the two primitives exist because the distinction
does.** The lead was treating **uniformity** as the goal; **non-arbitrariness** was the goal, and
B5's rule delivers it without flattening a real difference.

**And the thread-safety argument does not reach `registerPark`:** it already does `_parks.add(...)`
unsynchronised, so two threads calling it concurrently race **with or without** the arming — which
is what its `teardownIsSerialisedWithDispatch()` assertion forbids. *"The arming cannot make a
function that is single-threaded by contract less safe."* The argument holds where two non-loop
threads genuinely collide (`notifyHandleClosing`) and nowhere else.

- **A SILENT `||` MAKES A FUTURE REORDERING WORK; AN `assert` MAKES IT FAIL LOUDLY.** The lead's two
  messages on `armHostWake`'s missing `_closedParks` contradicted each other (fix defensively /
  comment only). **B5's reconciliation is better than either**: comment only, *because a defensive
  disjunct would make a future reordering succeed rather than announce itself* — the opposite of the
  stated goal — **plus `assert(_closedParks.empty())` in the host-driven branch as the loud
  instrument.** An assert is not inert code: it has no behaviour, it is an **executable statement of
  the invariant the comment describes**, and it fires exactly when someone gives a host-driven
  backend readiness without revisiting.
- **"A TEST THAT WAS MEASURING ITS OWN FIXTURE."** B5's `host.clear()` isolation dropped the host's
  queue while the **backend** still believed a pump was outstanding, so its coalescing swallowed the
  next request: `pendingCount == 1` expanded to **0**. Isolating by **pumping** — what a real host
  does — clears both sides. **The isolation mechanism was the neighbour of the thing under test**,
  the same shape as `spawn` rescuing the timer, one level in.
- **A PREDICTION CARRIED ACROSS A REWRITE IS A REMEMBERED LIST.** B5 predicted M-A would red 2 cases
  and it reds 3, because the rewritten `requestStop` case now needs `registerPark` to have armed
  something. The coupling is real and M-C still isolates, so the suite discriminates — but, in B5's
  words, *"I should have derived it from the rewrite instead of reusing the old number."*

### An idle lane is a stopped lane, and nothing announces it

B4 ended a turn with *"ASan, TSan and the deleted-tree `clang-tidy` outstanding. When they land
I'll report all four ... and tell B7a the window is open."* It then went **idle**. Idle is not
"working"; idle is "this agent's turn ended and it will not act again until someone messages it."
Two lanes -- B7a frozen at `320a9ab`, B5 holding fix round 2 -- were blocked behind a push that was
never going to happen, and **nothing in the system said so**. I found it by measuring:
`git worktree list` put `D:/core-cpp-wt-b4fix` at `5d7a5ae` and `origin/master` at `7bdd132`.

**Rule: a report is not a handoff.** A lane finishes its run and reports the result, or it names in
one line the answer it is blocked on. Reporting a plan and then stopping produces the one state
that reads identically to progress.

**Rule for me: a blocked lane is my liveness problem, not its own.** A lane cannot notice it has
been forgotten -- it has no clock and no turn. The controller's periodic question is not
*"how is it going"* (which costs a turn and tells me what the last message already said) but
**"which agents are idle, and does any of them owe work?"** `ListAgents` answers the first half in
one call; the ledger answers the second. This is the sixth instance of *a silent intermediary
decided the answer* -- here the intermediary is the scheduler, and its silent contribution is that
"idle" and "busy" render the same in the absence of a message.

### "Two commits" was one

The same report said two commits were ready to push. The worktree holds **one** (`5d7a5ae` on
`320a9ab`). I did not learn which of *folded in*, *unwritten* or *dropped* it is -- I asked -- but
the count itself was a claim, made in advance of the work, restated afterwards as though it had
been re-derived. **The same failure as B5's mutation prediction, in a different medium**: a number
that was true about the plan, repeated after the plan changed.

### A rule classifies the filed object, not the signature -- `schedule` is the near-miss

Having accepted **"ready work wakes; a park arms"**, I enumerated every wake/arm site in
`EventLoop.cpp` rather than trusting the rule's wording, and `schedule` reads as a counter-example:

```
schedule(deadline, work):
    if (isOnWorkerThread())  registerPark(ParkEntry::onDeadline(...))   -> arms
    else                     _inbound.scheduled.push_back(...); _backend.wake()
```

A deadline in the signature, and the off-thread branch **wakes**. Applied to the signature the new
rule condemns it. Applied to **what the member filed** it endorses it: the off-thread branch filed
an entry on the *inbound queue*, the park does not exist yet, and the turn that drains the inbound
queue is ready now. `registerPark` arms for the deadline when the drain reaches it, and the
end-of-turn `armHostWake()` arms again. **Changing that `wake()` to an arm would arm for a deadline
whose park has not been created, and the work would sit in the inbound queue until something else
asked for a turn.** A correct site, turned into a hang, by a correct rule applied one level off.

**So: every behavioural rule names the object it classifies.** "Ready work wakes; a park arms"
classifies *the thing the member just filed*, and the two members that file the same kind of thing
through different queues get the same answer. A rule stated over signatures is a rule that will be
applied over signatures by the next reader, who has only the sentence.

**And the enumeration is why this was caught.** I had the rule from the lane that measured it, I
agreed with it, and I still read the eleven sites. The sentence *"`post`, `submit`, `schedule`,
`spawn`, `requestCancel` and `stop` all wake the backend"* -- which I had written into the rulebook
myself -- would have been the third generation of the same wrong list.

### Seventh instance: `$( )` is a silent intermediary, and the positive marker caught it

The CI monitor's first event was:

```
MON-UP rc= tracked=12 tip=7bdd132
```

**`rc=` is empty.** `snap()` sets `ghrc=$?` and is called as `prev=$(snap)` -- a command
substitution runs its body in a **subshell**, so every variable it assigns dies with that subshell.
The loop's `[ "$ghrc" -ne 0 ]` then compares an empty string to an integer, which is a bash error
and evaluates false, so:

- the **GH-BLIND branch could never fire**, and a dead `gh` would have produced silence that reads
  exactly like "nothing landed";
- the previous monitor of the same shape (`bv0v1o46i`) had been "running" for hours and had
  **not even created its output file** -- there was nothing to read and nothing said so.

This is the seventh medium for one shape, after a `git status` filter, a finding regex, a poll
predicate, a sequencing instruction, a probe's `||` branch and an outcome assertion. And the fix
rhymes with all six: **make the intermediary report its own contribution through the channel that
actually crosses the boundary.** Here the only such channel is stdout, so `snap()` prints
`RC=<n>` as line 1 and the caller splits it off.

**What actually caught it was the positive marker.** I put `rc=$ghrc` in the startup line for
diagnostics, not as a check, and it is the reason the defect was visible in the first three seconds
rather than at the first `gh` outage -- which might never have come. A field that prints a value on
the success path exposes an empty value; a branch that only speaks on the failure path cannot
report that it is unreachable. **This is B4's canary positive-marker design, validated in a
different medium before B4 has written it**, and it generalises past canaries: an instrument states
what it measured, not only what it objected to.

**Rule: a monitor's startup line says whether its input answered.** `MON-UP` now carries `rc=`, and
a non-zero rc prints `MON-UP-BLIND` saying in words that "no events" means nothing. A periodic
`MON-ALIVE` distinguishes a quiet watch from a dead one -- the distinction the previous monitor,
with no output file at all, could not make.

### The enumeration rule, applied to the code, found an eleventh member the same evening

I had just written into `.agent/rules/async-and-net.md`: *derive the family from the code every
time, including from this page*. Applying it to the serialisation family rather than to the wake
family, within the hour:

**SUPERSEDED -- see "A finding derived from a stale tree" below: `5d7a5ae` already contained this assert when I reported it missing.** The finding as written was:

**`EventLoop::notifyHandleClosing` mutates `_parks`, `_closedParks` and `_abandoned` directly, and
is the only public member of that family that does not assert
`teardownIsSerialisedWithDispatch()`.** **Eleven** siblings do -- B5 re-derived the family and found `run` at `:194`, which my list omitted. I had enumerated instead of deriving, in the same hour I made deriving the rule. The members that genuinely are callable
from any thread route through `_inbound` under `_inboundMutex` and then wake; this one does neither.
Its requirement exists only in prose, at `EventLoop.hpp:469`: *"Call this BEFORE the `close()`
syscall, **on the loop thread**"*.

**It sits under a rulebook section titled "Thread affinity, asserted rather than documented", whose
opening sentence is that a rule written beside the code is not a rule the code applies.** The
section names two instances. This is the third, and it was found by the method the section
prescribes rather than by reading the section.

**What makes this worth recording is not the defect -- it is Important, not Critical, and no current
caller is on the wrong thread.** It is that *the same method, applied twice in one evening to two
different families in one file, found a missing member both times*. `addTimer` neither woke nor
armed; `notifyHandleClosing` documents instead of asserting. **Enumerating definitions is not a
better way of checking a list. It is a different activity, and it is the only one that can find the
member nobody wrote down.**

**And I checked the wrong hypothesis first, which is the part I would have skipped if I were
reasoning from the rule alone.** I suspected a single-threaded hazard -- a handle closed after
`std::exchange(_closedParks, {})` leaving waiters until some later turn. It is not there:
the exchange at `:257` is taken deliberately *before* the wait, a non-empty `closed` forces the
timeout to zero, and `hasPendingWork()` counts `_closedParks`. **The comment explaining why was
already in the code.** Reporting the hypothesis I disproved alongside the one I confirmed is what
keeps the finding from reading as broader than it is.

### My flagged correction was itself the error, and a zero in the wrong file nearly confirmed it

I had routed to B4 a correction of `build-and-toolchain.md:73` -- *"`cl` (`deps = msvc`,
`/showIncludes`) and GCC and Clang hits were unaffected"* -- because I had measured **314
`fastcache-cc` lines in `cl-debug`'s `build.ninja`** and concluded that `cl` obviously *is* affected,
since it plainly uses the launcher. Measured properly before editing:

```
cl-debug        CMakeFiles/rules.ninja:  deps = msvc ×87   /showIncludes ×88   -clang:-MF ×0
clangcl-release CMakeFiles/rules.ninja:  deps = gcc  ×29                       -clang:-MF ×29
both            build.ninja:             fastcache-cc ×314
```

**The rule was right.** "Unaffected" is a claim about the *dependency mechanism*, not about
caching: `cl` goes through the same launcher and never asks for a GNU depfile, so the
`-clang:-MF` defect cannot reach it. My 314-line measurement was true and irrelevant -- it answered
*does `cl` use the cache*, and the sentence was answering *can this defect reach `cl`*.

**The near-miss inside it is the part to keep.** My first attempt greped `build.ninja` for
`deps =` and got **zero matches in both trees**. Ninja keeps rule definitions in
`CMakeFiles/rules.ninja`; `build.ninja` holds the build statements. A zero from the wrong file is
byte-identical to a zero from the right one, and this particular zero would have *supported* the
correction I already believed in -- "neither tree declares a dependency mode, so the mechanism
described here is fiction."

**Rule: when a grep returns zero and the zero is the answer you were hoping for, prove the file you
searched is the file a match would be in.** One control search for something you know is present
costs one command and converts an absence of evidence into evidence. This is the
positive-marker principle in a third medium, after the canary exit status and the monitor's `rc=`:
**an instrument states what it measured, not only what it objected to.**

**And the meta-rule, which cost nothing this time and has cost hours before:** *a correction is a
claim.* I have been requiring every lane to verify the numbers in its dispatch; the corrections I
issue are subject to the same requirement, and this one would have had a lane edit a correct rule
into a wrong one on my authority.

### A grep for a wrong sentence always matches the document that corrects it

B5 checked whether our two edits to `async-and-net.md` had collided by grepping for the sentence
this round exists to remove -- *"the only one that did not"* -- and got **1**. It was one message
from reporting that the rulebook had resurrected the exact falsehood being corrected. It had not:
**the match was inside its own quotation of it**, in the paragraph explaining that the sentence was
wrong.

This is not a quirk of that file. **Every document that corrects a quotation contains the wrong
version verbatim.** A correction that does not quote what it corrects is unusable, so the property
is structural, and a grep for the old text therefore matches *every* correctly-corrected document
and reads identically to a regression.

- **Check for the replacement, not the absence of the original.** `grep -c "<new sentence>"` has
  no false positive of this kind.
- **Or count outside quotation context** -- `grep -v '^\s*>' | grep -c`, or require the match not
  be inside a fenced block or an italicised quote.
- **Or quote the wrong sentence with an ellipsis**, which is the cheapest of the three and the one
  found by accident: my own correction reads *an earlier draft said "every member ... asks"*, so
  `grep -c "every member that files"` returns **0** and the absence-check still works. **An elided
  quotation is still a usable correction and is no longer a grep target.** Verified after writing
  it, not before -- I had the property by luck and kept it on purpose.
- **A count standing in for the thing** again, in the medium where the thing and its negation are
  byte-identical.

**And the reason it was caught is worth as much as the finding.** B5 read four lines of context
*only because reporting it would have accused someone else of a regression*. A check whose failure
mode is an accusation earns the context read; a check whose failure mode is silence does not get
it. **That is backwards, and it is the correct generalisation**: the accusatory checks in this
project have been careful and the silent ones have not.

### Prove the assert executes, not that it holds

B5 was told to add `assert(_closedParks.empty())` to a branch with no case. It proved the
assertion **runs** by inverting it:

```
assert(!_closedParks.empty())  ->  SIGABRT, runner_rc=134, EventLoop.cpp:499
restored                       ->  81 assertions in 17 test cases, rc=0
```

**An assertion that never executes is indistinguishable from one that holds**, and both report
green. "Unreachable today" has to mean unreachable-but-live, which is a different claim requiring
its own evidence. The inversion is that evidence and it costs one build.

It also carries **two independent reasons** in its comment -- no host-driven park can be on a
handle, *and* the turn `std::exchange`s `_closedParks` before the wait with nothing able to refill
it. Two reasons means the assertion survives either premise changing alone, which is precisely the
case a defensive `||` would have absorbed without a word.

### My rulebook text stated a universal, and a reader falsifies it in one grep

I replaced a wrong enumeration with *"every member that files work asks the backend for the turn
that will run it"* -- a non-enumerating form, which was the point. B5 caught that it is **false**:
`notifyHandleClosing` appends to `_closedParks` and neither wakes nor arms. It is sound (the turn
exchanges `_closedParks` before the wait and a non-empty batch forces the timeout to zero;
`hasPendingWork()` counts it), but the sentence does not say so.

**Trading a wrong list for a wrong universal is not progress.** A universal invites a reader to
falsify it in one grep and then distrust the section that contains it -- which is worse than a
list, because a list at least looks finite. **State the property, or state the exception. Never the
quantifier alone.**

### CORRECTION to the "two commits was one" entry

B4 answered: *"two-commit push"* meant `origin/master` advances past **both** `320a9ab` (B7a's,
riding underneath) and `5d7a5ae` (B4's own). One commit of its own. The `WaitHandleAwaiter` guard
is folded into `5d7a5ae`, measured from the diff (`ScopeGuard` +4 lines; three guards now in
`EventLoop.hpp`: `DelayAwaiter`, `WaitHandleAwaiter`, `blockOn`'s exceptional-exit
`cancelPending`). **So the count was right and my reading of it was wrong** -- "two commits to
push" reads as two of yours, and it was one of yours plus one of someone else's. Asking beat
assuming in both directions here: I was right to ask and wrong about the answer.

### B4's measurement corrects my count matrix: the Release abstention is build-type-driven

`gcc-release` skipped **3** -- the same three `hostdriven-canary` modes that `clangcl-release`
skips as part of its 6. **Linux Release abstains from exactly what Windows Release abstains from**,
so the abstention is a property of the build type, not of the platform. **The canary's reporters
are therefore the four Debug legs** -- `clang-debug`, `clang-asan-ubsan`, `clang-tsan`, `cl-debug`
-- which matches the count I derived from the CI matrix, now measured from the other end. Two
derivations, independent inputs, same answer: that is what makes it a result rather than a claim.

### Which binary reaches the line is a separate question from whether a second binary is owed

B5 proved the `notifyHandleClosing` assertion executes by inverting it, and the result was not the
one either of us expected:

```
core-cpp-net_backend-test   rc=0    All tests passed (2271 assertions in 64 cases)   <- never reaches it
core-cpp-net-test           rc=134  EventLoop.cpp:981: Assertion failed, SIGABRT     <- reaches it
```

**The backend test never calls `notifyHandleClosing`; the socket test does, through real closes.**
So the guard is live, every current caller is demonstrably on the loop thread, and the severity
call was confirmed by measurement rather than by argument.

**And in B5's words: "had I probed one binary I would have reported the guard as dead."** The
abort-or-not mechanism *looks* conclusive -- it is a process either dying or not -- so a
single-binary probe produces a confident wrong answer with no tell. This is `testing.md`'s
*"a module has as many test binaries as it has things to link"* arriving from the opposite
direction: not **is a second binary owed**, but **which binary reaches the line I am asking
about**. Any liveness probe -- an inverted assert, a deliberately broken branch, a poisoned
constant -- has to name the binary it ran in and say why that binary is the one that would reach
it.

### A copied diagnostic that is wrong for its site is a defect, not consistency

The ten sibling asserts end `post() a call to it instead`. B5 deliberately did **not** copy that
into `notifyHandleClosing`, because the advice **cannot be followed**: the call is inseparable from
the `close()` that must follow it -- the handle has to still be open when the backend drops its
registration, or the removal lands on a descriptor number the kernel may have reassigned. Its
message says the whole close moves to the loop thread. **Approved as written.** A message telling
the reader to do something impossible is worse than no message, and matching the siblings verbatim
would have bought consistency with a lie in it.

### `grep -c 'Skipped'` double-counts, and B4 caught it inside the report that introduced skips

ctest names a skipped test **twice** -- inline as `***Skipped`, then again in the trailing "did not
run" list as `(Skipped)`. So `grep -c` gave `gcc-release skipped=6, ran_and_passed=26`; the truth is
**3 and 29**.

**B4 would have published a wrong skip count inside the very report that introduced counting
skips.** Its fix prints both forms so the doubling is visible rather than assumed:
`total=32 skipped=3 (trailing-list repeats=3) notrun=0 ran_and_passed=29`.

Same shape as everything else tonight, with one variation worth naming: **this summariser distorted
by REPEATING rather than by omitting.** Every prior instance was something silently dropped; a
double-count is something silently added, and it reads as the more alarming number, which is the
direction that gets believed. B4 also noted its earlier `clangcl-release` figure was right **by
luck of pattern** (`\*\*\*Skipped` happens to be inline-only) rather than by design -- the same
admission B5 made about its own grep, and the reason both are trustworthy.

### The rulebook citation I wrote decayed within the hour

Within one hour of writing *derive the family from the code every time, including from this page*, I
wrote into the same page: *"because `hasPendingWork()` counts it"* and *"`armHostWake` does not
count `_closedParks` although `hasPendingWork()` does"*. **B4's commit, landing beneath mine,
deleted `hasPendingWork()`.** `git grep hasPendingWork origin/master -- EventLoop.{cpp,hpp}` returns
nothing, and the exchange I cited as `:257` is now `:269`.

B5 caught it on its own comment and the same catch reached mine. **Nothing would have failed:** a
clean rebase carries a dangling citation into the tree, and no gate reads prose.

**The fix is not a better citation, it is no citation.** That paragraph now argues from the *order
of the steps in a turn* -- a property a reader re-derives from the code in front of them -- and
contains no function name and no line number. **A coordinate goes stale while nothing fails; a
property cannot.** That is the general answer to a class of decay this project has now hit in
provenance rows, dispatch source lists, family enumerations and, here, a rule citing the code it
governs.

### ctest canary registration semantics, MEASURED rather than inferred from the docs

B7a built a throwaway ctest project with four registrations carrying B4's exact properties. CMake
documents only that `PASS_REGULAR_EXPRESSION` makes the exit code ignored, which leaves the
question nobody had asked: **does it also swallow `SKIP_RETURN_CODE`?** If it did, the canary would
*fail* on every Release leg instead of abstaining.

| Case | Output / exit | Verdict |
|---|---|---|
| abstains (`NDEBUG`) | skip message, **77** | **Skipped** -- `SKIP_RETURN_CODE` survives |
| refusal fires | marker, exit 1 | **Passed** |
| refusal does not fire | marker **and** continue text, exit 0 | **Failed** -- `FAIL_` beats `PASS_` |
| constructor threw | **no output**, exit 1 | **Failed** -- required regex not found |

**The fifth path is closed by the positive match, not by any alternation** -- which is precisely why
no negative alternation could ever have reached it.

**Two details invisible from the implementing lane's side, both load-bearing:**

- **The marker must be per-guarantee.** A single G1-shaped regex fails a `g4` registration, because
  the loop is `foreach(guarantee IN ITEMS g1 g4)`.
- **The marker must go to `stderr`.** `onAbort` calls `_Exit`, which flushes nothing, so the same
  marker on `stdout` is lost **on exactly the path it exists to prove**. The positive-marker
  principle defeated by a buffering detail, in a form that ships looking correct.

### The configurations that exist locally and the ones CI runs are different sets, and nothing said so

B7a found `clangcl-debug` is defined in `CMakePresets.json` and referenced by **no workflow**. I
verified it, then enumerated the whole set instead of checking the one preset:

```
non-hidden configure presets exercised by NO workflow:
  clangcl-debug     appleclang-debug     gcc-debug
```

**All three are Debug**, and the consequence is larger than three missing legs:

```
macos job:    appleclang-release, clang-release    <- both Release
windows job:  cl-debug, cl-release, cl-release-tls, clangcl-release
```

**macOS runs no Debug configuration in CI at all.** Every `assert`,
every `teardownIsSerialisedWithDispatch()` and every canary is compiled out there --

> **CORRECTED, by the lane, against me.** My original said *"every G1-G5 guarantee"* too. Wrong:
> G1/G4 live in `windows/IocpBackend.*` and `ICompletionPort.hpp`, which **macOS never builds**.
> That is platform exclusion, not build-type exclusion, and merging the two overstates what a
> Debug leg would buy. I also wrote *"all 152 assertions in `src/core`"*, wrong by 5x -- the grep
> swept up ~130 `static_assert`s (compile-time) and 11 `Require()`/`Guarantee()` (which abort in
> Release too, so they are not `NDEBUG`-gated at all). The real figure is ~30 runtime `assert(`,
> **19 of them in the shared event-loop code**, including the twelve
> `teardownIsSerialisedWithDispatch()` checks and `ReadyBatch`'s re-entrancy trap.
> **The argument survives and is stronger stated honestly** -- and the wrong number had already
> reached `.agent/rules/build-and-toolchain.md`, where it would have become the citable source.
> **A grep for `assert(` counts three different things with three different lifetimes.**


**and `kqueue` is macOS-exclusive, so that backend's entire assertion surface is unexercised
anywhere.** macOS is the platform R101 exists because of: the `EV_EOF` divergence where a dial
believes a refused connect succeeded. It is the platform where no assert has ever fired.

**Checking the preset you care about finds one; enumerating the set finds three and the pattern.**
Third time tonight that the same substitution has paid, after the closed-handle family and the
serialisation family.

**Routed to B7a as Task B7c**, with the remedy stated as a *gate* rather than as three added legs:
a `tree-level` check making the set difference fatal, every preset either referenced by a workflow
or allowlisted with a written reason, plus the `style` step without which a tree-level check runs
nowhere. **And the rule written as a property, not a preset list** -- a list there would decay
exactly like the four enumerations that broke in this module tonight.

**The lane booked the omission against itself** -- *"your dispatch's Then section was in my context
the whole time; I filed it as background"*. Overruled: **a procedure that depends on every lane
remembering a preset list is the defect.** It found the hole; that is the arrangement working.

### Independently reproduced: the wrong-file grep, on a second lane's tree

B4 ran the control on its own tree:

```
deps =  in build.ninja               0
deps =  in CMakeFiles/rules.ninja  104
```

**Two lanes, same repository, same trap, found within the hour.** That moves it from an anecdote
about my carelessness to a property of this build system: `build.ninja` holds build statements and
`CMakeFiles/rules.ninja` holds rule definitions, so any grep for a rule-level token (`deps =`,
`command =`, `description =`) returns a confident zero from the file most people reach for first.

B4's framing of the remedy is the right one: **it is the positive marker in a third medium.** An
instrument states what it measured, not only what it objected to. It had applied that to gate
totals and not to greps; I had applied it to canaries and not to greps. **The principle does not
transfer between media on its own -- it has to be re-applied to each instrument by name.**

### Twice in one evening, a CORRECTION was the error rather than the rule

- **Mine:** *"`cl` … unaffected"* in `build-and-toolchain.md`. I had measured 314 `fastcache-cc`
  lines in `cl-debug`'s `build.ninja` and concluded the sentence was false.
- **B4's:** its own I2 justification, where the prose was wrong and the code was right.

B4's diagnosis of the pair is the one to keep: **both were caught by measuring the thing the
sentence was actually about, rather than a neighbour of it.** My 314 lines answered *does `cl` use
the cache*; the sentence was answering *can this defect reach `cl`*. Neighbouring questions,
opposite answers, and the measurement of the neighbour felt like evidence about the subject.

**So: before correcting a sentence, state in one line what question the sentence answers, and
check that your measurement answers that question and not an adjacent one.** A correction is a
claim, and it is subject to every rule a claim is subject to -- including this project's own
requirement that lanes verify the numbers handed to them in a dispatch. The corrections I issue
are handed to lanes the same way.

### The `onAbort` handler is load-bearing and nothing says so where a reader would look

Both `PASS_REGULAR_EXPRESSION` and `FAIL_REGULAR_EXPRESSION` are defeated by a raw `SIGABRT`.
Every canary property in this tree therefore depends on the `onAbort` handler converting the abort
into `_Exit(1)`. **That handler currently reads as tidiness.** A future cleanup removing "unused"
abort handling would silently convert every canary into a false pass -- the failure mode being
that the gate stops reporting, which reads as passed. Routed to B4: the statement goes in the
header beside the handler, not only in a report.

### A finding derived from a stale tree, which is the base problem in its third costume

I read `EventLoop.cpp` **in the shared checkout at `320a9ab`** and reported that
`notifyHandleClosing` asserts nothing. **`5d7a5ae` already contained B4's assert**, in a worktree
one `git show` away. I then ruled B5 to add a guard that existed, and B5 spent a rebase conflict
resolving the duplicate.

**The first two costumes were a lane writing against a stale tree. This one is the controller
FINDING A DEFECT in one**, which is worse in a specific way: a lane's stale edit shows up as a
conflict, and a stale finding shows up as a confirmed bug report that two lanes then act on.

**A finding is a claim and is subject to every rule this project applies to claims.** I had written,
within the hour, *derive the family from the code every time, including from this page* -- and then
derived a family from a checkout a commit behind the branch. **Read the ref you are making a claim
about, not the working tree you happen to be standing in.** `git show origin/master:<path>` costs
one command and is what I now use for every source read in a finding.

**What it produced anyway, which is why it was not worse:** B5's inverted-assert probe found that
only one of the two net binaries reaches the line, which neither of us would have measured
otherwise. **The accounting is that the duplication cost a conflict and bought a rule.** That is
not a defence; it is why this is recorded as a process failure rather than an incident.

### A label separated from its check by a `;` survives the check's failure

B5's conflict-marker check printed `(empty = none)` for a grep that **never ran**: the `git apply`
before it exited non-zero, the `&&` chain stopped there, and the `;` printed the label regardless
-- over two files that **did** contain markers.

**The most compact statement of the whole family yet.** The reassuring text and the thing it
reports on were never connected, and nothing in the output distinguishes "checked, clean" from
"never checked". The fix is the same one every time: **the label must be printed BY the check, not
beside it.** `foo && echo "clean"` and `foo; echo "clean"` differ by one character and by whether
the output means anything.

### The turn's numbered steps are not the order its work executes

My rulebook paragraph claimed a close performed by a timer callback lands *after* the turn's
`std::exchange` of `_closedParks` and is rescued by the next turn. Measured on master:

```
:255  drainReadyQueue          <- runDueCallback reached from here (:392)
:269  std::exchange(_closedParks)
:320  fireExpiredTimers        -> _ready.push_back(ReadyEntry{...})    QUEUES ONLY
```

**`fireExpiredTimers` never runs a callback.** It queues one, and the drain that runs it precedes
the exchange in the following turn -- so the close is caught by the turn that ran it. The second
reason for the assertion is independent after all.

**The error's mechanism is worth more than the correction: I read the sequence of the turn's
numbered steps as the sequence in which its work executes.** Step 5 comes after step 3, so
"something step 5 does" felt like it happened after the exchange. It does not; step 5 only files
work, and filed work runs in step 2 of the next turn. **A numbered list of phases invites exactly
this substitution**, and the paragraph has now been wrong three times tonight, each time caught by
someone deriving from the code rather than reading the prose.

### Crossed messages: every one was a party reporting the OTHER party's state as a fact

B5 and I crossed three times in twenty minutes; B4 and B7a once. The shape is identical every time
and it is symmetric:

| Who | Said | Truth at the time |
|---|---|---|
| me -> B5 | *"`origin/master` still `7bdd132`"* | `5d7a5ae`, pushed while the message was in flight |
| B5 -> me | *"your two rulebook sentences still cite `hasPendingWork`"* | both already fixed |
| B5 -> me | *"still holding for your window"* | window already open |
| B4 -> me | *"still outstanding: B7a's review"* | already sent |
| me -> B5 | *"`notifyHandleClosing` asserts nothing"* | it did, in `5d7a5ae` |

**Each party's information about its OWN state was current; its information about the OTHER's was
stale, and it was stated as a fact rather than as a question.** The last row is the expensive one,
because a stale claim about someone else's state, phrased as a finding, gets acted on by two lanes.

**Rule: report your own state; ask about the other's.** *"I am holding; is the window open?"* costs
the same as *"still holding for your window"* and cannot be wrong. The asymmetry is not a
communication problem -- it is the base problem again, where the shared thing that went stale is
the other party rather than a git ref.

**And the check is cheap on my side specifically:** before reporting a defect in code, read the
ref, not the tree (`git show origin/master:<path>`); before telling a lane it is blocked, read
`git rev-parse origin/master`. Both are one command, and both would have prevented the two rows
above that cost real work.

### CORRECTION: "exit 3 is the defect" is true for `assert`, FALSE for `invalid-parameter`

B4 retracted its own finding, which I had amplified. Verified in the source before accepting:
`SuppressWindowsDialogs.cpp:29` installs a **no-op** `_set_invalid_parameter_handler`, and the
canary's `invalid-parameter` mode does `strcpy_s(nullptr, 1, "x")` and returns
`ContinuedAfterFailure` when it fails. **Continuing IS the proof the handler is installed** --
dying there would mean it was not. The canary's contract is "no dialog", not "the process dies";
`TIMEOUT` enforces the shared half.

So the same string is the **failure** marker for two modes and the **pass** marker for the third,
and the registration now says so per mode.

**The three modes never shared a success condition, and `WILL_FAIL` hid that by accepting any
non-zero exit for all three.** B4's diagnosis: *a shared exit code is exactly the intermediary that
does not announce which mode's meaning it is carrying.* Same class as everything else tonight, with
the collapse performed by a person rather than an instrument.

**The new scheme earned its keep before it landed**: the positive-marker registration **failed a
correct behaviour on its first run**, which is the only reason the distinction surfaced. A negative
alternation would have gone green and left the mode's meaning undocumented forever.

Amends the stacked finding: that canary's three silences are still three, but silence 2 is
*"`assert` surviving reports as a pass"*, not *"exit 3 reports as a pass"*.

**B4 follow-up, batched:** all five canary markers verified on `stderr`; per-guarantee keying was
already correct (`iocp-canary: g1: …`, matched by `"${guarantee}: about to"`); the `onAbort`
load-bearing note is now in both canary sources beside the handler, not only in the registration.
**And its own first check raised a false alarm:** `grep -n "about to"` reported a marker as not on
`stderr` because the statement spans two lines with `stderr` on the second -- *a line-oriented
instrument giving a one-line answer about a two-line fact, inside the check for exactly that class
of instrument.* Next: mutate a marker away and confirm *required regular expression not found* on
the real binaries rather than carrying B7a's throwaway-project result across.

### Two readers of the same stale artifact agreeing is not corroboration, and it reads exactly like it

B5 refused my account of the `notifyHandleClosing` duplication as too generous to it:

> *"You derived the finding from the shared tree at `320a9ab` -- but I read the same stale tree,
> reached the same conclusion, and confirmed your finding as though independently."*

**That is the correct and harsher reading.** I had recorded it as *my* stale-tree error. It was
two people deriving from one stale base and calling the agreement evidence. **It cost only a merge
conflict because B4's version differed in wording rather than in behaviour** -- had it differed in
behaviour, B5 would have "verified" a finding already fixed and could have reverted the fix under
the authority of an independent confirmation.

**Rule: a confirmation is worth what its SOURCE independence is worth, not what its reader
independence is worth.** Before treating a lane's agreement as corroboration, ask which artifact it
read. Same file, same checkout, same ref -> **one observation, not two**, however many agents
looked at it. Genuine corroboration needs a different source: the ref instead of the tree, the
other binary, the inverted assert, the second platform.

This reframes several results tonight. The ones that held were cross-source -- B4's `gcc-release`
skip count against my CI-matrix derivation, B5's inverted assert against two different binaries,
B7a's throwaway ctest project against the documentation. **The ones that failed were same-source
agreement**, and they felt identical from the inside.

### Summarising an artifact instead of quoting it

I told B5 to drop line numbers from a comment. **The committed comment never had any** -- verified:
`git show 29e9b24 -- EventLoop.{cpp,hpp} | grep '^+' | grep -cE ':[0-9]{2,4}'` returns **0**, and
the shipped text names functions and states the ordering exactly as I prescribed. B5 had the
coordinates in its head from re-deriving the ordering and wrote them into its *message* as though
they were in the code.

**So I corrected the artifact it described rather than the one it committed** -- the night's
failure one level up, in the medium of a status report. Its own fix is the right one and applies to
every lane including me: **quote the artifact, do not describe it.** Three pasted lines would have
cost less than the exchange did.

### A rule written ahead of the code is unfalsifiable at the time it is written

B12's answer to "is my corrected rulebook text still wrong" is the sharpest epistemic point of
the session:

> *"The same words were false and then true without changing. The rulebook cannot be checked
> against itself, only against the tree at a stated commit."*

The paragraph was written while `29e9b24` was in flight. At `5d7a5ae` it was false in four
specific ways -- `requestStop` and `resumeSoon` did not wake, `registerPark` did not arm (so
`delay`/`sleepUntil`, which reach it rather than `addTimer`, armed nothing), and `schedule` woke
while the text named it nowhere. `29e9b24` made the identical sentences true. **Nothing about the
document changed; the tree moved underneath it.**

So a rule states the commit it was derived at, or it is a prediction wearing the grammar of an
observation. This is the same finding as *"a coordinate goes stale while nothing fails"* from the
other side: there, the document decayed against a moving tree; here, it **improved** against one,
which is worse, because a rule that becomes true by luck teaches nobody to check it.

### The brief was a partial copy of the rule, and that is what got measured

B12 did not read the rulebook. It read the **brief**, which carried **two** of the rulebook's four
bullets -- no `schedule` bullet, no `notifyHandleClosing` bullet -- because I wrote the brief
paragraph and the rulebook paragraph separately and only kept one of them complete. Measured:
`grep -c` for either name in the brief returned **0**, against **2** in the rulebook.

So the lane measured a two-bullet subset against the tree, found it wrong in four ways, and
reported **the rulebook** as broken. The rulebook was right.

**Rule: a brief names the rule and points at it; it never restates it.** A rule copied into a
second document is a second thing to keep true, and this project has now paid for exactly that
with one wrong enumeration propagating through a rulebook, a code comment and two briefs. It is
the same discipline B7a applied when it declined to write a rule to memory *because the repository
already records it and a memory would be a second copy that can drift*. **One authority per fact**,
and the brief's job is to say which authority.

### A measurement of a moving value carries its instant, or it is a claim about the past

I reported `fed98b4` as unpushed. The lane replied that my `origin/master` was a stale cached ref
and that `git ls-remote` would have shown the truth. **It was not stale** -- my command was
`git fetch origin -q && git rev-parse --short origin/master`, and `merge-base --is-ancestor`
answered **NO**. The remote really was `2ea0614` at the instant I asked. The lane pushed in the
window between my fetch and its reading my message, and my very next command fetched again and got
`fed98b4`.

**So both observations were correct and incompatible, and this is a different hazard from the one
we spent the night on.** A stale coordinate is *wrong when read*. This was **right when read and
wrong when quoted** -- the value moved between two correct measurements of it.

- **The remedy is not a better instrument.** It is stating the instant: *"`origin/master` was
  `2ea0614` when I fetched at 10:50"*, never *"`origin/master` is `2ea0614`". A measurement of a
  value that something else is changing is a claim about the past wearing the present tense.
- **The lane's `git ls-remote origin refs/heads/master` point stands anyway and is adopted.** It
  asks the remote and needs no local fetch, so it removes one of the two ways an auditor can be
  wrong -- *"the lane verifying and the lane checking the lane must both fetch, or the checker's
  negative is as unreliable as the lane's positive."* It would not have changed this outcome; it
  makes the check independent of my own fetch discipline, which is worth more than this instance.
- **And the lane's own false diagnosis is instructive**: presented with a disagreement it reached
  for the failure mode this session had trained it on, and that mode did not apply. **A well-known
  failure shape is the first hypothesis, not the conclusion.** Its own summary is better than mine:
  **"the diagnosis was stale, not the ref."** Dated from the reflog -- `2ea0614` was the remote head
  for twenty minutes (12:29:52 to 12:50:16) and my fetch fell inside that window.
- **It also corrected my reason for adopting `ls-remote`, and the correction is right.** It helps
  with *neither* decay nor race: it removes the auditor's own fetch discipline as a variable, which
  is a smaller and separate win. **Adopt it for that reason, not for the one either of us first
  gave** -- a remedy adopted for the wrong reason gets applied to the wrong cases later.
- **Decay and race belong side by side, not nested.** One is wrong when read and is fixed by
  deriving from the artifact; the other is right when read and wrong when quoted, and is fixed only
  by stating the instant. Nothing fixes both.

Also corrected in the same exchange: I told the lane four commits had landed since `c30f61a`. Two
had. It checked and said so.
