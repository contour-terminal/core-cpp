# SPDX-License-Identifier: Apache-2.0
#
# Builds tests/wasi-unicode for wasm32-wasip1 against the libunicode tree this build generated its
# tables into, and runs its width and grapheme table under Node's WASI: the decision record's
# option (a) for a guest's width tables (docs/design/tui-headless.md), exercised end to end. No
# UCD.zip is downloaded and no table generator is built for the guest -- the sub-build refuses a
# tree without the generated files rather than letting libunicode fetch them.
#
# Needs wasi-sdk (WASI_SDK_PREFIX, from the environment the test runs in, or -DWASI_SDK_PREFIX) and
# node. Without either it SKIPS and says which; CI's wasm job sets both, so there it runs.
#
# Usage: cmake -DSOURCE_DIR=<core-cpp> -DLIBUNICODE_SOURCE=<libunicode tree> -DWORK_DIR=<scratch>
#              -DCTEST=<ctest> [-DWASI_SDK_PREFIX=<wasi-sdk>] [-DNODE=<node>] [-DGENERATOR=<generator>]
#              [-DSKIP_EXIT_CODE=<code>] -P tests/cmake/run-wasi-unicode.cmake
#
# Per leg, not `tree-level`: it compiles, links and runs, so its answer is the toolchain's.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SKIP_EXIT_CODE OR SKIP_EXIT_CODE STREQUAL "")
    set(SKIP_EXIT_CODE 77)
endif()

## @brief Reports why the check could not run, and leaves with the skip code where CMake can.
## @param reason What is missing.
function(core_cpp_wasi_unicode_skip reason)
    message(STATUS "run-wasi-unicode: SKIPPED -- ${reason}")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
endfunction()

foreach(required SOURCE_DIR WORK_DIR CTEST)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run-wasi-unicode: ${required} is not set.")
    endif()
endforeach()

if(NOT DEFINED WASI_SDK_PREFIX OR WASI_SDK_PREFIX STREQUAL "")
    set(WASI_SDK_PREFIX "$ENV{WASI_SDK_PREFIX}")
endif()
set(toolchain "${WASI_SDK_PREFIX}/share/cmake/wasi-sdk-p1.cmake")
if(WASI_SDK_PREFIX STREQUAL "")
    core_cpp_wasi_unicode_skip("WASI_SDK_PREFIX is not set; it names the wasi-sdk to build the guest with")
    return()
elseif(NOT EXISTS "${toolchain}")
    core_cpp_wasi_unicode_skip("WASI_SDK_PREFIX ('${WASI_SDK_PREFIX}') has no share/cmake/wasi-sdk-p1.cmake")
    return()
endif()
if(NOT DEFINED NODE OR NODE STREQUAL "" OR NODE MATCHES "-NOTFOUND$")
    find_program(NODE NAMES node nodejs NO_CACHE)
endif()
if(NOT NODE)
    core_cpp_wasi_unicode_skip("no node to run the module under WASI preview 1")
    return()
endif()
# A libunicode found installed rather than fetched has no source tree; the tables then cannot be
# taken from this build. That is a configuration this check does not cover, not a failure.
if(NOT DEFINED LIBUNICODE_SOURCE OR LIBUNICODE_SOURCE STREQUAL "")
    core_cpp_wasi_unicode_skip("this build did not fetch libunicode, so it has no generated tree to hand on")
    return()
endif()

set(generatorArgs "")
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
    set(generatorArgs -G "${GENERATOR}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

## @brief Runs one step, failing the check with the step's own output if it fails.
## @param step What the step is, for the failure message.
function(core_cpp_wasi_unicode_step step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "run-wasi-unicode: ${step} failed (${result}):\n${out}${err}")
    endif()
    set(stepOutput "${out}${err}" PARENT_SCOPE)
endfunction()

# The toolchain file reads WASI_SDK_PREFIX as a variable; it is passed both ways so either reading
# finds it. CMAKE_BUILD_TYPE Release is what a guest ships.
core_cpp_wasi_unicode_step(configure
    "${CMAKE_COMMAND}" -E env "WASI_SDK_PREFIX=${WASI_SDK_PREFIX}"
    "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/wasi-unicode" -B "${WORK_DIR}" ${generatorArgs}
        "-DWASI_SDK_PREFIX=${WASI_SDK_PREFIX}"
        "-DCMAKE_TOOLCHAIN_FILE=${toolchain}"
        -DCMAKE_BUILD_TYPE=Release
        "-DCPM_libunicode_SOURCE=${LIBUNICODE_SOURCE}"
        "-DCORE_CPP_WASI_UNICODE_NODE=${NODE}"
        "-DCORE_CPP_SOURCE_DIR=${SOURCE_DIR}")
if(stepOutput MATCHES "CPM:|[Dd]ownloading")
    message(FATAL_ERROR
        "run-wasi-unicode: the guest's configure reached for the network, which it must not:\n${stepOutput}")
endif()
if(NOT stepOutput MATCHES "skipping UCD download and code generation")
    message(FATAL_ERROR
        "run-wasi-unicode: libunicode did not report its generated tables as found, so this build may have "
        "downloaded UCD.zip:\n${stepOutput}")
endif()
core_cpp_wasi_unicode_step(build "${CMAKE_COMMAND}" --build "${WORK_DIR}")
core_cpp_wasi_unicode_step(test "${CTEST}" --test-dir "${WORK_DIR}" --output-on-failure --no-tests=error -V)
message(STATUS "run-wasi-unicode: ${stepOutput}")
