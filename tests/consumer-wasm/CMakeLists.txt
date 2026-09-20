# SPDX-License-Identifier: Apache-2.0
#
# A consumer that builds core-cpp for WebAssembly, shaped like morph's: the core-cpp targets are
# gathered behind one INTERFACE library, and every program of the project links that rather than
# naming modules of its own.
#
#   emcmake cmake -S tests/consumer-wasm -B <build> -DCORE_CPP_SOURCE_DIR=<core-cpp>
#   cmake --build <build> && ctest --test-dir <build> --output-on-failure   # runs it under node
#
# What it proves is the WebAssembly subset of Part I §1 as a CONSUMER sees it: single-threaded (no
# -pthread, so no SharedArrayBuffer is forced on the page that loads it), with CORE_CPP_WITH_TUI
# off and CORE_CPP_FETCH_DEPS off, so the configure resolves every dependency without a fetch.
#
# Today the subset it links is core::base, core::async and core::net_types: core::net itself is
# native-only until Tasks B3 to B5 bring its WebAssembly backend, and there are no timers to await
# yet. Task B5 extends this program to await a coroutine `delay` on a host-driven loop under
# -sASYNCIFY, and links core::net here.
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-wasm VERSION 1.0.0 LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "consumer-wasm: configure this with `emcmake cmake ...` from an emsdk environment; it is "
        "the WebAssembly consumer.")
endif()
if(NOT DEFINED CORE_CPP_SOURCE_DIR)
    get_filename_component(CORE_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

enable_testing()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Werror)

# With C++20 or newer and Ninja, CMake scans every source for module imports, and the scanner it
# wants (clang-scan-deps) is not part of an emsdk. core-cpp turns the scan off on its own targets
# (core_cpp_apply_toolchain); a consumer's targets are the consumer's to turn off, and a
# WebAssembly build has to.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# core::async signals cancellation with an exception type (core::async::OperationCancelled), so a
# consumer that awaits its tasks compiles and links with exceptions on. Emscripten disables them by
# default; this is the consumer's choice to make, and core-cpp adds no flag of its own to it.
add_compile_options(-fexceptions)
add_link_options(-fexceptions)

include("${CORE_CPP_SOURCE_DIR}/cmake/CPM.cmake")
CPMAddPackage(
    NAME core-cpp
    SOURCE_DIR "${CORE_CPP_SOURCE_DIR}"
    SYSTEM YES
    EXCLUDE_FROM_ALL YES
    OPTIONS "CORE_CPP_WITH_TUI OFF" "CORE_CPP_FETCH_DEPS OFF")

# The subset, behind the one library the rest of the project links (morph's shape).
add_library(consumer-wasm-core INTERFACE)
target_link_libraries(consumer-wasm-core INTERFACE core::base core::async core::net_types)

# Nothing in a single-threaded WebAssembly build may link Threads: it would force -pthread, and
# with it SharedArrayBuffer, onto every page that loads this.
if(TARGET Threads::Threads)
    message(FATAL_ERROR "consumer-wasm: Threads was resolved, which a single-threaded WebAssembly build must not link.")
endif()

add_executable(consumer-wasm main.cpp)
target_link_libraries(consumer-wasm PRIVATE consumer-wasm-core)
set_target_properties(consumer-wasm PROPERTIES SUFFIX ".js")

find_program(CORE_CPP_NODE NAMES node nodejs)
if(NOT CORE_CPP_NODE)
    message(FATAL_ERROR "consumer-wasm: node was not found, and it is what runs the program.")
endif()
add_test(NAME consumer-wasm COMMAND "${CORE_CPP_NODE}" "$<TARGET_FILE:consumer-wasm>")
set_tests_properties(consumer-wasm PROPERTIES TIMEOUT 120)
