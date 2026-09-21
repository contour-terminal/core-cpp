# Task A2 review: docs site, guidelines, CI, GitHub repository

Reviewer: review-A2. Diff 0d80b3e..53ef21f (12 commits). Live state checked with `gh` on 2026-09-18,
read-only. Build run 35336343998 on HEAD 53ef21f finished green (ci-ok success); Docs run
35336344061 on 53ef21f is green.

### Spec Compliance

**Verified present and matching:**
- `build.yml` jobs as the spec §4 table and a2-context list them: style; linux (clang-22, gcc-14,
  gcc-15, clang-22-arm64 on `ubuntu-24.04-arm`, clang-22-cxx26); macos-15 (appleclang, Homebrew
  llvm-22, `OPENSSL_ROOT_DIR` from brew); windows (cl-release, clangcl-release, cl-debug,
  cl-release-tls via `lukka/run-vcpkg` and `ilammy/msvc-dev-cmd`); sanitizers (asan+ubsan all tests,
  tsan with `-LE no-tsan` and `TSAN_OPTIONS=halt_on_error=1`); clang-tidy (PyPI 22.1.8 as
  `CORE_CPP_CLANG_TIDY_EXE`, `-k 0`, matcher); emscripten (3.1.56 and latest, under node);
  compile-cache; coverage (not in `needs`); `ci-ok`. Every job ran and passed in run 35335132115
  (2a524ad) and 35336343998 (53ef21f). ctest counts: 5/5 on Linux, macOS, sanitizers, emscripten,
  compile-cache; 8/8 on the four Windows legs.
- `ci-ok`: `if: always()`, `needs:` the eight gating jobs, fails unless every
  `needs.*.result == 'success'` (`build.yml:664-682`). Correct: skipped and cancelled fail it.
- R11: `build.yml:561-570` asserts `CORE_CPP_CXX_COMPILER_LAUNCHER:INTERNAL=...fastcache-cc` and
  `LAUNCHER = ...fastcache-cc` in build.ninja; the run log shows 112 bindings, rebuild
  `hits: 111 (100.0%)`, `misses: 0`.
- R13 update: the caveat is scoped to fastcache-cc older than ca8dfc32 in
  `.agent/rules/build-and-toolchain.md`, the `AGENT.md` tripwire and checklist, and
  `docs/getting-started/building.md`.
- R15: no consumer-smoke job; `downstream.yml` holds only the drift job (dispatched run green).
- R16/R20: `emscripten` configure/build/test/workflow presets with `-fexceptions`,
  `CORE_CPP_WITH_TUI=OFF`, emulator `node`; CI asserts 0 commands mention pthread.
- R17: only `ci-ok` is meant to be required; `docs.yml` has path filters and says so in its header.
- R19: `libyaml-cpp0.8` installed in compile-cache.
- `docs.yml`: PR builds only; master runs `mkdocs build --strict`, then Doxygen into `site/api`,
  then `upload-pages-artifact@v3` and `deploy-pages@v4`. Only `deploy` has `pages: write` and
  `id-token: write`. The path filters match a2-context (`docs/Doxyfile` is covered by `docs/**`).
- `release.yml`: `check-release.cmake` on the tag (tag = `project(VERSION)`, CHANGELOG
  `## [X.Y.Z]`), self-test of 10 cases in ctest and in `style`, a draft release with generated
  notes, `contents: write` on that job only. The vendor step fails with a clear message until A8.
- Pages: `gh api .../pages` prints `workflow` and `https://contour-terminal.github.io/core-cpp/`.
  `/`, `/api/`, `/api/files.html` (lists `ExitCode.hpp`, `SuppressWindowsDialogs.hpp`) and every
  site URL cited in the repository return 200. Homepage is set and private vulnerability reporting
  is on, so SECURITY.md's link works.
- mkdocs: nav matches spec §4, `validation` warns on omitted and not-found files, snippets use
  `check_paths: true`. `docs/requirements.txt` is byte-identical to fastcached b5ded89c. Doxyfile
  excludes `*/tui/platform/*` rather than `*/platform/*`, so `src/core/platform/` stays public, as
  a2-context asked.
- `CLAUDE.md` is exactly `@AGENT.md\n`. All 12 commits end with the required `Signed-off-by:`
  trailer. The diff has no CR byte, and `.superpowers/` is not in any commit.
- Provenance: no bare `#NNN` in any tracked `.md`/`.yml`/`.json`/Doxyfile outside
  `docs/superpowers/`. `FastCache/` appears only in `.agent/guides/consumer-migration.md`'s rename
  table and leftover check. All five cited SHAs (contour 6777ff05, endo f774a210, fastcached
  b5ded89c, Lightweight f57dc2e0, tuidu 30107fba) are on each repository's `origin/master`, so
  the pinned blob URLs resolve. All five sources are Apache-2.0. 10 sampled fastcached issue
  citations (1531, 1533, 1128, 1452, 499, 315, 684, 100, 957, 1538) match their subjects. No
  relative or core-cpp-URL link in any Markdown file is dead.
- Carried rules I spot-checked against their sources: testing SKIP/exit-code (fastcached
  testing.md §"A SKIP that ctest scores as a failure"), Windows Debug leg (build-and-toolchain
  §2297, fastcached#315), WERROR decides fatality (the 7049-warning measurement), and contour's
  `enum class` underlying type and zero-is-off rule (contour AGENT.md:79-82). Each carries the
  substance correctly.
- Domain-specific fastcached rules are dropped: no framing, auth, keyspace, cluster, storage or
  packaging rule survives.

**Missing:**
- The plan's "Open work after v0.1.0" issues (plan:1140-1147). `gh issue list -R
  contour-terminal/core-cpp --state all` returns nothing, and no rule file has an `## Open work`
  section. See Important #1.
- A namespace=directory gate. Spec §4 (and plan:344) lists the `style` job as "pinned format,
  cmake-hygiene, layering, namespace=directory". Nothing checks namespaces: `style`
  (`build.yml:42-88`) runs clang-format, the hygiene scan and the two self-tests, and the scan
  (`tests/cmake/check-cmake-hygiene.cmake`) has no namespace rule. See Important #3.

**Extra (justified):** `.github/vcpkg/vcpkg.json`; four generic rules beyond the derivation
table; 11 small commits instead of the brief's one.

**Misunderstood:** none at the requirement level. Some docs statements are wrong; see Minor.

**Deviations the report discloses:** coverage runs in the workflow instead of through a CMake
`coverage` target (a2-context asked for the target); two A1 framework fixes in
`CoreCppTopLevel.cmake` (5d9f259, 2a524ad).

⚠️ **Could not verify:**
- Whether `.github/clang-tidy-matcher.json` turns a finding into an annotation. `.clang-tidy`
  has `UseColor: true`, and the canary log shows ANSI codes around `file:line:col: error:`. As
  far as I know, the runner strips colour codes before it matches, but no CI run had a finding
  to annotate.
- The local `mkdocs build --strict` and the local Windows/WSL runs. The brief says not to re-run
  them, and Docs CI is green.
- `release.yml` end to end: there is no tag, and the vendor step needs A8.
- Dependabot security alerts against the verbatim docs pins (Ruling R18, left for the user).

### Strengths

- **The CI checks that its gates ran.** The C++26 leg reads `-std=` back from the compile
  database. clang-tidy must report a planted canary, and `--tidy=` must appear in build.ninja.
  Emscripten must have no `pthread` command. The TLS leg must find vcpkg's `ssl.h`.
  compile-cache first checks that no other launcher is on PATH, then asserts the launcher record,
  the build.ninja bindings, `HIT` lines and `--show-stats` hits. These checks found real defects:
  - A1's normal-variable `CMAKE_CXX_STANDARD` shadowed the C++26 leg (5d9f259).
  - Module scanning broke every `try_compile` on FreeBSD (2a524ad).
  - apt read `g++-15` as a regex that matched clang-15 (8b2383b).
  - The auto-started daemon was missing libyaml (a1de1cc).
- **`ci-ok` semantics are right.** It avoids the `!cancelled()` trap and the skipped-means-green
  trap, and its header comment says a new gating job goes into `needs:` in the same change.
- **`check-release.cmake` has a real self-test.** It includes accept cases, and each refusal is
  asserted by its own phrase and by the absence of the other phrases. A `## [0.1.01]` near-miss
  and a code-span mention are both refused.
- **The permissions are least privilege:** `contents: read` at the top, `pages`/`id-token` only
  on deploy, `contents: write` only on the release job. Triggers and concurrency follow
  a2-context: cancel-in-progress runs only for pull requests, and a master deploy is never
  cancelled.
- **The guidelines are written against core-cpp, not pasted.** Rules name core-cpp paths,
  presets and targets, cite pinned blob URLs or issue URLs, and keep the failure each rule
  prevents. Examples: the SKIP section was rewritten as solved by exit-code normalisation, and the
  #1531 caveat was rewritten for the upstream fix.
- **The docs describe what A1 built.** Checked: `options.md` against
  `cmake/CoreCppOptions.cmake` and the presets; `building.md` and `README.md` preset lists against
  `CMakePresets.json` (all 17 exist, with matching `ci-*` workflows); `testing.md` against
  `core_cpp_add_test`; `cpm.md`'s `CORE_CPP_SKIP_EXIT_CODE` property against
  `src/core/testing/CMakeLists.txt:22`. Module stubs are factual and say what is planned.

### Issues

#### Critical

None.

#### Important

1. **The plan's open-work issues were not created (confirmed gap; plan-mandated).**
   - **What the plan says.** plan:1140 reads "Open work after v0.1.0 (tracked as core-cpp issues
     when Task A2 creates the repository)". It lists six items: `install()`/export plus a vcpkg
     port, removing `WfmoBackend`, unifying `core::Environment` with `EnvironmentProvider`,
     graduating further fastcached Core generics, the morph C8 Step 6 follow-up, and Doxygen
     `FAIL_ON_WARNINGS`.
   - **What exists.** `gh issue list -R contour-terminal/core-cpp --state all` is empty.
     Ruling R2 authorised creating the issues.
   - **What A2 wrote instead.** It wrote two of these residuals as prose, which its own
     `.agent/rules/README.md:59-62` forbids ("Deferred work does not belong here. Open a GitHub
     issue and link it from the file's `## Open work` section"):
     - `.agent/rules/library-hygiene.md:140-141`: "exporting them is open work after 0.1.0";
     - `docs/Doxyfile:62`: "(open work after 0.1.0)".
   - **More prose residuals of the same kind:**
     - `.agent/rules/build-and-toolchain.md:90`: an iterator-debug must-die canary, "core-cpp
       has none yet";
     - `.agent/rules/README.md:76`: no ctest reads the Open-work grammar yet.
   - **Fix.**
     - Create one issue per plan item, plus these residuals if the controller wants them.
     - Add `## Open work` entries using the README's grammar to the files that govern each
       item (library-hygiene, async-and-net or platform, build-and-toolchain), or to
       `.agent/rules/README.md` as the controller specified.
     - Replace the prose residuals with links to those issues.

2. **The vcpkg binary cache never works, so OpenSSL is rebuilt on every run and sets the
   `ci-ok` critical path.**
   - **What the workflow claims.** `build.yml:291-293` says "The binary archives are kept between
     runs, so OpenSSL is compiled once per vcpkg commit".
   - **What happens.** `lukka/run-vcpkg@v11` overrides `VCPKG_DEFAULT_BINARY_CACHE` with a temp
     directory and sets `VCPKG_BINARY_SOURCES=clear;x-gha,readwrite`. vcpkg then prints "The
     'x-gha' binary caching backend has been removed".
   - **What the logs show.** In three consecutive Build runs (bfa1c31, 2a524ad, 53ef21f):
     - "Cache vcpkg binary archives" reports "Cache not found";
     - its post step warns "Path(s) specified ... do(es) not exist, hence no cache is being
       saved";
     - `openssl:x64-windows` takes 8.6 and 8.4 min.
   - **Effect.** In run 35335132115 every other job finished by 10:36:49. `cl-release-tls`
     finished at 10:43:24, and `ci-ok` waited for it. That is about 7 extra minutes on the one
     required check of every PR. The rulebook itself says "a cache that reads like success is
     worse than none" (build-and-toolchain.md:40).
   - **Fix.** After the run-vcpkg step, set
     `VCPKG_BINARY_SOURCES=clear;files,<workspace>/.cache/vcpkg-archives,readwrite` (or configure
     run-vcpkg so it does not inject x-gha). Then confirm on a second run that the archive step
     restores and that OpenSSL is "Restored" rather than built.

3. **No namespace=directory gate (spec §4 `style` row; plan-mandated in part).**
   - **What is required.** The binding spec table and plan:344 list "layering,
     namespace=directory" under `style`. The build design's style row (a2-context:58) narrows
     this to "clang-format; cmake -P hygiene checks", and no other task creates the check.
   - **Layering is covered.** It is enforced at link level by every configure
     (`core_cpp_add_module`).
   - **The namespace rule is not.** It is stated in `AGENT.md`, `cpp-guidelines.md` and
     `library-hygiene.md`, and nothing checks it. A3 to A7 are the imports (crispy → `core`,
     endo → `core::tui`, and so on) where a wrong outer namespace would slip in.
   - **Fix.** Add a hygiene rule (and self-test cases): the first namespace in
     `src/core/<dir>/**.hpp` is `core::<dir>`, with nested helpers allowed, and in
     `src/core/*.hpp` it is `core`. It needs no build, so `style` runs it. An include-edge check
     (an include across modules must be a table edge) would close the other half of the spec's
     "layering".

#### Minor

1. **The ccache keys collide by prefix.**
   - **The keys.** `hendrikmuhs/ccache-action` restores by prefix, and:
     - `windows-cl-release` (`build.yml:272`) is a prefix of `windows-cl-release-tls`;
     - `linux-clang-22` (`build.yml:118`) is a prefix of `linux-clang-22-arm64` and
       `linux-clang-22-cxx26`.
   - **Measured.**
     - `windows (cl-release)` restored `ccache-windows-cl-release-tls-*` in both runs, with
       0/112 hits each time.
     - `linux (clang-22)` restored `ccache-linux-clang-22-arm64-*` (arm64 objects on x64) in run
       35335132115, with 0/111 hits.
   - **Fix.** Give every key a suffix no other key extends, or rename the legs.
2. **`cpp-guidelines.md:148-150` rests a rule on a check core-cpp turned off.**
   - **What it says.** It says "clang-tidy's padding check fails the build on the sum".
   - **What `.clang-tidy` does.** It disables `clang-analyzer-optin.performance.Padding`
     (`.clang-tidy:86`), with the opposite rationale: "field order follows meaning, not size"
     (`.clang-tidy:45`).
   - **Why it matters.** The rule was carried from fastcached without checking it against
     core-cpp's configuration, and the docs site publishes this file. Either drop the rule or
     restate it without the false consequence.
3. **`cpp-guidelines.md:185-186` misdescribes the `clang-tidy` preset.** It says the preset
   "points `CORE_CPP_CLANG_TIDY_EXE` at the pinned binary". The preset sets only
   `CORE_CPP_CLANG_TIDY=ON`. `find_program` then takes `clang-tidy` from PATH
   (`CoreCppToolchain.cmake:214`) and only warns on a version mismatch (`:229`).
   `options.md` has it right.
4. **`docs/modules/testing.md:48-50` is wrong for the pinned Catch2.** It says `SKIP_RETURN_CODE
   4` "makes a test with exactly four failed assertions read as skipped". Catch2 3.8 returns
   `TestFailureExitCode` (42) on failure (`catch_session.cpp:356`), so that collision exists only
   in older Catch2. The rules version (`testing.md`) qualifies it correctly; this page should too.
5. **`library-hygiene.md:32` describes `CORE_CPP_TARGETS` as if it exists.** It says "A parent
   instruments core-cpp itself through the `CORE_CPP_TARGETS` global property". Nothing under
   `cmake/` defines it yet: it is A8 Step 0. `cpm.md:82-83` correctly says it arrives with A8.
6. **The libyaml workaround does not link its issue.** `build.yml:538-541` does not cite
   fastcached#1538 (R19), so nothing tells a reader when the workaround can go.
7. **The `AGENT.md:69-70` tripwire is half true.** "`build.ninja`, not `CMakeCache.txt`, shows
   the launcher" is misleading, because CMakeCache does record `CORE_CPP_CXX_COMPILER_LAUNCHER`
   and the compile-cache job reads it. The rule file states it precisely.
8. **The CPM cache key is shared by jobs with different dependency sets.** It is
   `cpm-<os>-<hash>` for every job on an OS. The first job to save fixes the content for that
   key. Today all jobs fetch only Catch2, but once libunicode/stb/Tracy rows land, an emscripten
   save (TUI off) would pin a cache without libunicode, and the native jobs would re-download it
   on every run. Consider keying by job, or saving only from a job that fetches everything.
9. **There is no `coverage` CMake target, which a2-context asked for.** The coverage job does the
   work inline and works (disclosed as concern #8). Record this as an accepted deviation, or add
   the target when coverage becomes meaningful.

### Assessment

**Task quality:** Needs fixes

**Reasoning:** A2 met almost all of the spec and a2-context. The workflows, docs site, Pages
deployment and the rulebook's provenance are correct. `ci-ok` is green on HEAD with every listed
job running, and the gates check that they ran. Three items keep it from approval:
- The plan-mandated open-work issues were not created, and two residuals sit in prose that the
  rulebook forbids.
- The vcpkg binary cache is inert while the workflow comment claims otherwise. That costs about
  7 minutes on the one required check of every run.
- The spec's namespace=directory gate does not exist, and A3 to A7 are the tasks it would guard.

Each fix is small. The Minor items are accuracy corrections to a few carried statements and
CI-efficiency cleanups.
