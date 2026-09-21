# Brief for Task C6

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


### Task C6: contour PR (`contour-terminal/contour`, branch `build/vendor-core-cpp`)
- [ ] **Delta check (first step).** Run `git log --oneline <provenance SHA>..origin/master -- <replaced paths>` in this consumer. If it lists anything applicable, port it into core-cpp first (patch release v0.1.x via contour-workflows:draft-release/publish-release) and bump this PR's pin. Record the result in the PR body (`delta: none` or the ported commits).
- [ ] **Commit 1, link what you include:** add explicit `boxed-cpp`/`reflection-cpp` links to vtbackend, vtpty, vtrasterizer, vtworkspace, vthost, contour, contour/config, contour/input. This is green on its own.
- [ ] **Commit 2, vendor:**
  - `cmake -DMODE=sync -DREF=v0.1.0 -DREPO=https://github.com/contour-terminal/core-cpp -DDEST=vendor/core-cpp -DMODULES="base;log;cli;platform;coro;net;testing" -P D:/core-cpp/cmake/CoreCppVendor.cmake`
  - Add `.gitattributes` (`vendor/core-cpp/** -text`), `.clang-format-ignore` (`vendor/**`), `vendor/README.md` ("never edit; fix upstream; re-sync command"), and `_typos.toml` `extend-exclude` `"vendor/**"`.
- [ ] **Commit 3, switch:**
  - `cmake/ContourThirdParties.cmake`, before the sweep at ~232: set `CORE_CPP_FETCH_DEPS ${CONTOUR_USE_CPM}`, `CORE_CPP_TESTING ${CONTOUR_TESTING_CORE_CPP}` (new option, OFF), `CORE_CPP_WITH_TUI OFF`, `CORE_CPP_WITH_TLS ON`; `find_package(OpenSSL REQUIRED)`; `add_subdirectory(vendor/core-cpp … SYSTEM [EXCLUDE_FROM_ALL])`; a summary row; the verbatim ctest (`CoreCppVendor.cmake MODE=check`, label `lint`).
  - Delete `src/coro`, `src/net`, and crispy's generic half (Part I §1). Keep contour's own `src/crispy` for the renderer half; its CMake links `core::base`.
  - `src/CMakeLists.txt:7-9`; `vthost:60` `net` → `core::net`. The 12 `crispy::core` sites keep `crispy::core` (contour's own) and add `core::base core::log core::cli` where the generic headers are used.
  - Keep the `<stop_token>` probe (`CMakeLists.txt:97-137`) with reworded paths.
  - Codemod with `rewrite.py --profile contour src` (generic crispy headers only; the renderer half keeps `crispy::`). vtparser includes → `<core/Utils.hpp>`, `<core/testing/SuppressWindowsDialogs.hpp>`. The forward declarations at `remote/RemoteController.hpp:32` and `vthost/Daemon.hpp:35` → `namespace core::net`.
  - CI:
    - Remove `crispy_test`/`coro_test`/`net_test` from `build.yml:525,1032-1033,1310-1318,1477-1491` and `.github/fedora/contour.spec:72`.
    - Add the verbatim check to the `check_clang_format` job.
    - `.clang-tidy:188` HeaderFilterRegex loses `coro|net`.
  - Docs: `AGENT.md:210,231-258,306`.
- [ ] **Verify:** `clang-debug`, `clangcl-debug`, `msvc-debug`, `ctest -L lint`, `clang-debug -DCONTOUR_USE_CPM=OFF` in the Fedora container (no fetch), and once `-DCONTOUR_TESTING_CORE_CPP=ON`.
- [ ] Open a **draft** PR titled "build: coro, net and the generic crispy utilities come from a verbatim copy of core-cpp". Mark it ready only after the C1 and C2 PRs have merged.

