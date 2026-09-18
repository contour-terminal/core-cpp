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
  `--clean-first` after a header edit; from ca8dfc32 on, do not bother.** CMake's Ninja
  generator does not give clang-cl `/showIncludes`: it writes `deps = gcc` and asks for a GNU
  depfile through the pass-through, `-clang:-MD -clang:-MT<obj> -clang:-MF<obj>.d`. The older
  launcher did not recognise `-clang:-MF`, so a cache hit wrote the object but no depfile, and
  Ninja recorded no header dependencies for it without a warning. A later header edit then left
  that object stale: an incremental build that is wrong, found in core-cpp only because it
  became a duplicate-symbol link error. `cl` (`deps = msvc`, `/showIncludes`) and GCC and Clang
  hits were unaffected, and a clean build is always correct, because the cache key covers the
  preprocessed input; CI builds from clean trees. The fix is in the fastcache-cc binary alone,
  so the vendored `CompileCache.cmake` needs nothing, and entries cached without a depfile heal
  themselves: the fixed launcher does not serve them to a compile that names a depfile, but
  recompiles and stores again. With an older launcher:
  `cmake --build --preset clangcl-debug --clean-first`. `ninja -t deps <object>` showing
  `#deps 0` is the tell. Origin:
  [fastcached#1531](https://github.com/LASTRADA-Software/fastcached/issues/1531), fixed by
  [fastcached#1533](https://github.com/LASTRADA-Software/fastcached/pull/1533).

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
  stays green. A must-die canary is the remedy fastcached uses; core-cpp has none yet.

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
  (`FindOrNull`, which `core/Ranges.hpp` brings from fastcached in Task A3). Origin:
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
  builds on AppleClang and on libc++ 18 (Emscripten), so: select a missing facility by its
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

Every one of these was first classified "mechanical" by a text scan, because a scan fails
toward "nothing unusual here". Read the body. Target `std::views::iota`; for `argv`,
`std::span{argv, argc}.subspan(1)`. Origin:
[fastcached#1452](https://github.com/LASTRADA-Software/fastcached/issues/1452).
