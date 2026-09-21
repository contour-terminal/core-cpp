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
# The subset it links is core::base, core::async, core::net_types and -- since Task B5 --
# core::net, whose WebAssembly subset is the IoBackend contract, the host-driven backend and the
# event loop. What that buys is the scenario a browser consumer actually writes: a PlatformLoop
# with no thread behind it, advanced only by the host's own timer, with a coroutine `delay` and a
# frameless `DeadlineTimer` both parked on it. `-sASYNCIFY` is added HERE and not by core-cpp: it
# rewrites every instrumented function so a C++ stack can be unwound at `emscripten_sleep`, which
# costs code size and start-up time, and core-cpp adds no link flag to its consumers (Part I §3).
# A consumer that never blocks does not need it.
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
target_link_libraries(consumer-wasm-core INTERFACE core::base core::async core::net_types core::net)

## @brief Appends every target defined under directory @p dir, and under its subdirectories, to
## the list named by @p outVar.
function(consumer_collect_targets dir outVar)
    set(collected ${${outVar}})
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    list(APPEND collected ${targets})
    get_property(children DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        consumer_collect_targets("${child}" collected)
    endforeach()
    set(${outVar} "${collected}" PARENT_SCOPE)
endfunction()

# Nothing in a single-threaded WebAssembly build may link Threads: it would force -pthread, and
# with it SharedArrayBuffer, onto every page that loads this.
#
# Read off core-cpp's own targets, not with `if(TARGET Threads::Threads)`: find_package(Threads)
# inside core-cpp creates a DIRECTORY-SCOPED imported target, which this scope cannot see, so the
# target test is false here whether or not core-cpp resolved and linked Threads. A link is what
# would reach this consumer, and a link is what the closure of LINK_LIBRARIES and
# INTERFACE_LINK_LIBRARIES holds.
set(coreCppTargetsInTree "")
consumer_collect_targets("${CORE_CPP_SOURCE_DIR}" coreCppTargetsInTree)
list(FILTER coreCppTargetsInTree INCLUDE REGEX "^core-cpp-")
if(NOT coreCppTargetsInTree)
    message(FATAL_ERROR
        "consumer-wasm: core-cpp defined no target under '${CORE_CPP_SOURCE_DIR}', so the Threads "
        "check below would pass by having nothing to look at.")
endif()
set(threadedTargets "")
foreach(target IN LISTS coreCppTargetsInTree)
    foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(linked ${target} ${property})
        if(linked MATCHES "Threads::Threads|(^|;)-?pthread")
            list(APPEND threadedTargets "${target} ${property}='${linked}'")
        endif()
    endforeach()
endforeach()
if(threadedTargets)
    list(JOIN threadedTargets "\n  " named)
    message(FATAL_ERROR
        "consumer-wasm: core-cpp links threads, which a single-threaded WebAssembly build must "
        "not:\n  ${named}\n"
        "-pthread forces SharedArrayBuffer, and with it cross-origin isolation, onto every page "
        "that loads this program.")
endif()
list(LENGTH coreCppTargetsInTree inTreeCount)
message(STATUS "consumer-wasm: ${inTreeCount} core-cpp target(s), none of them linking threads")

add_executable(consumer-wasm main.cpp)
target_link_libraries(consumer-wasm PRIVATE consumer-wasm-core)
# The consumer's own flag, for the consumer's own program: main() below yields to the host with
# emscripten_sleep while a loop it owns runs to a deadline, and ASYNCIFY is what makes that stack
# unwindable. See the note at the head of this file.
target_link_options(consumer-wasm PRIVATE -sASYNCIFY)
set_target_properties(consumer-wasm PROPERTIES SUFFIX ".js")

find_program(CORE_CPP_NODE NAMES node nodejs)
if(NOT CORE_CPP_NODE)
    message(FATAL_ERROR "consumer-wasm: node was not found, and it is what runs the program.")
endif()
add_test(NAME consumer-wasm COMMAND "${CORE_CPP_NODE}" "$<TARGET_FILE:consumer-wasm>")
# PASS_REGULAR_EXPRESSION, because a WebAssembly program that owns an event loop cannot rely on its
# exit status. A pending `emscripten_async_call` -- which an armed deadline always leaves, that
# call being how the host is asked for its next turn -- takes an Emscripten runtime keepalive, so
# `main` returning is an IMPLICIT exit while the runtime is kept alive: the status is recorded and
# never published, and node exits 0. Measured on core-cpp's own
# `tests/wasm/HostDrivenTimer_smoke.cpp`, whose failing run printed FAIL and exited 0; neither
# `-sEXIT_RUNTIME=1` nor `emscripten_force_exit()` is a good fix, and the reasons are written out
# there. This is the shape a consumer should copy: say what happened on the last line, and have
# ctest read it.
set_tests_properties(consumer-wasm PROPERTIES
    PASS_REGULAR_EXPRESSION "consumer-wasm: every check passed"
    FAIL_REGULAR_EXPRESSION "check\(s\) failed"
    TIMEOUT 120)
