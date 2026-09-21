# Task A2 context: excerpts from the approved build design

The spec's Part I §4 is binding. This file adds the detail the plan refers to as "the build design".

## CI (`.github/workflows/`)
Common to all workflows:
- `permissions: contents: read`.
- `concurrency: ${{ github.workflow }}-${{ github.ref }}`; cancel-in-progress only for pull requests.
- Triggers: push to master, pull_request, `merge_group`, `workflow_dispatch`.
- Caches: `hendrikmuhs/ccache-action`, plus `actions/cache` on `.cache/cpm` keyed on `hashFiles('cmake/CoreCppDependencies.cmake')`.
- `env: LLVM_VERSION: "22"`, `GCC_VERSION: "14"`. Add a gcc-15 leg as the spec says.

### `build.yml` jobs

| Job | What it does |
|---|---|
| `style` | pinned clang-format (`scripts/clang-format.py --check`); `cmake -P` hygiene checks |
| `linux` | clang-22, gcc-14, gcc-15, clang-22 on `ubuntu-24.04-arm`, clang-22 with `-DCMAKE_CXX_STANDARD=26` |
| `macos` | macos-15: `appleclang-release` and Homebrew LLVM 22 `clang-release`; `OPENSSL_ROOT_DIR` from brew |
| `windows` | `cl-release`, `clangcl-release`, `cl-debug`, and `cl-release-tls` via `lukka/run-vcpkg` (`ilammy/msvc-dev-cmd`) |
| `sanitizers` | `clang-asan-ubsan` on all tests; `clang-tsan` on all tests except `no-tsan`, with `TSAN_OPTIONS=halt_on_error=1` |
| `clang-tidy` | pinned PyPI clang-tidy 22.1.8 as `CORE_CPP_CLANG_TIDY_EXE`, preset `clang-tidy`, `cmake --build -- -k 0`, problem matcher `.github/clang-tidy-matcher.json` |
| `emscripten` | matrix emsdk 3.1.56 and latest. Configures the `emscripten` preset without `-pthread`. Builds what the WebAssembly subset has so far (at A2, only `testing`) and runs the tests under node via `CMAKE_CROSSCOMPILING_EMULATOR` |
| `compile-cache` | ubuntu; `-DFASTCACHE_AUTO_INSTALL=ON -DFASTCACHE_AUTO_START=ON`; asserts fastcache-cc is the launcher (Ruling R11: grep `CORE_CPP_CXX_COMPILER_LAUNCHER` in CMakeCache.txt, or `fastcache-cc` in build.ninja); build; `fastcache-cc --zero-stats`; delete the objects; rebuild with `FASTCACHE_VERBOSE=1`; assert at least one `HIT` and that `fastcache-cc --show-stats` shows hits |
| `coverage` | not required. `clang-coverage` preset and `coverage` target; uploads HTML/lcov artifacts |
| `ci-ok` | `needs:` every required job, `if: always()`, and fails unless each `needs.*.result == 'success'`. **This is the one required check.** |

`consumer-smoke` is **added in Task A8**; do not add it here (Ruling R15).

### Other workflows
- `docs.yml`:
  - Pull requests: build only. On master: `mkdocs build --strict`, then `doxygen docs/Doxyfile` into `site/api` (Doxygen runs AFTER mkdocs, because mkdocs cleans `site/`), then `upload-pages-artifact@v3`, then `deploy-pages@v4`. Only the deploy job gets `pages: write` and `id-token: write`.
  - Path filters: `docs/**`, `mkdocs.yml`, `src/core/**/*.hpp`, `docs/Doxyfile`, `CHANGELOG.md`, `CONTRIBUTING.md`, `.agent/rules/cpp-guidelines.md`.
  - Model it on fastcached `origin/master:.github/workflows/docs.yml` and add the Doxygen step (`apt install doxygen graphviz`).
- `release.yml`: on tag `v*`, run `cmake -P tests/cmake/check-release.cmake -DTAG=…`, which requires the tag to equal `project(VERSION)` and a `## [x.y.z]` section in CHANGELOG. Create `check-release.cmake` in this task, with a self-test. The job then builds `core-cpp-vX.Y.Z-vendor.tar.gz` using `cmake/CoreCppVendor.cmake MODE=export` (that script arrives in A8; the release job only runs at B13) plus `SHA256SUMS`, and opens a DRAFT release with generated notes. The notes use `.github/release.yml` categories copied from fastcached, with the `"*"` catch-all kept and emoji removed.
- `downstream.yml`: nightly and on dispatch, not required. For now it has ONE job that diffs `cmake/portable/CompileCache.cmake` and `cmake/FetchTransferBound.cmake` against fastcached `master` (fetched via raw GitHub) and fails on drift. The consumer builds are added after the consumers migrate (Ruling R15).
- `portability.yml`: nightly and on dispatch, not required. FreeBSD through `vmactions/freebsd-vm`, building the skeleton with ctest.
- `dependabot.yml`: the github-actions ecosystem.
- Do not use `actions/labeler`.
- `.github/pull_request_template.md`: What / Why / Consumer impact / Tests.

## Docs (GitHub Pages)
- **`mkdocs.yml`**: copy fastcached `origin/master:mkdocs.yml` settings.
  - `theme: material`; `validation.nav.omitted_files: warn`; `not_found: warn`.
  - `pymdownx.snippets` with `base_path: [".", "docs/snippets"]` and `check_paths: true`, so pages can include `CHANGELOG.md`, `CONTRIBUTING.md`, `.agent/rules/cpp-guidelines.md` and (later) `examples/**` without copying them.
  - `site_url: https://contour-terminal.github.io/core-cpp/`; `repo_url` pointing at the GitHub repo.
- **`docs/requirements.txt`**: fastcached's pins verbatim (mkdocs 1.6.1, mkdocs-material 9.5.39, pymdown-extensions 10.11, pygments 2.19.1), keeping the reason for the pygments pin.
- **Nav**, from the spec §4:

  | Section | Pages |
  |---|---|
  | Home | `index.md` |
  | Getting started | `cpm.md`, `docs/vendoring.md`, `building.md`, `options.md` (the options table from `cmake/CoreCppOptions.cmake`) |
  | Modules | `index.md` (module table + layering DAG), `base`, `log`, `cli`, `platform`, `coro`, `net`, `tui`, `testing`. These are short factual stubs now ("imported in Task A3…"); later tasks fill them. No marketing tone |
  | Design notes | dependency-injection, error-handling, coroutines-and-lifetimes, threading, portability, profiling |
  | Contributing | `index.md`, `cpp-guidelines.md` (snippet-includes `.agent/rules/cpp-guidelines.md`), `releasing.md` |
  | Changelog | snippet-includes `CHANGELOG.md` |
  | API reference | external link `https://contour-terminal.github.io/core-cpp/api/` |

- **`docs/Doxyfile`**:
  - `INPUT=src/core`, `FILE_PATTERNS=*.hpp`, `RECURSIVE=YES`.
  - `EXCLUDE_PATTERNS=*/detail/* */posix/* */linux/* */darwin/* */windows/* */platform/*` (only private subdirectories of modules: not `src/core/platform/` itself, which is the public platform module; choose a pattern that achieves that).
  - `STRIP_FROM_INC_PATH=src`, `WARN_IF_UNDOCUMENTED=YES`, `WARN_AS_ERROR=NO`, `OUTPUT_DIRECTORY` such that `HTML_OUTPUT` ends up in `site/api`.
- **Enabling Pages**:

  ```sh
  gh api -X POST repos/contour-terminal/core-cpp/pages -f build_type=workflow
  ```

  If a site already exists, use `-X PUT` instead. Verify with `gh api repos/contour-terminal/core-cpp/pages --jq '.build_type, .html_url'`.

## Repository documents
- **`AGENT.md`**: short; tripwires and pointers, never derivations. `CLAUDE.md` is exactly `@AGENT.md`. Headings:
  - What this repository is: the modules table, the module DAG, and the consumers table (→ `.agent/reference/consumers.md`).
  - Library rules (tripwires):
    - no global CMake state; `CORE_CPP_`/`core_cpp_` prefixes;
    - namespace = directory; an include across modules = a module-table edge;
    - public API = FILE_SET HEADERS → CHANGELOG;
    - grep the consumers (contour, endo, fastcached, tuidu, Lightweight dbtool, morph) before changing a public signature;
    - no new dependency without an option gate, a dependency row and a fetch/system classification;
    - never edit a vendored copy downstream;
    - no consumer-specific concept enters core-cpp;
    - the WebAssembly subset stays single-thread safe;
    - the compiler cache (fastcache-cc via the verbatim CompileCache.cmake).
  - The rulebook: an index of `.agent/rules/*.md`, one tripwire bullet each; "every rule has already been a bug"; link as plain markdown, never `@`-import.
  - Design principles (short): DI, configuration at construction, data-driven design, `std::expected`, enum class over bool, RAII.
  - C++ coding guidelines (short list; the canonical text is `.agent/rules/cpp-guidelines.md`).
  - Building (preset table), Testing (tests next to their sources, `core_cpp_add_test`, exit codes, labels), Documentation (factual, no marketing; `mkdocs --strict`), Releasing (`project(VERSION)` = tag; CHANGELOG; the `/draft-release` → `/publish-release` skills).
  - Workflow checklist: pinned format → pinned tidy → `clang-debug` → `gcc-release` (or `cl-debug` + `clangcl-release` on Windows) → docs strict if touched → CHANGELOG entry → "Consumer impact" in the PR body.
- **`.agent/` provenance.** Rewrite everything against core-cpp paths. Cite each origin as a full URL (e.g. `https://github.com/LASTRADA-Software/fastcached/issues/1128`), so no bare `#NNN` or `FastCache/` path is left. Read fastcached files as `origin/master` blobs.

  | New file | Carried or adapted from | Dropped from that source |
  |---|---|---|
  | `rules/README.md` | fastcached `.agent/rules/README.md`: entry criterion, "Adding a rule", the `## Open work` grammar | its #876 census |
  | `rules/cpp-guidelines.md` (canonical; docs include it) | contour AGENT.md §C++ + zero-warning policy; fastcached AGENT.md §C++ Coding Guidelines (no `g_`/`k` prefixes, C-style loops forbidden, pinned tools, no NOLINT); Lightweight `.agent/cpp-guidelines.md` (self-contained rule set, checklist, `[[nodiscard]]` on builders, self-contained headers); tuidu (coroutine parameters by value) | Lightweight SQL/ODBC and contour Qt/QML carve-outs |
  | `rules/design-principles.md` | contour AGENT.md (enum class over bool, configuration at construction, testability); endo AGENT.md (DI by constructor injection, data-driven design); fastcached AGENT.md (RAII, caching only where staleness degrades safely) | domain examples, replaced with core seams (IClock, ISocket, FileSystem, EnvironmentProvider) |
  | `rules/library-hygiene.md` (new) | vendoring safety (spec §3 and §5), the crispy graduation rule (spec §1), versioning and API policy (spec §4) | n/a |
  | `rules/build-and-toolchain.md` | fastcached build-and-toolchain.md: §NDEBUG is not optimisation, §Windows Debug leg, §modal error dialog, §Language and ABI pitfalls, §What a char is, §Line endings, §WERROR decides fatality, §A claim about a tool is checked against the tool, §A gate that does not report, §C-style loop classified by its body. Add the compile-cache rule and the fastcached#1531 clang-cl caveat | local-gate lock/WSL archaeology, compile-cache internals, "What CI costs", merge-queue specifics |
  | `rules/testing.md` | fastcached testing.md: §wall vs steady clock, §bounded waits, §the case name is an argument, §REQUIRE above `Stop()`, §a fake that resolves what production suspends, §SUCCEED where the case could not run, §assert what distinguishes, §the first failure masks its siblings, §`--repeat until-fail`. §SKIP is rewritten as "solved by exit-code normalisation" (see `src/core/testing/ExitCode.cpp`) | fleet, cluster and e2e fixtures, the POSIX shell helper library, leader-pinned commands |
  | `rules/async-and-net.md` | fastcached wire-and-protocol.md §The Net boundary (becomes layering), §Sockets, §Dialing and the reactor, §Socket and coroutine lifetime; fastcached "zones never straddle `co_await`". Phase B tasks extend this file | §Framing, §Authentication, §keyspace events, §expiry, §disk tier |
  | `rules/platform.md`, `rules/tui.md` | endo AGENT.md §New Platform Feature; Windows specifics | endo builtins and language |
  | `guides/team-run.md` | fastcached `.agent/guides/team-run.md` (lanes become modules) | fastcached board specifics |
  | `guides/profiling-tracy.md` | fastcached guide; `FC_*` → `CORE_ZONE_*`; `TRACY_ENABLE` → `CORE_CPP_WITH_TRACY` | |
  | `guides/consumer-migration.md` (new) | per-consumer rewrite table (spec §7 and the rename map in §2) | |
  | `guides/releasing.md` | versioning (spec §4) | fastcached packaging |
  | `reference/source-map.md`, `reference/consumers.md` (new) | an annotated tree; per consumer, how it consumes core-cpp (CPM vs vendored) and where its pin lives | |

  **Dropped outright:** fastcached compile-cache, distributed-compilation, consensus-and-cluster, metrics-and-observability, storage, packaging-and-release and most of platform-service-and-config.
- **`CONTRIBUTING.md`**:
  - scope, including the graduation rule;
  - toolchain floor: clang 22, GCC 14, Xcode 16 AppleClang, VS 2022+, CMake 3.25, Ninja, Python 3; emsdk 3.1.56+ for WebAssembly;
  - presets;
  - pinned format and tidy (`pip install clang-format==22.1.8 clang-tidy==22.1.8`);
  - tests required;
  - small semantic commits with a `Signed-off-by:` trailer;
  - labels `type/{feature,bug,perf,docs,chore}`, `breaking-change`, `module/*`;
  - the "Consumer impact" PR section;
  - CHANGELOG entries under `[Unreleased]`;
  - the 0.x breaking-change policy;
  - contributions are Apache-2.0.
- **`README.md`**:
  - badges: Build, Docs, License Apache-2.0, C++23;
  - one factual paragraph;
  - a modules table (module, namespace, target, depends on, contents);
  - the CPM snippet (below);
  - one paragraph pointing at `docs/vendoring.md`;
  - building with presets; requirements;
  - "Used by": contour (vendored), endo, fastcached, tuidu, Lightweight dbtool, morph;
  - docs link; license.

  The README must not contain CR bytes; the Task 0 stub did.

  ```cmake
  CPMAddPackage(
      NAME core-cpp
      GITHUB_REPOSITORY contour-terminal/core-cpp
      GIT_TAG v0.1.0
      SYSTEM YES              # core-cpp headers never trip your -Werror
      EXCLUDE_FROM_ALL YES    # build only what you link
      OPTIONS "CORE_CPP_WITH_TUI ON" "CORE_CPP_WITH_TLS OFF")
  target_link_libraries(myapp PRIVATE core::coro core::net core::tui)
  # local development against a checkout: -DCPM_core-cpp_SOURCE=/path/to/core-cpp
  ```
- **`CHANGELOG.md`**: Keep a Changelog, starting with `## [Unreleased]`. List the imports so far with their SHAs: fastcached `eb9c9c68` for CompileCache.cmake and FetchTransferBound.cmake; contour `6777ff05`; endo `f774a210`.
- **`SECURITY.md`**: net/TLS code; private advisories.
- **Versioning**: SemVer with `vX.Y.Z` tags; in 0.x a minor may break (recorded under **Breaking** with a migration note); consumers pin a tag, or a full SHA temporarily, never a branch.
