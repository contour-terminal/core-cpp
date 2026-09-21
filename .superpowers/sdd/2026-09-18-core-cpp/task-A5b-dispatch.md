# Dispatch: Task A5b (rename `core::coro` → `core::async`; `Generator` → base)

## Where this fits

core-cpp (D:\core-cpp, github.com/contour-terminal/core-cpp, branch `master`) is the shared C++23 library of the Contour projects.
- Phase A imports code module by module. A1–A5 are done.
- A5 left `src/core/coro/`: Task, UniqueCoroHandle, Cancellation, WhenAll, WhenAny, Awaitable, StopToken (std alias + fallback) and Generator, plus the tests.
- The user decided to rename the module `core::async` before A6 imports networking on top of it.
- This is a mechanical rename plus one moved file and one new clang-tidy rule. No behaviour changes.

## Requirements

Read your brief first: `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A5b-brief.md`. It holds your requirements, including the exact rename table, the three commit subjects, and the grep that must come back empty. It names `global-constraints.md` and the spec; both are binding.

## What you need to know

- **Tests and binaries.** `src/core/coro/CMakeLists.txt` defines two test binaries through `core_cpp_add_test` (`NAME`/`DEFINITIONS` exist since A5):
  - `core-cpp-coro-test`;
  - `core-cpp-coro-fallback-test`, which compiles Task/WhenAll/WhenAny/StopToken tests with `CORE_CORO_FORCE_STOP_TOKEN_FALLBACK`.

  It also runs a configure-time check that prints which StopToken branch the toolchain takes (`message(STATUS "[core-cpp] coro: StopToken is …")`). Rename all of it per the table. The status line becomes `[core-cpp] async: …`.
- **Generator moves to base.** `Generator_test.cpp` currently lives in the coro test binary. It becomes part of the base test binary. Keep its fallback coverage (`CORE_GENERATOR_FORCE_FALLBACK` / `detail::GeneratorFallback`) exactly as is.
- **Module table.** The module DAG is in `cmake/CoreCppModules.cmake`, and configure refuses a link it does not list.
  - `platform` currently has `DEPS base log coro`. It becomes `DEPS base log`.
  - Check `src/core/platform/CMakeLists.txt` for a coro link as well.
- **The namespace-directory hygiene rule** is in `tests/cmake/check-cmake-hygiene.cmake` (around lines 100–110 and 228–250), and its self-test is in `tests/cmake/check-cmake-hygiene-selftest.cmake`.
  - Step 1 of the brief extends the rule so that a mixed-case namespace such as `namespace core::Async` is refused. Test first: the self-test row comes first.
  - `.clang-tidy` gains `readability-identifier-naming.NamespaceCase: lower_case`.
- **Provenance.**
  - `.agent/reference/provenance.md` has rows keyed by the core-cpp path. Rename the core-cpp path column and any core-cpp names in the notes.
  - NEVER change the upstream columns (contour `src/coro/...`, endo `src/platform/Generator.hpp`) or the SHAs.
  - The provenance hygiene rule refuses a file under `src/core/` without a row, so the rows must follow the `git mv`.
- **Docs and prose.** Many files mention "coro" in two senses, and you must tell them apart:
  - core-cpp's module (`core::coro`, `src/core/coro`, `<core/coro/…>`, "the coro module", target names): rename these.
  - Upstream contour/endo/tuidu `coro` (their `src/coro`, their `coro::` namespace, the migration notes saying contour's `coro::` becomes core-cpp's namespace): keep these. Where a migration note gives the *target* name, it becomes `core::async::`.
  - The word "coroutine(s)" stays.
  - The file list: `rg -l coro --glob '!out/**' --glob '!site/**' --glob '!.superpowers/**' --glob '!docs/superpowers/**'`. The spec and plan under docs/superpowers are controller-owned and already amended, so do not edit them.
  - `docs/modules/coro.md` becomes `docs/modules/async.md` (`git mv`), and the `mkdocs.yml` nav follows. `mkdocs build --strict` must pass (venv: `.cache/docs-venv`, or create it per `docs/requirements.txt`).
- **CHANGELOG (Ruling R36):**
  - No **Breaking** entry: v0.1.0 is unreleased, so no consumer ever saw `core::coro`.
  - Rewrite the existing `[Unreleased]` entries to the new names.
  - Keep the import rows naming contour's upstream `src/coro` at `6777ff05`.
  - Add one line saying `Generator` is in base, if the entries don't already make that clear.
- **Local commits.** The local `master` has three controller commits that are not pushed yet: 7c12506, 36c4eeb, and possibly later ones, all under docs/superpowers. Build on top of them. Your push carries them; do not rebase them away.

## Local builds (all green before you report)

**Windows.** In PowerShell, run:
```
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
```
then `cmake --preset clangcl-debug`, `cmake --build --preset clangcl-debug --clean-first` and `ctest --preset clangcl-debug`, and the same for `cl-debug`.
- The directory move leaves stale trees: delete `out/build/<preset>` first if configure complains about the old targets.
- The local fastcache-cc predates the fix for fastcached#1531, so use `--clean-first` on clangcl.

**WSL.** For `clang-debug`, `gcc-debug`, `clang-tsan` and `clang-tidy` (zero findings with the new NamespaceCase rule), run:
```
wsl -d Ubuntu-26.04 -- bash -lc 'cd /mnt/d/core-cpp && cmake --preset <p> && cmake --build --preset <p> && ctest --preset <p>'
```

**Checks.** Run `python scripts/clang-format.py --check` and `mkdocs build --strict`.

**CI.** Push to `origin master`, then watch it yourself with `gh run watch <id> -R contour-terminal/core-cpp --exit-status`. Never wait idle for notifications. The task is done only when `ci-ok` is green, including both emscripten legs. Fix forward if red.

## Commits

Use the brief's three commit subjects, in that order, each green on its own. Each commit message ends with the trailer:
```
Signed-off-by: Christian Parpart <christian@parpart.family>
```
Use `git mv` so history follows.

## Report contract

Write the full report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A5b-report.md`. It covers:
- what you did;
- the TDD evidence for the hygiene rule (the RED self-test output, then GREEN);
- the output of the brief's empty-grep;
- the local results for each preset;
- the CI run id and its conclusion;
- the files changed;
- your self-review;
- your concerns.

Then reply in under 15 lines: Status (DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT), commits (short SHA + subject), a one-line test summary, concerns, and the report path.

You never dispatch subagents: no helpers and no reviewers. Review comes from the controller after your report.

## Also fold in (A5's re-review left these small doc/comment items, and A5b touches the same files)

1. `src/core/coro/StopToken.hpp:10-12`: the `@file` comment names only libc++ 17 / emsdk 3.1.56. Align it with the module page: the fallback is live on libc++ before 20 without `-fexperimental-library`.
2. CI run 35379331171 measured that AppleClang 17 takes the fallback. In `coro.md:46` (soon `async.md`) and the CHANGELOG, replace "likely AppleClang" with "AppleClang 17 (measured in CI)".
3. `StopToken_test.cpp:449-452`: the race-case comment says the destructor "meets it running". Make it say that the gate makes this likely, not certain.
4. `coro.md:57` is about 114 columns wide; wrap it at 100 like the rest of the file.

Put these four items in a fourth commit after the brief's three, for example `async: the fallback's documented reach matches what CI measured`.
