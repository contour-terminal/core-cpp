# SPDX-License-Identifier: Apache-2.0

# Every check here carries its own TIMEOUT, and none of them inherits one.
#
# core_cpp_add_test() bounds what IT registers at 300 seconds, and cannot reach these: they are
# registered with a bare add_test() because they are not test binaries -- they are cmake -P scripts
# and Python runs over the tree. Left unbounded they keep ctest's own default, so a check that hangs
# reports "Timeout" after 25 minutes and names neither itself nor what it was waiting for. Every one
# of these is compute-bound -- it reads the tree, spawns cmake, or shells out to a LOCAL git -- so
# none of them is legitimately slow for a reason a bound would cut short, and a number that is
# generous against its own measurement can only ever fire on a defect.
#
# One number per check rather than one for the file, because their runtimes differ by three orders
# of magnitude. Measured worst-of-two on clang-debug (WSL) and cl-debug, in seconds:
#
#   check                    linux   windows   bound   headroom
#   exit-codes                0.09      0.09      60      600x
#   platform-sources          0.35      0.42      60      140x
#   check-release-selftest    0.47      1.16      60       50x
#   layering                  3.32      1.36     120       36x
#   cmake-hygiene             7.57     13.79     180       13x
#   cmake-hygiene-selftest   13.71      1.04     180       13x
#   vendor-selftest          92.80     30.73     600        6x
#
# The three cheapest share a floor of 60 rather than a multiple of their own runtime: at that scale
# the cost is process startup on a loaded runner, not the work. layering gets more than its 3.3s
# asks for because it configures real CMake projects, which is the part of this set most exposed to
# a cold runner. vendor-selftest gets the most in absolute terms and the least in multiples: it
# builds a git repository per case and hashes a tree, so it is both the slowest and the most
# variable (84s and 93s across the two runs above).
#
# migrate-renames, migrate-codemods, header-self-check-selftest and the Windows dialog canaries
# already set their own below, each beside the measurement it came from.

# THE `tree-level` LABEL (Ruling R96). A check carries it when its input is the source tree, so its
# answer cannot differ between platforms: it reads files, parses tables, or runs a cmake -P script
# over a tree it writes itself. Nothing it does asks a compiler, a linker or an operating system a
# question.
#
# Such a check does not need asking 24 times. Every build job runs `ctest --preset <p>`, so before
# this label one violated provenance row reported as 22 failed jobs out of 24 -- correct behaviour
# and unusable triage, because a total outage is indistinguishable from a real platform bug and
# buries whichever leg has one. CI's per-job ctest now excludes this label and the `style` job runs
# exactly these, once. `ctest -L hygiene` locally still runs every one of them, which is the point:
# a developer's command does not change.
#
# Two checks here deliberately do NOT carry it, and both build something:
#   core-cpp.exit-codes        builds a fixture and runs it through CMAKE_CROSSCOMPILING_EMULATOR,
#                              so the exit status it asserts IS the platform's answer.
#   core-cpp.async-link-smoke  (src/core/async/CMakeLists.txt) links a binary, and answers for the
#                              one toolchain that linked it.
#
# core-cpp.vendor-selftest is the borderline one. Its subject is the tree, but it copies and hashes
# FILE BYTES, which is where line endings differ between platforms -- so of everything labelled here
# it has the largest platform dimension. It carries the label because `.gitattributes` pins the tree
# to LF, the `style` job has a CR-byte check of its own, and it passes identically on both hosts;
# revisit that if either of those stops being true.

# The exit-code contract of core::testing_main, asserted from outside the binary: the fixture has one
# tag per outcome, and check-exit-codes.cmake runs it once per tag.
add_executable(core-cpp-exit-code-fixture ExitCodeFixture.cpp)
target_link_libraries(core-cpp-exit-code-fixture PRIVATE core::testing_main)
core_cpp_apply_toolchain(core-cpp-exit-code-fixture)
add_test(NAME core-cpp.exit-codes
         COMMAND "${CMAKE_COMMAND}"
                 "-DFIXTURE=$<TARGET_FILE:core-cpp-exit-code-fixture>"
                 "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-exit-codes.cmake")
set_tests_properties(core-cpp.exit-codes PROPERTIES LABELS "core-cpp;hygiene" TIMEOUT 60)

# What Part I §3 and the Global Constraints forbid, refused in this tree; and the scanner's own proof
# that it refuses each of those things by name.
add_test(NAME core-cpp.cmake-hygiene
         COMMAND "${CMAKE_COMMAND}"
                 "-DROOT=${CORE_CPP_SOURCE_DIR}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-cmake-hygiene.cmake")
add_test(NAME core-cpp.cmake-hygiene-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DSCANNER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-cmake-hygiene.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake-hygiene-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-cmake-hygiene-selftest.cmake")
set_tests_properties(core-cpp.cmake-hygiene core-cpp.cmake-hygiene-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 180)

# Which of a module's source lists each platform compiles, Emscripten included (Part I §1).
add_test(NAME core-cpp.platform-sources
         COMMAND "${CMAKE_COMMAND}"
                 "-DTARGETS=${CORE_CPP_SOURCE_DIR}/cmake/CoreCppTargets.cmake"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-platform-sources.cmake")
set_tests_properties(core-cpp.platform-sources PROPERTIES LABELS "core-cpp;hygiene;tree-level" TIMEOUT 60)

# What each row of the module table lets its target link, and that a link or a row outside it is
# refused by name. Each scenario configures a project of its own, with no language enabled.
add_test(NAME core-cpp.layering
         COMMAND "${CMAKE_COMMAND}"
                 "-DROOT=${CORE_CPP_SOURCE_DIR}"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/layering"
                 "-DGENERATOR=${CMAKE_GENERATOR}"
                 "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-layering.cmake")
set_tests_properties(core-cpp.layering PROPERTIES LABELS "core-cpp;hygiene;tree-level" TIMEOUT 120)

# "No OpenSSL type appears in any header" (Part I §2 item 8), refused rather than stated: an
# OpenSSL include outside the units that implement the TLS layer, and an OpenSSL type named in any
# header, forward declarations included. Registered whether or not CORE_CPP_WITH_TLS is on, because
# the headers it guards are part of every build. It reads files and nothing else, so it is
# tree-level, and the `style` job runs it.
#
# Bound from measurement: the scan took 65s under WSL, where reading ~560 files from /mnt/d costs
# far more than the work itself. The self-test stages twelve four-file trees.
add_test(NAME core-cpp.openssl-seam
         COMMAND "${CMAKE_COMMAND}"
                 "-DROOT=${CORE_CPP_SOURCE_DIR}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-openssl-seam.cmake")
add_test(NAME core-cpp.openssl-seam-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-openssl-seam.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/openssl-seam-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-openssl-seam-selftest.cmake")
set_tests_properties(core-cpp.openssl-seam core-cpp.openssl-seam-selftest PROPERTIES
    FAIL_REGULAR_EXPRESSION "CMake Error"
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 300)

# Two socket-contract scans ported from fastcached (0708dd54), each with the self-test that proves it
# refuses what it claims and accepts what complies. Both read files and nothing else, so both are
# tree-level and the `style` job runs them.
#
#   cancel-read-declared  every transport in src/core/net/ declares `cancelRead` rather than
#                         inheriting ISocket's no-op, which cannot retire a parked read.
#   read-buffer-guard     every `ISocket::read` calls `contract::requireReadBuffer` before its first
#                         return, or delegates to another socket's read -- the canary for that guard
#                         aborts at the first violation, so it can only ever watch one transport.
add_test(NAME core-cpp.cancel-read-declared
         COMMAND "${CMAKE_COMMAND}"
                 "-DROOT=${CORE_CPP_SOURCE_DIR}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-cancel-read-declared.cmake")
add_test(NAME core-cpp.cancel-read-declared-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-cancel-read-declared.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cancel-read-declared-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-cancel-read-declared-selftest.cmake")
add_test(NAME core-cpp.read-buffer-guard
         COMMAND "${CMAKE_COMMAND}"
                 "-DROOT=${CORE_CPP_SOURCE_DIR}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-read-buffer-guard.cmake")
add_test(NAME core-cpp.read-buffer-guard-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-read-buffer-guard.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/read-buffer-guard-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-read-buffer-guard-selftest.cmake")
# Spelled out rather than looped: check-tree-level-coverage.py reads test names from this file's
# text, and a name assembled from a loop variable is one it cannot see.
set_tests_properties(core-cpp.cancel-read-declared core-cpp.cancel-read-declared-selftest
                     core-cpp.read-buffer-guard core-cpp.read-buffer-guard-selftest PROPERTIES
    FAIL_REGULAR_EXPRESSION "CMake Error"
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 120)

# A release tag must name the tree it releases (Part I §4). The release workflow runs
# check-release.cmake on the pushed tag; this proves that it accepts a matching tree and refuses
# each mismatch by name.
add_test(NAME core-cpp.check-release-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-release.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/check-release-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-release-selftest.cmake")
set_tests_properties(core-cpp.check-release-selftest PROPERTIES LABELS "core-cpp;hygiene;tree-level" TIMEOUT 60)

# Editing `.clang-tidy`, or replacing the analyser at the same path, must re-analyse what it
# governs (core-cpp#36): cmake/CoreCppToolchain.cmake makes both OBJECT_DEPENDS of every analysed
# compile, and check-tidy-inputs.cmake holds the generated build.ninja to it. The check reads THIS
# build, so it exists only in a clang-tidy build with a Ninja generator; the self-test reads its
# own fixtures, and runs everywhere. NOT `tree-level`: the check's input is a build, not the tree.
add_test(NAME core-cpp.tidy-inputs-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-tidy-inputs.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/tidy-inputs-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-tidy-inputs-selftest.cmake")
set_tests_properties(core-cpp.tidy-inputs-selftest PROPERTIES
    FAIL_REGULAR_EXPRESSION "CMake Error"
    LABELS "core-cpp;hygiene"
    TIMEOUT 60)
if(CORE_CPP_CLANG_TIDY AND CMAKE_GENERATOR MATCHES "Ninja")
    add_test(NAME core-cpp.tidy-inputs
             COMMAND "${CMAKE_COMMAND}"
                     "-DBUILD_NINJA=${CMAKE_BINARY_DIR}/build.ninja"
                     "-DROOT=${CORE_CPP_SOURCE_DIR}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-tidy-inputs.cmake")
    set_tests_properties(core-cpp.tidy-inputs PROPERTIES
        FAIL_REGULAR_EXPRESSION "CMake Error"
        LABELS "core-cpp;hygiene"
        TIMEOUT 60)
endif()

# That core_cpp_add_header_self_check() refuses a header needing a neighbour and passes one that
# does not (core-cpp#31). The check itself IS the build -- if the generated object libraries
# compile, every public header stood alone -- so what needs a test is the generator, and each case
# here configures and builds a fixture project of its own.
#
# NOT `tree-level`, deliberately: it compiles, so its answer is the toolchain's. Running it once on
# the style runner would say nothing about MSVC, which is the compiler most likely to disagree
# about what a header drags in (Ruling R96).
#
# Bound from measurement, worst of the two: 7.2s under WSL clang, 1.7s on MSVC. It configures and
# builds two tiny projects, so most of that is CMake and compiler startup rather than work.
add_test(NAME core-cpp.header-self-check-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DMODULE=${CORE_CPP_SOURCE_DIR}/cmake/CoreCppHeaderSelfCheck.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/header-self-check-selftest"
                 "-DGENERATOR=${CMAKE_GENERATOR}"
                 "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                 "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-header-self-check-selftest.cmake")
set_tests_properties(core-cpp.header-self-check-selftest PROPERTIES LABELS "core-cpp;hygiene" TIMEOUT 120)

# core-cpp installs as a CMake package a consumer finds and links, and a parent adding it as a
# subproject can export a target of its own that links it (core-cpp#5): tests/cmake/check-install.cmake
# says how. NOT `tree-level`: it compiles and links against what this build installed.
#
# It needs core::net installed, and says why not where it is not: CORE_CPP_INSTALL off, or a target
# cmake/CoreCppInstall.cmake left out because it links a dependency this build fetched.
set(_coreCppInstallSkip "")
get_property(_coreCppInstalled GLOBAL PROPERTY CORE_CPP_INSTALLED_TARGETS)
get_property(_coreCppInstallSkipped GLOBAL PROPERTY CORE_CPP_INSTALL_SKIPPED)
if(NOT CORE_CPP_INSTALL)
    set(_coreCppInstallSkip "CORE_CPP_INSTALL is OFF in this build")
elseif(NOT "core-cpp-net" IN_LIST _coreCppInstalled)
    set(_coreCppInstallSkip "core::net is not installed by this build: ${_coreCppInstallSkipped}")
endif()
# An instrumented core-cpp needs its runtime on the consumer's link too. link.exe takes neither
# flag: on the MSVC driver the compiler embeds what the linker needs (cmake/CoreCppToolchain.cmake).
set(_coreCppInstrumentFlags ${CORE_CPP_SANITIZER_FLAGS} ${CORE_CPP_COVERAGE_FLAGS})
list(JOIN _coreCppInstrumentFlags "|" _coreCppInstrumentCompile)
set(_coreCppInstrumentLink "${_coreCppInstrumentCompile}")
if(CORE_CPP_MSVC_DRIVER)
    set(_coreCppInstrumentLink "")
endif()
list(JOIN _coreCppInstalled "|" _coreCppInstalledJoined)
string(REPLACE ";" "|" _coreCppBuildPrefixPath "${CMAKE_PREFIX_PATH}")
string(REPLACE ";" "," _coreCppInstallSkip "${_coreCppInstallSkip}")
# Bound from measurement: 7.7s on cl-debug, 25s on cl-release, 115s under WSL clang-debug on a
# loaded machine. It installs, builds a consumer and configures core-cpp twice as a subproject, so
# its time is CMake's and the compiler's startup, which is what a cold runner stretches.
add_test(NAME core-cpp.install
         COMMAND "${CMAKE_COMMAND}"
                 "-DBUILD_DIR=${CORE_CPP_BINARY_DIR}"
                 "-DSOURCE_DIR=${CORE_CPP_SOURCE_DIR}"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-check"
                 "-DLIBDIR=${CMAKE_INSTALL_LIBDIR}"
                 "-DINCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}"
                 "-DEXPECTED_VERSION=${core-cpp_VERSION}"
                 "-DINSTALLED=${_coreCppInstalledJoined}"
                 "-DSKIP_REASON=${_coreCppInstallSkip}"
                 "-DCONFIG=$<CONFIG>"
                 "-DGENERATOR=${CMAKE_GENERATOR}"
                 "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                 "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
                 "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
                 "-DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
                 "-DBUILD_PREFIX_PATH=${_coreCppBuildPrefixPath}"
                 "-DINSTRUMENT_COMPILE_FLAGS=${_coreCppInstrumentCompile}"
                 "-DINSTRUMENT_LINK_FLAGS=${_coreCppInstrumentLink}"
                 "-DSKIP_EXIT_CODE=${CORE_CPP_SKIP_EXIT_CODE}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-install.cmake")
set_tests_properties(core-cpp.install PROPERTIES
    SKIP_RETURN_CODE ${CORE_CPP_SKIP_EXIT_CODE}
    SKIP_REGULAR_EXPRESSION "check-install: SKIPPED"
    LABELS "core-cpp;hygiene"
    TIMEOUT 600)

# The vendoring tool exports the file set of Part I §5 byte for byte, and refuses each way a copy
# can stop being verbatim. Every case builds a git repository of its own; the tool's MODE=check
# needs no git, and one case proves that by running it with an empty PATH.
#
# This is the only test in the tree that needs an external program, and a build from a release
# tarball on a machine without git is a legitimate build, so a missing git is a SKIP rather than a
# red (.agent/rules/testing.md). The script leaves with the skip code where cmake_language(EXIT)
# exists (CMake 3.29) and says so in its output either way, so both properties are set: whichever
# of the two ctest can see, the skip is reported and never reads as a pass.
add_test(NAME core-cpp.vendor-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DTOOL=${CORE_CPP_SOURCE_DIR}/cmake/CoreCppVendor.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/vendor-selftest"
                 "-DSKIP_EXIT_CODE=${CORE_CPP_SKIP_EXIT_CODE}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-vendor-selftest.cmake")
set_tests_properties(core-cpp.vendor-selftest PROPERTIES
    SKIP_RETURN_CODE ${CORE_CPP_SKIP_EXIT_CODE}
    SKIP_REGULAR_EXPRESSION "check-vendor-selftest: SKIPPED"
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 600)

# The migration tooling of tools/migrate/: the rename table every consumer pull request reads, the
# codemod that applies it, and the drift gate that keeps the table and the delivered headers from
# parting company. Both are Python, so nothing is built; they are registered here rather than left as
# a command nobody types (Rulings R67 and R68).
#
# The cases are stdlib unittest, not pytest: pytest is installed on no CI image here, and these are
# plain assertions that need nothing it provides. One of them, over semantic_rename.py, needs
# libclang's optional Python bindings and skips by name without them -- the `style` CI job installs
# them and refuses a skip, so the pass is tested for real on every push.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.migrate-renames
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/tools/migrate/check-renames.py"
                     --root "${CORE_CPP_SOURCE_DIR}")
    add_test(NAME core-cpp.migrate-codemods
             COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                     -s "${CORE_CPP_SOURCE_DIR}/tools/migrate" -p "*_test.py")
else()
    # A gate that does not report reads as a gate that passed, so say why it did not run rather than
    # registering nothing (.agent/rules/build-and-toolchain.md).
    add_test(NAME core-cpp.migrate-renames COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.migrate-renames: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.migrate-codemods COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.migrate-codemods: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.migrate-renames core-cpp.migrate-codemods PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.migrate-renames core-cpp.migrate-codemods PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 300)

# Which provenance rows have an upstream that moved since they were synced (Task B12b), and the
# proof that the checker tells drift, a deletion, a malformed row and an unreachable upstream apart.
#
# The checker runs with --no-fetch here: a test that reaches the network is a test that fails for
# reasons that have nothing to do with the tree. Answering "has upstream moved" for real means
# fetching first, which is what a person or a nightly does by running it without that flag.
#
# What this registration gates is narrower than it once was. The defects that need no checkout --
# a short row, a SHA that is not 40 hex characters, a pattern upstream path, a core-cpp path that
# is gone -- moved to check-cmake-hygiene's `provenance` rule under Ruling R86, because that gate
# runs where these checkouts do not exist. They are still printed here, under OTHER GATE, but they
# no longer set the exit status: one defect reddening two gates gets both of them ignored. What
# reddens here is what only a checkout can answer.
#
# It SKIPS (77) where the upstream checkouts are absent, which is every CI runner: there it reports
# which upstream it could not read rather than reporting nothing and reading as a pass.
#
# Bounds from measurement, worst of the runs on each platform. The checker: 11.3s on Windows, 58.2s
# under WSL, where ~1200 git invocations against /mnt/d cost five times what they cost natively --
# 600 is ten times the slower of the two. The self-test builds fourteen throwaway git repositories:
# 4.2s on Windows, 1.8s on Linux, so 120 is nearly thirty times the slower.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.upstream-drift
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-upstream-drift.py"
                     --no-fetch)
    set_tests_properties(core-cpp.upstream-drift PROPERTIES
        SKIP_RETURN_CODE ${CORE_CPP_SKIP_EXIT_CODE}
        LABELS "core-cpp;hygiene;tree-level"
        TIMEOUT 600)
    add_test(NAME core-cpp.upstream-drift-selftest
             COMMAND "${Python3_EXECUTABLE}"
                     "${CORE_CPP_SOURCE_DIR}/scripts/check-upstream-drift-selftest.py")
    set_tests_properties(core-cpp.upstream-drift-selftest PROPERTIES
        LABELS "core-cpp;hygiene;tree-level"
        TIMEOUT 120)
else()
    # Same reasoning as the migrate checks above: say why it did not run rather than registering
    # nothing (.agent/rules/build-and-toolchain.md).
    add_test(NAME core-cpp.upstream-drift COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.upstream-drift: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.upstream-drift-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.upstream-drift-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.upstream-drift core-cpp.upstream-drift-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED"
        LABELS "core-cpp;hygiene;tree-level"
        TIMEOUT 60)
endif()

# Every `## Open work` entry leads with a core-cpp issue (core-cpp#12). Offline here -- the grammar
# only -- so a local run never fails for want of a network; the `style` job also runs the checker
# `--online`, which refuses an entry whose issue has closed. Bounds: both run in well under a second.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.open-work
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-open-work.py")
    add_test(NAME core-cpp.open-work-selftest
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-open-work-selftest.py")
else()
    add_test(NAME core-cpp.open-work COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.open-work: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.open-work-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.open-work-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.open-work core-cpp.open-work-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.open-work core-cpp.open-work-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 60)

# What keeps R96's arrangement honest. The build jobs exclude `tree-level`, so a labelled check
# that nobody adds a `style` step for runs NOWHERE in CI -- it neither fails nor skips, it is
# absent, and an absent check is indistinguishable from a passing one. This holds the label against
# the job's `# covers:` markers in both directions: a labelled test with no step, and a step naming
# a test that no longer carries the label, which is how the pairing rots after a rename.
#
# It is itself tree-level -- it reads two files of this tree -- so it carries the label and has a
# step of its own, which is the smallest case it has to get right.
#
# Bounds: both read two files and are measured in milliseconds; 60 is the floor this file gives
# anything whose cost is process startup rather than work.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.tree-level-coverage
             COMMAND "${Python3_EXECUTABLE}"
                     "${CORE_CPP_SOURCE_DIR}/scripts/check-tree-level-coverage.py")
    add_test(NAME core-cpp.tree-level-coverage-selftest
             COMMAND "${Python3_EXECUTABLE}"
                     "${CORE_CPP_SOURCE_DIR}/scripts/check-tree-level-coverage-selftest.py")
else()
    add_test(NAME core-cpp.tree-level-coverage COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.tree-level-coverage: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.tree-level-coverage-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.tree-level-coverage-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.tree-level-coverage core-cpp.tree-level-coverage-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.tree-level-coverage core-cpp.tree-level-coverage-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 60)

# Every visible configure preset is run by a workflow, or is allowlisted with a written reason.
#
# `CMakePresets.json` says how the project CAN be built; `.github/workflows/` says how it IS built,
# and nothing referred one to the other. So a preset could be offered, documented and used by a
# lane for hours while no job ran it -- and no gate was red, because a configuration absent from CI
# does not fail there, it is simply not present. Three were, when this was written, and all three
# were Debug: `gcc-debug`, `clangcl-debug`, and `appleclang-debug`, which was the ONLY Debug
# configuration macOS had. All 152 assertions in src/core were compiled out of every macOS job.
#
# tree-level: it reads two files of this tree, so its answer cannot differ between platforms.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.preset-coverage
             COMMAND "${Python3_EXECUTABLE}"
                     "${CORE_CPP_SOURCE_DIR}/scripts/check-preset-coverage.py")
    add_test(NAME core-cpp.preset-coverage-selftest
             COMMAND "${Python3_EXECUTABLE}"
                     "${CORE_CPP_SOURCE_DIR}/scripts/check-preset-coverage-selftest.py")
else()
    add_test(NAME core-cpp.preset-coverage COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.preset-coverage: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.preset-coverage-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.preset-coverage-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.preset-coverage core-cpp.preset-coverage-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.preset-coverage core-cpp.preset-coverage-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 60)

# Every tracked text file is clean UTF-8, with no U+FFFD and no double encoding (core-cpp#40): a repair
# that decoded a file as cp1252 and encoded it again turned 72 em-dashes into mojibake, which is
# still valid UTF-8, and no other gate reads bytes as text. tree-level: it reads the tree and git's
# list of it, so its answer cannot differ between platforms.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.text-encoding
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-text-encoding.py")
    add_test(NAME core-cpp.text-encoding-selftest
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-text-encoding-selftest.py")
else()
    add_test(NAME core-cpp.text-encoding COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.text-encoding: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.text-encoding-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.text-encoding-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.text-encoding core-cpp.text-encoding-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.text-encoding core-cpp.text-encoding-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 60)
# Outside a git checkout (a source archive) the check cannot tell which files are the tree's.
set_tests_properties(core-cpp.text-encoding PROPERTIES SKIP_RETURN_CODE 77)

# scripts/tidy-record.py refuses a clang-tidy result that carries no instrument record: the build's
# exit, the analyser the build resolved against the pin, the statements that carry it, the steps
# the log reached, and a planted violation reported. The script itself reads a build tree, so it is
# the clang-tidy job's last step rather than a test here; its self-test reads nothing but fixtures,
# so it is tree-level.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.tidy-record-selftest
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/tidy-record-selftest.py")
else()
    add_test(NAME core-cpp.tidy-record-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.tidy-record-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.tidy-record-selftest PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.tidy-record-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 60)

# design-principles.md's "anything that touches an ambient resource is reached through an interface
# the object is given", as a scan rather than a sentence: a direct clock or environment read in
# src/core/ outside the seam that owns it is refused, and every excused read is named with its
# reason. tree-level: it reads the tree.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.ambient-reads
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-ambient-reads.py")
    add_test(NAME core-cpp.ambient-reads-selftest
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-ambient-reads-selftest.py")
else()
    add_test(NAME core-cpp.ambient-reads COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.ambient-reads: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.ambient-reads-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.ambient-reads-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.ambient-reads core-cpp.ambient-reads-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.ambient-reads core-cpp.ambient-reads-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 60)

# async-and-net.md's "an `await_ready` stays trivial", as a scan: an `await_ready` body under src/ or
# tests/ that calls a function or constructs an object is refused by file and line
# (fastcached#1546: MSVC 19.44 on ARM64 drops the enclosing `try` of a `co_await` on a temporary
# awaiter whose `await_ready` makes a call). The defect itself is observable on one CI leg only;
# the shape is refused here, on every push. tree-level: it reads the tree.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.await-ready
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-await-ready.py")
    add_test(NAME core-cpp.await-ready-selftest
             COMMAND "${Python3_EXECUTABLE}" "${CORE_CPP_SOURCE_DIR}/scripts/check-await-ready-selftest.py")
else()
    add_test(NAME core-cpp.await-ready COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.await-ready: SKIPPED -- no Python 3 interpreter was found at configure time")
    add_test(NAME core-cpp.await-ready-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.await-ready-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.await-ready core-cpp.await-ready-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.await-ready core-cpp.await-ready-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 60)

# That scripts/clang-format.py and scripts/python-style.py refuse a path that is not theirs.
#
# Both filtered by extension only inside `sources()`, which serves `--all`, so a path a caller
# NAMED reached the tool unfiltered. clang-format reads its input as C++ whatever the name is, so
# naming a `.cmake` rewrote it as C++ and printed "1 file(s) formatted" -- and `--check` was not
# blind but inverted, failing the pristine file and passing the mangled one. Every other gate in
# this tree has a self-test proving it refuses; these two had none, which is exactly why the hole
# survived until a lane named a `.cmake` and lost it.
#
# It needs neither clang-format nor ruff: the refusal is an argument check that runs before either
# binary is looked up, and the cases that must be ACCEPTED assert on the absence of the refusal
# message rather than on an exit status, which a missing binary would otherwise supply for free.
#
# Bounds: 26 subprocess launches, each a Python start-up. 1.7s on Windows, so 120 is seventy times
# the measurement -- process start-up is the cost here, and it is the thing that varies most
# between a warm developer machine and a cold runner.
if(Python3_Interpreter_FOUND)
    add_test(NAME core-cpp.format-scripts-selftest
             COMMAND "${Python3_EXECUTABLE}"
                     "${CORE_CPP_SOURCE_DIR}/scripts/format-scripts-selftest.py")
else()
    add_test(NAME core-cpp.format-scripts-selftest COMMAND "${CMAKE_COMMAND}" -E echo
             "core-cpp.format-scripts-selftest: SKIPPED -- no Python 3 interpreter was found at configure time")
    set_tests_properties(core-cpp.format-scripts-selftest PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()
set_tests_properties(core-cpp.format-scripts-selftest PROPERTIES
    LABELS "core-cpp;hygiene;tree-level"
    TIMEOUT 120)

# The static-CRT twins (CORE_CPP_MSVC_STATIC_RUNTIME_VARIANTS), on a compiler with the MSVC ABI --
# the only one where a program chooses its C runtime and the linker refuses to mix two.
#
#   core-cpp.static-crt-mismatch  a /MT program linking the ordinary (/MD) core::net and core::log
#                                 must FAIL to link, naming RuntimeLibrary. It is registered with the
#                                 option on and off: it is why the twins exist, and it is asserted
#                                 rather than assumed, so a toolchain that stopped recording its C
#                                 runtime would turn this red instead of making the twins pointless
#                                 in silence. It builds its target from inside ctest (EXCLUDE_FROM_ALL,
#                                 so the main build never tries), hence RUN_SERIAL.
#   core-cpp.static-crt-smoke     with the option on, the same program linking core::net_mt and
#                                 core::log_mt links and runs a loopback echo and a log line.
#
# Bounds: the mismatch builds one translation unit and fails at link, 10s measured on cl-debug; the
# smoke is a loopback round trip, well under a second. 300 and 120 leave room for a cold runner.
if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(_coreCppStaticCrt "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_executable(core-cpp-static-crt-mismatch EXCLUDE_FROM_ALL StaticCrtSmoke.cpp)
    set_target_properties(core-cpp-static-crt-mismatch PROPERTIES MSVC_RUNTIME_LIBRARY "${_coreCppStaticCrt}")
    target_link_libraries(core-cpp-static-crt-mismatch PRIVATE core::net core::log)
    core_cpp_apply_toolchain(core-cpp-static-crt-mismatch)
    add_test(NAME core-cpp.static-crt-mismatch
             COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG>
                     --target core-cpp-static-crt-mismatch)
    set_tests_properties(core-cpp.static-crt-mismatch PROPERTIES
        PASS_REGULAR_EXPRESSION "RuntimeLibrary"
        RUN_SERIAL ON
        LABELS "core-cpp;static-crt"
        TIMEOUT 300)

    if(TARGET core::net_mt)
        add_executable(core-cpp-static-crt-smoke StaticCrtSmoke.cpp)
        set_target_properties(core-cpp-static-crt-smoke PROPERTIES MSVC_RUNTIME_LIBRARY "${_coreCppStaticCrt}")
        target_link_libraries(core-cpp-static-crt-smoke PRIVATE core::net_mt core::log_mt)
        core_cpp_apply_toolchain(core-cpp-static-crt-smoke)
        add_test(NAME core-cpp.static-crt-smoke COMMAND core-cpp-static-crt-smoke)
        set_tests_properties(core-cpp.static-crt-smoke PROPERTIES
            LABELS "core-cpp;static-crt;loopback"
            TIMEOUT 120)
    endif()
endif()

# A CRT assert, abort(), invalid parameter or crash must not open a dialog that waits for a click.
# What each run must do differs: `assert` and `abort` die, `invalid-parameter` is handled and
# CONTINUES -- a no-op handler is what the suppression installs, so surviving is the proof. What
# they share is that none of them may WAIT: a run that hangs has hit a dialog, and TIMEOUT is what
# catches that. The canary's main() asks for no suppression: linking core::testing_dialogs
# installs it.
if(WIN32)
    add_executable(core-cpp-windows-dialog-canary WindowsDialogCanary.cpp)
    target_link_libraries(core-cpp-windows-dialog-canary PRIVATE core::testing_dialogs)
    core_cpp_apply_toolchain(core-cpp-windows-dialog-canary)
    foreach(mode IN ITEMS assert abort invalid-parameter)
        add_test(NAME core-cpp.windows-dialog-canary.${mode} COMMAND core-cpp-windows-dialog-canary ${mode})
        # **A canary is judged by a POSITIVE match, not by WILL_FAIL.** WILL_FAIL inverts ANY
        # non-zero exit, so a run that never reached its assertion scored as a pass: measured,
        # a deliberately bogus mode exits 2 and ctest called it Passed. A negative alternation
        # only narrows that -- it can exclude the paths you enumerated, and the paths that print
        # NOTHING are invisible to it. `PASS_REGULAR_EXPRESSION` inverts the burden: the marker
        # printed immediately before the forbidden operation must appear, so reaching the
        # mechanism is what passes and every silent exit fails.
        #
        # Measured, because CMake documents none of it: SKIP_RETURN_CODE still wins over both
        # regexes (so a Release leg, where assertions are compiled out, abstains rather than
        # fails); FAIL_ beats PASS_ when both match, which is what makes the "continued" guard
        # work even though the marker was printed; and a run with no output at all fails with
        # "Required regular expression not found".
        #
        # PASS_REGULAR_EXPRESSION makes ctest IGNORE the exit code, so it REPLACES WILL_FAIL
        # rather than accompanying it -- together they would invert the regex verdict and the
        # test would pass when the marker was absent.
        #
        # **The `onAbort` handler in the canary is load-bearing for all of this**, not tidiness:
        # it turns SIGABRT into `_Exit(1)`, and both properties are documented as unable to
        # override a signal. Delete it as redundant and every scheme here breaks silently.
        #
        # This one was the worst case of it: `ContinuedAfterFailure` is 3, so the single outcome
        # this canary exists to detect -- the process SURVIVING a failure that should have killed
        # it -- was reported as a pass. The author gave that outcome its own exit code and a
        # comment saying so, and the registration discarded the distinction.
        #
        # Its marker already existed: main() prints "failing by <mode>" to stderr, flushed,
        # before the mode dispatch.
        #
        # **The three modes do not share a success condition**, which the previous registration
        # hid by accepting any non-zero exit for all of them. `assert` and `abort` must KILL the
        # process: surviving is the defect, and `CONTINUED AFTER FAILURE` names it.
        # `invalid-parameter` must SURVIVE: the suppression installs a no-op invalid-parameter
        # handler (`SuppressWindowsDialogs.cpp`), so `strcpy_s` returns EINVAL and execution
        # continues -- that is the proof the handler is installed, and dying there would mean it
        # was not. So the same string is the failure marker for two modes and the pass marker for
        # the third. Measured: registering `CONTINUED AFTER FAILURE` as a failure for all three
        # made `invalid-parameter` fail, which is how this difference was found.
        set(_coreCppDialogPass "failing by ${mode}")
        set(_coreCppDialogFail "CONTINUED AFTER FAILURE")
        # `abort` must also SAY so, in a Debug build: the CRT's own "abort() has been called", on
        # stderr and not in a dialog. Clearing `_WRITE_ABORT_MSG` along with `_CALL_REPORTFAULT`
        # made abort() silent, which passed on the marker alone; the marker is printed before the
        # abort, the message only by it. A Release UCRT writes no abort message whatever the flag
        # (measured on cl-release), so there the marker is all there is to match.
        if(mode STREQUAL "abort" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_coreCppDialogPass "abort\\(\\) has been called")
        endif()
        if(mode STREQUAL "invalid-parameter")
            set(_coreCppDialogPass "CONTINUED AFTER FAILURE")
            set(_coreCppDialogFail "")
        endif()
        set_tests_properties(core-cpp.windows-dialog-canary.${mode} PROPERTIES
            PASS_REGULAR_EXPRESSION "${_coreCppDialogPass}"
            FAIL_REGULAR_EXPRESSION "${_coreCppDialogFail}"
            TIMEOUT 60
            SKIP_RETURN_CODE ${CORE_CPP_SKIP_EXIT_CODE}
            LABELS "core-cpp;canary")
    endforeach()
endif()

# The MSVC Debug runtime's iterator checks are live (core-cpp#11): tests/IteratorDebugCanary.cpp
# indexes a vector out of range and must be stopped by the runtime's own `vector subscript out of
# range`. Registered in EVERY MSVC-driver Debug build, whatever `_ITERATOR_DEBUG_LEVEL` turns out to
# be -- a registration conditioned on the level would abstain in exactly the build that lost it --
# and judged by the diagnostic, never by an exit code: the line the program prints when the read
# RETURNED is the failure.
if(WIN32 AND CORE_CPP_MSVC_DRIVER AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(core-cpp-iterator-debug-canary IteratorDebugCanary.cpp)
    target_link_libraries(core-cpp-iterator-debug-canary PRIVATE core::testing_dialogs)
    core_cpp_apply_toolchain(core-cpp-iterator-debug-canary)
    add_test(NAME core-cpp.iterator-debug-canary COMMAND core-cpp-iterator-debug-canary)
    set_tests_properties(core-cpp.iterator-debug-canary PROPERTIES
        PASS_REGULAR_EXPRESSION "vector subscript out of range"
        FAIL_REGULAR_EXPRESSION "the out-of-range index returned"
        TIMEOUT 60
        LABELS "core-cpp;canary")
endif()

# The benchmark behind the 0.4.0 CHANGELOG's numbers for the current-executor context
# (bench/ExecutorContextBench.cpp, which says how to run it). Built so it cannot rot, and registered
# with nothing: a benchmark reports, it asserts nothing ctest could judge. POSIX only, because it
# reads user CPU with getrusage.
if(UNIX AND NOT EMSCRIPTEN AND TARGET core::net)
    add_executable(core-cpp-bench-executor-context bench/ExecutorContextBench.cpp)
    target_link_libraries(core-cpp-bench-executor-context PRIVATE core::net)
    core_cpp_apply_toolchain(core-cpp-bench-executor-context)
endif()

# The WebAssembly smoke, which is a program rather than a Catch2 binary and needs a link flag of
# its own. Its own directory, so that flag reaches nothing else. Empty off Emscripten.
add_subdirectory(wasm)
