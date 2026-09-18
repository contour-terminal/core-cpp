# SPDX-License-Identifier: Apache-2.0
#
# Asserts the exit-code contract of core::testing_main (Part I §3): 0 when every test passed,
# 1 when anything failed or Catch2 otherwise reported an error, 77 when every test case skipped,
# and 2 when nothing ran.
#
# FIXTURE is tests/ExitCodeFixture.cpp, whose test cases are selected by tag, one outcome each.
# EMULATOR, when set, runs it: CMAKE_CROSSCOMPILING_EMULATOR, e.g. node for an Emscripten build.
#
# Usage: cmake -DFIXTURE=<path> [-DEMULATOR=<command>] -P tests/cmake/check-exit-codes.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FIXTURE OR NOT EXISTS "${FIXTURE}")
    message(FATAL_ERROR "check-exit-codes: FIXTURE ('${FIXTURE}') is not set or does not exist.")
endif()

# "<exit status>;<fixture argument>...". Catch2 3.8's own main returns 42 for [fail4] and [mixed],
# 4 for [skipall], 2 for a tag that matches nothing, and 3 when a test ran and passed but, under
# -w UnmatchedTestSpec, another part of the test spec matched nothing (measured).
foreach(_row
        "0;[pass]"
        "1;[fail4]"
        "77;[skipall]"
        "1;[mixed]"
        "2;[nothing-has-this-tag]"
        "1;-w;UnmatchedTestSpec;[pass],[nothing-has-this-tag]")
    list(POP_FRONT _row _want)
    execute_process(COMMAND ${EMULATOR} "${FIXTURE}" ${_row} RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL _want)
        list(JOIN _row " " _arguments)
        message(FATAL_ERROR "exit code for ${_arguments}: got ${_rc}, want ${_want}")
    endif()
endforeach()
