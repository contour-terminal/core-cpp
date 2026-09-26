# Brief for Task C3+C4: fastcached migrates onto core-cpp v0.1.0, as ONE pull request

The plan text for both tasks follows verbatim, then the rulings and facts that supersede it.

### Task C3: fastcached PR-A (`LASTRADA-Software/fastcached`)
- [ ] **Delta check (first step).** Run `git log --oneline <provenance SHA>..origin/master -- <replaced paths>` in this consumer. If it lists anything applicable, port it into core-cpp first (patch release v0.1.x via contour-workflows:draft-release/publish-release) and bump this PR's pin. Record the result in the PR body (`delta: none` or the ported commits).
- [ ] File a tracking issue first (the fastcached branch convention is `claude/<issue#>-<slug>`). Create the worktree; all C3 work happens in it:
  ```powershell
  git -C D:\fastcached fetch origin
  git -C D:\fastcached worktree add D:\fastcached-worktrees\core-cpp-tui -b claude/<n>-core-cpp-tui origin/master
  ```

**Delete:**
- `vendor/` (176 files), `cmake/VendorLinkRoots.cmake`
- `scripts/check-vendor-{verbatim,verbatim-selftest,figures,figures-selftest,link-canary,link-canary-selftest,link-roots-selftest,vocabulary,vocabulary-selftest}.cmake`, `scripts/check-sanitized-objects.cmake`
- The vendor tests in `src/tests/CMakeLists.txt` (~6985-7490).

**CMake:**
- `CMakeLists.txt:448` `add_subdirectory(vendor)` → `CPMAddPackage(core-cpp … "CORE_CPP_WITH_TUI ${FASTCACHED_BUILD_TUI}" "CORE_CPP_WITH_IMAGES OFF" "CORE_CPP_WITH_TLS OFF" "CORE_CPP_TESTING OFF")`, placed after libunicode.
- Lines 466-508: the per-target sanitizer loop now iterates `get_property(... GLOBAL PROPERTY CORE_CPP_TARGETS)` and keeps its refusal of an empty set.
- `src/apps/fastcache-cli/CMakeLists.txt:52-86,136-138`: `TARGET fastcache-tui` → `core::tui`.

**Hygiene fallout:**
- `scripts/lib/third-party-roots.txt` → an explicit `none` sentinel; update `CheckCommon.cmake:307-311`, `third-party-roots.sh`, and `check-third-party-roots{,-selftest}.cmake`.
- `check-target-file-guards.cmake`: remove the #1369 second reader (~152-450) and its self-tests.
- `cmake/ErrorPopups.cmake:54`, `scripts/local-gate.sh` (~380-382, 1650-1710), `tidy-sweep.sh:357-370`, `tsan-gate.sh` (716-832, 1490).
- `.clang-format-ignore`.
- Comment-only touch-ups as listed in the migration design.

**Codemod:** the 6 adapter files (`EndoSixelEncoder.cpp` → `TuiSixelEncoder.cpp`).

**Docs:** the AGENT.md and `.agent/` rows that mention `vendor/`.

- [ ] Verify: `clang-debug`, `clang-tsan`, `clang-asan-ubsan`, `ctest -L hygiene`, `clangcl-debug`, `cl-debug`, `scripts/local-gate.sh` (WSL). Open the draft PR titled "tui: the live-stats view links core-cpp's terminal UI, and vendor/ goes".

### Task C4: fastcached PR-B, Async/Net
- [ ] **Delta check (first step).** Run `git log --oneline <provenance SHA>..origin/master -- <replaced paths>` in this consumer. If it lists anything applicable, port it into core-cpp first (patch release v0.1.x via contour-workflows:draft-release/publish-release) and bump this PR's pin. Record the result in the PR body (`delta: none` or the ported commits).
- [ ] Tracking issue. Create the worktree, based on PR-A's branch, plus a detached baseline worktree for the "before" benchmark:
  ```powershell
  git -C D:\fastcached worktree add D:\fastcached-worktrees\core-cpp-async-net -b claude/<m>-core-cpp-async-net claude/<n>-core-cpp-tui
  git -C D:\fastcached worktree add --detach D:\fastcached-worktrees\core-cpp-bench-base origin/master
  ```

**Three commits, each green:**
1. **Semantic rename in-tree.** `python D:/core-cpp/tools/migrate/semantic_rename.py --decl-paths src/FastCache/Async src/FastCache/Net src/FastCache/Core/Clock.hpp --compile-db out/build/clang-debug --compile-db out/build/clangcl-debug`. macOS/kqueue leftovers are caught by CI. No behaviour change.
2. **Swap.** Delete `src/FastCache/Async/**`, `src/FastCache/Net/**`, `Core/{Clock,Ranges,Profiling}` + tests, the four Net canaries (`src/tests/CMakeLists.txt:663-900`, now in core-cpp), and `scripts/check-net-boundary{,-selftest}.cmake` + their tests (1705-1735, 1860-1880).
   - `target_link_libraries(FastCache PUBLIC core::async core::net)`.
   - `FASTCACHED_ENABLE_TLS` forwards to `CORE_CPP_WITH_TLS` and links `core::net_tls`.
   - Includes via `rewrite.py --profile fastcached`.
   - `fastcache-cc` links only `core::async core::net_types` (assert with its existing link check, `.agent/rules/wire-and-protocol.md:134-135`).
3. **Semantic deltas** with tests: stop tokens replace polled cancellation (16 sites), `DeadlineTimer` without a poll interval, deferred resumption (add `runUntilIdle()` where a test asserted inline completion after `close()`).

**Benchmark gate:** run fastcached's GET benchmark (`bench/`) in `core-cpp-bench-base` (before) and `core-cpp-async-net` (after) on the same machine; record both in the PR body; any regression over 5% blocks. Remove the baseline worktree afterwards with `git -C D:\fastcached worktree remove D:\fastcached-worktrees\core-cpp-bench-base`.

**Docs:** `AGENT.md:20-27,615,653-654,696,1448-1449,1730,2205`, `.agent/reference/source-map.md:31-42`, and the rules files listed in the migration design.

- [ ] Verify the full preset matrix and the TSan gate. Open the draft PR titled "async: Task, the reactors and the sockets come from core-cpp".


# Rulings and facts that supersede the plan text above (2026-09-23)

1. **One pull request, one branch** (user ruling, 2026-09-22). C3 (tui/vendor) and C4 (async/net) go on a single branch `claude/<issue#>-core-cpp` from `origin/master`, with ONE tracking issue. The "PR-A / PR-B" split and the "based on PR-A's branch" worktree are void. Keep the plan's commit structure inside the one branch: the vendor/tui swap, the semantic rename, the swap, and the semantic deltas each stay a separate green commit.
2. **Worktrees only (user requirement).** Never pull, checkout, build or commit in `D:\fastcached`. The only commands allowed against it are `git -C D:\fastcached fetch origin` and `git -C D:\fastcached worktree add ...`. Worktrees go under `D:\fastcached-worktrees\<name>`: the branch worktree `core-cpp`, plus the detached benchmark baseline `core-cpp-bench-base` at origin/master (remove it when done). fastcached origin/master is `6eaea2cb` as of this brief; record what you base on.
3. **Pin:** `CPMAddPackage(NAME core-cpp GITHUB_REPOSITORY contour-terminal/core-cpp GIT_TAG v0.1.0 VERSION 0.1.0 ...)`. Iterate locally with `-DCPM_core-cpp_SOURCE=D:/core-cpp-wt-b13`, a clean checkout of core-cpp master at or after v0.1.0. The committed pin must be the tag.
4. **The module is `core::async`, not `core::coro`.** The plan's `core::coro`/`<core/coro/...>` spellings are stale. Use core-cpp's `tools/migrate/renames.json` (`--profile fastcached`) and `semantic_rename.py` as the source of truth for names.
5. **Breaking changes fastcached will meet** (all in core-cpp CHANGELOG 0.1.0 under Breaking; read it):
   - `WaitReadable` no longer returns 1 at once, and a full pipe no longer returns `WouldBlock`. Tests that expected an instant answer will park.
   - `probeHttpStatus` and `openUdpSocket` return `std::expected`, not null.
   - EPIPE and WSAECONNABORTED arrive as `SystemError`, not `ConnReset`.
   - TLS: EOF without close_notify is `ConnReset`; `wrapTls`/`ITlsContext::wrap` take an `IExecutor&`; `generateSelfSignedCertificate` takes `SelfSignedOptions` (default CN "localhost", P-256 keys). Keep fastcached's CN by passing `.commonName = "fastcache-node"` explicitly.
   - A flow's own stop throws `OperationCancelled`; `Task` co_await is rvalue-only.
6. **Known gap:** core-cpp did NOT import `BlockingListener` or `AcceptRaw`, which the admin endpoint uses. Keep fastcached's own copy of those two as fastcached code, built on core-cpp's socket types, and name that in the PR body as a candidate for core-cpp graduation. Do not reintroduce a second socket layer.
7. **Benchmark gate stands:** fastcached's GET benchmark, before (baseline worktree) and after, on the same machine. Both numbers go in the PR body; a regression above 5% blocks.
8. **Outward-facing actions you may take:** the tracking issue (fastcached's issue template, if any), pushing the branch, and opening the PR as a **draft** with the `contour-workflows:draft-pr` skill if available, otherwise `gh pr create --draft`. The PR body has a "Consumer impact" / migration section, the benchmark numbers, and the BlockingListener note. No merge, no ready-for-review.
9. **Commits:** every commit ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`. Follow fastcached's own AGENT.md / `.agent/rules/` for its gates (e.g. `scripts/local-gate.sh`, the TSan gate). Its rules win inside its repo.
10. **Environment:** WSL is shared with other sessions. Use a login shell (`wsl.exe bash -l <script>`), write logs to your scratchpad (not /tmp), and never touch foreign build dirs or processes. Run git on the Windows side for Windows-created worktrees. Windows writes CRLF: use Python write_bytes and check for `\r`. With MSVC /O2, keep calls out of `co_await` full-expressions (C4737).
