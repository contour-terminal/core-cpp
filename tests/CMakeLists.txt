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
# migrate-renames, migrate-codemods and the Windows dialog canaries already set their own below.

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
    LABELS "core-cpp;hygiene"
    TIMEOUT 180)

# Which of a module's source lists each platform compiles, Emscripten included (Part I §1).
add_test(NAME core-cpp.platform-sources
         COMMAND "${CMAKE_COMMAND}"
                 "-DTARGETS=${CORE_CPP_SOURCE_DIR}/cmake/CoreCppTargets.cmake"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-platform-sources.cmake")
set_tests_properties(core-cpp.platform-sources PROPERTIES LABELS "core-cpp;hygiene" TIMEOUT 60)

# What each row of the module table lets its target link, and that a link or a row outside it is
# refused by name. Each scenario configures a project of its own, with no language enabled.
add_test(NAME core-cpp.layering
         COMMAND "${CMAKE_COMMAND}"
                 "-DROOT=${CORE_CPP_SOURCE_DIR}"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/layering"
                 "-DGENERATOR=${CMAKE_GENERATOR}"
                 "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-layering.cmake")
set_tests_properties(core-cpp.layering PROPERTIES LABELS "core-cpp;hygiene" TIMEOUT 120)

# A release tag must name the tree it releases (Part I §4). The release workflow runs
# check-release.cmake on the pushed tag; this proves that it accepts a matching tree and refuses
# each mismatch by name.
add_test(NAME core-cpp.check-release-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-release.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/check-release-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-release-selftest.cmake")
set_tests_properties(core-cpp.check-release-selftest PROPERTIES LABELS "core-cpp;hygiene" TIMEOUT 60)

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
    LABELS "core-cpp;hygiene"
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
    LABELS "core-cpp;hygiene"
    TIMEOUT 300)

# Which provenance rows have an upstream that moved since they were synced (Task B12b), and the
# proof that the checker tells drift, a deletion, a malformed row and an unreachable upstream apart.
#
# The checker runs with --no-fetch here: a test that reaches the network is a test that fails for
# reasons that have nothing to do with the tree. Answering "has upstream moved" for real means
# fetching first, which is what a person or a nightly does by running it without that flag; what
# this registration gates is the part that needs no network -- a row whose SHA, whose upstream path
# or whose core-cpp path is wrong, which is a defect in the table itself.
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
        LABELS "core-cpp;hygiene"
        TIMEOUT 600)
    add_test(NAME core-cpp.upstream-drift-selftest
             COMMAND "${Python3_EXECUTABLE}"
                     "${CORE_CPP_SOURCE_DIR}/scripts/check-upstream-drift-selftest.py")
    set_tests_properties(core-cpp.upstream-drift-selftest PROPERTIES
        LABELS "core-cpp;hygiene"
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
        LABELS "core-cpp;hygiene"
        TIMEOUT 60)
endif()

# A CRT assert, abort(), invalid parameter or crash must end the process instead of opening a dialog
# that waits for a click. Each run must fail (WILL_FAIL) within the timeout; a run that hangs has hit a
# dialog. The canary's main() asks for no suppression: linking core::testing_dialogs installs it.
if(WIN32)
    add_executable(core-cpp-windows-dialog-canary WindowsDialogCanary.cpp)
    target_link_libraries(core-cpp-windows-dialog-canary PRIVATE core::testing_dialogs)
    core_cpp_apply_toolchain(core-cpp-windows-dialog-canary)
    foreach(mode IN ITEMS assert abort invalid-parameter)
        add_test(NAME core-cpp.windows-dialog-canary.${mode} COMMAND core-cpp-windows-dialog-canary ${mode})
        set_tests_properties(core-cpp.windows-dialog-canary.${mode} PROPERTIES
            WILL_FAIL TRUE
            TIMEOUT 60
            SKIP_RETURN_CODE ${CORE_CPP_SKIP_EXIT_CODE}
            LABELS "core-cpp;canary")
    endforeach()
endif()
