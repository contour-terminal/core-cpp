# Task C2 report: tuidu migrates onto core-cpp v0.2.0

## Status

Done. The draft PR is open, the branch is pushed, and every local gate is green. Nothing is merged and nothing is marked ready.

- PR: https://github.com/contour-terminal/tuidu/pull/13 (draft), titled "build: the TUI, platform and coroutine layers come from core-cpp"
- Branch: `build/core-cpp`, from origin/master 30107fb, in the worktree D:\tuidu-worktrees\core-cpp
- Pin: `CPMAddPackage(NAME core-cpp GITHUB_REPOSITORY contour-terminal/core-cpp GIT_TAG v0.2.0 VERSION 0.2.0 EXCLUDE_FROM_ALL YES SYSTEM YES OPTIONS "CORE_CPP_TESTING OFF" "CORE_CPP_WITH_TLS OFF")`

## Commits

All three are signed off.

1. `051210c` build: the TUI, platform and coroutine layers come from core-cpp. This commit does the following:
   - deletes `src/{coro,platform,testing,tui}` and changes CMake;
   - runs the codemod: `rewrite.py --profile tuidu src/tuidu`, 41 files, 447 replacements;
   - adds the hand edits: `completer::` qualification, `core::cli` in `Cli.cpp`, `nativeFileInfoProvider()`, and the App on `EventLoop` + `InputSource` with a Wakeup-to-`notifyAgentReady` relay;
   - rewrites the App tests;
   - narrows `.clang-tidy`'s HeaderFilterRegex to `src/tuidu/`.
2. `f8b3e87` ci: nothing fetches crispy any more. It removes the `get_contour_dirs.py` steps from `build.yml`, `clang-tidy.yml` and `release.yml`, and the `src/crispy` exclusion from `clang-format.yml`. It deletes the script and its `.gitignore` lines, and updates the README build note.
3. `c0abd8d` docs: AGENT.md describes core-cpp rather than the vendored copies.

Commit 1 holds every source and CMake change. Commits 2 and 3 touch only CI and docs. Between commits 1 and 2, CI still runs the old fetch script, which is harmless because nothing adds `src/crispy` any more. So each commit builds on its own; that follows from how the changes are split, and I did not build the intermediate commits separately.

## Gates

Every row below used `-DCPM_core-cpp_SOURCE=<a clean worktree at v0.2.0 (ec47681)>` except "pin-check". `USE_SCCACHE` and `USE_CCACHE` were OFF, and no row's build.ninja has a launcher.

| Preset | Configure | Build | Tests |
|---|---|---|---|
| clang-debug (WSL, ASan+UBSan, clang-tidy 22.1.8 on) | 0 | 0 (297 steps on the first full build) | ctest 1/1 passed; 587 assertions in 137 test cases |
| clang-release (WSL) | 0 | 0 (289/289) | ctest 1/1 passed |
| gcc-debug (WSL) | 0 | 0 (289/289) | ctest 1/1 passed |
| clangcl-release (Windows, VS 18 dev shell) | 0 | 0 (324 steps) | ctest 1/1 passed; 587 assertions in 137 test cases |
| clangcl-debug (Windows) | 0 | 0 (324 steps) | ctest 1/1 passed; 587 assertions in 137 test cases |
| pin-check (WSL, Release, no override, fetched v0.2.0 from GitHub) | 0 | 0 (331/331) | ctest 1/1 passed. The fetched tree is `ec47681 Release 0.2.0`, and its `src/` is identical to the local worktree's |
| App tests repeated, clang-debug | | | 200 runs of `[app]` with `--order rand` and seeds 1-200: 0 failures |
| clang-format 21.1.8 (the version CI pins) | | | `--dry-run --Werror` on all 62 `src/tuidu` files: clean |

The test-case count is 137 on master and 137 on this branch.

**Interactive check.** In WSL, `pty.fork`, 80x24, `tuidu /usr/include`, run against the clang-debug build and the pin-check release build:

- tuidu enters the alternate screen and draws the tree and a status line ("37.6 MiB  3567 items").
- `q` exits with code 0 and leaves the alternate screen.
- It passed on both builds.

The master baseline, built with June's crispy, behaves the same way and writes byte-identical output size in the first 3 s (700233 bytes, against 700233 on clang-debug).

Not run: an interactive check on Windows. The clang-tidy CI configuration (clang-tidy 21, RelWithDebInfo) was not run separately; clang-debug ran tidy 22.1.8 over the same targets and was clean.

## core-cpp defects

1. **A hung-up terminal makes the TUI spin at 100% CPU forever, if SIGHUP is ignored.**
   - Repro: start tuidu under a pty with SIGHUP set to SIG_IGN, then close the master.
   - Measured: 2.0 s of CPU per 2 s of wall time, and no exit after 5 s. The pin-check release build measured 1.5 s.
   - Likely cause, from reading the code: `TuiRuntime::inputFlow` is woken readable by POLLHUP. `TerminalInputSource::readReady()` then reads 0 bytes and returns nothing. The flow arms the escape flush and parks again, which repeats forever. Nothing in v0.2.0 turns end-of-input into a stop.
   - Not a regression. tuidu master, built against June's crispy, spins the same way (2.00 s per 2 s), even though tuidu c20bcac tried to fix exactly this in the old `TerminalEventSourcePosix`.
   - Where it would matter: any `nohup`ed or detached TUI, and ssh drops where SIGHUP is ignored.
   - Repro script: `scratchpad/c2/pty_check.py`, part 2.
2. None other found.

## Delta check

tuidu's eight commits to the vendored paths are 71350c5, fa186a3, 1df2570, f59c594, b0cec81, 17c87d5, 3481890 and c20bcac. I checked each one against v0.2.0:

- UTF-8 Windows names via `normalizePath`, and the console CP_UTF8 save/restore;
- `ThisCoroStopToken`;
- `TreeTableView` click, double-click and `halfPageBy`;
- `MockTerminalOutput::clipboardText` and `MockFileInfoProvider`;
- the `Modifier` `operator~` and `&=`;
- the alternate screen, `?1049`.

All are present. c20bcac's POLLHUP handling has no equivalent (defect 1). Result: delta: none.

## Concerns

- **Brief deviation.** The brief says "ScriptedBackend against `TuiRuntime(EventLoop&, Terminal&)`". I used the default backend with a real `SystemPipe` behind `ScriptedInputSource`, and `App` takes an `InputSource&`.
  - Why: the App relays the workers' real `Wakeup`, which a `ScriptedBackend` never watches.
  - Why: a mock-output `Terminal`'s input would read the test process's real stdin.
  - This is the setup `ScriptedInputSource`'s own header recommends.
- **Interrupt handler removed.** The App no longer installs `setInterruptHandler(request_stop)`. The old handler only fired on the old event source's `interrupted` outcome (poll error or POLLHUP). tuidu passes no `interruptWakeup`, so it would be dead code now. Behaviour is unchanged: Ctrl+C is a key in raw mode.
- **Master does not build today.** tuidu master cannot configure against contour's current crispy, which needs `Threads::Threads` and `contour::tracy`. I noted this in the PR body. It also means master CI would be red independently of this PR.
- **Files kept as they were.** tuidu keeps its own `EnableCcache.cmake`; it does not use core-cpp's `CompileCache.cmake`, which the provenance note says tuidu includes. That is out of the plan's scope. `cmake/EndoThirdParties.cmake` also keeps its name.
- **Include order changed.** clang-format 21 regrouped includes: `<core/...>` now sorts with `<tuidu/...>` into the last block, and the `<tui/...>` priority-5 group is gone. `.clang-format` is untouched, as in endo#187.
- **Extra worktree.** I created D:\tuidu-worktrees\baseline, detached at origin/master, for the regression comparison. It contains a local, untracked `src/crispy` from contour 973e5a9 and a `build/`. It can be removed with `git -C D:/tuidu worktree remove --force D:/tuidu-worktrees/baseline`.
- **Mishap.** A mis-quoted `wsl.exe bash -c` ran tuidu's `.cpp`/`.hpp` files as shell scripts, and every line errored. I checked afterwards: no untracked files in the worktree, and nothing new in the WSL home directory.
- **Code review.** `/code-review` was not run on the tuidu diff. I self-reviewed only the hand-edited parts (App, main, App_test).

## Fix round 1 (review: CHANGES, no blockers)

New head `3b658cc`, force-pushed with a lease from c0abd8d. The branch now has four commits:

1. `6a6ab2e` fix(app): read the tree under its mutex when the status line is refreshed (new)
2. `d7b92d7` build: the TUI, platform and coroutine layers come from core-cpp
3. `2208170` ci: nothing fetches crispy any more
4. `3b658cc` docs: AGENT.md describes core-cpp rather than the vendored copies

- **Relay test.** The new case "App: a worker's push wakes the idle App without input" types no input. After the scan it pushes a `ScanProgress` carrying an error, then gives the App 400 ms to apply it. The App idles on a 1000 ms poll, so only the relay can deliver it in time.
  - Probe with the relay's `spawn` replaced by a no-op: that case fails ("timed out waiting for the idle App to apply a pushed progress message"), and the other 8 `[app]` cases pass.
  - The delete cases no longer spam `q`. They wait for the delete to finish, then type `q`.
- **Failed-delete test.** `App` gains `deleteError()`, which records the last delete's failure. The failed-delete case waits for it and asserts it, so an App that reports no error now times out on that step instead of passing.
- **`.clang-format`.** `'^<(core)/'` at priority 5 replaces `<tui/>`; the `<crispy/>` row is removed. The re-sort put `<core/...>` includes first again, as `<tui/...>` were.
- **Race found while stressing, a pre-existing tuidu bug.** `App::refreshStatus()` reads the tree. Two of its callers did not take the tree mutex while the scan worker grows it: `mainFlow()`'s first refresh and the one after a delete.
  - Evidence before the fix: 3 SIGSEGVs in 2000 Release runs of `[app]`, 2 ASan heap-use-after-free reports (read in `refreshStatus`, freed by `Tree::addChild` on the scan thread), and 38 TSan data-race reports on every full run.
  - Master's code has the same race, but master's tests never tripped it in 3000 runs; the new harness's timing exposes it.
  - I fixed it in its own commit, placed first. On master's code with June's crispy, that commit builds and passes all 137 test cases.
  - After the fix: 0 failures in 2000 Release runs, 0 in 1000 ASan runs, 0 in 200 TSan runs, and a TSan full run with no reports.
- **Local gates on the new head.**
  - clang-debug, clang-release, gcc-debug and clangcl-release: all exit 0, with 138 test cases and 594 assertions.
  - clang-format 21.1.8: clean.
  - Pty check: draws, and `q` exits with code 0.
  - clangcl-debug was not re-run on this head.
- **CI on `3b658cc`: all green.**
  - Clang-Tidy 35976586977: clang-tidy (full project) passed. This is the CI clang-tidy 21 RelWithDebInfo configuration, so it covers the lead's ask to run it.
  - Build 35976586892: macOS ARM64, Linux x86_64 and Windows clang-cl passed.
  - Release 35976586978: Build linux, macos and windows-x86_64 passed.
  - Clang-Format 35976586887 passed.
  - The earlier reruns of 35972433995 and 35972434026, on the old head, also finished green.
- **PR body.** It now describes the race fix and the relay test, and has a local-gate table.
- **Housekeeping.** I added a worktree, D:	uidu-worktrees\c2-fix (branch `c2/refresh-status-lock`, local only), to build the fix commit on master's code. The baseline worktree has that fix cherry-picked. Both can be removed.

## Round 3: re-pin to core-cpp v0.2.1

New head `9f8a673`, force-pushed with a lease from 3b658cc. The branch has four commits:

1. `6a6ab2e` fix(app): read the tree under its mutex (unchanged)
2. `6d0b545` build: the TUI, platform and coroutine layers come from core-cpp
3. `7f0bf68` ci: nothing fetches crispy any more
4. `9f8a673` docs: AGENT.md describes core-cpp rather than the vendored copies

- **Pin.** `GIT_TAG v0.2.1`, `VERSION 0.2.1` (the tag's commit is a6d49f2). Local builds used a clean worktree at D:/core-cpp-wt-tuidu-021.
- **End of input.**
  - Behaviour: when `nextActivity()` throws `OperationCancelled` and `_runtime.inputClosed()` is true, `App::mainFlow()` quits (`running = false`). Any other cancellation is rethrown, as before.
  - `runModal`: tuidu has no caller of it, so nothing else re-enters a wait.
  - New test "App: the end of terminal input quits the App": `closeInput()` after the scan, and no `q`. It passes. With the fix disabled, the probe fails it: run() is cancelled, `REQUIRE_FALSE(cancelled)` fails, and the other 9 `[app]` cases pass.
- **Pty check, part 2** (master closed, SIGHUP ignored): "hangup: exited by itself, exit code 0" on clang-debug and on clang-release. On v0.2.0 it measured 2.0 s of CPU per 2 s and never exited.
- **Gates** (all against v0.2.1):
  - clang-debug, clang-release, gcc-debug (WSL): exit 0, 139 test cases and 597 assertions.
  - clangcl-release and clangcl-debug (Windows): exit 0, 139 test cases and 597 assertions.
  - clang-format 21: clean.
  - `[app]` random order: 0 failures in 1000 Release runs and 500 ASan runs.
  - TSan: the full run exits 0 with 139 cases and no reports.
- **Not finished locally.** WSL stopped responding (`Wsl/Service/0x8007274c`) during the 100-run TSan loop and before the pin check. I did not restart WSL, because it is shared. CI covers the pin check instead: every job fetches v0.2.1 with no override.
- **CI on 9f8a673: all green.**
  - Build 36027381541: macOS ARM64, Linux x86_64 and Windows clang-cl passed.
  - Clang-Tidy 36027381608: clang-tidy (full project) passed.
  - Clang-Format 36027381554 passed.
  - Release 36027381643: linux, macos and windows-x86_64 passed.
- **PR body.** It now says v0.2.1, adds the end-of-input bullet, replaces the "known issue" section with "Terminal hangup (fixed by v0.2.1)", and updates the gate table.

## Round 4: re-pin to core-cpp v0.3.0

New head `ee3e065`, force-pushed with a lease from 9f8a673. The branch has four commits:

1. `6a6ab2e` fix(app)
2. `0d4b21c` build (pin changed to `GIT_TAG v0.3.0`, `VERSION 0.3.0`; the tag is at 1ae59fd)
3. `ce4dae9` ci
4. `ee3e065` docs

- **0.3.0's Breaking notes.** None reach tuidu, so there are no code changes; I grepped src/ for their APIs:
  - socket re-arm: tuidu has no `ISocket`;
  - drain-callback resume order: nothing depends on it;
  - `IHostScheduler::callAfter` and `ManualHostScheduler`: unused.
- **Gates** (against v0.3.0). The WSL builds ran one at a time, at -j8 under nice 10, and only started while the 1-minute load was below 16 (it was 7 to 16).
  - clang-debug, clang-release, gcc-debug (WSL): exit 0, 139 test cases.
  - clangcl-release and clangcl-debug (Windows): exit 0, 139 test cases.
  - Pty check: draws, and `q` exits with code 0. With the master closed and SIGHUP ignored, tuidu exits by itself with code 0.
  - `[app]` random order: 0 failures in 300 Release and 300 ASan runs.
  - Pin check with no override: the fetched tree is `1ae59fd Release 0.3.0`; it builds, and 139 test cases pass.
  - Not run this round: TSan (no source change since round 3) and clang-format (no source change).
- **CI on ee3e065: all green.**
  - Build 36063981444, Release 36063981438, Clang-Tidy 36063981513 and Clang-Format 36063981445: success.
- **PR body.** It now says v0.3.0, adds a section on 0.3.0's breaking notes, and updates the gate table.

## Round 5: re-pin to core-cpp v0.4.0

New head `c4b3caf`, force-pushed with a lease from ee3e065. The branch has four commits:

1. `6a6ab2e` fix(app)
2. `18392f5` build (pin changed to `GIT_TAG v0.4.0`, `VERSION 0.4.0`; the tag is at e3647c0)
3. `70338b6` ci
4. `c4b3caf` docs

- **0.4.0 notes.** None reach tuidu, so there are no code changes:
  - Breaking, `AsyncQueue::pop` resuming on the parking executor: tuidu has no `AsyncQueue`.
  - Breaking, `resumeSoonOn` moved to `detail`: tuidu never calls it.
  - Known issue, `DetachedTask` under clang-cl at -O0 (core-cpp#51): tuidu creates no `DetachedTask`, and core-cpp's EventLoop mentions it only in comments. The clangcl-debug gate passes.
- **Gates** (against v0.4.0). The WSL builds ran one at a time, at -j8 under nice 10, while the 1-minute load was below 16.
  - clang-debug, clang-release, gcc-debug (WSL): exit 0, 139 test cases.
  - clangcl-release and clangcl-debug (Windows): exit 0, 139 test cases.
  - Pty check: draws, and `q` exits with code 0. After a hangup with SIGHUP ignored, tuidu exits by itself with code 0.
  - `[app]` random order: 0 failures in 300 Release and 300 ASan runs.
  - Pin check with no override: the fetched tree is `e3647c0 Release 0.4.0`; it builds, and 139 test cases pass.
  - Not run this round: TSan and clang-format (no source change).
- **CI on c4b3caf: all green.**
  - Build 36158515016, Release 36158514937, Clang-Tidy 36158515170 and Clang-Format 36158514859: success.
- **PR body.** It now says v0.4.0, and its notes section covers the breaking notes and known issues since 0.2.1.

## Round 6: re-pin to core-cpp v0.4.3

New head `002daf1`, force-pushed with a lease from c4b3caf. The PR is still a draft. The branch has four commits:

1. `6a6ab2e` fix(app)
2. `0650763` build (pin changed to `GIT_TAG v0.4.3`, `VERSION 0.4.3`; the tag is at 76ef2d0)
3. `02841a4` ci
4. `002daf1` docs

- **0.4.1 to 0.4.3 notes.** There are no Breaking sections. The 0.4.1 known issue (cl 19.51 `/O2`, #54) does not reach tuidu, which builds with clang-cl. 0.4.2 fixes 0.4.0's known issue (#51).
- **Local gates, reduced for disk space.** C: had 4.7 GB free, above the 2 GB floor.
  - One clangcl-debug build against D:/core-cpp-wt-tuidu-043: configure and build exit 0, 324 steps.
  - Dialog canary before any test binary ran: a clang-cl program linking `core::testing` calls `suppressWindowsDialogs()`, then `abort()`. It exited by itself with code 3 and wrote "abort() has been called" to stderr, with no dialog.
  - Then the tests ran once: ctest passed, test-tuidu passed with 139 test cases and 597 assertions.
  - I deleted the build tree afterwards, along with all older tuidu build trees (2.2 GB).
- **CI on 002daf1: all green.**
  - Build 36213161899 (Windows clang-cl, macOS ARM64, Linux x86_64): success.
  - Release 36213161893 (linux, macos, windows): success.
  - Clang-Tidy 36213161895 and Clang-Format 36213161892: success.
- **PR body.** Every pin mention now says v0.4.3. The notes section covers 0.4.1 to 0.4.3, and the gate table says which gates ran on which version.

## Round 7: re-pin to core-cpp v0.5.0

New head `09cde07`, pushed with a lease from 002daf1. It is a fast-forward: one new commit on top of the four. The PR is still a draft.

- **New commit.** `09cde07` build: core-cpp v0.5.0, whose cli::parse() reports errors as a value. It carries the pin (`GIT_TAG v0.5.0`, `VERSION 0.5.0`; the tag is at 7f5f741) together with the Cli change, because the old Cli code does not compile against v0.5.0.
  - `parseCommandLine()` does `auto const parsed = core::cli::parse(...)`; on failure it prints `tuidu: {parsed.error().message}` and exits 2, as before.
  - The try/catch and `<exception>` are gone.
  - New case "Cli: a value flag without its value is a usage error".
- **0.5.0's other two breaking notes, verified by grep.** tuidu names none of `Wfmo`, `BackendKind`, `EnvironmentProvider`, `configHome`, `homeDirectory`, `UserPaths`, `changeDirectory` or `currentDirectory` (core-cpp#6, #7).
- **Local gate.** C: had 78 GB free.
  - clangcl-debug: configure and build exit 0, 324 steps.
  - Dialog canary first: it exited by itself with code 3 and printed "abort() has been called".
  - Tests ran once: ctest passed, and test-tuidu passed with 140 test cases and 599 assertions.
  - `tuidu --units` prints `tuidu: Option "units" expects a value.` and exits 2. `--version` exits 0.
  - The build tree is deleted.
- **CI on 09cde07: all green.**
  - Build 36232152628 (Windows clang-cl, macOS ARM64, Linux x86_64): success.
  - Release 36232152605 (linux, macos, windows): success.
  - Clang-Tidy 36232152637 and Clang-Format 36232152607: success.
- **PR body.** It now says v0.5.0, adds a bullet on the Cli change, and updates the gate table.
