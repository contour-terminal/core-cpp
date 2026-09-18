# SPDX-License-Identifier: Apache-2.0
#
# Asserts the exit-code contract of core::testing_main (Part I §3): 0 when every test passed,
# 1 when anything failed, 77 when every test case skipped, and 2 when nothing ran.
#
# FIXTURE is tests/ExitCodeFixture.cpp, whose test cases are selected by tag, one outcome each.
# EMULATOR, when set, runs it: CMAKE_CROSSCOMPILING_EMULATOR, e.g. node for an Emscripten build.
#
# Usage: cmake -DFIXTURE=<path> [-DEMULATOR=<command>] -P tests/cmake/check-exit-codes.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FIXTURE OR NOT EXISTS "${FIXTURE}")
    message(FATAL_ERROR "check-exit-codes: FIXTURE ('${FIXTURE}') is not set or does not exist.")
endif()

# "<tag>;<exit status>". Catch2 3.8's own main returns 42 for [fail4] and [mixed], 4 for [skipall]
# and 2 for a tag that matches nothing (measured).
foreach(_row "[pass];0" "[fail4];1" "[skipall];77" "[mixed];1" "[nothing-has-this-tag];2")
    list(GET _row 0 _tag)
    list(GET _row 1 _want)
    execute_process(COMMAND ${EMULATOR} "${FIXTURE}" "${_tag}" RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
    if(NOT _rc EQUAL _want)
        message(FATAL_ERROR "exit code for ${_tag}: got ${_rc}, want ${_want}")
    endif()
endforeach()
