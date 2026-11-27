# SPDX-License-Identifier: Apache-2.0
#
# core-cpp configures on a Clang that has no clang-scan-deps, as the top-level build and as a parent's
# subproject, because nothing in its scope scans for C++ module imports unless asked (the CMP0155 line
# of the root CMakeLists.txt, and CMAKE_CXX_SCAN_FOR_MODULES in cmake/CoreCppTopLevel.cmake). Without
# them every try_compile() of C++20 or later scans, FindThreads's among them, and the absent scanner
# fails the configure with exit 127.
#
# The scanner is made absent by naming one that does not exist: CMake's own lookup then never runs,
# which holds wherever this machine's Clang keeps its tools, and it is the state a parent is in whose
# Clang ships without the scanner.
#
# Usage: cmake -DSOURCE_DIR=<core-cpp> -DWORK_DIR=<dir> -DCXX_COMPILER_ID=<id> [-DCXX_COMPILER=<path>]
#        [-DCXX_FRONTEND=<GNU|MSVC>] [-DTARGET_SYSTEM=<name>] [-DGENERATOR=<g>] [-DMAKE_PROGRAM=<path>] [-DTOOLCHAIN_FILE=<path>] [-DWITH_SYSTEMD=ON|OFF]
#        [-DSKIP_EXIT_CODE=<code>] -P tests/cmake/check-module-scan.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SKIP_EXIT_CODE OR SKIP_EXIT_CODE STREQUAL "")
    set(SKIP_EXIT_CODE 77)
endif()
foreach(required SOURCE_DIR WORK_DIR CXX_COMPILER_ID)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "check-module-scan: -D${required}=... is required")
    endif()
endforeach()

# GCC and MSVC scan with the compiler itself, so there is no separate tool to be missing; AppleClang
# has no module scanning in CMake at all.
if(NOT CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "check-module-scan: SKIPPED -- the compiler is ${CXX_COMPILER_ID}, whose scanning "
                   "needs no clang-scan-deps")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
    return()
endif()

# clang-cl is Clang behind the MSVC front end, and there the refused row configures too: a
# subproject that asks for modules is not refused for the absent scanner, so the passing rows could
# not show that it was consulted. Windows CI measured it; the check judges GNU-front-end Clang only.
# Emscripten's and WASI's Clang configure the refused row too, for the same want of proof.
if(TARGET_SYSTEM MATCHES "^(Emscripten|WASI)$")
    message(STATUS "check-module-scan: SKIPPED -- ${TARGET_SYSTEM}'s Clang configures the refused row too, "
                   "so nothing here could prove the scanner was asked")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
    return()
endif()
if(CXX_FRONTEND STREQUAL "MSVC")
    message(STATUS "check-module-scan: SKIPPED -- clang-cl configures the refused row too, so nothing "
                   "here could prove the scanner was asked")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
        cmake_language(EXIT ${SKIP_EXIT_CODE})
    endif()
    return()
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(scanner "${WORK_DIR}/no-scanner/clang-scan-deps")
set(configureArgs "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${scanner}")
if(GENERATOR)
    list(APPEND configureArgs -G "${GENERATOR}")
endif()
if(MAKE_PROGRAM)
    list(APPEND configureArgs "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(CXX_COMPILER)
    list(APPEND configureArgs "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(TOOLCHAIN_FILE)
    list(APPEND configureArgs "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
# Each configure is afresh, so an option whose default needs a package this machine lacks
# (CORE_CPP_WITH_SYSTEMD) is forwarded as the build under test set it.
set(nestedArgs "")
set(topLevelArgs "")
if(NOT "${WITH_SYSTEMD}" STREQUAL "")
    list(APPEND nestedArgs "-DNESTED_WITH_SYSTEMD=${WITH_SYSTEMD}")
    list(APPEND topLevelArgs "-DCORE_CPP_WITH_SYSTEMD=${WITH_SYSTEMD}")
endif()

# One row per configure: its name, the expected outcome, the source tree, and its own arguments
# ('|'-separated). The refused row is what proves the other two asked the question: with the
# scanner present, or not consulted, it would configure too.
set(cases
    "subproject|pass|${SOURCE_DIR}/tests/consumer-module-scan|-DCORE_CPP_SOURCE_DIR=${SOURCE_DIR}|${nestedArgs}"
    "top-level|pass|${SOURCE_DIR}|-DCORE_CPP_TESTING=OFF|-DCORE_CPP_WITH_TUI=OFF|-DCORE_CPP_WITH_TUI_OUTPUT=OFF|-DCORE_CPP_FETCH_DEPS=OFF|${topLevelArgs}")
# Before CMake 3.28, CMAKE_CXX_SCAN_FOR_MODULES scans nothing (module support was experimental), so a
# subproject asking for modules configures without the scanner and the refused row cannot fail there.
# The two passing rows still prove no probe needs the scanner; only the proof that it was asked is lost.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    list(APPEND cases
         "subproject asking for modules|fail|${SOURCE_DIR}/tests/consumer-module-scan|-DCORE_CPP_SOURCE_DIR=${SOURCE_DIR}|-DCMAKE_CXX_SCAN_FOR_MODULES=ON|${nestedArgs}")
else()
    message(STATUS "check-module-scan: the refused row is skipped -- CMake ${CMAKE_VERSION} scans for "
                   "modules from 3.28 on only")
endif()

set(index 0)
foreach(case IN LISTS cases)
    string(REPLACE "|" ";" fields "${case}")
    list(POP_FRONT fields name expected source)
    math(EXPR index "${index} + 1")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${WORK_DIR}/case-${index}" ${configureArgs} ${fields}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(expected STREQUAL "pass" AND NOT rc EQUAL 0)
        message(FATAL_ERROR
            "check-module-scan: the ${name} configure failed without clang-scan-deps (exit ${rc}):\n${out}\n${err}")
    endif()
    if(expected STREQUAL "fail" AND rc EQUAL 0)
        message(FATAL_ERROR
            "check-module-scan: the ${name} configure succeeded without clang-scan-deps, so the absent "
            "scanner was never consulted and the passing cases prove nothing:\n${out}")
    endif()
    # What tells the outcomes apart is whether a probe ran the scanner, which the configure log
    # records (CMake 3.26 on): a passing case never names it, and the refused one failed on it rather
    # than on anything else.
    set(configureLog "${WORK_DIR}/case-${index}/CMakeFiles/CMakeConfigureLog.yaml")
    if(EXISTS "${configureLog}")
        file(READ "${configureLog}" log)
        string(FIND "${log}" "\"${scanner}\" -format=p1689" scanned)
        if(expected STREQUAL "pass" AND NOT scanned EQUAL -1)
            message(FATAL_ERROR "check-module-scan: a probe of the ${name} configure ran the scanner (${configureLog})")
        endif()
        if(expected STREQUAL "fail" AND scanned EQUAL -1)
            message(FATAL_ERROR
                "check-module-scan: the ${name} configure failed, but no probe ran the scanner, so it failed "
                "for another reason (exit ${rc}):\n${out}\n${err}")
        endif()
    endif()
    message(STATUS "check-module-scan: ${name}: as expected (exit ${rc})")
endforeach()
