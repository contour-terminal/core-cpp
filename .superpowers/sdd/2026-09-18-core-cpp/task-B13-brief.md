# Brief for Task B13

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


**Every B task follows this pattern:**
1. Port the named tests from `D:\fastcached\src\FastCache\{Async,Net}` (renamed per the Part I §2 rename map, into core names) and/or adapt the contour tests.
2. Build and confirm the new cases FAIL.
3. Implement.
4. Confirm PASS on Windows (`clangcl-debug`, `cl-debug`) and WSL (`clang-debug`, `gcc-debug`, `clang-tsan`, `clang-asan-ubsan`).
5. Push, and require CI `ci-ok` green.
6. Commit.

**Sources:** every fastcached path named in Phase B is read as `git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show origin/master:<path>`, never from the `D:\fastcached` working tree.

Implementation must preserve the lifetime rules in `.agent/rules/wire-and-protocol.md` of fastcached `origin/master` (§Sockets, §Dialing and the reactor, §Socket and coroutine lifetime) and `D:\fastcached\AGENT.md` (grep: lifetime, ParkedWork, teardown, IOCP). Each rule carried over is written to `.agent/rules/async-and-net.md` in the same task that implements it.


### Task B13: Gates, docs, release v0.1.0
- [ ] Port fastcached's `reactor-teardown-gate`, `check-cancel-read-declared` and `check-read-buffer-guard` (with self-tests) into `tests/cmake/`, naming core files. Add `tests/cmake/check-layering.cmake`, which reads the module table and refuses an include edge that is not in it.
- [ ] Write the docs: `docs/design/coroutines-and-lifetimes.md` (rules + `fastcached#NNN` URLs), `docs/design/threading.md` (G1–G5, teardown order), `docs/modules/{coro,net,tui}.md`, `docs/provenance.md`. Run `mkdocs build --strict`.
- [ ] Update `CHANGELOG.md` `## [0.1.0]`: imports with SHAs, the merged design, the Breaking notes for contour/endo/tuidu callers.
- [ ] Release (outward-facing, authorized by the plan): run the **contour-workflows:draft-release** skill. Confirm the `release.yml` draft includes `core-cpp-v0.1.0-vendor.tar.gz` and `SHA256SUMS`, then run **contour-workflows:publish-release**. Verify with `gh release view v0.1.0 -R contour-terminal/core-cpp`.


---

# An audit owed to you, and a procedure to land while you are in the gates

## The audit: every earlier lane's "clang-tidy clean" is unverified

On one gate, in one hour, two lanes produced a false clean **three different ways**: a log read at
179 of 526 files; a gate never started and reported against a remembered list; and a run whose
analyser was **absent**, printing `warnings=0` from a tool never invoked.

**None of the Phase A or Phase B lanes recorded which binary answered.** The one result known good
is B4's main round — it **found three real defects** (`_outermost` member-initializer ordering,
deleted members under `protected`, a `std::move` of a trivially-copyable struct), and an absent
analyser finds zero. Every other "clean" on that gate is a string with no instrument behind it.

**So: run `clang-tidy` once over the whole tree at your base, with the analyser proven, and report
the finding count as a number.** If it is zero, the audit closes and the record is worth having. If
it is not, those findings have been latent across the merge and belong in your round.

Measured, so you need not re-derive it:

```
wsl -d Ubuntu-26.04 -- bash -lc 'command -v clang-tidy'  ->  ~/.local/bin/clang-tidy   FOUND
wsl -d Ubuntu-26.04 -- bash -c  'command -v clang-tidy'  ->  NOT ON PATH
~/.local/bin/clang-tidy --version                        ->  LLVM 22.1.8  (matches the pin)
```

**Login, not interactivity, is the variable.** A nested `bash -c` or `cmake -E env` inside an `-lc`
re-enters a non-login shell and loses PATH again. A configure that cannot find the analyser fails
with `CORE_CPP_CLANG_TIDY is ON but no clang-tidy was found`, which a script checking only the
*build* exit code reports as zero warnings.

## The procedure to make permanent

**A null result is indistinguishable from a non-result unless the instrument proves itself.**
`0 findings` reads identically whether the analyser ran clean, ran on nothing, or never ran.

B4 is landing the prose form in `.agent/rules/build-and-toolchain.md` in a follow-up commit —
record `tidy-path`, `tidy-version` and the pin; delete the tree; refuse to report without
`build.ninja`. **Your job is to make it a gate rather than a habit.** The existing rule *a gate that
does not report reads as passed* has never had an enforcing check, and this is the shape one would
take: a check that refuses a tidy result carrying no instrument record. It is `tree-level` only if
its input is the source tree — if it reads a build artefact, it is per-leg, and adding a
`tree-level` check means adding a `style` step for it or it runs nowhere in CI.

---

# Moved to you off B5's round: the loop-thread-only assertions have no case, and cannot have one from inside Catch

B5 declared this honestly rather than implying coverage, and I took it off its round because **a
third canary mode is infrastructure, not a timer fix**.

`addTimer` and `cancelTimer` are loop-thread-only and say so with `assert`. **An `assert` cannot be
observed from inside a Catch2 case — it aborts the binary**, so the case that would prove the
assertion fires is the case that takes the whole test run down with it. The tree's existing answer
to that shape is a separate canary process (`HostDrivenCanary.cpp`, and the Windows
dialog canary before it), which is why this is yours: adding a third canary mode is gate work.

**`WILL_FAIL` is gone from every registration in this tree, removed by `a48e727` — do not
reintroduce it.** A canary is now judged by a **marker it prints immediately before the forbidden
operation**, matched with `PASS_REGULAR_EXPRESSION`, plus `FAIL_REGULAR_EXPRESSION` for the case
where it continued past that operation. Three things you will otherwise get wrong, each measured
rather than reasoned:

- **`PASS_REGULAR_EXPRESSION` ignores the exit code**, so it *replaces* `WILL_FAIL` rather than
  accompanying it — and `SKIP_RETURN_CODE` still survives beside it, so Release legs abstain rather
  than fail. That last one was an open question until a throwaway ctest project answered it.
- **The marker goes to `stderr`.** The `onAbort` handler calls `_Exit`, which flushes nothing, so a
  `stdout` marker is lost on exactly the abort path it exists to prove.
- **Both regex properties are defeated by a raw `SIGABRT`**, so the `onAbort` handler converting it
  to `_Exit(1)` is load-bearing rather than tidiness. Both canary sources now say so beside the
  handler; keep that note if you touch them.

**Read the registrations `a48e727` left in `tests/CMakeLists.txt` and `src/core/net/CMakeLists.txt`
rather than this paragraph** — it is a description, and a description of an artifact decays while
the artifact does not.

**And you own the cleanup that proves the point.** `a48e727` changed the registrations and left
**nine doc comments describing the scheme it replaced**, so the code and its own documentation
disagree today:

```
src/core/net/EventLoop.cpp:521              src/core/net/windows/IocpCanary.cpp:20,23,24
src/core/net/HostDrivenCanary.cpp:8,17,18   src/core/net/windows/IocpBackend_test.cpp:25
tests/WindowsDialogCanary.cpp:4,12
```

Not folded into `a48e727` because two lanes held uncommitted work across `src/core/net/` when it
was found. **Leave the comments that explain the replacement** — the `PASS_REGULAR_EXPRESSION …
REPLACES WILL_FAIL` notes in the three `CMakeLists.txt` files and the "Load-bearing for the ctest
registration" blocks in both canary sources are correct and are the reason the property is
understood. Fix only the ones that still assert the registration *is* `WILL_FAIL`. **Re-derive the
list yourself** — it was produced by one grep and this brief is a description of it.

Two facts from B4's round that bear on how you build it:

- **A canary registered with a bare `add_test()` lands in every consumer's ctest suite.** That is
  what `a040878` fixed — `core_cpp_add_test` returns early without `CORE_CPP_TESTING`, and a bare
  `add_test()` never sees that gate, so a CPM or vendored consumer got a red naming an executable
  their build never made. **Whatever you add must go through `core_cpp_add_test`.**
- **A canary must be proved able to die.** `216d1d1` fixed a Windows canary that **hung** instead of
  failing, because it did not link `core::testing_dialogs`. A canary that hangs reads as a timeout,
  and one that cannot reach its mechanism reads as a pass under any exit-code scheme. **Prove it by
  mutation, on the real binary**: remove the marker with the assertion intact and confirm
  *"Required regular expression not found"*, then restore and confirm the source is byte-identical.
  A throwaway project shaped like the real one proves the ctest semantics, not that your binary
  reaches its mechanism — those are different claims and both are owed.
  core-cpp#11 asks for exactly this on the `cl-debug` leg.

Under `NDEBUG` the assertions vanish, so the canary skips on Release presets. **`SKIP`, never
`SUCCEED`** — `clangcl-release` already skips the existing canaries for this reason.

---

# A case that does not exist and would have caught B5's Critical on its own

B5's sharpening of B4's adjacency rule, which is better than the rule:

> **A member that files work asks the backend for the turn that will run it.**

**That sentence reached this brief in a form that was false, and it is the fourth copy of the same
false list.** It read *"`post`, `submit`, `schedule`, `spawn`, `requestCancel` and `stop` all wake
the backend; `addTimer` was the sixth member of that family and the only one that did not."*
`registerPark`, `resumeSoon` and `requestStop` did not either, and `registerPark` is `addTimer`'s
own implementation path — so the enumeration was wrong, the fix it recorded sat one level above the
primitive, and the wrong list propagated from the rulebook into a code comment, then into two task
briefs. **Every copy read exactly like an audit.**

**What is true on master**, and the shape your case must encode:

- **Ready work that carries no time wakes** — `post`, `submit`, `spawn`, `stop`, `requestStop`,
  `requestCancel`, `resumeSoon` call `_backend.wake()`.
- **A park filed with a time arms** — `registerPark` calls `armHostWake()`, and `addTimer`, `delay`
  and `sleepUntil` inherit it through that one primitive. `wake()` is `scheduleAt(now)`, so waking
  for a park would discard its deadline; that is not a style choice and a test caught it as
  `0 == 50`.
- **`schedule` off the loop's thread wakes, and that is the rule rather than an exception**: what it
  filed is an inbound entry and the turn draining it is ready now.
- **`notifyHandleClosing` files work and asks for nothing**, soundly — the turn exchanges
  `_closedParks` before the wait. It is the one exception and your case should assert it
  deliberately rather than omit it.

**So: derive the family from `EventLoop.cpp` yourself before you write the case, and say in your
report what you found.** If it disagrees with the four bullets above, the bullets are what is
wrong — that has now been true four times. **One parameterised case** over a `HostDrivenBackend`
with `testing::ManualHostScheduler`: call each member and assert the host is asked for a turn, with
the arming members asserting the *deadline* they armed for rather than merely that something was
asked.

**B4's caution, which the next writer would not derive:** `spawn` wakes the backend but is
**loop-thread-only** and now asserts `teardownIsSerialisedWithDispatch()`. *"Call each from off the
loop thread"* trips that assert for `spawn` specifically — the case then fails for the wrong reason
and gets weakened until it proves nothing. **It wants `spawn` called off-turn-but-unopposed.** The
invariant the six share is not *"safe from any thread"*, it is *"the loop is asked for a turn"*. **It fails on exactly one
of the six against `fe48143`**, which is the best shape a regression case can have — and it would
have caught B5's Critical without anyone having to notice the family existed.

Why it is yours rather than B5's or B4's: the family spans `EventLoop.{hpp,cpp}` (B4's file) while
the defect was in timers (B5's), so it belongs to neither lane and to the gates. Adding it to
either lane's round turns a fix round into a new task.

**The general rule it instances, which is worth more than the case:** *when a function joins a
family that all do X, "why does this one not do X" must be answered out loud* — and where the family
is enumerable, answered by a parameterised test rather than a comment. Look for other families while
you are here: the loop's six mutators that now `assert(teardownIsSerialisedWithDispatch())` are a
second one, and they have no case either (see the canary item above).

---

# A rulebook audit, and the worked example that earns it

B4 went to add a clang-tidy procedure to `.agent/rules/build-and-toolchain.md` and found its new
bullet landing in a section **already titled** *"A gate that does not report reads as a gate that
passed"*, whose last bullet **already said** *"A diagnostic that never ran and one that ran and found
nothing are the same green."*

**The rule was there, correct and precise, and it prevented none of four failures in one hour:** a
gate never started; an analyser absent while the script printed `warnings=0`; a log read at 179 of
526 files; and a finding-counting regex that reported `0` twice while the analyser worked perfectly,
inside the instrument built to catch the first three.

**A rulebook entry that states a principle with no executable step is a sentence people agree with
and do not execute.** So: **audit `.agent/rules/` for principle-without-procedure**, and for each
one either add the step or record that no mechanical step exists. Two candidates visible from here
beyond the worked example — *"assert what distinguishes"* in `testing.md` and *"inject every ambient
resource"* in `design-principles.md` — but derive the list from the files, not from this paragraph.

The shape a procedure takes, from the case that produced the finding:

- **Count the work, print the findings.** A count of work done (`lines_in_log=535`) is evidence the
  instrument ran and cannot be silently zeroed; a count of *findings* passes through a regex, which
  is exactly where `warning:` versus `error:` zeroes it in silence.
- **Prove the instrument with a mutation, not a version check.** A version check proves *a* tool
  exists; a fed violation proves the right tool ran on your code and that its findings are fatal.

# Addendum (2026-09-23): items routed to B13 since this brief was written

Master is f6d669a (B6d, B9, B10, B11, B7b landed). Fix or rule on each; none is optional without a written ruling.

1. **core-cpp#41**: cancelPending leaves a readiness park attached. Fix before v0.1.0, test first.
2. Triage core-cpp#35 (HttpServer closes only via destructors), #39 (style job wiring), #40 (mojibake gate + banned-token comment), #42 (toolchain-install retries), #43 (ParkId header weight): fix, or label post-0.1.0 with a one-line reason on the issue.
3. Nine stale `WILL_FAIL` doc comments (WILL_FAIL was removed tree-wide in a48e727): grep `WILL_FAIL` across src/ tests/ docs/ .agent/ and correct each.
4. Re-decide the EventLoop.cpp canary justification (see the B6 note in progress.md).
5. B12 re-review (task-B12-rereview1.md, H1): add the regression case that review showed is writable, and correct H1's justification comment.
6. Five pre-existing clang-tidy findings visible only on Windows: WindowsSocket.hpp:128, platform/Types.hpp:33 (x2), windows/DialPrimitives.cpp:101, SocketsWin32.cpp:92.
7. Error tables: PosixSocket and WindowsSocket still classify errors themselves. Move them onto detail/SocketErrors (B9's classifySocketError), as B7b did for the IOCP socket. The SocketClosedStates parity test must stay green.
8. posix/TlsCancelRead_test.cpp: its read-driven case was made POSIX-only because WindowsSocket had no cancelRead. IOCP (with cancelRead) is now the Windows default, so bring it back to every platform if it passes on IOCP.
9. `check-cancel-read-declared` port (already in item 1 of the brief; B11 noted its grep missed it).
10. Master CI must be green on the final head, with every job counted against the workflow list, before the release step.
