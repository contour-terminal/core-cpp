# SPDX-License-Identifier: Apache-2.0
#
# A consumer's build, with core-cpp added to it through CPM the way endo, fastcached, tuidu,
# Lightweight's dbtool and morph add it. It is a project of its own, configured against a core-cpp
# source tree, and its job is to catch core-cpp changing a build that is not its own:
#
#   cmake -S tests/consumer-cpm -B out/consumer-cpm -DCORE_CPP_SOURCE_DIR=<core-cpp>
#   cmake --build out/consumer-cpm
#   ctest --test-dir out/consumer-cpm --output-on-failure
#
# Everything it asserts is a promise core-cpp makes in docs/getting-started/cpm.md: it adds no
# directory-wide flag, no compiler launcher, no include directory and no PUBLIC or INTERFACE flag
# of its own to the parent; it builds no test of its own unless asked; and it publishes its
# compiled targets in CORE_CPP_TARGETS so a parent can instrument them with its own code.
#
# A failure here is the point of this project, so each message says which value changed and to what.
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-cpm VERSION 1.0.0 LANGUAGES CXX)

if(NOT DEFINED CORE_CPP_SOURCE_DIR)
    get_filename_component(CORE_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
endif()
if(NOT EXISTS "${CORE_CPP_SOURCE_DIR}/cmake/CoreCppModules.cmake")
    message(FATAL_ERROR
        "consumer-cpm: CORE_CPP_SOURCE_DIR ('${CORE_CPP_SOURCE_DIR}') is not a core-cpp source tree.")
endif()

enable_testing()

# --- the consumer's own build, before core-cpp is anywhere near it --------------
#
# A warning set at directory scope, as every one of the consumers has: core-cpp's headers must not
# trip it (SYSTEM YES is what keeps them out of it), and core-cpp must not change it.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()
# An include directory of this project's own, so the comparison below has one to notice a change
# in: core-cpp's headers reach a consumer through its targets, never through the directory.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

# What this project's build consists of, recorded before core-cpp is added and compared after.
# These are the values a subproject can reach: a cache entry written with FORCE, a global property,
# a PARENT_SCOPE assignment. A directory property is read with get_directory_property(), a variable
# directly.
set(consumerDirectoryProperties COMPILE_OPTIONS LINK_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES)
set(consumerVariables
    CMAKE_CXX_FLAGS
    CMAKE_CXX_FLAGS_DEBUG
    CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO
    CMAKE_CXX_COMPILER_LAUNCHER
    CMAKE_CXX_STANDARD
    CMAKE_CXX_EXTENSIONS
    CMAKE_EXE_LINKER_FLAGS
    CMAKE_POSITION_INDEPENDENT_CODE
    CMAKE_INTERPROCEDURAL_OPTIMIZATION)

foreach(property IN LISTS consumerDirectoryProperties)
    get_directory_property(consumerBefore_${property} ${property})
endforeach()
foreach(variable IN LISTS consumerVariables)
    set(consumerBefore_${variable} "${${variable}}")
endforeach()

# --- core-cpp, as a consumer adds it -------------------------------------------

include("${CORE_CPP_SOURCE_DIR}/cmake/CPM.cmake")
CPMAddPackage(
    NAME core-cpp
    SOURCE_DIR "${CORE_CPP_SOURCE_DIR}"
    SYSTEM YES
    EXCLUDE_FROM_ALL YES
    OPTIONS "CORE_CPP_WITH_TUI ON" "CORE_CPP_WITH_TLS OFF")

# --- what core-cpp may not have changed ----------------------------------------

set(changes "")
foreach(property IN LISTS consumerDirectoryProperties)
    get_directory_property(after ${property})
    if(NOT "${after}" STREQUAL "${consumerBefore_${property}}")
        string(APPEND changes
            "\n  the directory property ${property} was '${consumerBefore_${property}}' and is now '${after}'")
    endif()
endforeach()
foreach(variable IN LISTS consumerVariables)
    if(NOT "${${variable}}" STREQUAL "${consumerBefore_${variable}}")
        string(APPEND changes
            "\n  ${variable} was '${consumerBefore_${variable}}' and is now '${${variable}}'")
    endif()
endforeach()
if(changes)
    message(FATAL_ERROR
        "consumer-cpm: adding core-cpp changed this project's build:${changes}\n"
        "core-cpp sets global state only when it is the top-level project "
        "(cmake/CoreCppTopLevel.cmake), and every flag it compiles with is PRIVATE to its own "
        "targets (cmake/CoreCppToolchain.cmake).")
endif()

# --- what core-cpp built --------------------------------------------------------

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

set(coreCppTargetsInTree "")
consumer_collect_targets("${CORE_CPP_SOURCE_DIR}" coreCppTargetsInTree)
# Only core-cpp's own: a dependency core-cpp fetched (libunicode, stb) is added under its
# directory and is not core-cpp's to publish.
list(FILTER coreCppTargetsInTree INCLUDE REGEX "^core-cpp-")

set(problems "")

# CORE_CPP_TARGETS is what a parent reads to apply its sanitizers or coverage per target. An empty
# list, or one naming something that is not a compiled library, silently leaves core-cpp's code
# uninstrumented inside an instrumented build -- which is the TSan false report the property exists
# to prevent.
get_property(published GLOBAL PROPERTY CORE_CPP_TARGETS)
if(NOT published)
    string(APPEND problems "\n  the global property CORE_CPP_TARGETS is empty")
endif()
foreach(target IN LISTS published)
    if(NOT TARGET ${target})
        string(APPEND problems "\n  CORE_CPP_TARGETS names '${target}', which is not a target")
        continue()
    endif()
    get_target_property(type ${target} TYPE)
    if(NOT type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY|SHARED_LIBRARY)$")
        string(APPEND problems
            "\n  CORE_CPP_TARGETS names '${target}', whose type is ${type}; only a compiled library "
            "can carry a parent's instrumentation")
    endif()
    if(target MATCHES "-test$")
        string(APPEND problems "\n  CORE_CPP_TARGETS names the test binary '${target}'")
    endif()
    # The no-PUBLIC-flags rule, read off the targets rather than off the CMake sources: a usage
    # requirement here would reach this project's own compiles through target_link_libraries().
    foreach(property IN ITEMS INTERFACE_COMPILE_OPTIONS INTERFACE_COMPILE_DEFINITIONS INTERFACE_LINK_OPTIONS)
        get_target_property(value ${target} ${property})
        if(value)
            string(APPEND problems "\n  ${target} has ${property}='${value}', which its consumers would compile with")
        endif()
    endforeach()
endforeach()

# The other direction: a compiled library core-cpp built but did not publish would be the one left
# uninstrumented.
foreach(target IN LISTS coreCppTargetsInTree)
    get_target_property(type ${target} TYPE)
    if(type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY|SHARED_LIBRARY)$" AND NOT target IN_LIST published)
        string(APPEND problems "\n  ${target} is a compiled library that CORE_CPP_TARGETS does not name")
    endif()
endforeach()

# CORE_CPP_TESTING defaults to off for a subproject, so no test binary is built and Catch2 is
# never fetched.
foreach(target IN LISTS coreCppTargetsInTree)
    if(target MATCHES "-test$")
        string(APPEND problems "\n  ${target} exists although CORE_CPP_TESTING is off")
    endif()
endforeach()

if(problems)
    list(JOIN published ", " publishedList)
    message(FATAL_ERROR
        "consumer-cpm: core-cpp's targets are not what a consumer is promised:${problems}\n"
        "CORE_CPP_TARGETS holds: ${publishedList}")
endif()
list(LENGTH published publishedCount)
message(STATUS "consumer-cpm: core-cpp published ${publishedCount} compiled target(s): ${published}")

# --- and it all links and runs --------------------------------------------------

add_executable(consumer-cpm main.cpp)
target_link_libraries(consumer-cpm PRIVATE core::async core::net core::log core::tui)
add_test(NAME consumer-cpm COMMAND consumer-cpm)
# A loopback echo that never completes must fail rather than hang the suite.
set_tests_properties(consumer-cpm PROPERTIES TIMEOUT 120)
