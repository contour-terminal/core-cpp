# Task C1 report: endo migrates onto core-cpp v0.2.0

## Status

Done, pending review. The draft PR is open. Every CI leg is green on the head, and every local gate is green apart from the environmental exceptions listed below. One `/code-review` I started before the ruling was abandoned. It still runs in the background, because an agent cannot stop it. The lead is dispatching the review.

## PR, branch and commits

- PR: https://github.com/contour-terminal/endo/pull/187 (draft), titled "build: coro, net, crispy, tui and the generic platform layer come from core-cpp".
- Branch: `build/core-cpp`, from `origin/master` `fd953ff1`. Worktree: `D:\endo-worktrees\core-cpp`.
- Head: `6688f507`, after review fix rounds 1 and 2. The pre-review head was `98f02363`.
- Pin: `CPMAddPackage(NAME core-cpp GITHUB_REPOSITORY contour-terminal/core-cpp GIT_TAG v0.2.0 VERSION 0.2.0 EXCLUDE_FROM_ALL YES SYSTEM YES ...)`.
- Delta check: `git log f774a210..origin/master -- src/tui src/platform src/testing` is empty, so delta: none.

| # | SHA | Subject |
|---|---|---|
| 1 | 5d2f3399 | agent, platform: three declarations clang-tidy could not see |
| 2 | c9195c1d | build: coro, net, crispy, tui and the generic platform layer come from core-cpp |
| 3 | 6688f507 | docs: AGENT.md and the roadmaps describe core-cpp rather than the contour fetch |

Each commit is signed off.
- Commit 1 was built and tested on its own: the base with contour fetched, clang-debug-agent, 968 steps, 21/21 tests.
- Commit 2 is the tip minus AGENT.md, which is all commit 3 changes, so the tip gates cover it.
- The endo-highlighter step is folded into commit 2. The swap cannot compile without replacing `registerEndoHighlighter()`, and the alternative was a commit in which endo highlighting regresses.

## Gate table (local runs on fresh trees, USE_COMPILER_CACHE=OFF)

The Linux legs below and both Windows legs were rerun on the tree of `6688f507`, with the same results as on `3bb66ca1`. The test totals are for `3bb66ca1`. The emscripten leg and the commit-1 build were not rerun and date from `98f02363`. At `3bb66ca1`, `test-endo-shell` has 1019 cases, one more than the baseline: `shell.highlighters.endo_selected_by_extension_and_fence`.

Setup:
- Linux: WSL2, clang 22.1.2 or gcc 14.3. An LF clone of the branch is built against an LF export of the v0.2.0 tag (`ec47681`).
- Windows: this worktree, built against the `D:\core-cpp-wt-endo` worktree at v0.2.0.
- Baseline: `fd953ff1`, with contour master fetched the way `get_contour_dirs.py` does it.

| Preset | Build exit, steps | Tests | Baseline |
|---|---|---|---|
| clang-debug (ASan+UBSan+tidy) | 0, 727 | 15/15 | 0, 785; 19/20 (a) |
| clang-debug-agent | 0, 910 | 16/16 | 0, 968; 20/21 (a) |
| clang-tsan | 0, 727 | 15/15 | 0, 785; 19/20 (a) |
| gcc-debug | 0, 727 | 15/15 | 0, 785; 18/20 (a, plus contour's coro_test segfaults) |
| clang-release-static | 0, 877 (static binary) | 15/15 | configure fails locally (b) |
| emscripten-release | local emsdk 3.1.56: 1, 110/112 (c); CI deploy with current emsdk: 111/111, endo-playground linked | n/a | same local failure |
| clangcl-debug (Windows) | 0, 465 | 15/15, windows-dialog-canary included | not run |
| clangcl-release, ENDO_ENABLE_AGENT=ON (Windows) | 0, 648 | 15/16 (d) | not run |
| commit 1 alone, clang-debug-agent | 0, 968 | 21/21 | - |

**Test totals** (clang-debug; the candidate matches the baseline):
- endo-test, the .endo e2e suite: 1770 passed, 35 skipped.
- endo-examples: 19 passed, 1 skipped.
- check-doc-snippets: 341 passed, 150 skipped.
- Catch2 test cases:
  - test-endo-language: 1068
  - test-endo-shell: 1018
  - test-endo-lsp: 238
  - test-corevm: 82
  - test-endo-dap: 71
  - test-endo-http: 19
  - test-endo-platform: 127 before, 24 after. The other 103 cases moved to core-cpp.

**Notes**
- (a) The baseline tree is an export with no commits, so `live_git_checkout_suggests_branches` finds no branches. This is environmental.
- (b) This host's static OpenSSL lacks `ZLIB::ZLIB`, so FindOpenSSL fails. That is the OpenSSL requirement this PR removes.
- (c) The same 5 endo-language TUs fail with emsdk 3.1.56 on both sides: an incomplete `ModuleDescriptor` inside `unique_ptr`.
- (d) `resolveTraceLogDirectory.returns_project_dir_in_git_repo` expects `.git` to be a directory, and in a git worktree it is a file. It passes on Linux, where the tree is a clone.

**GitHub CI at 6688f507:** all 9 checks pass:
- Linux x86_64
- Linux x86_64 (no AI features)
- Linux static
- Linux .deb
- Windows clang-cl Release
- macOS ARM64
- clang-tidy (PR diff)
- deploy (the emscripten playground plus mkdocs)
- VS Code Extension

The .deb leg failed twice for reasons outside this PR, and passed each time on a rerun or the next push:
- On `ec8a377c`, apt.llvm.org's GPG key was unreachable inside Docker.
- On `3bb66ca1`, `llvm.sh` rejected Ubuntu `26.04.1`. The rerun passed.

**Other checks**
- Purity of the mechanical pass: an independent regex re-derivation from the endo rows of renames.json gives pure: 188, not pure: 0.
- clang-format 22.1.8: every changed file that was formatted at the base is formatted. 8 files were already unformatted at the base and were left alone.

## core-cpp defects and gaps found (none worked around in endo)

1. **tools/migrate/renames.json is missing the completer rows.** It maps `tui::` to `core::tui::` as a namespace, but has no rows for the completion types that moved to `core::tui::completer::`: CompletionItem, CompletionProvider, Completer, CompletionConfig, FuzzyMatch, FuzzyMatchResult, FuzzyConfig, SmartCaseMatch and SmartCaseConfig. The codemod therefore produces `core::tui::CompletionItem`, which does not compile. These were qualified by hand.
2. **provenance.md is wrong about SuppressWindowsDialogsAtStartup.cpp.** It records the file as "verbatim" from endo, but endo's product variant was dropped: the one that suppresses only when `ENDO_SUPPRESS_WINDOWS_DIALOGS` is set. endo keeps its own source for its two variants, now built on `core::testing::suppressWindowsDialogs()`.
3. **Two signal-fd types disagree.** `SignalHandler::initialize()` returns `int`, while `TuiRuntimeOptions::signalFd` is a `platform::NativeHandle` (`void*` on Windows). A caller needs `#if !defined(_WIN32)` to pass one to the other. `Shell.cpp` carries that `#if`, with a comment.

## Concerns

- **More first-party code is now linted.** `src/` used to reach every target that linked `crispy::core` as a SYSTEM include directory, so clang-tidy and -Werror saw nothing in endo's own headers there. Now they do. Three findings were fixed in commit 1. Other configurations may surface more; macOS was covered by CI only.
- **The composition root keeps a process-lifetime provider.** `Shell()` builds `nativeEnvironmentProvider()` once, as a function-local static. That matches the old `PosixEnvironmentProvider::instance()` semantics: the prompt's resolver sees the variables the shell set without exporting them. The prompt now reads the environment the shell injected, where it used to read the singleton.
- **Agent mode bridges the wakeup through a flow.** Messages reach `TuiRuntime::notifyAgentReady()` through a flow parked on `_agentWakeup`'s handle, which is `relayAgentWakeup`. `~EventLoop` cancels that flow. It is covered by the clang-debug-agent and clangcl-release agent builds, but no test drives agent mode interactively.
- **CI's OpenSSL installs are reverted.** These were added for the vendored net (2bec2efb, bae2c90d): apt libssl-dev, Homebrew openssl@3, the deb image, and vcpkg's openssl. CI is green without them.
- **ROADMAP files were not rewritten.** They still name `src/tui` and `src/coro` paths as historical records.

## Review fix round 1

Each change is amended into the commit it belongs to.
- **Should-fix, the endo registration had no test.** It now has `shell.highlighters.endo_selected_by_extension_and_fence` in `Shell_test.cpp`, which checks that:
  - `main.endo` and `/home/user/scripts/init.endo` are endo;
  - the `endo` fence tag is endo;
  - `.endo-format` is not endo;
  - the registered highlighter classifies `let x = 42`.

  The test was watched failing in two builds: without `.extensions`, lines 5310 and 5311 fail; without `.fenceTags`, line 5312 fails. It passes as committed.
- **relayAgentWakeup:** it catches `core::net::FdRegistrationFailed`, logs the reason through the shell's debug log category, and returns, as TuiRuntime's source flows do.
- **Stale comment near Shell.cpp:3171:** it now describes the relay flow.
- **agent/auth/TerminalInput.cpp:** the backend, loop and runtime live in an immediately-invoked lambda, so they are destroyed before `terminal.shutdown()`.
- **ModuleLoader.hpp:** the compiler library `endo` now links `core::platform` PUBLIC. It no longer relies on the shared include root. core-cpp builds `core::platform` under Emscripten too, and CI's deploy leg builds the wasm target. AGENT.md's Emscripten line is updated to match.

## Review fix round 2 (nits 5-7)

- **.clang-tidy:** the comment that said first-party headers go unchecked now says they are checked.
- **.clang-format:** a `^<(core)/` include category, at the priority `crispy` had, replaces the stale `tui`, `vtparser` and `crispy` rows. The branch's files are re-sorted with clang-format 22.1.8. The 8 files that were already unformatted at the base were left alone.
- **ROADMAP.md and ROADMAP-AGENT.md:** point at core-cpp's `src/core/{tui,async,net}` paths. The mention of `registerEndoHighlighter()` now describes the Shell-owned registry.
- **Re-gated on the new tree:**
  - clang-debug 15/15
  - clang-debug-agent 16/16
  - gcc-debug 15/15
  - clang-tsan 15/15
  - clang-release-static 15/15
  - clangcl-debug 15/15
  - clangcl-release with the agent enabled 15/16: the same failure as before, a test that expects `.git` to be a directory, which a git worktree does not have.
- **CI:** all 9 checks pass.



## Follow-up: re-pin to core-cpp v0.2.1

**Head:** `479e3e6d`. The branch now has four commits:

| # | SHA | Subject |
|---|---|---|
| 1 | 5d2f3399 | agent, platform: three declarations clang-tidy could not see |
| 2 | 7d4dd8d5 | shell: RealTTY's destructor restores the terminal mode without throwing |
| 3 | 3f5f0ca1 | build: coro, net, crispy, tui and the generic platform layer come from core-cpp |
| 4 | 479e3e6d | docs: AGENT.md and the roadmaps describe core-cpp rather than the contour fetch |

**Pin:** `GIT_TAG v0.2.1`, `VERSION 0.2.1`. `Shell.cpp` passes `SignalHandler::nativeHandle()` as the runtime's signal fd, so its Windows `#if` is gone.

**Hangup handling**
- When the wait is cancelled and `runtime->inputClosed()` is true, `Prompt::read()` ends the prompt the way Ctrl+D does, and sets `terminalGone()`.
- Both REPL loops break straight after the read in that case, and skip disabling the semantic-block extension on the dead terminal.
- **Unmasked:** the shell still did not exit cleanly after that. `~RealTTY()` called `restoreMode()`, which throws when `tcsetattr()` fails with EIO, so the process got SIGABRT at exit. This is fixed in its own commit (7d4dd8d5).

**The test:** `scripts/check-hangup-exit.py`, registered as ctest `check-hangup-exit` on POSIX.
- It runs endo on a pty with SIGHUP ignored and HOME set to a temporary directory, waits for the prompt, and closes the master side.
- It requires a clean exit within 15 s.
- If the shell is still running, it reports the shell's CPU time and prints its stderr.

| Tree | Result |
|---|---|
| A: `6688f507` on v0.2.0 | still running after 15 s, at 1.00 s of CPU per second |
| B: v0.2.1 without the prompt's handling | SIGABRT from "write: Input/output error" |
| C: the tip | exit code 0, 3 runs out of 3 |

**Gates on the tip's tree**
- clang-debug (ASan, UBSan, tidy): 727 steps, 16/16, `check-hangup-exit` included.
- Commits 1 and 2 alone, clang-debug with contour fetched: 785 steps, 20/20.
- clangcl-debug: 466 steps, 15/15.
- clangcl-release with the agent enabled: 649 steps, 15/16. The failure is the known worktree `.git` test.
- **The four remaining legs.** WSL stopped answering after they were started. Once it recovered, they were rerun one at a time at -j8 under `nice -n 10`, each starting only when the 1-minute load was below 16:

  | Preset | Steps | Tests |
  |---|---|---|
  | clang-debug-agent | 910 | 17/17 |
  | clang-tsan | 727 | 16/16 |
  | gcc-debug | 727 | 16/16 |
  | clang-release-static | 877 | 16/16 |

  `check-hangup-exit` passes in all five Linux legs.

**GitHub CI at `479e3e6d`:** all 9 checks pass.

**PR body:** updated with the v0.2.1 pin, the 4-commit table, the gate table, and two Consumer impact entries: a hung-up terminal ends the shell, and piped output carries no synchronized-output markers.


## Follow-up: re-pin to core-cpp v0.3.0

**Head:** `86d7e725`. `GIT_TAG v0.3.0` and `VERSION 0.3.0` are set in the swap commit (`a36536c7`), and the pin in AGENT.md in the docs commit (`86d7e725`). Commits 1 and 2 are unchanged: `5d2f3399` and `7d4dd8d5`.

**v0.3.0's Breaking entries, as they reach endo**
- **A second read or write parked on one handle now aborts in every build.**
  - endo's loops watch each handle with one reader: the terminal input, the resize handle, the interrupt and agent wakeups, and the signal fd.
  - `httpServe` uses core-cpp's own `serve()`.
  - The full suites were run in Release: clang-release (new this round), clang-release-static, and clangcl-release with the agent enabled. All pass, apart from the known worktree `.git` test.
- **The drain order of callback-completed waiters, and `IHostScheduler` / `ManualHostScheduler`:** nothing in endo uses them.
- **Fixed, but observable:** on Windows, `suppressWindowsDialogs()` now keeps abort()'s message. `windows-dialog-canary` passes.
- **No code change was needed in endo.**

**Gates** (every WSL leg ran alone, at -j8 under `nice -n 10`, starting only when the 1-minute load was below 16)

| Preset | Steps | Tests |
|---|---|---|
| clang-debug | 727 | 16/16 |
| clang-release | 727 | 16/16 |
| clang-debug-agent | 910 | 17/17 |
| clang-tsan | 727 | 16/16 |
| gcc-debug | 727 | 16/16 |
| clang-release-static | 877 | 16/16 |
| clangcl-debug (Windows) | 466 | 15/15 |
| clangcl-release, agent enabled (Windows) | 649 | 15/16 |

- The one Windows failure is the known worktree `.git` test.
- Release suite totals: `endo-test` 1770 passed and 35 skipped; `test-endo-shell` 1019 cases; `test-endo-language` 1068 cases.

**GitHub CI at `86d7e725`:** all 9 checks pass.

**PR body:** updated with the v0.3.0 pin, a Consumer impact entry for each v0.3.0 break, and the gate table.

Status: done. The PR stays a draft for the user to merge.

## Follow-up: re-pin to core-cpp v0.4.0

**Head:** `57cb7c35`. v0.4.0's Breaking entries (`AsyncQueue::pop` resuming on the executor it parked on, `resumeSoonOn` moving to `detail`) and its known issue (`DetachedTask` under clang-cl at `-O0`) reach nothing endo uses. No code change.

**Gates:** clang-debug, clang-release, clang-tsan, gcc-debug 727 steps and 16/16; clang-debug-agent 910 and 17/17; clang-release-static 877 and 16/16; clangcl-debug 466 and 15/15; clangcl-release with the agent 649 and 15/16 (the known worktree `.git` test).

**GitHub CI at `57cb7c35`:** 9 of 9 after reruns, neither caused by this PR.
- `Linux .deb`: llvm.sh's apt.llvm.org key fetch failed inside Docker; the rerun passed.
- `clang-tidy (PR diff)`: three attempts failed to link llama.cpp (`undefined reference to ggml_backend_amx_buffer_type()`). The job builds ggml with ggml's default `GGML_NATIVE=ON`, so `-march=native`, through a shared ccache. With the lead's approval all 15 `ccache-ccache-clang-tidy-*` Actions caches were deleted and nothing else; the cold rerun (2/910 hits) passed on the same tree. Filed as [endo#190](https://github.com/contour-terminal/endo/issues/190), with `-DGGML_NATIVE=OFF` or a CPU-keyed cache as the fix. The `Passthrough` `-Wswitch` warning in that log comes from master's 807bb98a, before the merge base.

## Follow-up: re-pin to core-cpp v0.4.3

**Head:** `a08c4250`. The pin is folded into the swap commit and AGENT.md's into the docs commit; the tree differs from `57cb7c35` in 4 lines. v0.4.1 to v0.4.3 change no signature; v0.4.1's known issue (#54, MSVC `cl` `/O2`) does not reach endo, which builds Windows with clang-cl only.

**Gates:** one fresh clangcl-debug leg (C: was short of disk): 466 steps, canary first, 15/15. From a PowerShell parent the 2 `which` cases of `test-endo-shell` fail for want of Git's `usr/bin` on PATH; with it they pass. The tree was deleted after.

**GitHub CI at `a08c4250`:** 9 of 9; `Linux .deb` passed on rerun after the same apt.llvm.org failure.

## Follow-up: re-pin to core-cpp v0.5.0

**Head:** `184c87b2`. Commits 1-4 are unchanged. The pin moves to v0.5.0 in a new commit 5, "shell: the environment and the working directory are core-cpp v0.5.0's two seams", together with the #7 migration: commit 3 at v0.5.0 would not compile without it, so folding the pin there would leave a red commit. Raised with the lead.

**v0.5.0's Breaking entries, as they reach endo**
- **#7, `ProcessEnvironment` and `WorkingDirectory`:** 44 files.
  - `Shell` takes a `WorkingDirectory&` and hands it to the `DirectoryConfigManager`, the `Completer` (for its `HistoryCompleter`) and the prompt.
  - Text consumers take `normalizePath(cwd.currentDirectory())`, the old member's value; on Windows it is still the on-disk case.
  - `homeDirectory`, `userName` and `configHome` are the free functions. `normalizedHomeDirectory()` takes `core::Environment const&`.
  - Every `std::expected` is handled. `Shell::reportEnvironmentError()` writes `endo: <what>: <why>` to stderr. `set`, `unset`, `export` and `read` exit 1 on a refusal, and `source-env` fails if anything was refused. A directory config reports a failed `unset` through its sink. Tests `REQUIRE` what they seed; the injected fixture's `seedVariable()` throws; `endo-test` seeds through the double's constructor.
  - Two new cases, `set "A=B" value` and `unset "A=B"`, pin the reporting.
  - `PlatformError::InvalidArgument` left endo's `toShellError()` switch unhandled; `ShellError::InvalidArgument` is added and mapped.
- **#6 (WFMO removed) and #13 (`cli::parse` returns `std::expected`):** endo names none of the removed types, and calls no `core::cli::parse()`; its one use of `core::cli` is `App::customizeLogStoreOutput()` in the test mains.

**Gates** (fresh trees, deleted after; C: about 77 GB free)

| Leg | Steps | Tests |
|---|---|---|
| clang-debug, WSL (ASan, UBSan, clang-tidy) | 729 | 16/16; no warning but master's `Passthrough` |
| clangcl-debug (Windows) | 465 | canary first, then 15/15; the 2 `which` cases need Git's `usr/bin` on PATH (1024 cases) |

**GitHub CI at `184c87b2`:** 9 of 9, first attempt.

**PR body:** updated with the v0.5.0 pin, commit 5, a #7 migration section, the 0.5.0 Consumer impact and the gate rows.

Status: done. The PR stays a draft for the user to merge.
