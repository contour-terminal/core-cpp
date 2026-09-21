# Testing

Rules about how tests are registered, what they may assume, and the ways a suite has
previously reported a defect as something else. Read this before adding a test binary, a
script-driven test, a test that waits on another thread, or a test double.

Most of these rules were paid for in fastcached; the full measurements are in
[fastcached `.agent/rules/testing.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/testing.md)
(cited below as "fastcached testing").

## How core-cpp's tests are registered

- **Tests sit next to their sources:** `Foo_test.cpp` beside `Foo.cpp`, in the module's
  directory. A module has one binary named after it, `core-cpp-<module>-test`, and one more for
  each **ground** it has to split. What follows is the grounds and not a list of the binaries, and
  carries no count of either: a count is a completeness claim, nothing checks one, and this bullet
  used to state the reason for a single split in the voice of a general rule — the splits that
  contradicted it were each added by a lane that did not own this sentence.
  - **A definition that changes what the module's headers declare.** Such a definition holds for a
    whole program or for none of it, never for some of its files, so the cases that need it are a
    program of their own (`CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK`).
  - **A narrower or conditional target.** A binary may link one of the module's other targets
    instead of `core::<module>` — `core_cpp_add_test(net NAME net_types ...)` — and a target that
    exists only under an option (`CORE_CPP_WITH_TLS`) cannot share a binary with one that always
    does.
  - **A platform or label scope that differs.** A label and a `TIMEOUT` are properties of a
    BINARY, so cases that must not carry the module's `loopback` label or its timeout, or that
    build where the rest of the module does not (Emscripten included), can only be given different
    ones by being a binary of their own.

  Which ground a split rests on is said where the split is, in the module's `CMakeLists.txt`, and
  that is the only place it is said: a reason recorded twice is a reason that can disagree with
  itself.
- **`core_cpp_add_test(<module> SOURCES ... [LIBS ...] [LABELS ...])` registers it**
  (`cmake/CoreCppTargets.cmake`): it links `core::<module>` and `core::testing_main`, registers
  the ctest `core-cpp.<module>`, and sets `SKIP_RETURN_CODE 77` and the labels `core-cpp` and
  `<module>`. `NAME <name>` makes it the second binary, `core-cpp-<name>-test` and ctest
  `core-cpp.<name>`, with the same labels; `DEFINITIONS` are that binary's alone. A `NAME` that
  is a target of the module (`core_cpp_add_test(net NAME net_types ...)`) links that target
  rather than `core::<module>`, and builds only where its row in the module table says the target
  does: `core-cpp.net_types` runs under Emscripten, `core-cpp.net_tls` only with
  `CORE_CPP_WITH_TLS`.
- **Labels:** `core-cpp` (everything), the module's name, `hygiene` (checks over the tree and
  the build contract), `canary` (a program that must fail), `loopback` (needs a loopback
  socket), `tree-level` (its input is the source tree, so its answer cannot differ between
  platforms). There is no ThreadSanitizer exclusion: every test in the suite runs under TSan, and
  the `no-tsan` label that once promised one was carried by nothing (core-cpp#32). Do not
  reintroduce it for a test that is merely slow or flaky under TSan — that removes coverage from
  the tool which has already caught a real data race in this repository.
- **`tree-level` decides where a check runs, so it carries an obligation.** CI's per-job `ctest`
  excludes it and the `style` job runs exactly those, once — one violated provenance row used to
  report as 22 failed jobs out of 24, which is correct behaviour and unusable triage. A labelled
  check therefore needs a `style` step declaring `# covers: core-cpp.<name>`, or it is excluded
  everywhere and run by nothing, and an absent check looks exactly like a passing one.
  `core-cpp.tree-level-coverage` refuses that in both directions, but know the rule as well as the
  gate: a gate tells you that you broke it, the rule tells you what it is.
- **Script-driven checks are `cmake -P` scripts under `tests/cmake/`**, registered in
  `tests/CMakeLists.txt`, each with a self-test that proves every rule it states refuses.

## Exit codes, and the SKIP problem they solve

**`core::testing_main` normalises Catch2's exit status** (`src/core/testing/ExitCode.cpp`):

| Outcome | Exit status |
|---|---|
| any assertion or test case failed | 1 |
| every test case that ran was skipped | 77 |
| no test case ran and Catch2 said so | 2 |
| nothing failed, but Catch2 still reported an error | 1 |
| otherwise | 0 |

ctest's `SKIP_RETURN_CODE 77` then scores an all-skipped binary as skipped. The contract is
asserted from outside the binary by `tests/cmake/check-exit-codes.cmake`, which runs a fixture
once per outcome, under node too in the Emscripten build.

Why it is a separate `main()` rather than a ctest property: Catch2 3.8's own `main` returns 4
when every case skipped and 42 when anything failed, earlier releases returned the
failed-assertion count, and ctest reads every non-zero status as a failure. fastcached first
registered `SKIP_RETURN_CODE 4`, which scored a case failing exactly four assertions as
*skipped* inside "100% tests passed", in every test binary. It then measured every other
channel ctest offers and found each one worse: every exit status was already taken by a failure
count, and a `SKIP_REGULAR_EXPRESSION` over the output shares its channel with test names and
messages and cannot be anchored through `cmd.exe`. Normalising the status in `main()` is the fix
that ticket arrived at and could not ship; core-cpp starts with it. Origin:
[fastcached#499](https://github.com/LASTRADA-Software/fastcached/issues/499),
[fastcached#1128](https://github.com/LASTRADA-Software/fastcached/issues/1128),
[fastcached#1152](https://github.com/LASTRADA-Software/fastcached/issues/1152).

**A consumer that registers its own Catch2 tests links `core::testing_main` and copies the
property**: `get_target_property(code core::testing_main CORE_CPP_SKIP_EXIT_CODE)`.

## Every wait is bounded, and says what it was waiting for

- **A wait that spins on a condition a regression never satisfies hangs instead of failing**,
  and a ctest timeout that names nothing is the least useful way CI can report a defect. Every
  wait on another thread or process has a bound.
- **A bound is an assumption about the machine.** A budget that passes on a warm laptop encodes
  that laptop; write it for a cold two-core runner.
- **A wait that runs out says which kind of failure it was:** what it waited for, how long, the
  state at the end, and whether that state was still moving. A loaded machine and a wedged
  process are fixed in different places; a wait that cannot tell them apart gets its timeout
  raised, because that is what makes the red go away. Where the signals disagree, say
  "inconclusive" and name what is missing.
- **Bound it on a monotonic clock, never by counting sleeps.**
- **Catch2's assertions belong to the thread that runs the case.** A helper thread keeps its
  own account and the case asserts it after the join.
- Origin: fastcached testing, the introduction and "A bounded wait must also say WHICH KIND of
  failure it was"; [fastcached#1446](https://github.com/LASTRADA-Software/fastcached/issues/1446).

## A wall clock is not a duration

- **`std::chrono::system_clock` can be stepped**, in both directions, by time sync, so an
  interval computed from two of its readings is not a lower bound, not an upper bound, and can
  be negative. Durations use `std::chrono::steady_clock`, or in core-cpp an injected
  `core::platform::IClock`.
- **A negative interval does not read as an error; it reads as the best answer.** Handed to a
  table of ascending thresholds it lands in the first row, and the check passes, confidently
  wrong. The fix is a third outcome, never a clamp, which restores the wrong answer by the front
  door.
- Origin: [fastcached#1313](https://github.com/LASTRADA-Software/fastcached/issues/1313).

## A case name is an argument

- **ctest and Catch2 receive a case's name on a command line**, and Catch2 parses options
  before test specs. A name starting with `-` is read as an option: `--help ...` printed usage
  and exited 0, so the case was reported passed without ever having run. Put the flag anywhere
  but first.
- **A comma splits a Catch2 test spec**, so a name containing one cannot select itself, and
  "No tests ran" exits non-zero: a filter that matched nothing reads as a deterministic failure.
  Select by tag, or by a comma-free substring, wherever a tool picks a case.
- **Anything that selects a subset asserts how many cases ran**, not only how many failed. A run
  that executed nothing and a run that passed are otherwise the same number.
- **Two cases with one name** make every entry run both and name neither.
- Origin: [fastcached#636](https://github.com/LASTRADA-Software/fastcached/issues/636),
  [fastcached#895](https://github.com/LASTRADA-Software/fastcached/issues/895),
  [fastcached#729](https://github.com/LASTRADA-Software/fastcached/issues/729).

## A failing `REQUIRE` above an explicit stop turns a red into a hang

- A test starts something that needs an explicit stop (an event loop on a `std::jthread`, a
  server, a helper thread) and asserts with `REQUIRE` before the stop. **Catch2 unwinds on a
  failed `REQUIRE`**, the stop is skipped, and `~jthread` joins a loop nobody asked to end. The
  test hangs instead of failing, and the timeout does not name the assertion.
- **`CHECK` does not unwind**; only `REQUIRE`, `FAIL` and `SKIP` throw.
- **The fixes:** stop before you assert; or an RAII guard whose destructor stops the thing,
  declared *after* whatever the running thread touches, so it is destroyed first. Get the guard
  right and the order wrong and the hang becomes a use-after-free. A `std::jthread` whose body
  takes a `std::stop_token` and loops on `stop_requested()` is not exposed at all.
- Origin: fastcached testing, "A failing `REQUIRE` above an explicit `Stop()` turns a RED into
  a HANG".

## A fake that resolves what production suspends makes every test over it vacuous

- The question to ask of a test double is not "is it correct" but **"can it reach the state the
  case is about?"** An in-memory socket whose `waitReadable()` always answers at once is a
  correct transport, and it never enters the parked state a cancellation or teardown property is
  defined by, so every assertion written over it holds whether the property is implemented or
  not. Where the double cannot park, the case needs one that does, or a real socket.
- **Fakes drift in the permissive direction**, because a fake is written to satisfy the callers
  that exist. Three in one week in fastcached: a `close()` that did not retrieve a parked read,
  an awaitable address taken in the factory instead of `await_suspend`, a store modelling
  behaviour the real one no longer had.
- **A fixture that re-acquires what production binds once tests a different object.** Bind a
  collaborator the way the call site does, before the mutation, and read back through that
  binding.
- Origin: [fastcached#778](https://github.com/LASTRADA-Software/fastcached/issues/778),
  [fastcached#734](https://github.com/LASTRADA-Software/fastcached/issues/734),
  [fastcached#405](https://github.com/LASTRADA-Software/fastcached/issues/405).

## `SUCCEED` where the case could not run is a false green

- **`SUCCEED` records a passing assertion.** A case that bails out because its environment could
  not be arranged (no loopback listener, no IPv6, no console, running as root) then reports a
  pass for a property nothing established.
- **`SKIP("reason")` is the fix**, and with `core::testing_main` a binary whose every case
  skipped exits 77 and ctest scores it skipped. `SUCCEED` is right only when the case ran and
  there was nothing to assert. "Covered by another test" is a reason to skip, never to pass.
- Origin: [fastcached#685](https://github.com/LASTRADA-Software/fastcached/issues/685).

## Assert what distinguishes

- **A test must assert something the healthy and the broken states produce differently.** In
  one evening five fastcached tests could not fail for the reason they existed: a bind that
  returns a non-null object in an errored state passed `REQUIRE(listener)`, and a refusal test
  checked a message both the old and the new rule emit. A refusal test asserts *which* refusal.
- **Prove the test can fail:** neuter the fix, run it, and check that the failures are the ones
  you expect *and only those*. The asymmetry is the evidence; an all-red run could equally be a
  harness that stopped working.
- **An acceptance criterion is a hypothesis.** "What would prove this fixed" and "what would
  fail if it were not" are different questions, and only the second one tests anything.
- Origin: [fastcached#355](https://github.com/LASTRADA-Software/fastcached/issues/355),
  [fastcached#405](https://github.com/LASTRADA-Software/fastcached/issues/405).

## The first failure masks its identical siblings

- A fixture fails at its first site and never reaches a second site with the same defect, so
  the second has a clean record. **Fixing only the observed site relocates the failure**, and
  the relocated one then presents as a regression introduced by the fix.
- When a failure is fixed, ask what else has this shape, walk every call site of the pattern,
  and ask whether you would have seen each one fail.
- Origin: [fastcached#172](https://github.com/LASTRADA-Software/fastcached/issues/172).

## `--repeat until-fail` reports the last iteration

- **`ctest --repeat until-fail:N` reports only the last run**, so a test that fails 1% of the
  time reads "100% tests passed" whenever the final iteration passes. A ~1% flake survived six
  such runs; a loop that counted every outcome found it on the first try (`ran=200 pass=198
  fail=2`).
- **Count every outcome, and print N with the verdict.** "Did not reproduce in 120 runs" does
  not disprove a 1% rate.
- Origin: [fastcached#735](https://github.com/LASTRADA-Software/fastcached/issues/735).

## A crashed run is not a run with zero failures

- A test binary that crashes mid-run reports no failure count at all, so anything that reads a
  count scores it as clean. Read completeness first (did the reporter finish?), then the exit
  status, then the count. Origin:
  [fastcached#1212](https://github.com/LASTRADA-Software/fastcached/issues/1212).

## Showing a red by swapping files is safe for behaviour, not for layout

Proving a case fails before the fix usually means putting the old implementation back for one
run: `git checkout -- <file>`, build, watch it fail, restore. That is sound when the change is
behavioural. It is a trap when the change alters a type's **layout** — a member added or
retyped, a base class introduced — because every translation unit that includes the header has
to be recompiled, and nothing tells you when one was not.

- A build that mixes objects compiled against the two versions links without a word, and then
  corrupts the heap at run time. Task A10 spent an hour on a `gcc-release` run aborting with
  glibc's `double free or corruption (out)` while `clang-debug`, `clang-release`,
  `clang-asan-ubsan`, `clang-tsan`, `cl-debug` and `clangcl-release` were all green on the same
  sources. The sources were fine; one object was not.
- A compiler cache makes it easier to miss, because the object is rewritten with a fresh
  timestamp whether or not its content reflects the header you just changed
  ([`build-and-toolchain.md`](build-and-toolchain.md) says the same for `clangcl-*` trees).
- **So: after restoring a file whose change was structural, rebuild that preset's tree from
  scratch before trusting a green — and before trusting a red.** Deleting the module's
  directory under `out/build/<preset>/` is enough; `--clean-first` is the blunt version.
- The tell is a failure that only one toolchain sees, in code the diff did not touch, and that
  moves or vanishes when tests are run individually. Suspect the build before the code.
