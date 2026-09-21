# SPDX-License-Identifier: Apache-2.0
#
# Proves that core_cpp_add_header_self_check() refuses a header that needs a neighbour, and passes
# one that does not (core-cpp#31).
#
#   cmake -DMODULE=<cmake/CoreCppHeaderSelfCheck.cmake> -DWORK_DIR=<dir> \
#         -DGENERATOR=<g> -DMAKE_PROGRAM=<p> -DCXX_COMPILER=<c> -P check-header-self-check-selftest.cmake
#
# Unlike this repository's other self-tests, the thing under test is a COMPILE, so each case
# configures and builds a project of its own rather than scanning text. That is also why the check
# and this test are per-leg rather than `tree-level`: the answer is the toolchain's, so a run that
# proved it under clang would say nothing about MSVC (Ruling R96).
#
# The fixture headers are deliberately trivial. A case that needed a real module would be testing
# the module; what is under test is whether compiling a header alone, with nothing included before
# it, is what the generated translation unit actually does.

cmake_minimum_required(VERSION 3.25)

foreach(required MODULE WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "check-header-self-check-selftest: -D${required}=... is required")
    endif()
endforeach()
if(NOT EXISTS "${MODULE}")
    message(FATAL_ERROR "check-header-self-check-selftest: no module at '${MODULE}'")
endif()

set(failures "")

## @brief Writes the fixture tree for one case under @p dir, publishing @p headers as its file set.
function(core_cpp_selftest_fixture dir headers)
    file(MAKE_DIRECTORY "${dir}/include/fixture")
    file(WRITE "${dir}/include/fixture/Neighbour.hpp"
         "#pragma once\nnamespace fixture { struct Neighbour { int value; }; }\n")
    # Self-contained: it includes what it names.
    file(WRITE "${dir}/include/fixture/Alone.hpp"
         "#pragma once\n#include <fixture/Neighbour.hpp>\nnamespace fixture { struct Alone { Neighbour n; }; }\n")
    # NOT self-contained: it names Neighbour and includes nothing. Compiles happily from a .cpp
    # that included Neighbour.hpp first, which is exactly how this defect survives in a real tree.
    file(WRITE "${dir}/include/fixture/NeedsNeighbour.hpp"
         "#pragma once\nnamespace fixture { struct NeedsNeighbour { Neighbour n; }; }\n")

    set(fileSet "")
    foreach(header IN LISTS headers)
        string(APPEND fileSet "        \"\${CMAKE_CURRENT_SOURCE_DIR}/include/fixture/${header}\"\n")
    endforeach()
    file(WRITE "${dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.25)
project(header_self_check_fixture CXX)
set(CORE_CPP_TESTING ON)
# The real module calls this on every target it creates; the fixture needs no toolchain policy.
function(core_cpp_apply_toolchain target)
endfunction()
include(\"${MODULE}\")
add_library(fixture INTERFACE)
target_sources(fixture INTERFACE
    FILE_SET HEADERS
    BASE_DIRS \"\${CMAKE_CURRENT_SOURCE_DIR}/include\"
    FILES
${fileSet})
target_include_directories(fixture INTERFACE \"\${CMAKE_CURRENT_SOURCE_DIR}/include\")
set_property(GLOBAL APPEND PROPERTY CORE_CPP_HEADER_TARGETS fixture)
core_cpp_add_header_self_check()
")
endfunction()

## @brief Configures and builds @p dir, setting @p rcVar and @p outputVar.
function(core_cpp_selftest_build dir rcVar outputVar)
    set(arguments "")
    if(GENERATOR)
        list(APPEND arguments -G "${GENERATOR}")
    endif()
    if(MAKE_PROGRAM)
        list(APPEND arguments "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
    endif()
    if(CXX_COMPILER)
        list(APPEND arguments "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${dir}" -B "${dir}/build" ${arguments}
        RESULT_VARIABLE configureStatus OUTPUT_VARIABLE configureOut ERROR_VARIABLE configureErr)
    if(NOT configureStatus EQUAL 0)
        set(${rcVar} "configure-failed" PARENT_SCOPE)
        set(${outputVar} "${configureOut}${configureErr}" PARENT_SCOPE)
        return()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${dir}/build"
        RESULT_VARIABLE buildStatus OUTPUT_VARIABLE buildOut ERROR_VARIABLE buildErr)
    set(${rcVar} "${buildStatus}" PARENT_SCOPE)
    set(${outputVar} "${buildOut}${buildErr}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")

# A file set of headers that all stand alone builds. If this fails the fixture is wrong, not the
# check -- and without it a check that refused EVERYTHING would pass the case below.
core_cpp_selftest_fixture("${WORK_DIR}/clean" "Neighbour.hpp;Alone.hpp")
core_cpp_selftest_build("${WORK_DIR}/clean" cleanStatus cleanOutput)
if(NOT cleanStatus EQUAL 0)
    list(APPEND failures "a file set whose headers all stand alone was refused (${cleanStatus}):\n${cleanOutput}")
endif()

# And one that does not stand alone is refused, naming the header.
core_cpp_selftest_fixture("${WORK_DIR}/violation" "Neighbour.hpp;Alone.hpp;NeedsNeighbour.hpp")
core_cpp_selftest_build("${WORK_DIR}/violation" violationStatus violationOutput)
if(violationStatus STREQUAL "configure-failed")
    list(APPEND failures "the violating fixture did not configure:\n${violationOutput}")
elseif(violationStatus EQUAL 0)
    list(APPEND failures
         "NeedsNeighbour.hpp names a type it does not include and was NOT refused -- the check "
         "compiled it with something included first, or generated no translation unit for it")
elseif(NOT violationOutput MATCHES "NeedsNeighbour")
    list(APPEND failures "the violating header was refused, but the output does not name it:\n${violationOutput}")
endif()

if(failures)
    string(REPLACE ";" "\n  " printable "${failures}")
    message(FATAL_ERROR "check-header-self-check-selftest:\n  ${printable}")
endif()
message(STATUS
    "check-header-self-check-selftest: a self-contained file set built, and a header needing a "
    "neighbour was refused by name")
