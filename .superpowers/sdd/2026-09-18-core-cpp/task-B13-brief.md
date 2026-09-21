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

