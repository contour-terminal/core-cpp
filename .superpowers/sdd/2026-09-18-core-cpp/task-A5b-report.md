# Task A5b report: rename `core::coro` -> `core::async`; `Generator` moves to base

## Status: DONE

Branch `master`, pushed to `origin/master`. `ci-ok` is green on the final commit
(`e45f730`), including both `emscripten (emsdk 3.1.56)` and `emscripten (emsdk latest)` legs.

## What I did

Worked the three steps in the brief's dependency order (check first, then the Generator move,
then the module rename), but split the result into the four commits the dispatch names, plus one
"fix forward" commit CI required:

1. **`16478ae` — `base: Generator is core::Generator, and platform no longer needs the coroutine
   module`.** `git mv src/core/coro/Generator{,_test}.* src/core/`; namespace `core::coro` ->
   `core`; `src/core/CMakeLists.txt` gains `Generator.hpp`/`Generator_test.cpp`;
   `cmake/CoreCppModules.cmake`'s `platform` row and `src/core/platform/CMakeLists.txt` drop the
   `coro` dependency; `FileSystem.hpp`/`NativeFileSystem.{hpp,cpp}`/
   `testing/InMemoryFileSystem.{hpp,cpp}` drop the now-redundant `coro::` qualification on
   `Generator<T>` (unqualified lookup finds `core::Generator` from `core::platform` the same way
   it already found `core::coro::Generator`); provenance rows follow the move.
2. **`12bb267` — `async: the coroutine module is core::async`.** `git mv src/core/coro
   src/core/async`; every occurrence of `core::coro`, `<core/coro/...>`, `core-cpp-coro*`,
   `core-cpp.coro*`, the `coro` ctest label and `CORE_CORO_*` renamed per the brief's table,
   including two macros the table didn't spell out by name (`CORE_CORO_STOP_TOKEN_IS_STD`,
   `CORE_CORO_STOP_TOKEN_HAS_THREADS`) and the CMake `try_compile` variable
   `CORE_CPP_CORO_STOP_TOKEN_PROBE`, for consistency with `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK`.
   `docs/modules/coro.md` -> `async.md` (its own Generator section moved into `base.md`'s new
   "Generator" section instead of being deleted); `mkdocs.yml` nav; AGENT.md, README.md,
   CONTRIBUTING.md, NOTICE, the module docs, `.agent/rules/*`, `.agent/reference/{source-map,
   consumers,provenance}.md`, CHANGELOG's `[Unreleased]` (rewritten in place, no Breaking entry —
   Ruling R36, since v0.1.0 never shipped `core::coro`).
3. **`4008aaa` — `tidy: namespaces are lowercase`.** `.clang-tidy` gains
   `readability-identifier-naming.NamespaceCase: lower_case`. Extended
   `tests/cmake/check-cmake-hygiene.cmake`'s `namespace-directory` rule to also refuse an
   uppercase character anywhere in a namespace that already matched its directory (the old rule
   only checked the directory match, so a *nested* namespace such as a top-level file's own
   `core::Async` passed silently). TDD evidence below.
4. **`a8ee84c` — `async: the fallback's documented reach matches what CI measured`.** The four
   fold-in items: `StopToken.hpp`'s `@file` comment now says "libc++ before 20" instead of
   "libc++ 17 / emsdk 3.1.56"; `async.md` and CHANGELOG say "AppleClang 17 (measured in CI)"
   instead of "likely AppleClang"; `StopToken_test.cpp`'s race-case comment says the gate makes
   the destructor meeting a running callback *likely*, not certain; one line at 114 columns in
   `async.md` wrapped at 100, along with one my own edit above introduced at 102.
5. **`e45f730` — `cli: the CLI alias is lowercase too, cli::`** ("Fix round 1", CI-driven — see
   Concerns). `src/core/cli/App.cpp`'s `namespace CLI = core::cli;` and its six call sites renamed
   to `cli::`, and the provenance row gets a note.

### Disambiguating "coro"

Per the dispatch, I renamed only core-cpp's own module and left every upstream/consumer mention
alone: contour's own `src/coro/...` (provenance's upstream column, NOTICE, CHANGELOG's import
rows, `async.md`'s "Imported from contour's `src/coro`..."), contour's own `coro::` namespace
(consumer-migration.md's rename-map "From" column), and each consumer's own current directory
names in `.agent/reference/consumers.md`'s "Today" column (`src/coro`, `src/{crispy,vtparser,
coro,net}`) — those describe what contour/endo/fastcached/tuidu *currently* carry, unaffected by
this task. The one genuinely ambiguous spot, `.agent/rules/async-and-net.md:175` ("`coro::IExecutor`"), I resolved rather than left ambiguous: I checked fastcached's actual source
(`git -C D:/fastcached ... grep IExecutor origin/master`) and found `IExecutor` lives in
`FastCache::Async`, never in anything spelled `coro::`, so the file's "coro::IExecutor" was
already this repo's own translated shorthand (matching the bare `coro`/`async` shorthand used
elsewhere in the same file), not a literal fastcached name — renamed to `async::IExecutor`.

## TDD evidence for the hygiene rule (commit `4008aaa`)

Self-test row added first, in `tests/cmake/check-cmake-hygiene-selftest.cmake`: a `namespace
core::Async` overwriting `src/core/Base64.hpp` in a scratch tree (a *nested* namespace under the
correct top-level `core`, so only the case is wrong — a case the old rule's directory-match check
did not catch).

RED (ran against the unmodified scanner):
```
CMake Error at tests/cmake/check-cmake-hygiene-selftest.cmake:148 (message):
  hygiene-selftest:

    namespace-directory: src/core/Base64.hpp was not refused
```

Then extended `check-cmake-hygiene.cmake`'s `namespace-directory` rule with an `elseif(declared
MATCHES "[A-Z]")` branch alongside the existing directory-mismatch check.

GREEN:
```
-- hygiene-selftest: the clean tree passed and all 23 violations were refused by name
```

## The brief's empty-grep

```
$ rg -n "core::coro|core/coro|core-cpp-coro|CORE_CORO_|core-cpp\.coro" --glob '!docs/superpowers/**' --glob '!.superpowers/**'
(no output, exit 1)
```

## Local results

All green, in this order, after every commit (re-verified in full after the App.cpp fix, commit
`e45f730`):

| Preset | Build | Tests |
|---|---|---|
| WSL `clang-debug` | OK | 12/12 |
| WSL `gcc-debug` | OK | 12/12 |
| WSL `clang-tsan` | OK | 12/12 |
| WSL `clang-tidy` | OK, **clean `--clean-first` rebuild**, zero findings | 12/12 |
| Windows `clangcl-debug` (`--clean-first`) | OK | 15/15 |
| Windows `cl-debug` | OK | 15/15 |

Plus: `python scripts/clang-format.py --check` clean (123 files); `mkdocs build --strict` clean
(via `.cache/docs-venv`).

## CI

Push 1 (`a8ee84c`, before the App.cpp fix): Build run
[35385000948](https://github.com/contour-terminal/core-cpp/actions/runs/35385000948) — **red**,
`clang-tidy` job: `invalid case style for namespace 'CLI'` at `src/core/cli/App.cpp#40`. Every
other job (24 of them) passed.

Push 2 (`e45f730`, after the fix): Build run
[35386108380](https://github.com/contour-terminal/core-cpp/actions/runs/35386108380) —
**success**, `ci-ok` green, both emscripten legs green. Docs run
[35385001208](https://github.com/contour-terminal/core-cpp/actions/runs/35385001208) (from the
prior push, unaffected by the App.cpp-only fix) — success.

## Files changed

54 files across the five commits (`git diff --stat 36c4eeb..e45f730`): the `src/core/coro` ->
`src/core/async` directory move (12 files) plus `Generator{,_test}` to `src/core/` (2 files);
`.clang-tidy`; `cmake/CoreCppModules.cmake`; `src/core/CMakeLists.txt`; `src/core/platform/{CMakeLists.txt,FileSystem.hpp,NativeFileSystem.{hpp,cpp},testing/InMemoryFileSystem.{hpp,cpp}}`;
`src/core/cli/App.cpp`; `tests/cmake/check-cmake-hygiene{,-selftest}.cmake`; `docs/modules/{coro.md->async.md,base.md,index.md,net.md,platform.md,tui.md}`; `docs/design/{coroutines-and-lifetimes,error-handling}.md`; `docs/getting-started/cpm.md`; `docs/vendoring.md`; `mkdocs.yml`;
`AGENT.md`, `README.md`, `CONTRIBUTING.md`, `NOTICE`, `CHANGELOG.md`; `.agent/rules/{README,async-and-net,cpp-guidelines,design-principles,library-hygiene,testing,tui}.md`; `.agent/reference/{source-map,consumers,provenance}.md`; `.agent/guides/{consumer-migration,team-run}.md`.

## Self-review

- Confirmed the module-table layering check still holds: `platform`'s `DEPS` and `PUBLIC_LIBS`
  agree (both dropped `coro`/`async`), and `async`'s row has no `DEPS` (header-only, std only),
  matching its `KIND INTERFACE`.
- Confirmed `Generator<T>` call sites in `platform` now use the unqualified name rather than
  `core::Generator::` or a stale `coro::` — they rely on the same enclosing-namespace lookup the
  code already used for `coro::Generator` from `core::platform`.
- Caught and fixed a real bug in my own process before it reached this report: three of my
  edits (`.agent/reference/provenance.md`, `.agent/guides/consumer-migration.md`,
  `.agent/rules/async-and-net.md`) went through a `python3` heredoc from the Bash tool, and that
  `python3` resolves to the Windows install, whose default text-mode write translates `\n` to
  `\r\n`. That silently put CRLF into three files that were LF before. Found it via `od -c`
  diffing against `git show HEAD:<path>`, fixed all three with a binary `\r\n` -> `\n` pass, and
  audited every file in the diff for the same issue (none found). `.gitattributes`'
  `* text=auto eol=lf` would have normalized the *committed* blob regardless, but the working-tree
  files would have stayed CRLF, and Global Constraints explicitly refuse CR bytes in imported
  content — worth fixing outright rather than relying on git to paper over it.
- The CI failure (`invalid case style for namespace 'CLI'`) was a real gap in my own local
  clang-tidy verification, not just bad luck: `.clang-tidy` is not a tracked input to any
  translation unit's build rule, so changing it does not invalidate an already-cached object file.
  My first `clang-tidy` preset run reused a cached `App.cpp.o` from before the `NamespaceCase`
  change and never re-ran the analyser over it. I did not think to `--clean-first` the
  `clang-tidy` preset specifically (the dispatch's `--clean-first` guidance is scoped to `clangcl`
  trees, for a different reason). I've now done a clean rebuild of every preset I touch for
  clang-tidy-relevant changes for the rest of this task, and would generalize this: any edit to
  `.clang-tidy` itself needs a `--clean-first` clang-tidy rebuild to be trusted locally.
- Re-ran the full local matrix (including the two Windows presets and all four WSL presets) after
  the `App.cpp` fix, not just the one preset that had failed.

## Concerns

- **The CI-driven fix round.** I pushed once, watched CI, found the `clang-tidy` job red, fixed
  it, and pushed again (documented above as "Fix round 1" even though it happened before this
  first report, since it came from CI rather than a controller review). Net result: `ci-ok` green
  on `e45f730`, but the push history has one red Build run in it
  ([35385000948](https://github.com/contour-terminal/core-cpp/actions/runs/35385000948)).
- **`docs/modules/base.md`'s Generator section duplicates prose from the old `coro.md`/new
  `async.md`** (the `std::generator`-vs-fallback explanation) rather than just linking to it,
  because the module page convention documents a module's own headers self-containedly. Flagging
  in case the controller would rather it be terser and link out.
- **README.md's `async` row status** ("`Task`, cancellation and combinators **available** (A5);
  executors and `AsyncQueue` planned (B1)") corrects a pre-existing staleness I found while
  touching that exact line (it previously said "`Generator` available; the rest planned (A5, B1)"
  — inconsistent with CHANGELOG's `[Unreleased]`, which already listed Task/StopToken/whenAll/
  whenAny as done). I fixed only what my own edit touched; I did not audit README.md's other rows
  for similar drift, since that's outside this task's scope.
- `docs/modules/index.md`'s "Public headers under Emscripten" table row order and the earlier
  `.agent/reference/provenance.md` table's row order were not reshuffled to stay alphabetical
  except where I moved the `async/*` block itself (which I did, to sit before `cli/*` — the
  hygiene rule doesn't care about table order, but a human reader does).

## Report path

`D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A5b-report.md`
