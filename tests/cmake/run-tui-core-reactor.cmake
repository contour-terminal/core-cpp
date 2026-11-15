# SPDX-License-Identifier: Apache-2.0
#
# core::tui_core in a WASI reactor under wasmtime (docs/design/tui-headless.md, U.12): tests/wasi-reactor,
# configured with wasi-sdk's wasm32-wasip1 toolchain file and -fno-exceptions -fno-rtti, links
# core::tui_core into a module with no main() and runs its exported core_tui_reactor_run under
# wasmtime, which calls _initialize first. The guest parses bytes with VtParser, delivers the events to
# a Screen over a HeadlessSurface, diffs two frames with diffFrames() and checks the tree against
# layout::solve(); this check reads its summary line.
#
# libunicode comes from the tree this build generated its tables into, as for core-cpp.tui-core-wasi,
# so the guest's build downloads nothing.
#
# Needs wasi-sdk (WASI_SDK_PREFIX) and wasmtime (WASMTIME, or on PATH), each from the environment the
# test runs in or as -D. Without either it SKIPS and says which; CI's `wasi` job provides both pinned.
#
# Usage: cmake -DSOURCE_DIR=<core-cpp> -DLIBUNICODE_SOURCE=<libunicode tree> -DWORK_DIR=<scratch>
#              -DCTEST=<ctest> [-DWASI_SDK_PREFIX=<wasi-sdk>] [-DWASMTIME=<wasmtime>]
#              [-DGENERATOR=<generator>] [-DSKIP_EXIT_CODE=<code>] -P tests/cmake/run-tui-core-reactor.cmake
#
# Per leg, not `tree-level`: it compiles, links and runs, so its answer is the toolchain's.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SKIP_EXIT_CODE OR SKIP_EXIT_CODE STREQUAL "")
    set(SKIP_EXIT_CODE 77)
endif()

## @brief Reports why the check could not run, and leaves with the skip code where CMake can.
## @param reason What is missing.
function(core_cpp_tui_core_reactor_skip reason)
    message(STATUS "run-tui-core-reactor: SKIPPED -- ${reason}")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
endfunction()

foreach(required SOURCE_DIR WORK_DIR CTEST)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run-tui-core-reactor: ${required} is not set.")
    endif()
endforeach()

if(NOT DEFINED WASI_SDK_PREFIX OR WASI_SDK_PREFIX STREQUAL "")
    set(WASI_SDK_PREFIX "$ENV{WASI_SDK_PREFIX}")
endif()
set(toolchain "${WASI_SDK_PREFIX}/share/cmake/wasi-sdk-p1.cmake")
if(WASI_SDK_PREFIX STREQUAL "")
    core_cpp_tui_core_reactor_skip("WASI_SDK_PREFIX is not set; it names the wasi-sdk to build the guest with")
    return()
elseif(NOT EXISTS "${toolchain}")
    core_cpp_tui_core_reactor_skip("WASI_SDK_PREFIX ('${WASI_SDK_PREFIX}') has no share/cmake/wasi-sdk-p1.cmake")
    return()
endif()
if(NOT DEFINED WASMTIME OR WASMTIME STREQUAL "")
    set(WASMTIME "$ENV{WASMTIME}")
endif()
if(WASMTIME STREQUAL "")
    find_program(WASMTIME NAMES wasmtime NO_CACHE)
endif()
if(NOT WASMTIME OR NOT EXISTS "${WASMTIME}")
    core_cpp_tui_core_reactor_skip("no wasmtime to run the reactor under (WASMTIME, or wasmtime on PATH)")
    return()
endif()
if(NOT DEFINED LIBUNICODE_SOURCE OR LIBUNICODE_SOURCE STREQUAL "")
    core_cpp_tui_core_reactor_skip("this build did not fetch libunicode, so it has no generated tree to hand on")
    return()
endif()

set(generatorArgs "")
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
    set(generatorArgs -G "${GENERATOR}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

## @brief Runs one step, failing the check with the step's own output if it fails.
## @param step What the step is, for the failure message.
function(core_cpp_tui_core_reactor_step step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "run-tui-core-reactor: ${step} failed (${result}):\n${out}${err}")
    endif()
    set(stepOutput "${out}${err}" PARENT_SCOPE)
endfunction()

# The toolchain file reads WASI_SDK_PREFIX as a variable; it is passed both ways so either reading finds it.
core_cpp_tui_core_reactor_step("the reactor's configure"
    "${CMAKE_COMMAND}" -E env "WASI_SDK_PREFIX=${WASI_SDK_PREFIX}"
    "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/wasi-reactor" -B "${WORK_DIR}" ${generatorArgs}
        "-DWASI_SDK_PREFIX=${WASI_SDK_PREFIX}"
        "-DCMAKE_TOOLCHAIN_FILE=${toolchain}"
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_CXX_FLAGS=-fno-exceptions -fno-rtti"
        "-DCPM_libunicode_SOURCE=${LIBUNICODE_SOURCE}"
        "-DCORE_CPP_SOURCE_DIR=${SOURCE_DIR}"
        "-DWASMTIME=${WASMTIME}")
if(NOT stepOutput MATCHES "skipping UCD download and code generation")
    message(FATAL_ERROR "run-tui-core-reactor: libunicode did not find its generated tables:\n${stepOutput}")
endif()
core_cpp_tui_core_reactor_step("the reactor's build" "${CMAKE_COMMAND}" --build "${WORK_DIR}")

# A reactor exports _initialize, which runs its static constructors; a command module has _start instead.
file(READ "${WORK_DIR}/tui-core-reactor.wasm" module HEX)
foreach(export IN ITEMS _initialize core_tui_reactor_run)
    string(HEX "${export}" exportHex)
    if(NOT module MATCHES "${exportHex}")
        message(FATAL_ERROR "run-tui-core-reactor: the module names no ${export}; it is not the reactor it should be")
    endif()
endforeach()

core_cpp_tui_core_reactor_step("the reactor's run"
    "${CTEST}" --test-dir "${WORK_DIR}" --output-on-failure --no-tests=error -V)
message(STATUS "${stepOutput}")
if(NOT stepOutput MATCHES "tui-core-reactor: ([0-9]+) of ([0-9]+) checks passed" OR NOT CMAKE_MATCH_1 EQUAL CMAKE_MATCH_2)
    message(FATAL_ERROR "run-tui-core-reactor: the reactor ran, but did not report every check passed:\n${stepOutput}")
endif()
message(STATUS "run-tui-core-reactor: core::tui_core ran as a wasm32-wasip1 reactor under wasmtime, ${CMAKE_MATCH_1} checks")
