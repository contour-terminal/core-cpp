# SPDX-License-Identifier: Apache-2.0
#
# core::tui_core on WASI's row (docs/design/tui-headless.md, U.11), in two steps:
#
#   1. core-cpp itself, configured with wasi-sdk's wasm32-wasip1 toolchain file and its options left
#      at their defaults, must list core::tui_core and not core::tui -- the terminal half is off under
#      WASI, the headless half is not -- and must build everything it enables warning-free
#      (CORE_CPP_WERROR) with -fno-exceptions -fno-rtti;
#   2. tests/consumer-tui-core, configured the same way, must link a wasm32-wasip1 module and run it
#      under Node's WASI preview 1: a component tree rendered, diffed and laid out in a guest.
#
# Both take libunicode from the tree this build generated its tables into, so neither downloads
# UCD.zip nor builds libunicode's table generator for the guest.
#
# Needs wasi-sdk (WASI_SDK_PREFIX, from the environment the test runs in, or -DWASI_SDK_PREFIX) and
# node. Without either it SKIPS and says which; CI's wasm job sets both, so there it runs.
#
# What builds under WASI is the rows of cmake/CoreCppModules.cmake marked `PLATFORMS any wasi`; the
# configure output names each module's state, which is what step 1 reads.
#
# Usage: cmake -DSOURCE_DIR=<core-cpp> -DLIBUNICODE_SOURCE=<libunicode tree> -DWORK_DIR=<scratch>
#              -DCTEST=<ctest> [-DWASI_SDK_PREFIX=<wasi-sdk>] [-DNODE=<node>] [-DGENERATOR=<generator>]
#              [-DSKIP_EXIT_CODE=<code>] -P tests/cmake/run-tui-core-wasi.cmake
#
# Per leg, not `tree-level`: it compiles, links and runs, so its answer is the toolchain's.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SKIP_EXIT_CODE OR SKIP_EXIT_CODE STREQUAL "")
    set(SKIP_EXIT_CODE 77)
endif()

## @brief Reports why the check could not run, and leaves with the skip code where CMake can.
## @param reason What is missing.
function(core_cpp_tui_core_wasi_skip reason)
    message(STATUS "run-tui-core-wasi: SKIPPED -- ${reason}")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
endfunction()

foreach(required SOURCE_DIR WORK_DIR CTEST)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "run-tui-core-wasi: ${required} is not set.")
    endif()
endforeach()

if(NOT DEFINED WASI_SDK_PREFIX OR WASI_SDK_PREFIX STREQUAL "")
    set(WASI_SDK_PREFIX "$ENV{WASI_SDK_PREFIX}")
endif()
set(toolchain "${WASI_SDK_PREFIX}/share/cmake/wasi-sdk-p1.cmake")
if(WASI_SDK_PREFIX STREQUAL "")
    core_cpp_tui_core_wasi_skip("WASI_SDK_PREFIX is not set; it names the wasi-sdk to build the guest with")
    return()
elseif(NOT EXISTS "${toolchain}")
    core_cpp_tui_core_wasi_skip("WASI_SDK_PREFIX ('${WASI_SDK_PREFIX}') has no share/cmake/wasi-sdk-p1.cmake")
    return()
endif()
if(NOT DEFINED NODE OR NODE STREQUAL "" OR NODE MATCHES "-NOTFOUND$")
    find_program(NODE NAMES node nodejs NO_CACHE)
endif()
if(NOT NODE)
    core_cpp_tui_core_wasi_skip("no node to run the module under WASI preview 1")
    return()
endif()
if(NOT DEFINED LIBUNICODE_SOURCE OR LIBUNICODE_SOURCE STREQUAL "")
    core_cpp_tui_core_wasi_skip("this build did not fetch libunicode, so it has no generated tree to hand on")
    return()
endif()

set(generatorArgs "")
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
    set(generatorArgs -G "${GENERATOR}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

## @brief Runs one step, failing the check with the step's own output if it fails.
## @param step What the step is, for the failure message.
function(core_cpp_tui_core_wasi_step step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "run-tui-core-wasi: ${step} failed (${result}):\n${out}${err}")
    endif()
    set(stepOutput "${out}${err}" PARENT_SCOPE)
endfunction()

## @brief Fails the check unless the configure output holds @p pattern.
## @param what What the pattern stands for, for the failure message.
function(core_cpp_tui_core_wasi_expect output pattern what)
    if(NOT output MATCHES "${pattern}")
        message(FATAL_ERROR "run-tui-core-wasi: the WASI configure did not say ${what}:\n${output}")
    endif()
endfunction()

# The toolchain file reads WASI_SDK_PREFIX as a variable; it is passed both ways so either reading
# finds it. Release is what a guest ships.
set(wasiArgs
    "-DWASI_SDK_PREFIX=${WASI_SDK_PREFIX}"
    "-DCMAKE_TOOLCHAIN_FILE=${toolchain}"
    -DCMAKE_BUILD_TYPE=Release
    "-DCPM_libunicode_SOURCE=${LIBUNICODE_SOURCE}")

# --- 1. core-cpp's own configure lists tui_core and not tui ---------------------------------------
core_cpp_tui_core_wasi_step("core-cpp's configure"
    "${CMAKE_COMMAND}" -E env "WASI_SDK_PREFIX=${WASI_SDK_PREFIX}"
    "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${WORK_DIR}/core-cpp" ${generatorArgs} ${wasiArgs}
        "-DCMAKE_CXX_FLAGS=-fno-exceptions -fno-rtti"
        -DCORE_CPP_TESTING=OFF -DCORE_CPP_BUILD_EXAMPLES=OFF -DCORE_CPP_INSTALL=OFF -DCORE_CPP_WERROR=ON)
set(configured "${stepOutput}")
core_cpp_tui_core_wasi_expect("${configured}" "skipping UCD download and code generation"
    "that libunicode found its generated tables")
core_cpp_tui_core_wasi_expect("${configured}" "CORE_CPP_WITH_TUI is OFF under WASI" "that the terminal half is off")
core_cpp_tui_core_wasi_expect("${configured}" "module tui: core::tui_output, core::tui_core only"
    "that it builds core::tui_core and core::tui_output, and nothing else of the TUI")
core_cpp_tui_core_wasi_expect("${configured}" "module platform: off \\(its row is not marked wasi, and this is WASI\\)"
    "that core::platform, whose row is not marked wasi, is off")
core_cpp_tui_core_wasi_expect("${configured}" "module base: on" "that core::base, which core::tui_core links, is on")
if(configured MATCHES "module tui: on")
    message(FATAL_ERROR "run-tui-core-wasi: the WASI configure builds core::tui:\n${configured}")
endif()
# The whole tree, not the one target: everything a WASI configure enables must build there.
core_cpp_tui_core_wasi_step("the WASI build" "${CMAKE_COMMAND}" --build "${WORK_DIR}/core-cpp")

# --- 2. a consumer links a module and runs it ------------------------------------------------------
# The consumer compiles everything beneath it with -Werror, libunicode included; the libunicode row keeps
# the one warning WASI's no-SIMD libunicode has off its target (cmake/CoreCppDependencies.cmake), so the
# consumer's flags are a guest's flags and nothing more.
#
# The emulator is a list (node and the script it runs), which no -D argument passed through a function's
# ARGN survives, so it reaches the consumer's cache through an initial-cache script.
file(WRITE "${WORK_DIR}/emulator.cmake"
     "set(CMAKE_CROSSCOMPILING_EMULATOR \"${NODE};${SOURCE_DIR}/tests/wasi-unicode/run-wasi.mjs\" CACHE STRING \"\")\n")
core_cpp_tui_core_wasi_step("the consumer's configure"
    "${CMAKE_COMMAND}" -E env "WASI_SDK_PREFIX=${WASI_SDK_PREFIX}"
    "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/consumer-tui-core" -B "${WORK_DIR}/consumer" ${generatorArgs}
        ${wasiArgs}
        "-DCMAKE_CXX_FLAGS=-fno-exceptions -fno-rtti"
        -C "${WORK_DIR}/emulator.cmake"
        "-DCORE_CPP_SOURCE_DIR=${SOURCE_DIR}")
core_cpp_tui_core_wasi_step("the consumer's build" "${CMAKE_COMMAND}" --build "${WORK_DIR}/consumer")
core_cpp_tui_core_wasi_step("the consumer's run"
    "${CTEST}" --test-dir "${WORK_DIR}/consumer" --output-on-failure --no-tests=error -V)
if(NOT stepOutput MATCHES "consumer-tui-core: [0-9]+ bytes of frame, no terminal")
    message(FATAL_ERROR "run-tui-core-wasi: the module ran, but did not report its frame:\n${stepOutput}")
endif()
message(STATUS "run-tui-core-wasi: core::tui_core configured, built and ran for wasm32-wasip1")
