# SPDX-License-Identifier: Apache-2.0
#
# A consumer that renders a component tree with no terminal under it, added through CPM the way a
# WebAssembly guest's native twin adds it: CORE_CPP_WITH_TUI off, CORE_CPP_WITH_TUI_CORE on.
#
#   cmake -S tests/consumer-tui-core -B out/consumer-tui-core -DCORE_CPP_SOURCE_DIR=<core-cpp>
#   cmake --build out/consumer-tui-core
#   ctest --test-dir out/consumer-tui-core --output-on-failure
#
# What it asserts is the promise docs/design/tui-headless.md makes for core::tui_core: that it builds
# with the terminal half of the TUI switched off, and that what it links names nothing of
# core::platform, core::async or core::net -- read off the link closure here, at configure time, and
# off the archive's symbols by core-cpp's own core-cpp.tui-core-symbols test.
#
# Each check says which value was wrong, because a failure here is a consumer's build changing.
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-tui-core VERSION 1.0.0 LANGUAGES CXX)

if(NOT DEFINED CORE_CPP_SOURCE_DIR)
    get_filename_component(CORE_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
if(NOT EXISTS "${CORE_CPP_SOURCE_DIR}/cmake/CoreCppModules.cmake")
    message(FATAL_ERROR
        "consumer-tui-core: CORE_CPP_SOURCE_DIR ('${CORE_CPP_SOURCE_DIR}') is not a core-cpp source tree.")
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
    OPTIONS "CORE_CPP_WITH_TUI OFF" "CORE_CPP_WITH_TUI_CORE ON" "CORE_CPP_WITH_TLS OFF")

set(problems "")

if(NOT TARGET core::tui_core)
    string(APPEND problems "\n  core::tui_core is not a target, although CORE_CPP_WITH_TUI_CORE is ON")
endif()
if(TARGET core::tui)
    string(APPEND problems "\n  core::tui is a target, although CORE_CPP_WITH_TUI is OFF")
endif()

# The link closure of core::tui_core, walked through every target it names. An alias is resolved to
# the target it stands for, and $<LINK_ONLY:...> -- how a static library carries its PRIVATE links
# into its interface -- to what it wraps, so a private link of a dependency is walked too. Any other
# generator expression, or a plain library name, ends the walk there.
set(pending core::tui_core)
set(seen "")
while(pending)
    list(POP_FRONT pending current)
    while(current MATCHES "^\\$<LINK_ONLY:(.+)>$")
        set(current "${CMAKE_MATCH_1}")
    endwhile()
    if(current IN_LIST seen OR NOT TARGET "${current}")
        continue()
    endif()
    list(APPEND seen "${current}")
    get_target_property(aliased "${current}" ALIASED_TARGET)
    if(aliased)
        list(APPEND pending "${aliased}")
        continue()
    endif()
    get_target_property(links "${current}" INTERFACE_LINK_LIBRARIES)
    if(links)
        list(APPEND pending ${links})
    endif()
endwhile()
foreach(forbidden IN ITEMS core::platform core::async core::net core::tui)
    if(forbidden IN_LIST seen)
        string(APPEND problems "\n  core::tui_core links ${forbidden} (its link closure: ${seen})")
    endif()
endforeach()

if(problems)
    message(FATAL_ERROR
        "consumer-tui-core: a consumer of core::tui_core did not get what it was promised:"
        "${problems}\n"
        "core::tui_core links base, tui_output and libunicode alone (cmake/CoreCppModules.cmake), "
        "so CORE_CPP_WITH_TUI_CORE must build it without the terminal half of the TUI.")
endif()
message(STATUS "consumer-tui-core: core::tui_core is a target; core::tui is not; its link closure holds nothing of platform, async or net: ${seen}")

add_executable(consumer-tui-core main.cpp)
target_link_libraries(consumer-tui-core PRIVATE core::tui_core)
add_test(NAME consumer-tui-core COMMAND consumer-tui-core)
set_tests_properties(consumer-tui-core PROPERTIES TIMEOUT 60)
