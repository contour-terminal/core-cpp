# SPDX-License-Identifier: Apache-2.0

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
set_tests_properties(core-cpp.exit-codes PROPERTIES LABELS "core-cpp;hygiene")

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
set_tests_properties(core-cpp.cmake-hygiene core-cpp.cmake-hygiene-selftest PROPERTIES LABELS "core-cpp;hygiene")

# Which of a module's source lists each platform compiles, Emscripten included (Part I §1).
add_test(NAME core-cpp.platform-sources
         COMMAND "${CMAKE_COMMAND}"
                 "-DTARGETS=${CORE_CPP_SOURCE_DIR}/cmake/CoreCppTargets.cmake"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-platform-sources.cmake")
set_tests_properties(core-cpp.platform-sources PROPERTIES LABELS "core-cpp;hygiene")

# What each row of the module table lets its target link, and that a link or a row outside it is
# refused by name. Each scenario configures a project of its own, with no language enabled.
add_test(NAME core-cpp.layering
         COMMAND "${CMAKE_COMMAND}"
                 "-DROOT=${CORE_CPP_SOURCE_DIR}"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/layering"
                 "-DGENERATOR=${CMAKE_GENERATOR}"
                 "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-layering.cmake")
set_tests_properties(core-cpp.layering PROPERTIES LABELS "core-cpp;hygiene")

# A release tag must name the tree it releases (Part I §4). The release workflow runs
# check-release.cmake on the pushed tag; this proves that it accepts a matching tree and refuses
# each mismatch by name.
add_test(NAME core-cpp.check-release-selftest
         COMMAND "${CMAKE_COMMAND}"
                 "-DCHECKER=${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-release.cmake"
                 "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/check-release-selftest"
                 -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-release-selftest.cmake")
set_tests_properties(core-cpp.check-release-selftest PROPERTIES LABELS "core-cpp;hygiene")

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
    LABELS "core-cpp;hygiene")

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
