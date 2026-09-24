# SPDX-License-Identifier: Apache-2.0
#
# core-cpp installs as a CMake package that a consumer finds and links, and a parent that adds it as
# a subproject can export targets of its own that link it (core-cpp#5).
#
#   cmake -DBUILD_DIR=<core-cpp build> -DSOURCE_DIR=<core-cpp> -DWORK_DIR=<dir>
#         -DLIBDIR=<CMAKE_INSTALL_LIBDIR> -DINCLUDEDIR=<CMAKE_INSTALL_INCLUDEDIR>
#         -DEXPECTED_VERSION=<x.y.z> -DINSTALLED=<targets, |-separated> [-DSKIP_REASON=<why>]
#         [-DCONFIG=<config>] [-DGENERATOR=<g>] [-DMAKE_PROGRAM=<p>] [-DCXX_COMPILER=<c>]
#         [-DBUILD_TYPE=<t>] [-DTOOLCHAIN_FILE=<f>] [-DBUILD_PREFIX_PATH=<dirs, |-separated>]
#         [-DINSTRUMENT_COMPILE_FLAGS=<flags, |-separated>] [-DINSTRUMENT_LINK_FLAGS=<flags, |-separated>]
#         [-DSKIP_EXIT_CODE=<code>] -P tests/cmake/check-install.cmake
#
# Three steps, each of which has failed on its own in some project:
#   1. `cmake --install` of the build under test into an empty prefix, component core-cpp alone:
#      the package files and the generated core/Config.hpp must be there.
#   2. tests/consumer-install, configured against that prefix and nothing else, finds the package,
#      refuses the next minor version, builds a program linking core::net, and runs it.
#   3. tests/consumer-install-nested adds core-cpp as a subproject and installs an export of its own that
#      links core-cpp's targets: with CORE_CPP_INSTALL=ON it generates, and with it OFF CMake refuses
#      it as "not in any export set" -- the refusal morph hit, and the proof that the option decides.
#
# Per leg, not `tree-level`: it compiles and links, so its answer is the toolchain's.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SKIP_EXIT_CODE OR SKIP_EXIT_CODE STREQUAL "")
    set(SKIP_EXIT_CODE 77)
endif()
# A build that installs nothing, or cannot install core::net, has nothing for this to find. The
# reason comes from the build (cmake/CoreCppInstall.cmake decided it), so a skip names it.
if(SKIP_REASON)
    message(STATUS "check-install: SKIPPED -- ${SKIP_REASON}")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
    return()
endif()

foreach(required BUILD_DIR SOURCE_DIR WORK_DIR LIBDIR INCLUDEDIR EXPECTED_VERSION)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "check-install: -D${required}=... is required")
    endif()
endforeach()

set(prefix "${WORK_DIR}/prefix")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

## @brief Runs a command, failing the check with its output unless its exit status is 0.
## @param what What the command is doing, for the message.
function(core_cpp_install_step what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "check-install: ${what} failed (exit ${rc}):\n${out}\n${err}")
    endif()
    message(STATUS "check-install: ${what}: ok")
endfunction()

# --- 1. install ---------------------------------------------------------------------------------

set(configArgs "")
if(CONFIG)
    set(configArgs --config "${CONFIG}")
endif()
core_cpp_install_step("installing component core-cpp into ${prefix}"
    "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}" --component core-cpp ${configArgs})

set(packageDir "${prefix}/${LIBDIR}/cmake/core-cpp")
foreach(file IN ITEMS
        "${packageDir}/core-cppConfig.cmake"
        "${packageDir}/core-cppConfigVersion.cmake"
        "${packageDir}/core-cppTargets.cmake"
        "${prefix}/${INCLUDEDIR}/core/Config.hpp"
        "${prefix}/${INCLUDEDIR}/core/net/EventLoop.hpp")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "check-install: the install has no ${file}")
    endif()
endforeach()
# Every core-cpp header an installed header includes is installed too. A public header that
# includes a private one (a detail/ header outside every HEADERS file set) compiles in the source
# tree, where src/ is on the include path, and nowhere else.
file(GLOB_RECURSE installedHeaders LIST_DIRECTORIES false "${prefix}/${INCLUDEDIR}/core/*")
set(unresolved "")
foreach(header IN LISTS installedHeaders)
    file(STRINGS "${header}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*<core/[^>]+>")
    foreach(line IN LISTS includes)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*<(core/[^>]+)>.*$" "\\1" included "${line}")
        if(NOT EXISTS "${prefix}/${INCLUDEDIR}/${included}")
            file(RELATIVE_PATH from "${prefix}/${INCLUDEDIR}" "${header}")
            string(APPEND unresolved "\n  ${from} includes <${included}>, which is not installed")
        endif()
    endforeach()
endforeach()
if(unresolved)
    message(FATAL_ERROR "check-install: an installed header includes one that is not:${unresolved}\n"
                        "Add the included header to its module's HEADERS file set.")
endif()

# What the build said it installed is what the package defines: a target left out of the export
# would otherwise surface only as a consumer's "target not found".
file(READ "${packageDir}/core-cppTargets.cmake" targetsFile)
string(REPLACE "|" ";" installed "${INSTALLED}")
foreach(target IN LISTS installed)
    string(REGEX REPLACE "^core-cpp-" "core::" exported "${target}")
    string(FIND "${targetsFile}" "add_library(${exported} " at)
    if(at EQUAL -1)
        message(FATAL_ERROR "check-install: the build installed ${target}, but core-cppTargets.cmake defines no ${exported}")
    endif()
endforeach()

# --- 2. a consumer of the installed package -----------------------------------------------------

set(expectTestingMain OFF)
if("core-cpp-testing_main" IN_LIST installed)
    set(expectTestingMain ON)
endif()

set(toolchainArgs "")
if(GENERATOR)
    list(APPEND toolchainArgs -G "${GENERATOR}")
endif()
if(MAKE_PROGRAM)
    list(APPEND toolchainArgs "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(CXX_COMPILER)
    list(APPEND toolchainArgs "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(BUILD_TYPE)
    list(APPEND toolchainArgs "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
endif()
# The prefix first, then wherever the build under test found its own dependencies, so the package's
# find_dependency() calls find what core-cpp was built against. Passed '|'-separated.
string(REPLACE "|" ";" buildPrefixPath "${BUILD_PREFIX_PATH}")
# A list, so it is passed as ONE quoted argument below: expanded unquoted, its ';' would split it.
set(searchPath "${prefix}" ${buildPrefixPath})
set(searchArgs "")
if(TOOLCHAIN_FILE)
    list(APPEND toolchainArgs "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
    # A cross toolchain (Emscripten's) re-roots every package search under its sysroot, so the
    # prefix is named as the package's directory, which is used as given.
    list(APPEND searchArgs "-Dcore-cpp_DIR=${packageDir}")
endif()

set(consumer "${WORK_DIR}/consumer")
core_cpp_install_step("configuring tests/consumer-install against the prefix"
    "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/consumer-install" -B "${consumer}" ${toolchainArgs}
    "-DCMAKE_PREFIX_PATH=${searchPath}" ${searchArgs}
    "-DEXPECTED_VERSION=${EXPECTED_VERSION}" "-DINSTRUMENT_COMPILE_FLAGS=${INSTRUMENT_COMPILE_FLAGS}"
    "-DINSTRUMENT_LINK_FLAGS=${INSTRUMENT_LINK_FLAGS}" "-DEXPECT_TESTING_MAIN=${expectTestingMain}")
core_cpp_install_step("building tests/consumer-install" "${CMAKE_COMMAND}" --build "${consumer}")
core_cpp_install_step("running tests/consumer-install"
    "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer}" --output-on-failure)

# --- 3. a parent that exports a target linking core-cpp's -----------------------------------------

foreach(nestedInstall IN ITEMS ON OFF)
    set(nested "${WORK_DIR}/nested-${nestedInstall}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/consumer-install-nested" -B "${nested}"
                ${toolchainArgs} "-DCORE_CPP_SOURCE_DIR=${SOURCE_DIR}" "-DNESTED_INSTALL=${nestedInstall}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(nestedInstall AND NOT rc EQUAL 0)
        message(FATAL_ERROR
            "check-install: a parent with CORE_CPP_INSTALL=ON could not export a target linking core-cpp's "
            "(exit ${rc}):\n${out}\n${err}")
    endif()
    if(NOT nestedInstall)
        # Refused, and refused for the reason under test: a configure that failed on anything else
        # would pass this half without having asked the question.
        if(rc EQUAL 0 OR NOT err MATCHES "requires[ \t\r\n]+target \"core-cpp-[a-z_]+\" that is not in any[ \t\r\n]+export set")
            message(FATAL_ERROR
                "check-install: with CORE_CPP_INSTALL=OFF the parent's export should be refused as "
                "\"not in any export set\", and was not (exit ${rc}):\n${out}\n${err}")
        endif()
    endif()
    message(STATUS "check-install: a parent's export with CORE_CPP_INSTALL=${nestedInstall}: as expected")
endforeach()

message(STATUS "check-install: passed")
