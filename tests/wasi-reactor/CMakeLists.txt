# SPDX-License-Identifier: Apache-2.0
#
# core::tui_core linked into a WASI *reactor* (docs/design/tui-headless.md, U.12): a module with no
# main() whose host calls _initialize and then an exported function, the shape a guest of a terminal's
# extension host takes. reactor.cpp drives VtParser -> Screen -> diffFrames -> solve and prints a line
# per check; tests/cmake/run-tui-core-reactor.cmake builds it with wasi-sdk and runs it under wasmtime.
#
#   cmake -S tests/wasi-reactor -B <dir> -DCMAKE_TOOLCHAIN_FILE=<wasi-sdk>/share/cmake/wasi-sdk-p1.cmake
#         -DCPM_libunicode_SOURCE=<a libunicode tree with its generated tables> -DWASMTIME=<wasmtime>
#   cmake --build <dir> && ctest --test-dir <dir>
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-tui-core-reactor VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    message(FATAL_ERROR "tui-core-reactor: a WASI reactor is built with a WASI toolchain file, not for ${CMAKE_SYSTEM_NAME}.")
endif()
if(NOT DEFINED CORE_CPP_SOURCE_DIR)
    get_filename_component(CORE_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()

enable_testing()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Werror)

include("${CORE_CPP_SOURCE_DIR}/cmake/CPM.cmake")
CPMAddPackage(
    NAME core-cpp
    SOURCE_DIR "${CORE_CPP_SOURCE_DIR}"
    SYSTEM YES
    EXCLUDE_FROM_ALL YES
    OPTIONS "CORE_CPP_WITH_TUI_CORE ON")

# -mexec-model=reactor: crt1-reactor.o, so the module exports _initialize (the static constructors)
# instead of _start, and the host decides what to call and when.
add_executable(tui-core-reactor reactor.cpp)
set_target_properties(tui-core-reactor PROPERTIES SUFFIX ".wasm")
target_link_libraries(tui-core-reactor PRIVATE core::tui_core)
target_link_options(tui-core-reactor PRIVATE -mexec-model=reactor)

if(DEFINED WASMTIME AND NOT WASMTIME STREQUAL "")
    add_test(NAME tui-core-reactor
             COMMAND "${WASMTIME}" run --invoke core_tui_reactor_run "$<TARGET_FILE:tui-core-reactor>")
    # A failed check traps, so the exit status judges; "not ok" catches a check that printed and returned.
    set_tests_properties(tui-core-reactor PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "not ok")
endif()
