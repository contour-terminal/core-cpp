# Task B7c — the preset/CI gap, and the gate that makes it fatal

**Status:** complete. Two of the three new legs measured green locally; the third (`appleclang-debug`)
cannot be measured here and is the one flagged in §5.4.
**Worktree:** `D:/core-cpp-wt-b7a`, detached at `5d7a5ae`.
**Commit:** `2224192` — **pushed**, `29e9b24..2224192`, 7 files, +629/−3.
`origin/master` and local `HEAD` are the identical SHA and the tree diff between them is empty, so
it travelled byte-identical.

**How it got there.** Built at `c969e10` on base `5d7a5ae`; amended to `ea100ab` to add the
exact-match-gating rule of §7.5 to the rulebook; rebased onto `29e9b24` (B5's park fix) to
`2224192`. `29e9b24` shares exactly one file with this change set, `CHANGELOG.md`. I checked the
merge in memory before relying on it — `git merge-tree --write-tree origin/master HEAD` exited 0 and
named no conflict — and checked the result afterwards rather than trusting the rebase: the
`CHANGELOG.md` diff against `origin/master` **removes nothing** and adds only this entry, and
diffing against `29e9b24` shows none of B5's text gone. This change set touches no file under
`src/`.
**Shared checkout:** untouched. `D:\core-cpp`'s working tree and `master` ref were not written to;
this report is the only file added there, untracked, as the other lane reports are.

---

## 1. What was actually wrong

`CMakePresets.json` is what a developer reads to learn how this project is built.
`.github/workflows/` is what actually builds it. **Neither file refers to the other**, and nothing
in the tree compared them. So a preset could be offered, documented, used by a lane for hours, and
run by no job — with nothing red anywhere, because a configuration absent from CI does not fail
there. It is simply not present.

At `5d7a5ae`, three of the seventeen visible presets were referenced by no workflow, and **all
three were Debug**:

| preset | what was dark |
|---|---|
| `gcc-debug` | GCC with assertions. `clang-debug` covered Linux Debug for clang; `gcc-release` covered GCC's codegen; nothing covered their intersection. |
| `appleclang-debug` | the **only** Debug configuration macOS had. |
| `clangcl-debug` | the only Windows Debug leg with clang-cl. |

That all three were Debug is not a coincidence. A Release leg is the one somebody adds in order to
ship; the Debug leg is the one that has to be asked for.

### 1.1 A correction to my own framing, and to the number

I first wrote that "all 152 assertions in `src/core`" were compiled out of every macOS job. **That
number is wrong and I corrected it in all four files before committing.** It came from a grep for
`assert`, which is a shorthand for the thing rather than the thing. Counted properly at `5d7a5ae`:

| family | count | compiled out by `NDEBUG`? |
|---|---|---|
| runtime `assert(` | **30** | yes |
| `static_assert(` | 122 | **no** — compile time, fires in every configuration |
| `Require()` / `Guarantee()` | 11 | **no** — `src/core/Assert.hpp` calls `detail::fail()` unconditionally; they abort in Release too |
| `SoftRequire()` | 3 | partly — keeps its log, loses only its debug-only abort arm |

So the honest claim is **30**, and a grep for `assert` overstates the dark set by a factor of five.
I have written that trap down in `build-and-toolchain.md` next to the number, so the next person to
re-derive it does not repeat the mistake.

The corrected number is still the argument, because of *where* the 30 sit:

```
15  src/core/net/EventLoop.cpp        — 12 of them teardownIsSerialisedWithDispatch()
 3  src/core/net/windows/IocpBackend.cpp   (Windows-only; not part of the macOS claim)
 2  src/core/net/EventLoop.hpp
 2  src/core/tui/runtime/TuiRuntime.hpp
 2  src/core/async/AsyncQueue.hpp
 1  each: ReadyBatch.hpp, DeadlineTimer.cpp, TuiRuntime.cpp, LogStore.hpp,
          SuppressWindowsDialogs.hpp, log/Assert.hpp
```

**19 of the 30 are in the shared event-loop code**, including the twelve
`teardownIsSerialisedWithDispatch()` thread-affinity checks and `ReadyBatch`'s re-entrancy trap.
kqueue is macOS-exclusive, so those shared checks had never once been evaluated with kqueue
underneath them — on the platform Ruling R101 exists because of. Both canaries also abstain with
exit 77 under `NDEBUG` (`IocpCanary.cpp:154`, `HostDrivenCanary.cpp:87`), so macOS ran neither.

I also dropped the "every G1–G5 guarantee" phrasing from the macOS argument: G1–G5 are asserted in
`IocpBackend.cpp`, which is Windows-only and never built on macOS. It was true of the sentence and
false of the platform.

---

## 2. The three legs

`.github/workflows/build.yml`:

- **linux** gains `- { name: gcc-14-debug, os: ubuntu-24.04, preset: gcc-debug, compiler: gcc, gcc: "14" }`
- **macos** gains `- { name: appleclang-debug, preset: appleclang-debug }`
- **windows** matrix becomes `[cl-release, clangcl-release, cl-debug, clangcl-debug, cl-release-tls]`

Each carries a comment saying what it covers that no other leg does, so the next person to prune
the matrix for time has the reason in front of them.

**One change that is not a leg and matters as much** — now also a rule in
`.agent/rules/build-and-toolchain.md`, because it generalises past this change: *a setup step gated
on an exact preset name breaks the moment the matrix grows, and it breaks green.* The tell is a
step whose `if:` names a single preset while the thing it installs is needed by a family; when
adding a preset to a matrix, read every `if:` in that job and ask which were written as "the only
one" rather than "this kind".

The LLVM-version install step was gated on
`matrix.preset == 'clangcl-release'`. Adding `clangcl-debug` to the matrix without widening that
would have produced a leg that runs, passes, and tests **the wrong compiler** — the runner's
bundled clang-cl, silently below the project's floor of 22. It is now
`startsWith(matrix.preset, 'clangcl')`. A green leg on an unintended toolchain is worse than no leg,
because it also carries a claim.

---

## 3. `gcc-debug`: the call you asked me to argue

**I added it.** Both directions honestly:

**Against.** It is the weakest of the three. `clang-debug` already runs Linux Debug with assertions
on, and `gcc-release` already compiles every line with GCC. Between them, every assertion has been
*evaluated* somewhere and every line has been *compiled by GCC* somewhere. A third Linux leg costs
runner time on the busiest platform for a combination that looks like the intersection of two
things already covered, and "a leg per cell of the matrix" is how CI becomes unaffordable.

**For, and why it wins.** The two premises are each true and the conclusion still does not follow,
because for a coroutine library GCC is not a second front end — **it is a second implementation.**
Clang and GCC lower coroutines differently: frame layout, when the promise is constructed relative
to parameter copies, and what survives a suspension point are all implementation choices, not
spelling. `core::async` and `core::net` are built almost entirely out of that machinery. So an
assertion reachable only under GCC's frame layout was, before this change, **compiled out of every
job that ran GCC and absent from every job that had assertions.** That is not an intersection of
two covered things; it is a cell no leg could reach.

The other two legs are not close calls and I record that plainly: `appleclang-debug` was the only
Debug configuration an entire platform had, and `clangcl-debug` was the only Windows Debug leg on
the driver whose depfile path is the one fastcached#1531 breaks.

---

## 4. The gate

### 4.1 `scripts/check-preset-coverage.py`

The rule is stated as a **property**, not a list — a list here would decay exactly like the
enumerations this module has already had to correct:

> **Every visible configure preset is named by a workflow, or is allowlisted with a written reason.**

It refuses in **three directions**, and the second and third are the ones that rot:

1. a visible preset no workflow names and the allowlist does not excuse — *runs nowhere*;
2. an allowlist entry for a preset a workflow **does** now run — a **stale exemption**, which makes
   the allowlist look maintained while the next preset to go dark inherits its credibility;
3. a workflow naming a preset `CMakePresets.json` does not define — a rename or typo, which
   otherwise fails at run time, on one platform, the slowest possible way to learn it.

An allowlist entry's reason is **required and checked for emptiness**: a bare name records that
somebody once decided something and gives a later reader nothing to review. `ALLOWLIST` is
currently empty — every one of the seventeen presets is now genuinely run.

Hidden presets are out of scope: they cannot be configured by name, so there is nothing for a job to
run. Visibility is read from `CMakePresets.json` and never inferred from a name.

**Two instrument guards, because a checker that read nothing reports the same "0 problems" as a
clean tree.** `verify_parse()` refuses a run that found no visible presets, or that found no preset
referenced by any workflow. Both are the failure mode where the regexes stop matching after a
workflow-syntax change and the gate silently starts passing everything.

**Comments are stripped before anything is matched** (Ruling R81's hazard in this file's terms). A
comment reading `preset: clang-debug` would otherwise be taken for a leg that does not exist — and
that is the dangerous direction, because it *invents* coverage. The residual error, stripping a `#`
inside a quoted scalar, is the safe direction: it reports a preset as uncovered rather than
inventing a job for it.

Three spellings count as a reference — `--preset <name>`, `preset: <name>` (matrix `include:`), and
`preset: [<names>]` (matrix list). `${{ matrix.preset }}` is deliberately **not** matched: it is an
indirection, and the names it resolves to are the matrix entries already read.

### 4.2 The self-test

`scripts/check-preset-coverage-selftest.py`, 15 cases, in the shape the other `check-*` scripts
use, loading the checker by path via `importlib` because its name has hyphens.

**The fixtures are written, not the real tree read.** A self-test that passes only while this
repository happens to be consistent proves nothing about the checker and would go red for reasons
that have nothing to do with it — the next lane to add a preset would be told its self-test broke.

Each rule the checker states gets a fixture that violates exactly that rule and nothing else, plus
the agreeing cases. Three cases are about the **instrument** rather than the comparison: the empty
preset list, the zero-references case, and the comment that must not invent a leg. Two more drive
the real command line in a subprocess to pin the **exit-status contract** (0 / 1), since that is
what ctest reads and it is not exercised by calling `check()` directly.

### 4.3 Registration

`tests/CMakeLists.txt` registers `core-cpp.preset-coverage` and
`core-cpp.preset-coverage-selftest` with `LABELS "core-cpp;hygiene;tree-level"`, `TIMEOUT 60`, and
the established SKIP fallback when Python is absent.

Both are `tree-level`: their input is the source tree, so their answer cannot differ between
platforms. Per Ruling R96 that obligates a `style` job step for each, and both were added —
`check-tree-level-coverage.py` now reports **"15 tree-level check(s), each covered by a style job
step."** Without those steps the checks would have been excluded from every per-job `ctest` and run
by the `style` job never: a gate that runs nowhere, which is the exact defect this task is about.

### 4.4 Documentation

- **`AGENT.md`** — the Building section now states the property and says plainly that the preset
  table and what CI runs are two different lists. The Testing section names
  `core-cpp.tree-level-coverage` as the enforcer of the `style`-step obligation, so that rule is
  enforced rather than remembered.
- **`.agent/rules/build-and-toolchain.md`** — the property, the three directions, what it cost
  before it existed, and the counting trap from §1.1.
- **`CHANGELOG.md`** — one entry under Added.

---

## 5. Verification

### 5.1 The gate fires on the real tree

Asserting a check passes is worth nothing unless it has been seen to fail. Removing `clangcl-debug`
from the windows matrix produced **exactly one** failure, naming that preset and no other. Restoring
it produced:

```
check-preset-coverage: 17 of 17 visible preset(s) are run by a workflow, 0 allowlisted
```

### 5.2 `clangcl-debug` — the new Windows leg, measured

The pre-existing `clangcl-debug` tree predated the rebase, so B4's headers had changed underneath
it. Rather than trust `--clean-first`, **I deleted the tree and built from scratch**, which removes
the fastcache-cc depfile hazard entirely instead of working around it.

```
CONFIGURE_EXIT=0     BUILD_EXIT=0     [531/531]     CTEST_EXIT=0
100% tests passed, 0 tests failed out of 38
tree-level = 15 tests      hygiene = 18 tests
```

531 of 531 steps is the tell that this was a real compile and not an inherited pass. Zero warnings;
the single `grep -i warning` hit is `PEDANTIC_COMPILER_WERROR` inside a CPM argument list.

**So `clangcl-debug` is green and will not go red when CI first runs it.**

### 5.3 `gcc-debug` — the new Linux leg, measured

Built and run in WSL on this same worktree:

```
configure: clean      build: [528/528], exit 0      ctest: exit 0
100% tests passed, 0 tests failed out of 34
tree-level = 15 tests, including #32 core-cpp.preset-coverage and #33 …-selftest
```

34 rather than Windows' 38: the four Windows-only canary cases are not registered on Linux.

`CORE_CPP_WERROR=ON` is in this tree's cache (and in `clangcl-debug`'s), so **exit 0 is itself the
proof of zero warnings** — a warning would have been an error. I record that explicitly because my
first pass filtered the build log with `grep -iE 'error|warning'` and got hits, all of which were
filenames such as `PlatformError_test.cpp.o`. The filter, not the build, was the thing that looked
alarming; `WERROR` is what actually answers the question.

**So `gcc-debug` is green and will not go red when CI first runs it.**

### 5.4 `appleclang-debug` — not measured, and I cannot measure it

**I have no macOS host.** This leg has never run in the project's history and I am adding it
sight-unseen. Per your instruction, if it goes red the failures are to be reported rather than
fixed, and **it is the one of the three most likely to**, precisely because of §1.1: it is the first
time the shared event-loop assertions will be evaluated with kqueue underneath them. A first red
there is a finding about kqueue, not a regression introduced by this change, and the PR body must
say so or someone will read it as one.

### 5.5 Gates

`clang-format --check` and `python-style` clean over the touched paths; `mkdocs build --strict`
clean; hygiene 18/18 on `cl-debug` and 18/18 within the 38 on `clangcl-debug`.

---

## 6. Consumer impact

**None.** No public header, no exported target, no CMake option, no runtime behaviour changed. The
change set is CI configuration, two new developer scripts, two ctest registrations, and prose.
Consumers see a project whose CI covers three configurations it did not cover before.

---

## 6a. For the PR body — three things a reader must not conclude on their own

Ruled by the team lead and recorded here so they are not lost between the commit and the notes:

1. **"17 of 17 covered" must not be read as "17 of 17 tested."** The gate checks that a preset is
   *named* by a workflow. A leg that builds and never runs `ctest` counts as covered. Presence was
   the hole; meaningfulness is a different check that does not exist.
2. **If `appleclang-debug` goes red on its first run, that is a finding about kqueue, not a
   regression from this change.** The leg has never run in the project's history. It is the first
   time the shared event-loop assertions will be evaluated with kqueue underneath them, which is
   the entire point of adding it. Nobody should read a first red there as something this commit
   broke.
3. **`gcc-debug` is not a third Linux leg for symmetry.** It covers a cell no existing leg could
   reach — GCC's coroutine lowering *with assertions compiled in* — not the intersection of two
   things already covered. §3 has the argument.

## 7. Concerns

1. **`appleclang-debug` has never run and I could not run it.** §5.4. Highest-probability red of the
   three, for a reason that would be a genuine finding.

2. **This gate compares two lists; it does not check that a leg is meaningful.** A preset referenced
   by a workflow that builds but never runs `ctest` counts as covered. That is a deliberate
   boundary — the check is about presence, and presence was the hole — but it means
   "17 of 17 covered" should not be read as "17 of 17 tested".

3. **The wrong-by-5× assertion count nearly shipped in four files.** It was mine, I caught it only
   by re-deriving the number instead of reusing it, and it had already been written into a rule
   file where it would have become the citable source. The generalisable form is in
   `build-and-toolchain.md` now, but the near miss is worth more than the fix: a number that
   travels between documents stops being measured and starts being quoted.

4. **The `style` job is now the single point of failure for 15 checks.** `tree-level` is the right
   design, but the blast radius of that one job being skipped, renamed or made non-required is now
   15 gates rather than 13. `check-tree-level-coverage.py` enforces the pairing; nothing enforces
   that the `style` job itself is a required check. I did not touch that — it is
   `ci-ok`'s `needs:` list, outside this task — but it is the next link in the chain.
   **Disposition: the team lead is filing this rather than growing this task.**

5. **Three Debug legs are added at once, on three platforms, none of which has run them together.**
   `gcc-debug` and `clangcl-debug` I measured locally; the interaction I cannot measure is runner
   time. If the `macos` job now exceeds its 45-minute `timeout-minutes`, that surfaces as a timeout
   rather than as a test failure, and should be read as a budget problem, not a correctness one.
