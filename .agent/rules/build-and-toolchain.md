# Build system, toolchain and language pitfalls

Rules about what differs between compilers, standard libraries, hosts and tool versions, and
about the CI gates that exist to catch those differences before a consumer does.

Read this before changing `cmake/`, `CMakePresets.json`, `.github/workflows/`, a header every
module includes, or anything that decides which flags, tools or launcher a build uses.

Most of these rules were paid for in fastcached, whose rulebook holds the full measurements:
[fastcached `.agent/rules/build-and-toolchain.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/build-and-toolchain.md)
(cited below as "fastcached build-and-toolchain"). They are restated here against core-cpp's
presets, scripts and paths.

## The compiler cache

- **core-cpp builds through fastcache-cc whenever it is available.**
  `cmake/portable/CompileCache.cmake` selects, in order: fastcache-cc when a fastcached daemon
  accepts its probe compile at `FASTCACHE_ADDR` (default `127.0.0.1:6674`); sccache, only with
  `-DALLOW_SCCACHE_FALLBACK=ON`; ccache; none. It leaves a launcher that is already set (on the
  command line, in a preset, or by a toolchain file) untouched, and it never fails a
  configure. `cmake/CoreCppTopLevel.cmake` includes it first, before any dependency is
  resolved, so CPM-fetched dependencies are compiled through the cache too.
- **The module is a verbatim copy of fastcached's and is never edited here.** A fix goes to
  fastcached first; then the whole file is re-synced as a git blob and
  `cmake/portable/README.md` records the new commit. The nightly `downstream.yml` fails when
  fastcached's `master` has moved on. Its unprefixed options (`USE_COMPILER_CACHE`,
  `FASTCACHE_*`) are exempt by path from the prefix rule.
- **Never set `-DUSE_COMPILER_CACHE=OFF` locally, except for coverage.** The `clang-coverage`
  preset turns the cache off because a replayed object carries the coverage mapping of the
  checkout that produced it, so the report would describe another tree. Every other preset
  keeps it on.
- **`CMakeCache.txt` does not show the launcher; `build.ninja` does.** The module sets
  `CMAKE_CXX_COMPILER_LAUNCHER` as a normal variable, never in the cache, so grepping the
  cache for it answers "none" on a tree that uses fastcache-cc. `CoreCppTopLevel.cmake`
  records the choice as the internal cache entry `CORE_CPP_CXX_COMPILER_LAUNCHER`, which is
  what the `compile-cache` CI job reads, and the generated `LAUNCHER =` bindings in
  `build.ninja` are the artefact itself. A configure's output is the module's *claim*; the
  generated build system is the fact. Origin: fastcached build-and-toolchain, "A configure's
  OUTPUT is the module's claim".
- **A cache that reads like success is worse than none.** The `compile-cache` job therefore
  asserts the whole chain rather than a status line: configure with
  `-DFASTCACHE_AUTO_INSTALL=ON -DFASTCACHE_AUTO_START=ON`, find fastcache-cc in the launcher
  record and in `build.ninja`, build, zero the statistics, delete the objects, rebuild with
  `FASTCACHE_VERBOSE=1`, and require `HIT` lines and a non-zero hit count from
  `fastcache-cc --show-stats`. Every other job uses ccache, which the module picks when no
  daemon answers. Origin: fastcached build-and-toolchain, "Running the launcher is not testing
  it".
- **With a fastcache-cc older than fastcached ca8dfc32, rebuild a `clangcl-*` tree with
  `--clean-first` after a header edit; from ca8dfc32 on, do not bother.** **Decide which you have
  rather than guessing** — the version string ends in `-g<sha>`, which is the commit it was built
  from, so two commands answer it:

  ```sh
  fastcache-cc --version              # 0.2.0-739-gd4451c3b  ->  the sha is d4451c3b
  git -C <fastcached> merge-base --is-ancestor ca8dfc32 <sha> && echo new || echo old
  ```

  A condition nobody can evaluate reads as optional and gets skipped, which is why the commands
  are here and not left implied. The launcher is per-platform and this machine carries two -- a
  Windows build for the `cl-*`/`clangcl-*` trees and a WSL build for the rest -- so **run the
  check in the environment that builds the tree you are asking about**: `fastcache-cc --version`
  answers from whichever one is on that shell's PATH, they are upgraded independently, and on
  2026-09-21 they were nine commits apart (`0.2.0-739-gd4451c3b` on Windows,
  `0.2.0-748-g73fb0457` in WSL). They agreed on the verdict that day; agreeing is not guaranteed.
  **On this machine on 2026-09-21 the answer was `old`**:
  `d4451c3b` is dated 2026-09-16 and `ca8dfc32` 2026-09-18, so `--clean-first` was required.
  Re-check after a launcher upgrade; do not carry that answer forward. CMake's Ninja
  generator does not give clang-cl `/showIncludes`: it writes `deps = gcc` and asks for a GNU
  depfile through the pass-through, `-clang:-MD -clang:-MT<obj> -clang:-MF<obj>.d`. The older
  launcher did not recognise `-clang:-MF`, so a cache hit wrote the object but no depfile, and
  Ninja recorded no header dependencies for it without a warning. A later header edit then left
  that object stale: an incremental build that is wrong, found in core-cpp only because it
  became a duplicate-symbol link error. `cl` (`deps = msvc`, `/showIncludes`) and GCC and Clang
  hits were unaffected — **`cl` goes through the same launcher, so "unaffected" is about the
  dependency mechanism and not about caching.** Measured in
  `out/build/<preset>/CMakeFiles/rules.ninja` on 2026-09-21: `cl-debug` has `deps = msvc` ×87,
  `/showIncludes` ×88 and `-clang:-MF` ×**0**; `clangcl-release` has `deps = gcc` ×29 and
  `-clang:-MF` ×29. Both trees carry 314 `fastcache-cc` lines in `build.ninja`. **The rule
  definitions are in `rules.ninja`, not `build.ninja`** — grep the wrong one and `deps =` returns
  zero matches, which reads exactly like a tree that has no dependency mode at all. A clean build
  is always correct, because the cache key covers the preprocessed input; CI builds from clean
  trees. The fix is in the fastcache-cc binary alone,
  so the vendored `CompileCache.cmake` needs nothing, and entries cached without a depfile heal
  themselves: the fixed launcher does not serve them to a compile that names a depfile, but
  recompiles and stores again. With an older launcher:
  `cmake --build --preset clangcl-debug --clean-first`. `ninja -t deps <object>` showing
  `#deps 0` is the tell. Origin:
  [fastcached#1531](https://github.com/LASTRADA-Software/fastcached/issues/1531), fixed by
  [fastcached#1533](https://github.com/LASTRADA-Software/fastcached/pull/1533).
- **The incremental run produces a plausible GREEN, not an error, so nothing prompts you to
  remember the rule above. The step count is the only tell.** Measured on 2026-09-21: after a
  change to `EventLoop.hpp`, `clangcl-release` rebuilt **7 steps** and reported `ctest 33/33`;
  `--clean-first` rebuilt **517 steps** and reported the same `33/33`. **A seven-step build cannot
  have rebuilt that header's dependents, so the pass was inherited — and the verdict was identical
  either way.** Report the step count beside every `clangcl-*` pass; a pass alone cannot say which
  of the two runs produced it. This also narrows what was *analysed*: ninja skipping a statement
  skips the `CODE_CHECK` inside it, so an inherited object is an unanalysed one, and `--clean-first`
  restores analysed surface rather than only object correctness.
- **A near-zero work count means three different things, and the third is this one.** It is the
  verdict for an **analyser** gate (worthless — what you are measuring sits *inside* the statement
  ninja skipped). It is **not** the verdict for a **compile-and-test** gate (legitimate — ninja's
  currency check verified every object is newer than its inputs, and ctest then ran those
  binaries). **Unless the currency check is itself unsound, in which case it is the verdict again,
  for the opposite reason**: the graph it answered from was missing edges. `clangcl-*` on a
  fastcache-cc older than `ca8dfc32` is a live instance of the third case — the depfile is never
  reproduced, so the header edge does not exist, so "up to date" is an answer from an incomplete
  graph rather than a check that passed.

## `NDEBUG` is not optimisation, and only the compiler can say which build this is

- **`NDEBUG` does not say the optimiser ran.** `-O0 -DNDEBUG` defines it and optimises
  nothing; a figure labelled "optimised" on that evidence is a confident wrong signal.
- **`CMAKE_BUILD_TYPE` is a label and decides nothing.** A verdict about the build comes from a
  macro the compiler defines in the translation unit that asks.
- **Which drivers state the optimiser was measured, not assumed:** gcc, clang and clang-cl
  define `__OPTIMIZE__` from `-O1`/`/O1` up; `cl` never defines it. clang-cl defines `__clang__`
  and not `__GNUC__`. So an absent `__OPTIMIZE__` means "not optimised" on three drivers and
  means nothing on `cl`, and a figure's standing has three values: optimised, not optimised,
  unconfirmed.
- Origin: [fastcached#1439](https://github.com/LASTRADA-Software/fastcached/issues/1439).

## The Windows Debug leg exists for the runtime

- **`cl-debug` runs `ctest`, not only a build.** Its value is MSVC's Debug CRT, which sets
  `_ITERATOR_DEBUG_LEVEL=2` and traps invalidated iterators, out-of-range indexing and
  mismatched container iterators where they happen: the class a Release build tolerates in
  silence. A Debug leg that only compiles exercises none of it.
- **It was the one Windows configuration nobody ran in CI, while being the preset every Windows
  developer debugs with**, so a `cl-debug`-only defect could sit on `master` with every check
  green. Origin: [fastcached#315](https://github.com/LASTRADA-Software/fastcached/issues/315).
- **Nothing in core-cpp states that level; it follows from `_DEBUG`, from the runtime flavour,
  from the build type.** Any of those moving removes the checks with no warning while the leg
  stays green. So a must-die canary states it, as fastcached's does: `core-cpp.iterator-debug-canary`
  (`tests/IteratorDebugCanary.cpp`), registered in every MSVC-driver Debug build, indexes a
  `std::vector` out of range and passes only on the runtime's own `vector subscript out of range`
  ([core-cpp#11](https://github.com/contour-terminal/core-cpp/issues/11)). Its registration does not
  depend on the level, or it would abstain in exactly the build that lost it.
- **The Debug runtime ends a failed check with `__fastfail`**, which ctest scores as a crash
  (`0xc0000409`) that no `PASS_REGULAR_EXPRESSION` overrides, and which `core::testing_dialogs`'
  invalid-parameter handler never sees. The canary installs a CRT report hook that passes the
  runtime's report on to stderr and exits with a plain failure first.

## No executable raises a modal error dialog, and the build installs that

- **A failed `assert()`, an iterator-debug check or an `abort()` in a Windows Debug build opens
  a dialog and waits.** That does not fail a test; it hangs it until ctest's timeout, or the
  job's limit on a runner, where it reads as a slow job. An endo test runner hung for 58
  minutes this way, because its `main()` was the one that forgot the call.
- **So the build installs the suppression, never each `main()`.** `core::testing_dialogs` is
  an object library whose static initialiser runs in the `init_seg(lib)` segment, ahead of
  ordinary static initialisers, and `core::testing_main` puts its object on the link line of
  every test executable. An executable with its own `main()` links `core::testing_dialogs`.
- **The canary proves the runtime, not the wiring.** `tests/WindowsDialogCanary.cpp` asks for
  no suppression, trips each mode (`assert`, `abort`, `invalid-parameter`), and must end,
  within a bound, with a failure: a dialog would hold it past the timeout.
- Origin: [fastcached#1389](https://github.com/LASTRADA-Software/fastcached/issues/1389) and
  endo's `SuppressWindowsDialogsAtStartup.cpp`.

## Language and ABI pitfalls

- **`readability-qualified-auto`'s own suggested fix does not compile on MSVC.** Over a
  `std::array`, libstdc++ and libc++ hand `std::ranges::find` a raw pointer, so the check asks
  for `auto const* const`; MSVC's iterator is a class type that declaration cannot deduce, and
  spelling the type out trips `modernize-use-auto` instead. Taking the analyser's advice trades
  a Linux-only lint for a Windows-only build failure, found by whoever next builds on Windows.
  Scan for a *value* and name no iterator, or use a helper that returns a pointer
  (`core::findOrNull` or `core::findIfOrNull` in `<core/Ranges.hpp>`, from fastcached's
  `FindOrNull`). Origin:
  [fastcached#1342](https://github.com/LASTRADA-Software/fastcached/issues/1342).
- **A return type is not part of a free function's mangled name on Linux.** Two functions
  differing only in their return type, each declared in a different header, both compile, and
  the linker keeps one symbol: every caller of the other then reads an `expected` as an
  `optional` and crashes, while MSVC, whose mangling includes the return type, links both and
  reports the tree green. A standalone reproducer does not reproduce it, because the defect is
  in the link; `nm -C` on the object finds it. Origin: fastcached build-and-toolchain,
  "Language and ABI pitfalls".
- **A library facility can be absent from one standard library, or present without the
  overload you want.** `std::from_chars` has no floating-point overload in libc++ before
  macOS 26; `std::ranges::iota` and `std::ranges::fold_left` are missing from AppleClang's
  libc++; `views::enumerate` arrived late there too. Each compiled on libstdc++ and MSVC and
  failed on one macOS leg, after the merge when that leg was not required. A grep for the name
  answers "macOS already compiles it" and is wrong when the hazard is a signature. core-cpp
  builds on AppleClang and on libc++ 17 (emsdk 3.1.56), so: select a missing facility by its
  `__cpp_lib_*` feature-test macro in one header, never an `#if` at a call site and never a
  compiler ID, and compile and test the fallback on every platform. Origin: fastcached
  build-and-toolchain, "The local gate" (the libc++ entries).
- **An instruction-set extension is used only inside a function that asks for it**
  (`__attribute__((target("...")))`), and that function runs only after a runtime CPU check.
  A global `-m` flag makes the translation unit's out-of-line copies of every inline function
  it uses carry the instructions too, the linker keeps one arbitrary copy, and the scalar
  fallback then dies of SIGILL on a CPU without them. Every CI runner has the instructions, so
  only the flags can be checked. Origin:
  [fastcached#1420](https://github.com/LASTRADA-Software/fastcached/issues/1420),
  [fastcached#1442](https://github.com/LASTRADA-Software/fastcached/issues/1442),
  [fastcached#1447](https://github.com/LASTRADA-Software/fastcached/issues/1447).
- **C++20 module scanning is off, for the configure's probes too.** core-cpp has no module
  units, and `core_cpp_apply_toolchain()` sets `CXX_SCAN_FOR_MODULES OFF` on its targets, but a
  `try_compile()` has no such target: from C++20 on, CMake scans every probe of
  `check_cxx_compiler_flag()` and FindThreads. Where the compiler has no `clang-scan-deps`
  (FreeBSD's base clang) every probe failed, so the configure dropped the whole pedantic set,
  reported that the compiler does not accept `-pthread`, and stopped at Threads; nothing in the
  output mentioned scanning. `CoreCppTopLevel.cmake` defaults
  `CMAKE_CXX_SCAN_FOR_MODULES` to OFF. Origin: core-cpp's first Portability run (FreeBSD 14.3,
  clang 19.1.7), reproduced on Linux by pointing `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` at a
  missing file.
- **Run clang-format and clang-tidy at the pinned version, in a build directory of their own.**
  An older clang-tidy is not a laxer one: it is silent about checks it does not have (a file
  clean under 18 arrived at CI with thirteen findings from 22). The PyPI wheel finds its
  resource headers relative to the binary, so run it in place; a copied executable reports
  `'stddef.h' file not found`, and every finding after that is noise over a translation unit
  that did not parse. The compile database must be a clang one, generated with the flags CI
  uses (the `clang-tidy` preset), with module scanning off. Origin: fastcached
  build-and-toolchain, "Language and ABI pitfalls", and
  [fastcached#454](https://github.com/LASTRADA-Software/fastcached/issues/454).
- **A clang-tidy run that cannot prove it analysed anything is worth nothing.** A binary that
  did not execute, a database that did not parse, and a `--checks` expression that selects no
  check (`-*,clang-diagnostic-<x>` is a filter over compiler diagnostics, not a check) all
  print zero findings with exit status 0. Plant a finding and watch it reported before
  believing a run of zeros. Same origin.
- **After editing `.clang-tidy`, rebuild the `clang-tidy` preset with `--clean-first`**
  (`cmake --build --preset clang-tidy --clean-first`, or a fresh tree). `.clang-tidy` is not a
  build input: ninja compares an object with its sources and headers, never with the
  configuration clang-tidy reads, so every object that is up to date skips the analysis, and a
  finding the new configuration adds stays hidden until someone edits that file. A5b turned on
  `NamespaceCase: lower_case`, a warm local tree reported nothing, and CI, building cold, found
  the `namespace CLI` alias in `src/core/cli/App.cpp`; commit `e45f730` fixed it.

## What a `char` is

- **A `char` is UTF-8, at compile time and at run time.** Every MSVC-driver compile gets
  `/utf-8` (`cmake/CoreCppToolchain.cmake`), so the compiler agrees with the runtime about what
  a narrow literal is; without it MSVC reads a source file in the host's ANSI code page and
  re-encodes narrow literals into it, which is byte-identical on a CP-1252 or CP-65001 host and
  different anywhere else.
- **Converting one boundary is the wrong fix.** Turning `argv` into UTF-8 while `getenv`,
  `std::filesystem::path` and the `...A` Windows APIs stay on the legacy code page makes a
  wrong encoding into a *split* one. The process code page decides all of them, so it is set
  for the whole executable or not at all. Origin:
  [fastcached#155](https://github.com/LASTRADA-Software/fastcached/issues/155).
- **`std::filesystem::path`'s narrow constructor throws** on such a host for bytes that are
  not UTF-8, before any `error_code` overload downstream is reached. A narrow path from outside
  the process is converted through one checked function, not constructed directly. Origin:
  fastcached build-and-toolchain, "What a `char` is".

## Line endings

- **LF everywhere, by `.gitattributes` (`* text=auto eol=lf`)**, never by each developer's
  `core.autocrlf`. Without the rule two people editing one file disagree about what a line
  ending is, and a two-line edit comes back as "every line changed".
- **Imports are read as git blobs with `git -c core.autocrlf=false -c core.eol=lf show
  <sha>:<path>`** and refused if they contain a CR byte, so no working tree's conversion
  settings reach core-cpp. The vendoring tool refuses CR bytes the same way.
- Origin: fastcached build-and-toolchain, "Line endings".

## `CORE_CPP_WERROR` decides fatality, not which warnings exist

- **A warning flag and the suppression it makes necessary are governed by one condition.**
  fastcached added `-pedantic` under one option and the matching `-Wno-c2y-extensions` under
  its WERROR option, so the four presets with pedantic on and WERROR off got 7049 warnings and
  none of the suppressions, every build exited 0, and the one place the defect became visible
  was a clang-tidy database, where it read as a stale cache. Origin:
  [fastcached#611](https://github.com/LASTRADA-Software/fastcached/issues/611),
  [fastcached#454](https://github.com/LASTRADA-Software/fastcached/issues/454).
- **So split by what a flag does:** `-Wno-<x>` selects a diagnostic and sits in
  `CORE_CPP_PEDANTIC_TABLE` beside the flag that makes it necessary; `-Werror` and `/WX` decide
  fatality and sit in `CORE_CPP_WERROR_TABLE`. Copying a suppression outward while leaving it
  inward is the same defect with a second place to keep in step.
- **An unpaired `-Wno-error=<x>` is load-bearing by construction** (it keeps a diagnostic
  visible and non-fatal), and retiring one is a measurement, not a tidy-up. Origin:
  [fastcached#805](https://github.com/LASTRADA-Software/fastcached/issues/805).
- **A symptom with two mechanisms reads as unreproducible the moment either one alone is ruled
  out.** A build directory reused across presets can hold one option on and the other off,
  which no correction to the configure line explains. Configure a fresh directory per preset.

## A claim about a tool is checked against the tool

- **A pattern is broader than its author reads it as.** `pgrep -f "scripts/local.gate"` is a
  regular expression; its `.` matched a family of files and its own shell. Anchor it, or use a
  fixed-string match.
- **A guard that has never been seen to refuse is not a guard, and one never seen to accept is
  not known to work.** Run a check in both directions: plant the violation and watch it
  refused, and watch a correct tree pass. Which direction you skipped decides which way the
  instrument lies. core-cpp's hygiene scan has a self-test for exactly this reason.
- **The tree you measured is not necessarily the tree in question.** A grep over a checkout
  parked on an old branch produces a confident "correction" that is wrong. Check what you
  measured (`git rev-parse HEAD` where you searched) before reporting a contradiction; the tell
  is an answer that was too convenient.
- **Check the claim against the tool in front of you, not against how tools of that kind
  behave.** A pipeline's exit status, a `.` in a pattern, a signal disposition, an unset git
  timeout: each was a fact somebody knew in general and did not verify locally.
- **A `cmake -P` check is judged by what it prints and by its exit status together.**
  `message(WARNING)` exits 0 while printing `CMake Warning`; a nested `cmake -P` whose
  `RESULT_VARIABLE` is unread exits 0 with its child's error in the output; and CMake wraps
  diagnostic messages at about 76 columns, so a phrase you match can exist in the output and in
  no single line of it. Flatten whitespace before matching a diagnostic, and assert *which*
  refusal fired, not only that one did.
- Origin: fastcached build-and-toolchain, "A claim about a tool is checked against the tool"
  and "A `cmake -P` check cannot fail its own test".

## A gate that does not report reads as a gate that passed

- **A configuration CI does not run is the same shape, one level up: it does not fail, it is
  absent.** The presets a developer can build and the presets a workflow runs are two lists, and
  nothing made one refer to the other. So the rule is a property rather than a list, because a list
  here would decay exactly like the four enumerations this module has already had to correct:
  **every visible configure preset is named by a workflow, or is allowlisted with a written
  reason.** `core-cpp.preset-coverage` refuses a preset no job runs, an allowlist entry for a
  preset a job now runs (a stale exemption makes the list look maintained while the next preset to
  go dark inherits its credibility), an entry for a preset that no longer exists, and a workflow
  naming a preset `CMakePresets.json` does not define.
- **A setup step gated on an exact preset name breaks the moment the matrix grows, and it breaks
  green.** The Windows job installed the pinned LLVM under `if: matrix.preset == 'clangcl-release'`.
  Adding `clangcl-debug` to that matrix would have produced a leg that configures, builds, tests and
  reports success — on the runner's *bundled* clang-cl, silently below the project's floor of 22.
  **A green leg on an unintended toolchain is worse than no leg, because it also carries a claim**:
  an absent leg says nothing, while that one says "clang-cl 22 passes" and is believed. Gate a
  toolchain-setup step on the property that made it necessary — `startsWith(matrix.preset,
  'clangcl')` — never on one member of the set. The tell that this class is present: a step whose
  condition names a single preset while the thing it installs is needed by a *family*. When adding a
  preset to a matrix, read every `if:` in that job and ask which were written as "the only one"
  rather than "this kind".
- **What it cost before it existed: macOS ran no Debug configuration at all.** The `macos` job was
  `appleclang-release` and `clang-release`, so `NDEBUG` was defined in both and all 30 runtime
  assertions in `src/core` were compiled out of every macOS job — 19 of them in the shared
  event-loop code, among them the twelve `teardownIsSerialisedWithDispatch()` thread-affinity
  checks in `EventLoop.cpp` and `ReadyBatch`'s re-entrancy trap — and both canaries abstain with 77
  under `NDEBUG`. kqueue is macOS-exclusive, so those shared checks had never once been evaluated
  with kqueue underneath them, on the platform Ruling R101 exists because of. Count the runtime
  `assert()`s only when you re-derive this: the 122 `static_assert`s fire at compile time and
  `Require()`/`Guarantee()` abort in Release too, so a grep for `assert` overstates the dark set by
  a factor of five.
  `gcc-debug` and `clangcl-debug` were dark the same way. **All three of the presets nothing ran
  were Debug**, which is not a coincidence: a Release leg is the one somebody adds to ship, and the
  Debug leg is the one that has to be asked for.
- **A failure nobody is shown is indistinguishable from a success.** In fastcached, five of six
  failing merge-queue runs failed a job that was not a required check, and all five pull
  requests merged with nobody told. Origin:
  [fastcached#684](https://github.com/LASTRADA-Software/fastcached/issues/684); a push to
  `master` is the second door
  ([fastcached#774](https://github.com/LASTRADA-Software/fastcached/issues/774)).
- **core-cpp has one required check, `ci-ok`, and it `needs:` every job that gates.** It runs
  `if: always()` and fails unless every one of those jobs *succeeded*: a skipped or cancelled
  job is not a pass. A new job that should gate is added to its `needs:` in the same change.
- **A workflow with a `paths` filter must not be a required check.** When the filter excludes
  a change, the workflow never runs, no check is created, and a required check that never
  reports leaves the pull request blocked with nothing saying why. Gate at the job level
  instead, where a skipped job still reports. `docs.yml` has path filters and is therefore not
  required; `build.yml` has none.
- **A skipped job reports; a skipped *matrix* job does not expand, so its per-leg checks never
  exist.** Never let a dependency's failure skip a gate. Origin:
  [fastcached#300](https://github.com/LASTRADA-Software/fastcached/issues/300).
- **A diagnostic that never ran and one that ran and found nothing are the same green.** A
  probe step with `continue-on-error` must still say it was reached. Origin: fastcached
  build-and-toolchain, "A diagnostic that never RAN".
- **That rule is ten lines above, and it did not stop three instances of itself in one hour.**
  On 2026-09-21 `clang-tidy` went quiet three ways inside sixty minutes: a log read at 179 of 526
  files; a gate never started, because its runner counted against a remembered list instead of
  against this checklist -- **a remembered list can only drop the item you never did**, so memory
  rebuilds something complete by construction and wrong by omission; and a run whose analyser was
  absent from the shell's PATH, which reported `warnings=0` from a tool that was never invoked.
  **What was missing was never the rule. It was the procedure.** A rulebook entry that states a
  principle without an executable step is a sentence people agree with and do not carry out: the
  reader nods, moves to the next paragraph and supplies compliance from memory. So a gate reports
  **four facts, and they are a CONJUNCTION rather than a hierarchy** -- no one of them implies
  another, and the ways they fail are different ways:

  0. **The build's EXIT CODE is the verdict.** Findings are fatal here, so an object cannot exist
     unless its statement passed the analyser: a zero exit over a build that executed statements
     is the answer, and it is the only check that **passes through no pattern anyone wrote**. It
     is therefore cheaper and stronger than reading the log, and it is what demotes the log to a
     diagnostic -- how you find out *why* -- rather than evidence. Two runners on 2026-09-21
     differed only in that one grepped `warning:` and the other `warning:|error:`; the second was
     right **by luck of pattern, not by design**, and a rule that says "print the findings"
     leaves the next person writing a pattern and being lucky or not. Note the pairing: **exit 0
     over zero statements is also exit 0**, so the exit code and the work count are the pair, and
     neither is the verdict alone.
  1. **Which binary answered.** The build already prints it -- `-- [core-cpp] clang-tidy <version>
     (<path>)` from `cmake/CoreCppToolchain.cmake`, in every configure this project has ever run.
     Match the **`[core-cpp] ` prefix**, never the bare string `clang-tidy`: a dependency prints
     `[clang-tidy] Disabled.` and `Enable clang-tidy: OFF ()` into the same output from its own
     options, so grepping for the tool's name returns the proof and a flat contradiction of it,
     adjacent. The general fact: **a dependency reports in the same voice you do.** `message(STATUS)`
     has no namespace, so anything grepped from a configure log is grepped from every project in
     the tree, and a vendored dependency's feature flags collide with yours by name because you
     both named them after the same tool. Our `[core-cpp] ` prefix exists for this. Prefer that line to `command -v`, which answers what *your shell* would find rather
     than what the build resolved: `CORE_CPP_CLANG_TIDY_EXE` is a CACHE variable, so on a tree
     that was not deleted the two can differ. **Deleting the tree is what licenses the shorthand,
     not hygiene.**
  2. **That it was applied to the files you changed.** The configure line proves the tool exists,
     not that it ran on your source. `ninja -t commands <your object>` shows `--tidy="<path>;..."`
     per file. That is what makes a zero mean anything about a particular file -- and with a
     compile launcher in the mix, it is also what rules out a replayed object having skipped the
     analysis.
  3. **That it did work.** Record a count that cannot be zero when the analyser ran; the build
     log's line count serves. `warnings=0` and `warnings=0` are indistinguishable; `535` and `0`
     are not. **Count the WORK; and where you do read the log, PRINT the findings rather than
     counting them** -- a finding count passes through a regex, and a regex is where this
     silently fails. A probe written to catch a silent gate reported `0 findings` twice from a
     run that had just produced two, because it matched `warning:` while `-warnings-as-errors`
     emits `error:`. The work count cannot be zeroed by a bad pattern.

     **A work count does NOT prove the analyser ran on YOUR files, and reading it that way is a
     mistake this rule's own authors made within an hour of writing it.** The count includes
     every statement in the tree -- Catch2, libunicode, link steps -- while `CORE_CPP_CLANG_TIDY`
     applies to core-cpp's targets alone, so *"526 statements ran and none of them carried
     `--tidy`"* is indistinguishable by step count from a clean analysis. A post-rebase run
     reported `steps=25` on an already-built tree: real work, the rebase delta only, and a
     verdict inherited from a previous run rather than earned. **Level 3 says the build did
     something; only levels 2 and 4 say it analysed your change.**
  4. **That the analyser's findings are FATAL, by feeding it one.** A deliberate violation in a
     file you changed proves the right tool ran on your code and that a finding fails the build,
     which is more than 1 and 2 together can say -- a tool can be present, correctly wired, and
     idle.

     **The mutation and the work count answer different questions, and neither implies the
     other.** The mutation proves the **instrument**: the right analyser, on your code, with
     fatal findings. The work count proves the **surface**: that the statements ran at all rather
     than inheriting a previous run's verdict. **A fed violation reports from the file you
     mutated and says nothing about the other five hundred** -- an incremental run can analyse
     exactly that one file, inherit every other verdict, and the mutation reports cleanly either
     way. That is what `steps=25` was. Feed a violation AND count the work; 1 and 2 are then what
     you read to find out why a mutation did not report. Proved twice on 2026-09-21, independently:
     `readability-identifier-naming` on `DeadlineTimer.cpp` and `misc-redundant-expression` on
     `EventLoop.cpp`, both `error:`, both fatal.

  **And DELETE THE TREE, which is load-bearing rather than fastidious.** Ninja skips a build
  statement whose object is up to date -- and `CODE_CHECK`, which runs the analyser, is INSIDE
  the statement. So an incremental run over an unchanged tree reports zero for the same reason an
  absent analyser does, and `warnings=0` then means *nothing needed recompiling*. This composes
  with the `--clean-first` rule above into one rule: a clang-cl cache hit replays no
  `/showIncludes`, so Ninja records no header dependencies, so after a header edit the dependent
  objects look up to date, so their statements are skipped -- **and their analysis with them.**
  The depfile bug does not only leave stale objects, it silently narrows the analysed surface.
  (`.clang-tidy` is not an input of any object either, so editing it rebuilds nothing:
  [core-cpp#36](https://github.com/contour-terminal/core-cpp/issues/36).)

  Refuse to report at all when the build tree does not exist: a configure that failed leaves no
  `build.ninja`, and a runner that greps its absent log finds zero warnings.

  **The conjunction is a script now, not a paragraph: `python scripts/tidy-record.py`.** It
  deletes the tree, configures and builds the `clang-tidy` preset, and prints one record --
  `exit`, the analyser `build.ninja` resolved and the version it answers against the pin, the
  statements carrying `--tidy=`, the `[k/N]` the log reached, and whether a planted violation was
  reported -- then refuses the result unless every field vouches for it. The `clang-tidy` job
  records its build through it (`--log`, `--exit-code`). Its first real run refused, on two
  findings a hand-counted run would have had to spell `error:` to see (Task B13). And note that **a
  version mismatch is a `WARNING`, not a `FATAL_ERROR`** -- absence is fatal, a wrong version is
  not. That is deliberate and it does report, so it satisfies the rule, but read the warning's own
  words before dismissing it: the pin *"is what CI analyses with"*, so a local green from an
  unpinned analyser is not evidence about the gate.

## A C-style loop is classified by its body

`tests/cmake/check-cmake-hygiene.cmake` refuses new three-clause `for` loops, and code imported
from contour and endo still has some. Converting one is not mechanical, and the head of the
loop does not say which kind it is:

- **The body advances the variable** (to consume an escaped character or a length prefix). In a
  range-`for` that `++i` advances a copy, and `auto i` compiles silently; only `auto const i`
  refuses. In fastcached it produced a wrong cache key.
- **A callee advances it through `std::size_t&`.** No body scan sees it, and converting breaks
  every `--key value` option parser at once.
- **The bound is compound** (`i < n && budget > 0`). Two clauses are not one range.
- **The bound is inclusive** (`i <= n` is `iota(0, n + 1)`), which is convertible with a stated
  premise that `n + 1` cannot overflow and that the range is empty where the loop ran zero
  times.
- **The variable outlives the loop**, and a later loop resumes where this one broke off.
- **The step is not an index** (a linked list: `ai = ai->ai_next`, `CMSG_NXTHDR`), and the body
  `continue`s. A `while` whose last statement steps is wrong the moment one `continue` skips it,
  and loops for ever on that node. Step first: `auto const* ai = std::exchange(next,
  next->ai_next);` at the top of the body, so every `continue` moves on. Origin: core-cpp's
  import of contour's `src/net` (Task A6).

Every one of these was first classified "mechanical" by a text scan, because a scan fails
toward "nothing unusual here". Read the body. Target `std::views::iota`; for `argv`,
`std::span{argv, argc}.subspan(1)`. Origin:
[fastcached#1452](https://github.com/LASTRADA-Software/fastcached/issues/1452).

## Open work

- **[core-cpp#38](https://github.com/contour-terminal/core-cpp/issues/38)** — no job runs a
  sanitiser over a Windows source (the clang-tidy half is the `windows (clang-tidy)` job). Measured
  on 2026-09-26: `clangcl-debug` with `CORE_CPP_SANITIZERS=address` configures but does not compile,
  because the Debug CRT refuses ASan (`-MDd not allowed with -fsanitize=address`), and
  `clangcl-release` with it does not link, because on the MSVC driver `core_cpp_apply_toolchain`
  passes no sanitiser flag to the link step (`__asan_init` unresolved).
