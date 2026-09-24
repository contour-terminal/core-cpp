# SPDX-License-Identifier: Apache-2.0
#
# A consumer that wants styled terminal output and nothing else, added through CPM the way
# Lightweight's dbtool adds it: CORE_CPP_WITH_TUI off, CORE_CPP_WITH_TUI_OUTPUT on.
#
#   cmake -S tests/consumer-tui-output -B out/consumer-tui-output -DCORE_CPP_SOURCE_DIR=<core-cpp>
#   cmake --build out/consumer-tui-output
#   ctest --test-dir out/consumer-tui-output --output-on-failure
#
# What it asserts is the promise docs/modules/tui.md makes for core::tui_output: that a program
# linking it "takes nothing else with it". Before 0.2.1 the leaf was gated on CORE_CPP_WITH_TUI, so
# the only configuration that produced it also fetched libunicode -- and libunicode's own configure
# downloads UCD.zip from www.unicode.org -- for a target that links core::base alone. dbtool found
# it (plan C5).
#
# Each check says which value was wrong, because a failure here is a consumer's build changing.
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-tui-output VERSION 1.0.0 LANGUAGES CXX)

if(NOT DEFINED CORE_CPP_SOURCE_DIR)
    get_filename_component(CORE_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
if(NOT EXISTS "${CORE_CPP_SOURCE_DIR}/cmake/CoreCppModules.cmake")
    message(FATAL_ERROR
        "consumer-tui-output: CORE_CPP_SOURCE_DIR ('${CORE_CPP_SOURCE_DIR}') is not a core-cpp source tree.")
endif()

enable_testing()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

include("${CORE_CPP_SOURCE_DIR}/cmake/CPM.cmake")
CPMAddPackage(
    NAME core-cpp
    SOURCE_DIR "${CORE_CPP_SOURCE_DIR}"
    SYSTEM YES
    EXCLUDE_FROM_ALL YES
    OPTIONS "CORE_CPP_WITH_TUI OFF" "CORE_CPP_WITH_TUI_OUTPUT ON" "CORE_CPP_WITH_TLS OFF")

set(problems "")

if(NOT TARGET core::tui_output)
    string(APPEND problems
        "\n  core::tui_output is not a target, although CORE_CPP_WITH_TUI_OUTPUT is ON")
endif()
if(TARGET core::tui)
    string(APPEND problems "\n  core::tui is a target, although CORE_CPP_WITH_TUI is OFF")
endif()

# libunicode is core::tui's dependency and nothing else's. Asked three ways, because each of them
# alone has a way to answer "no" while it was fetched: the target is what a find_package() or a
# fetch leaves behind; CPM_PACKAGES and CPM_PACKAGE_libunicode_VERSION are the cache entries in
# which CPM records that it added the package, whether or not the target survived.
if(TARGET unicode::unicode)
    string(APPEND problems "\n  unicode::unicode is a target: libunicode was found or fetched")
endif()
if("libunicode" IN_LIST CPM_PACKAGES)
    string(APPEND problems "\n  CPM_PACKAGES names libunicode: '${CPM_PACKAGES}'")
endif()
if(DEFINED CACHE{CPM_PACKAGE_libunicode_VERSION})
    string(APPEND problems "\n  CPM recorded libunicode ${CPM_PACKAGE_libunicode_VERSION} as added")
endif()

if(problems)
    message(FATAL_ERROR
        "consumer-tui-output: a consumer of core::tui_output alone did not get what it was promised:"
        "${problems}\n"
        "core::tui_output links core::base alone (cmake/CoreCppModules.cmake), so "
        "CORE_CPP_WITH_TUI_OUTPUT must build it without CORE_CPP_WITH_TUI and without libunicode.")
endif()
message(STATUS "consumer-tui-output: core::tui_output is a target; core::tui and libunicode are not")

add_executable(consumer-tui-output main.cpp)
target_link_libraries(consumer-tui-output PRIVATE core::tui_output)
add_test(NAME consumer-tui-output COMMAND consumer-tui-output)
set_tests_properties(consumer-tui-output PROPERTIES TIMEOUT 60)
