# SPDX-License-Identifier: Apache-2.0
#
# Refuses a clang-tidy build whose compile statements do not depend on what clang-tidy reads
# (core-cpp#36). Ninja reruns a statement when one of its inputs or its command line changes, and
# neither the `.clang-tidy` files nor the analyser binary is either unless the build says so: edit a
# rule, or replace the analyser at the same path, and every current object keeps the verdict it was
# built under. cmake/CoreCppToolchain.cmake adds them as OBJECT_DEPENDS; this holds the generated
# build to it.
#
# For every build statement that runs clang-tidy (`--tidy=` in its CODE_CHECK), the statement's
# inputs must name the analyser the statement runs and every `.clang-tidy` from the source's
# directory up to ROOT. A build with no such statement at all is refused too: it is not a
# clang-tidy build, and a check over none of them is no check.
#
# Usage: cmake -DBUILD_NINJA=<build.ninja> -DROOT=<source tree> -P tests/cmake/check-tidy-inputs.cmake
#
# The verdict is `CMake Error` in the output, never the exit code alone.

cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS BUILD_NINJA ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "check-tidy-inputs: ${required} is not set.")
    endif()
endforeach()
if(NOT EXISTS "${BUILD_NINJA}")
    message(FATAL_ERROR "check-tidy-inputs: ${BUILD_NINJA} does not exist; this check reads a Ninja build.")
endif()
get_filename_component(ROOT "${ROOT}" ABSOLUTE)

# CMake's list syntax gives ';', '[' and ']' a meaning, and a statement's launcher list is full of
# ';'. Each is replaced by a control character while the file is split into statements, which CMake's
# Ninja generator separates with a blank line.
string(ASCII 1 semicolonCode)
string(ASCII 2 openBracketCode)
string(ASCII 3 closeBracketCode)
string(ASCII 5 escapedSpaceCode)
file(READ "${BUILD_NINJA}" text)
string(REPLACE ";" "${semicolonCode}" text "${text}")
string(REPLACE "[" "${openBracketCode}" text "${text}")
string(REPLACE "]" "${closeBracketCode}" text "${text}")
string(REPLACE "\n\n" ";" statements "${text}")

set(refusals "")
set(refusalCount 0)
set(checkedCount 0)
foreach(statement IN LISTS statements)
    if(NOT statement MATCHES "--tidy=\"([^\"${semicolonCode}]+)")
        continue()
    endif()
    set(analyser "${CMAKE_MATCH_1}")
    if(NOT statement MATCHES "(^|\n)build ([^\n]*)")
        continue()
    endif()
    set(buildLine "${CMAKE_MATCH_2}")
    # "build <output>: <rule> <source> | <implicit inputs> || <order-only>". Ninja escapes ':' and
    # ' ' in a path with '$'; the paths compared here are taken back out of that.
    string(REPLACE "$:" ":" buildLine "${buildLine}")
    string(REPLACE "$ " "${escapedSpaceCode}" buildLine "${buildLine}")
    if(NOT buildLine MATCHES "^[^ ]+: [^ ]+ ([^ |]+)")
        continue()
    endif()
    set(source "${CMAKE_MATCH_1}")
    string(REPLACE "${escapedSpaceCode}" " " source "${source}")
    set(implicit "")
    if(buildLine MATCHES " \\| ([^|]*)")
        string(STRIP "${CMAKE_MATCH_1}" implicit)
        string(REPLACE " " ";" implicit "${implicit}")
        list(TRANSFORM implicit REPLACE "${escapedSpaceCode}" " ")
    endif()
    math(EXPR checkedCount "${checkedCount} + 1")

    set(wanted "${analyser}")
    cmake_path(IS_PREFIX ROOT "${source}" NORMALIZE underRoot)
    if(underRoot)
        cmake_path(GET source PARENT_PATH directory)
        while(TRUE)
            if(EXISTS "${directory}/.clang-tidy")
                list(APPEND wanted "${directory}/.clang-tidy")
            endif()
            cmake_path(COMPARE "${directory}" EQUAL "${ROOT}" atRoot)
            cmake_path(GET directory PARENT_PATH parent)
            if(atRoot OR parent STREQUAL directory)
                break()
            endif()
            set(directory "${parent}")
        endwhile()
    endif()
    foreach(input IN LISTS wanted)
        if(NOT input IN_LIST implicit)
            string(APPEND refusals "\n  ${source}: its compile runs clang-tidy and does not depend on ${input}")
            math(EXPR refusalCount "${refusalCount} + 1")
        endif()
    endforeach()
endforeach()

if(checkedCount EQUAL 0)
    message(FATAL_ERROR
        "check-tidy-inputs: no build statement in ${BUILD_NINJA} runs clang-tidy, so this is not a "
        "clang-tidy build and there was nothing to check.")
endif()
if(refusalCount GREATER 0)
    message(FATAL_ERROR
        "check-tidy-inputs: ${refusalCount} missing input(s) over ${checkedCount} analysed compile(s). "
        "Editing one of these files, or replacing the analyser, would re-analyse nothing whose object "
        "is current (core-cpp#36):${refusals}")
endif()
message(STATUS "check-tidy-inputs: ${checkedCount} analysed compile(s), each depending on the analyser and its .clang-tidy files")
