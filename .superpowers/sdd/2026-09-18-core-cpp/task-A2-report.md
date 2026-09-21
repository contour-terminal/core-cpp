# Task A2 report: docs site, guidelines, CI, GitHub repository

Status: DONE_WITH_CONCERNS. `ci-ok` and Docs are green on the latest master commit (53ef21f).

## Commits (master, in order)

| SHA | Subject |
|---|---|
| 5d9f259 | build: default the C++ standard to 23 rather than override it |
| 1ff7fb4 | build: emscripten presets for single-threaded WebAssembly |
| 1b09b3e | test: a release tag must name the tree it releases |
| 6a370f6 | docs: README, changelog, contributing guide and security policy |
| dfa97ed | docs: agent guidelines and the rulebook |
| 4947a6a | docs: documentation site and API reference |
| bfa1c31 | ci: build, docs, release, downstream and portability workflows |
| 8b2383b | ci: install GCC 15 from the toolchain PPA, and assert the compiler |
| 1be374d | ci: read the C++26 leg's standard from core-cpp's own sources only |
| a1de1cc | ci: install libyaml-cpp for the auto-installed fastcached daemon |
| 2a524ad | cmake: default C++20 module scanning off, for the configure's probes too |
| 53ef21f | docs: the clang-cl depfile caveat is fixed in fastcache-cc ca8dfc32 |
Every commit is LF-only and ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`.
`.superpowers/` is not in any commit.

## Files created

Repository documents: `README.md` (replaced the stub), `CHANGELOG.md`, `CONTRIBUTING.md`,
`SECURITY.md`, `AGENT.md`, `CLAUDE.md` (`@AGENT.md`); `NOTICE` extended with the documentation and
CI adaptations (contour 6777ff05, endo f774a210, fastcached b5ded89c, Lightweight f57dc2e0,
tuidu 30107fba).

`.agent/`:
- `rules/`: `README.md`, `cpp-guidelines.md`, `design-principles.md`, `library-hygiene.md`,
  `build-and-toolchain.md`, `testing.md`, `async-and-net.md`, `platform.md`, `tui.md`
- `guides/`: `team-run.md`, `profiling-tracy.md`, `consumer-migration.md`, `releasing.md`
- `reference/`: `source-map.md`, `consumers.md`

Docs site: `mkdocs.yml`, `docs/requirements.txt` (fastcached's, byte-identical, blob d3947e2a),
`docs/Doxyfile`, `docs/index.md`, `docs/vendoring.md`, `docs/changelog.md` (snippet of
`CHANGELOG.md`), `docs/getting-started/{cpm,building,options}.md`,
`docs/modules/{index,base,log,cli,platform,coro,net,tui,testing}.md`,
`docs/design/{dependency-injection,error-handling,coroutines-and-lifetimes,threading,portability,profiling}.md`,
`docs/contributing/{index,cpp-guidelines,releasing}.md` (index and cpp-guidelines are snippets of
`CONTRIBUTING.md` and `.agent/rules/cpp-guidelines.md`).

`.github/`: `workflows/{build,docs,release,downstream,portability}.yml`, `release.yml`,
`dependabot.yml`, `pull_request_template.md`, `clang-tidy-matcher.json` (endo's, byte-identical,
blob a1542237), `vcpkg/vcpkg.json` (OpenSSL for the Windows TLS leg, manifest mode).

Release check: `tests/cmake/check-release.cmake` (tag = `project(VERSION)`, and CHANGELOG has the
section) and `tests/cmake/check-release-selftest.cmake` (10 cases; every refusal asserted by its
own phrase and no other), registered as ctest `core-cpp.check-release-selftest`.

Files modified from A1: `CMakePresets.json` (emscripten presets, R16), `tests/CMakeLists.txt`
(the release self-test), `cmake/CoreCppTopLevel.cmake` (two framework fixes, below).

## How each guideline file was derived

All fastcached text was read as `origin/master` blobs (b5ded89c), the others at the SHAs in NOTICE.
Every carried rule is rewritten against core-cpp paths and cites its origin as a full URL (issue
URL, or a blob URL pinned at the SHA). A scan over `.agent/`, `AGENT.md`, `docs/` and the
repository documents finds no bare `#<digits>` and no `FastCache/` or `src/FastCache` path, except
in `guides/consumer-migration.md`, whose rename table and leftover check must name the old paths.

| File | Sources | Carried | Dropped |
|---|---|---|---|
| `AGENT.md` | fastcached AGENT.md (the rulebook shape: index plus one tripwire per file); spec Part I | index of the rulebook, library rules, the C++ guidelines as a short list pointing to the canonical file, building/testing/docs/releasing, workflow checklist; the fastcached#1531 clang-cl caveat as a tripwire | fastcached's product, cluster and packaging sections |
| `rules/README.md` | fastcached `.agent/rules/README.md` | entry criterion ("every rule was a bug"), "Adding a rule", the `## Open work` grammar, "do not `@`-import" | its census, and the ctest that enforces the grammar (nothing in core-cpp reads it yet; said so) |
| `rules/cpp-guidelines.md` | contour AGENT.md §C++ and zero-warning policy; fastcached AGENT.md §C++ Coding Guidelines; Lightweight `.agent/cpp-guidelines.md`; tuidu AGENT.md; endo AGENT.md; fastcached#366, #395, #1452 | baseline, forbidden constructs (C-style loops, `NOLINT`, `g_`/`k` prefixes), zero warnings, the naming table (checked against `.clang-tidy`: local const camelBack, constexpr and class constants CamelCase), self-contained headers, `[[nodiscard]]` on builders, coroutine parameters by value, docs, pinned tools, the checklist. Absolute links only, because the docs site includes it | Lightweight SQL/ODBC and contour Qt/QML carve-outs; the `views::enumerate` suggestion (libc++ gap) |
| `rules/design-principles.md` | contour AGENT.md; endo AGENT.md; fastcached AGENT.md, wire-and-protocol and build-and-toolchain; fastcached#188 | DI by constructor injection, configuration at construction, data-driven design, `std::expected`, `enum class` over `bool`, RAII, testability, caching only where staleness degrades safely | domain examples, replaced by core seams (`IClock`, `ISocket`, `FileSystem`, `EnvironmentProvider`) |
| `rules/library-hygiene.md` | spec §1, §3, §4, §5; contour, endo, tuidu AGENT.md; fastcached#100 | no global state unless top-level, prefixes, layering, dependency table, single-thread-safe WASM subset, vendoring, graduation rule, API and versioning | n/a (new) |
| `rules/build-and-toolchain.md` | fastcached build-and-toolchain.md and the issues it cites (#155, #300, #315, #454, #611, #684, #774, #805, #1342, #1389, #1420, #1439, #1442, #1447, #1452) | the ten sections of the table (NDEBUG, Windows Debug leg, modal dialogs, language and ABI pitfalls, `char`, line endings, WERROR, claims checked against the tool, a silent gate, C-style loops); the compile-cache rule (R11) and the fastcached#1531 clang-cl caveat (R13, as updated: needed only with a fastcache-cc older than ca8dfc32, with the measured cause from fastcached#1533); a module-scanning rule measured in this task (2a524ad) | local-gate lock and WSL archaeology, compile-cache internals, "What CI costs", merge-queue specifics |
| `rules/testing.md` | fastcached testing.md; #172, #355, #405, #499, #636, #685, #729, #734, #735, #778, #895, #1128, #1152, #1212, #1313, #1446 | bounded waits, wall vs steady clock, the case name as an argument, `REQUIRE` above a stop, permissive fakes, `SUCCEED` vs `SKIP`, assert what distinguishes, first failure masks its siblings, `--repeat until-fail`; the SKIP section rewritten as solved by `core::testing_main`'s exit-code normalisation; plus "a crashed run is not a run with zero failures" (#1212) | fleet, cluster and e2e fixtures, the POSIX shell helper library, leader-pinned commands |
| `rules/async-and-net.md` | fastcached wire-and-protocol.md §The Net boundary, §Sockets, §Dialing and the reactor, §Socket and coroutine lifetime; profiling-tracy guide ("zones never straddle `co_await`"); about twenty issues, each cited | layering, the event-loop contract, sockets, dialling and backends, socket and coroutine lifetime, zones never span `co_await` | framing, authentication, keyspace events, expiry, disk tier |
| `rules/platform.md` | endo AGENT.md §New Platform Feature; fastcached platform-service-and-config.md ("what a machine is") | an implementation per platform, never an `#ifdef` in logic; what a machine is; Windows specifics; the WASM subset | endo builtins and language; the service and config parts of the fastcached file |
| `rules/tui.md` | endo `src/tui/CMakeLists.txt` (the stb UBSan reason); tuidu AGENT.md | two targets and the leaf, output through the destination, DI, runtime on the event loop, Windows console tests, images | endo builtins |
| `guides/team-run.md` | fastcached `.agent/guides/team-run.md` | roles, lanes (become modules), ticket assignment, coordination, review gates, docs and consumers in every PR, bug intake, verification | fastcached board specifics |
| `guides/profiling-tracy.md` | fastcached `.agent/guides/profiling-tracy.md` | building, zones, capture; `FC_*` became `CORE_ZONE_*`, `TRACY_ENABLE` became `CORE_CPP_WITH_TRACY` | the fastcached daemon's build target and capture recipe |
| `guides/consumer-migration.md` | spec §2 (rename map) and §7 | order, common steps, CPM snippet, per-consumer steps, renames, prohibitions, leftover checks | n/a (new) |
| `guides/releasing.md` | spec §4 | versioning, what a release carries, cutting one, the `check-release.cmake` gate | fastcached packaging |
| `reference/source-map.md`, `reference/consumers.md` | spec Part I; the consumers' build files | annotated tree and "where a new file goes"; per consumer, CPM or vendored and where the pin lives | n/a (new) |

Rules carried beyond the table's list, because they are generic and had a measured failure behind
them: "a `cmake -P` check is judged by its output" and the libc++ gaps (build-and-toolchain),
"a crashed run is not zero failures" (testing), "what a machine is" (platform).

## Local verification

- Docs: `mkdocs build --strict` in a venv from `docs/requirements.txt`:
  ```
  INFO    -  Cleaning site directory
  INFO    -  Building documentation to directory: D:\core-cpp\site
  INFO    -  Documentation built in 0.33 seconds
  ```
  No omitted-nav or not-found warning. Negative controls (a page left out of the nav, a snippet
  path that does not exist) each fail the strict build. `doxygen docs/Doxyfile` wrote
  `site/api/index.html`.
- Builds: Windows `clangcl-debug` (clean) 8/8 and `cl-debug` 8/8; WSL `clang-debug` 5/5 and
  `gcc-debug` 5/5; clang-format check clean; Emscripten 3.1.56 and latest 4/4 with no `pthread`
  in any command; a local compile-cache run (111 `fastcache-cc: HIT`); a clang-tidy run clean,
  with the planted canary reported.
- actionlint 1.7.12 over every workflow: clean.
- After the CI fixes: the C++26 assertion script run against two local compile databases (C++26
  build passes 6/6, default build rejects 6/6); a Linux configure with
  `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` pointing at a missing file reproduced the FreeBSD failure
  exactly and passed configure, build and ctest 5/5 with 2a524ad; the hygiene scan clean
  (28 files).

## GitHub

- Repository: https://github.com/contour-terminal/core-cpp (public, default branch master,
  homepage set, labels `type/{feature,bug,perf,docs,chore}`, `breaking-change`, one `module/<m>`
  per module). No branch protection was configured.
- Pages: `gh api repos/contour-terminal/core-cpp/pages --jq '.build_type, .html_url'` prints
  `workflow` and `https://contour-terminal.github.io/core-cpp/`. The site answers 200 (title
  "core-cpp"), `/api/` answers 200 (title "core-cpp: Main Page").
- Green Build run on 53ef21f (latest master, 20/20 jobs including ci-ok):
  https://github.com/contour-terminal/core-cpp/actions/runs/35336343998
- Docs run on 53ef21f (build and deploy): https://github.com/contour-terminal/core-cpp/actions/runs/35336344061
- Green Build run on 2a524ad (the job evidence below is from this run; 53ef21f changed only docs):
  https://github.com/contour-terminal/core-cpp/actions/runs/35335132115
- Docs run on 2a524ad (build and deploy, dispatched because the commit touched no docs path):
  https://github.com/contour-terminal/core-cpp/actions/runs/35335178305. The first Docs run, on
  bfa1c31, also succeeded: https://github.com/contour-terminal/core-cpp/actions/runs/35334252047
- Portability (FreeBSD 14.3, dispatched) on 2a524ad: success,
  https://github.com/contour-terminal/core-cpp/actions/runs/35335132099
- Downstream (dispatched) on bfa1c31: success; both verbatim files are identical to fastcached
  master ca8dfc32. https://github.com/contour-terminal/core-cpp/actions/runs/35334305010
- Release: not run (no tag). Its vendor-archive step fails with a clear message until A8 lands
  `cmake/CoreCppVendor.cmake`.

### Final status of every Build job (run 35335132115)

| Job | Result | Evidence from the log |
|---|---|---|
| style | success | pinned clang-format, hygiene, hygiene and release self-tests, CR check |
| linux (clang-22) | success | ctest 5/5 |
| linux (gcc-14) | success | ctest 5/5 |
| linux (gcc-15) | success | `g++-15 (Ubuntu 15.2.0-14ubuntu1~24~ppa1) 15.2.0`, ctest 5/5 |
| linux (clang-22-arm64) | success | ctest 5/5 |
| linux (clang-22-cxx26) | success | `6 compile(s) under .../src/, .../tests/; 0 not compiled as C++26`, ctest 5/5 |
| macos (appleclang) | success | |
| macos (llvm-22) | success | |
| windows (cl-release) | success | |
| windows (clangcl-release) | success | |
| windows (cl-debug) | success | |
| windows (cl-release-tls) | success | `found .../vcpkg_installed/x64-windows/include/openssl/ssl.h` |
| sanitizers (clang-asan-ubsan) | success | |
| sanitizers (clang-tsan) | success | |
| clang-tidy | success | canary reported, `--tidy=` wired into build.ninja |
| emscripten (emsdk 3.1.56) | success | no `pthread` in any command |
| emscripten (emsdk latest) | success | no `pthread` in any command |
| compile-cache | success | `Auto-started fastcached ... 127.0.0.1:6674`, `112 launcher binding(s) in build.ninja name fastcache-cc`, rebuild `hits: 111 (100.0% of 111 cacheable)`, `misses: 0`, ctest 5/5 |
| coverage (not required) | success | |
| ci-ok | success | |

### CI fixes pushed

The first Build run (bfa1c31, https://github.com/contour-terminal/core-cpp/actions/runs/35334252040)
had three red jobs, and the first Portability run one:

1. **linux (gcc-15)**, 8b2383b. `apt-cache show g++-15` succeeded although noble has no g++-15:
   apt reads an unknown name as a regex, `g++-15` matches `clang-15`, and `apt-get install g++-15`
   then installed clang-15 and friends. Configure failed with "g++-15 is not a full path and was not
   found in the PATH". The step now tests `command -v g++-N`, adds the toolchain PPA only when it
   is missing, and asserts the compiler exists afterwards.
2. **linux (clang-22-cxx26)**, 1be374d. The assertion excluded dependencies by `/_deps/`, but
   with `CPM_SOURCE_CACHE` set Catch2's sources are in `.cache/cpm`, so its 106 compiles (pinned
   to C++23 by A1's WRAP, on purpose) counted as core-cpp's. It now selects compiles under `src/`
   and `tests/` and names each rejected file in the message. The A1 standard fix (5d9f259) was
   already doing its job: core-cpp's 6 compiles were `-std=c++26`.
3. **compile-cache**, a1de1cc. `[cache] Not starting a daemon: fastcached exited immediately
   (127)`, then `Not using fastcache-cc ... fetch exchange failed`. `readelf -d` on the 0.2.0
   release shows `fastcached` needs `libyaml-cpp.so.0.8`, which the ubuntu-24.04 image lacks
   (`fastcache-cc` needs only libstdc++ up to GLIBCXX_3.4.32). The job now installs
   `libyaml-cpp0.8` (noble main). No assertion changed, and fastcached was not touched.
4. **Portability (FreeBSD)**, 2a524ad, an A1 framework fix. Every `check_cxx_compiler_flag()`
   probe failed, then "compiler accepts -pthread - no" and a fatal "Threads is needed". Cause:
   from C++20 on CMake scans every try_compile for modules, and FreeBSD's base clang 19.1.7 has
   no clang-scan-deps; `core_cpp_apply_toolchain()`'s `CXX_SCAN_FOR_MODULES OFF` only reaches
   targets. Reproduced on Linux by pointing `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` at a missing
   file. Fix: `CoreCppTopLevel.cmake` defaults `CMAKE_CXX_SCAN_FOR_MODULES` to OFF (top-level
   only, `if(NOT DEFINED)`), with the rule in `.agent/rules/build-and-toolchain.md`. The re-run
   shows `CORE_CPP_HAS_Wall - Success`, `-pthread - yes`, ctest 5/5 on FreeBSD.

Earlier A1 framework fix, before the first push: 5d9f259. `CoreCppTopLevel.cmake` set
`CMAKE_CXX_STANDARD 23` as a normal variable, which shadowed `-DCMAKE_CXX_STANDARD=26`, so a C++26
leg would have built C++23 (112 of 112 compiles were `-std=c++23`). It is now a default.

## Self-review

- Every required file of the brief exists; the nav lists every page and `--strict` holds it.
- R11: the compile-cache job asserts `CORE_CPP_CXX_COMPILER_LAUNCHER:INTERNAL=...fastcache-cc`
  and a `LAUNCHER = ...fastcache-cc` binding in build.ninja, then proves hits (a rebuild after
  deleting the objects must log at least one `fastcache-cc: HIT`, and `--show-stats` must count
  one). The assertion was not weakened: the first red was a real defect (the daemon could not
  start), fixed by installing its runtime library.
- R13 in `rules/build-and-toolchain.md`, as a tripwire and checklist note in `AGENT.md`, and on
  the building page, all rephrased after the update: `--clean-first` only with a fastcache-cc
  older than fastcached ca8dfc32; cause `-clang:-MF` not recognised (not `/showIncludes`); fix in
  the binary only, entries heal; citing fastcached#1531 and fastcached#1533. R15: no consumer-smoke
  job; `downstream.yml` holds only the drift job. R16: `emscripten` presets as specified plus
  `-fexceptions`, and a matrix of emsdk 3.1.56 and latest.
- `ci-ok` needs every required job and checks each result is `success`, with `if: always()`.
- Each gate that could pass without testing anything has a check that it ran: the C++26 leg reads
  the standard back from the compile database, the clang-tidy job must report a planted canary,
  the compile-cache job must see hits, the Windows TLS leg must find vcpkg's `ssl.h`, the
  Emscripten job must find no `pthread`.
- The CompileCache.cmake copy was not edited; the drift job found it identical to fastcached
  master.

## Concerns

1. **Docs as a required check.** The spec calls docs.yml required, but it has path filters: a
   pull request that does not touch those paths never gets a Docs result and would wait forever.
   It is not in `ci-ok`. No branch protection was configured (as instructed), so nothing is
   required yet; when protection is set up, require `ci-ok` only.
2. **Dependabot.** The org enables Dependabot security updates on new repositories. There are six
   open alerts against `docs/requirements.txt`, which is fastcached's file byte for byte
   (mkdocs-material 9.5.39, pymdown-extensions 10.11, Pygments 2.19.1). These are docs build tools
   and nothing from them ships. Dependabot opened PRs #1 (a group of 7 action major bumps, for
   example checkout 4 to 7), #2 (Pygments), #3 (mkdocs-material) and #4 (pymdown-extensions). #4
   fails Docs because pip cannot satisfy it together with the mkdocs-material pin. I merged none
   of them, since the file is meant to stay verbatim. The decision is to bump fastcached's pins
   and re-sync, or to let core-cpp diverge.
3. **A fastcached defect I did not file.** The 0.2.0 Linux release needs `libyaml-cpp.so.0.8`
   at runtime. `FASTCACHE_AUTO_INSTALL` neither provides it nor names it, and the only sign is
   "exited immediately (127)". This is worth a fastcached issue. Filing one is outward-facing,
   so I left it for you.
4. **Two A1 framework changes, both in `CoreCppTopLevel.cmake`** (global state, top-level
   only): the C++ standard is now a default (5d9f259), and `CMAKE_CXX_SCAN_FOR_MODULES` defaults
   to OFF (2a524ad).
5. **Commits.** There are 12 small semantic commits, not the brief's single commit message.
6. **Open-work issues.** The plan's open-work issues were not created, so no `## Open work`
   entry exists yet.
7. **`FastCache/` paths.** `.agent/guides/consumer-migration.md` names them on purpose: its
   rename table and leftover check need the old paths. They appear nowhere else.
8. **Coverage.** It is done inside the workflow (llvm-profdata, llvm-cov, lcov and html as an
   artifact), not as a CMake `coverage` target. It went green in CI but was not run locally.
9. **clang-tidy annotations.** The problem matcher is endo's verbatim file. `.clang-tidy` has
   `UseColor: true`, and it is not verified that the matcher still turns a finding into an
   annotation. The CI run had no findings to annotate; the canary step reads plain text.
10. **Emscripten presets.** They add `-fexceptions` to R16's list. Catch2's `SKIP` and `REQUIRE`
    throw, and a build without it did not test correctly (measured locally).
11. **Rules beyond the table.** The rulebook carries four generic rules the derivation table does
    not list; see "How each guideline file was derived".
12. **Side effect on this machine.** My local compile-cache simulation ran `fastcache-cc
    --zero-stats`, which cleared the WSL user's fastcache-cc statistics
    (`~/.local/state/fastcache-cc/invocations.log`).
13. **fastcached SHAs.** `D:\fastcached`'s `origin/master` now reads ca8dfc32. The guideline
    citations stay pinned at b5ded89c, where they were read.

## Fix round 1

This round fixes the three Important findings of `task-A2-review.md`. The Minor findings are
deferred, as instructed. `ci-ok` and Docs are green on the final commit, 2a7b79f.

| SHA | Subject |
|---|---|
| d333c7b | docs: track the open work as core-cpp issues, not prose |
| 37972a6 | ci: keep vcpkg's binary archives in a files provider that works |
| 2a7b79f | test: the hygiene scan refuses a namespace its directory does not name |

### 1. Open work tracked as issues (d333c7b)

Issues created in contour-terminal/core-cpp (R2), one per item:

| Item | Issue | Labels |
|---|---|---|
| (a) install()/export, vcpkg port | https://github.com/contour-terminal/core-cpp/issues/5 | type/feature |
| (b) remove WfmoBackend after one release with IOCP green | https://github.com/contour-terminal/core-cpp/issues/6 | type/chore, module/net |
| (c) unify core::Environment with EnvironmentProvider | https://github.com/contour-terminal/core-cpp/issues/7 | type/chore, module/base, module/platform |
| (d) graduate further fastcached Core generics | https://github.com/contour-terminal/core-cpp/issues/8 | type/feature, module/base, module/log, module/cli |
| (e) morph's follow-up (executors/strand, logger, FileIoOps, DateTime clock seam, morph::net on Windows) | https://github.com/contour-terminal/core-cpp/issues/9 | type/feature, module/coro, module/net, module/log, module/platform |
| (f) Doxygen FAIL_ON_WARNINGS | https://github.com/contour-terminal/core-cpp/issues/10 | type/docs |
| prose residual: the cl-debug must-die canary (`build-and-toolchain.md:90`) | https://github.com/contour-terminal/core-cpp/issues/11 | type/chore, module/testing |
| prose residual: no check reads the Open work grammar (`.agent/rules/README.md:76`) | https://github.com/contour-terminal/core-cpp/issues/12 | type/chore |

- The review's `README.md:76` is `.agent/rules/README.md:76` ("nothing in core-cpp reads it
  yet"). The top-level `README.md:76` is a row of the presets table and defers nothing.
- Each issue body says what is left, and where in the rulebook it is recorded. Issues (a) to (f)
  also cite the plan's "Open work after v0.1.0" list.
- `## Open work` sections were added in the grammar of `.agent/rules/README.md`: a top-level
  bullet whose leading reference is the bold issue link, then an em dash. They are:
  - `library-hygiene.md`: #5, #8
  - `async-and-net.md`: #6, #9
  - `platform.md`: #7
  - `cpp-guidelines.md`: #10
  - `build-and-toolchain.md`: #11
  - `rules/README.md`: #12
- The prose deferrals now link their issue:
  - `library-hygiene.md` ("exporting them is core-cpp#5");
  - `build-and-toolchain.md` ("core-cpp's is core-cpp#11");
  - `rules/README.md` ("until core-cpp has that check (core-cpp#12)");
  - `docs/Doxyfile` (the comment now gives the #10 URL).
- A `git grep` for "open work", "after 0.1.0", "none yet" and "not yet" finds no other prose
  deferral.
- `mkdocs build --strict` is clean locally (0.36 s). `cpp-guidelines.md` is included in the site,
  and its links are absolute.

### 2. vcpkg binary cache (37972a6)

**Cause.** The evidence is in the log of run 35336343998:
- `lukka/run-vcpkg@v11` logged `Set the workflow environment variable 'VCPKG_BINARY_SOURCES' to
  value 'clear;x-gha,readwrite'`.
- vcpkg then warned `The 'x-gha' binary caching backend has been removed`.
- The cache step found nothing to restore and nothing to save, so OpenSSL was compiled on every
  run.

**Changes.**
- The job env now has `VCPKG_COMMIT` (one source for run-vcpkg and the cache key) and
  `VCPKG_ARCHIVES` (`<workspace>/.cache/vcpkg-archives`).
- After run-vcpkg, the step "Use vcpkg's toolchain and binary archives" writes
  `VCPKG_BINARY_SOURCES=clear;files,<archives>,readwrite` to `GITHUB_ENV`. It uses forward
  slashes, and it overrides run-vcpkg's value.
- `actions/cache` keeps `.cache/vcpkg-archives`. The key is
  `vcpkg-archives-x64-windows-<vcpkg commit>-<hash of .github/vcpkg/vcpkg.json>`, with restore
  prefix `vcpkg-archives-x64-windows-<vcpkg commit>-`.
- The claim at the old `build.yml:291-293` is rewritten to say what happens now.
- The OpenSSL assertion step now also requires at least one `*.zip` in the archive directory. An
  inert provider therefore fails the job instead of reading as working.

**Proof.** Run 35337806655 (37972a6) populated the cache, and run 35338121277 (2a7b79f) restored
it.
- Populating run, `windows (cl-release-tls)`:
  ```
  Restored 0 package(s) from D:/a/core-cpp/core-cpp/.cache/vcpkg-archives in 44.2 us.
  Building openssl:x64-windows@3.6.4...
  Elapsed time to handle openssl:x64-windows: 4.5 min
  Completed submission of openssl:x64-windows@3.6.4 to 1 binary cache(s) in 1.9 s (1/1)
  4 binary archive(s) in D:\a\core-cpp\core-cpp/.cache/vcpkg-archives
  Cache saved with key: vcpkg-archives-x64-windows-e6f9e70a29a3e80a1fc510d8503304315447112f-7d1276ee3b83c9bbc0bcd24006a58541c0af96138efc1bd8d5b6a2945511522a
  ```
  `gh api .../actions/caches` lists that key at 21,792,912 bytes, on refs/heads/master.
- Restoring run, `windows (cl-release-tls)`:
  ```
  Cache hit for: vcpkg-archives-x64-windows-e6f9e70a29a3e80a1fc510d8503304315447112f-7d1276ee3b83c9bbc0bcd24006a58541c0af96138efc1bd8d5b6a2945511522a
  Restored 4 package(s) from D:/a/core-cpp/core-cpp/.cache/vcpkg-archives in 575 ms.
  Installing 4/4 openssl:x64-windows@3.6.4...
  Elapsed time to handle openssl:x64-windows: 33.1 ms
  ```
  There is no `Building openssl` line. The job ran from 11:10:39 to 11:12:03, 1 min 24 s, against
  about 10 min before. `ci-ok` finished at 11:13:12.

### 3. namespace = directory (2a7b79f), per R21

**The rule.** A new file-level rule, `namespace-directory`, is in
`tests/cmake/check-cmake-hygiene.cmake`. For every C++ source under `src/core/`:
- In `src/core/<dir>/...`, where `<dir>` is the first path component under `src/core/`, the first
  named namespace must be `core::<dir>` or nested in it. Examples: `core::<dir>::detail`; a file
  in `src/core/net/detail/` declaring `core::net::detail`.
- In a file directly in `src/core/`, the first named namespace must be `core` or nested in it,
  such as `core::base64`.
- Only one spelling is accepted: the nested-block spelling `namespace core { namespace <dir>` is
  refused, because its first named namespace is `core`. This is stated in `cpp-guidelines.md`.
- Anonymous namespaces and namespace aliases are skipped. A file with no named namespace is not
  checked: a `main()`, TU-local helpers, a macro-only header such as `Config.hpp.in`.
- Helper namespaces inside the first are free.

**Implementation.** The allowlist lookup moved into `core_cpp_hygiene_refuse()`, which the line
rules and the new rule share. The rule is in `LIST_RULES`, so the self-test requires cases for
it.

**Self-test.**
- Each case now has its own directory (`<index>-<rule>`), so a rule can have several cases.
- Refused, each by name and file:
  - `namespace bar` in `src/core/foo/Foo.cpp`;
  - `namespace core::foobar` in the same file, the prefix trap;
  - the nested spelling in `src/core/foo/detail/Bar.hpp`;
  - `namespace crispy` in `src/core/Top.hpp`.
- The clean tree gained an alias followed by `core::foo::detail`, an anonymous-only
  `Main.cpp`, `core` with a nested `views`, and `core::base64` directly in `src/core/`. It
  passes.
- The case rows' `namespace foo {}` became `namespace core::foo {}`, so each case produces only
  its own refusal.
- `cpp-guidelines.md` and `library-hygiene.md` now name the check. The `style` step carries a
  one-line comment. The step itself was already running the scan and the self-test.

**Commands and output.**
- Windows, CMake 4.3.1-msvc1:
  ```
  cmake -DROOT=D:/core-cpp -P tests/cmake/check-cmake-hygiene.cmake
  -- check-cmake-hygiene: 28 file(s) under D:/core-cpp are clean
  cmake -DSCANNER=D:/core-cpp/tests/cmake/check-cmake-hygiene.cmake -DWORK_DIR=<scratch> -P tests/cmake/check-cmake-hygiene-selftest.cmake
  -- hygiene-selftest: the clean tree passed and all 20 violations were refused by name
  ```
- WSL, CMake 4.2.3: the same two commands print `28 file(s) under /mnt/d/core-cpp are clean` and
  `all 20 violations were refused by name`.
- Proof that the test can fail:
  - With the refusal replaced by a `message(STATUS)`, the self-test fails exactly the four
    namespace cases (`namespace-directory: src/core/foo/Foo.cpp was not refused` twice, then
    `.../detail/Bar.hpp` and `src/core/Top.hpp`).
  - With the `::` boundary dropped from the nesting regex, it fails exactly the `core::foobar`
    case.
- Negative control on a copy of the real tree, with `ExitCode.hpp`'s `namespace core::testing`
  changed to `namespace testing`:
  ```
  src/core/testing/ExitCode.hpp:8: [namespace-directory] a source's first namespace is the one its directory names: src/core/<dir>/ is core::<dir>, src/core/ is core (Part I §1)
      namespace testing    (expected core::testing)
  ```
- The existing `src/core/testing/*` files pass: `ExitCode.{hpp,cpp}` and
  `SuppressWindowsDialogs.{hpp,cpp}` declare `core::testing`, and the three files with only an
  anonymous namespace are not checked.
- CI `style` job, run 35338121277:
  - `check-cmake-hygiene: 28 file(s) ... are clean`;
  - `hygiene-selftest: the clean tree passed and all 20 violations were refused by name`.
- Other checks this round: actionlint 1.7.12 clean, pinned clang-format clean, `mkdocs build
  --strict` clean, and every changed file is LF.

### CI on the final push

- Build https://github.com/contour-terminal/core-cpp/actions/runs/35338121277 (2a7b79f): 20 of 20
  jobs succeeded, including `ci-ok`.
- Docs https://github.com/contour-terminal/core-cpp/actions/runs/35338121295 (2a7b79f): build
  and deploy succeeded.
- The intermediate push 37972a6 was also green: Build 35337806655 and Docs 35337806554.

### Concerns from this round

- The namespace rule checks the *first named* namespace only. A file that declares no named
  namespace, for example a header whose API is at global scope, passes. Checking that would
  need a rule for "a header declares something", which is out of this round's scope.
- The review suggested an include-edge check for the other half of "layering". It was not
  requested and was not added; link-level layering is still enforced by `core_cpp_add_module`.
- Issue (e) records morph's follow-up before morph's own issue exists; Task C8 Step 6 creates
  that, and its link belongs in #9.
