# Brief for Task C4

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


The common steps for every consumer are:
1. Create the worktree with superpowers:using-git-worktrees, using the Global Constraints location. Never pull into or build in the main checkout:
   ```powershell
   git -C D:\<repo> fetch origin
   git -C D:\<repo> worktree add D:\<repo>-worktrees\core-cpp -b <branch> origin/master
   ```
2. Pin with `GIT_TAG v0.1.0` + `VERSION 0.1.0`; iterate locally with `-DCPM_core-cpp_SOURCE=D:/core-cpp`.
3. Build and test with the repo's own presets on Windows and WSL.
4. Run superpowers:requesting-code-review, then open the PR with **contour-workflows:draft-pr**.
5. Drive CI to green with **contour-workflows:fix-ci**.


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
   - `target_link_libraries(FastCache PUBLIC core::coro core::net)`.
   - `FASTCACHED_ENABLE_TLS` forwards to `CORE_CPP_WITH_TLS` and links `core::net_tls`.
   - Includes via `rewrite.py --profile fastcached`.
   - `fastcache-cc` links only `core::coro core::net_types` (assert with its existing link check, `.agent/rules/wire-and-protocol.md:134-135`).
3. **Semantic deltas** with tests: stop tokens replace polled cancellation (16 sites), `DeadlineTimer` without a poll interval, deferred resumption (add `runUntilIdle()` where a test asserted inline completion after `close()`).

**Benchmark gate:** run fastcached's GET benchmark (`bench/`) in `core-cpp-bench-base` (before) and `core-cpp-async-net` (after) on the same machine; record both in the PR body; any regression over 5% blocks. Remove the baseline worktree afterwards with `git -C D:\fastcached worktree remove D:\fastcached-worktrees\core-cpp-bench-base`.

**Docs:** `AGENT.md:20-27,615,653-654,696,1448-1449,1730,2205`, `.agent/reference/source-map.md:31-42`, and the rules files listed in the migration design.

- [ ] Verify the full preset matrix and the TSan gate. Open the draft PR titled "async: Task, the reactors and the sockets come from core-cpp".

