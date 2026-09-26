# SPDX-License-Identifier: Apache-2.0
#
# Proves tests/cmake/check-tidy-inputs.cmake: it must be SEEN to refuse each thing it claims, and
# seen to accept a compliant build -- a check that refused everything would pass every refusal.
#
#   compliant         a tidy statement naming the analyser and its .clang-tidy passes, counted
#   noconfig          one without the root .clang-tidy is refused, naming it
#   noanalyser        one without the analyser is refused, naming it
#   nested            a source under a directory with its own .clang-tidy must name that one too
#   untidied          a statement that runs no clang-tidy is not held to any of it
#   none              a build with no tidy statement at all is refused as not a clang-tidy build
#
# Usage: cmake -DCHECKER=<path to check-tidy-inputs.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-tidy-inputs-selftest.cmake
#
# The verdict is `CMake Error` in the output, never the exit code alone.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CHECKER OR NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "check-tidy-inputs-selftest: CHECKER ('${CHECKER}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "check-tidy-inputs-selftest: WORK_DIR is not set.")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
set(root "${WORK_DIR}/tree")
set(analyser "${WORK_DIR}/bin/clang-tidy")
file(WRITE "${root}/.clang-tidy" "Checks: '*'\n")
file(WRITE "${root}/src/sub/.clang-tidy" "InheritParentConfig: true\n")
file(WRITE "${root}/src/A.cpp" "")
file(WRITE "${root}/src/sub/B.cpp" "")
file(WRITE "${analyser}" "")

set(failures "")

## @brief One build statement for @p source, running clang-tidy when @p tidy is ON, with @p deps as
##        its implicit inputs.
function(core_cpp_selftest_statement outVar source tidy deps)
    set(line "build ${source}.o: CXX_COMPILER__x_Debug ${root}/${source}")
    if(deps)
        string(APPEND line " | ${deps}")
    endif()
    string(APPEND line " || order\n  FLAGS = -O0\n")
    if(tidy)
        string(APPEND line "  CODE_CHECK = cmake -E __run_co_compile --tidy=\"${analyser}\" --source=${root}/${source}\n")
    endif()
    set(${outVar} "${line}" PARENT_SCOPE)
endfunction()

## @brief Runs the checker over a build.ninja of @p text; @p want is ACCEPT or a phrase the refusal
##        must contain.
function(core_cpp_selftest_case name text want)
    set(ninja "${WORK_DIR}/${name}/build.ninja")
    file(WRITE "${ninja}" "# a build.ninja\n\n${text}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_NINJA=${ninja}" "-DROOT=${root}" -P "${CHECKER}"
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(said "${out}${err}")
    string(REPLACE "\n" " " said "${said}")
    string(REGEX REPLACE "  +" " " said "${said}")
    if(want STREQUAL "ACCEPT")
        if(said MATCHES "CMake Error")
            list(APPEND failures "${name}: refused what it must accept: ${said}")
        endif()
    elseif(NOT said MATCHES "CMake Error")
        list(APPEND failures "${name}: accepted what it must refuse: ${said}")
    else()
        string(FIND "${said}" "${want}" at)
        if(at EQUAL -1)
            list(APPEND failures "${name}: refused, but not with '${want}': ${said}")
        endif()
    endif()
    set(failures "${failures}" PARENT_SCOPE)
    set(lastSaid "${said}" PARENT_SCOPE)
endfunction()

core_cpp_selftest_statement(a "src/A.cpp" ON "${analyser} ${root}/.clang-tidy")
core_cpp_selftest_statement(b "src/sub/B.cpp" ON "${analyser} ${root}/src/sub/.clang-tidy ${root}/.clang-tidy")
core_cpp_selftest_case(compliant "${a}\n${b}\n" ACCEPT)
if(NOT lastSaid MATCHES "2 analysed compile")
    list(APPEND failures "compliant: accepted, but did not count the two statements: ${lastSaid}")
endif()

core_cpp_selftest_statement(a "src/A.cpp" ON "${analyser}")
core_cpp_selftest_case(noconfig "${a}\n" "does not depend on ${root}/.clang-tidy")

core_cpp_selftest_statement(a "src/A.cpp" ON "${root}/.clang-tidy")
core_cpp_selftest_case(noanalyser "${a}\n" "does not depend on ${analyser}")

core_cpp_selftest_statement(b "src/sub/B.cpp" ON "${analyser} ${root}/.clang-tidy")
core_cpp_selftest_case(nested "${b}\n" "does not depend on ${root}/src/sub/.clang-tidy")

core_cpp_selftest_statement(a "src/A.cpp" ON "${analyser} ${root}/.clang-tidy")
core_cpp_selftest_statement(c "src/sub/B.cpp" OFF "")
core_cpp_selftest_case(untidied "${a}\n${c}\n" ACCEPT)
if(NOT lastSaid MATCHES "1 analysed compile")
    list(APPEND failures "untidied: accepted, but counted a statement that runs no clang-tidy: ${lastSaid}")
endif()

core_cpp_selftest_case(none "${c}\n" "not a clang-tidy build")

if(failures)
    string(REPLACE ";" "\n  " printable "${failures}")
    message(FATAL_ERROR "check-tidy-inputs-selftest:\n  ${printable}")
endif()
message(STATUS "check-tidy-inputs-selftest: the compliant builds passed and all four refusals were seen")
