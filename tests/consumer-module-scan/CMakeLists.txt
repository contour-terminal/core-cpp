# SPDX-License-Identifier: Apache-2.0
#
# A parent that adds core-cpp as a subproject and never asks for C++ modules: Isoline's shape.
# tests/cmake/check-module-scan.cmake configures it with a clang-scan-deps that does not exist, which
# core-cpp's configure must survive, and once more with CMAKE_CXX_SCAN_FOR_MODULES=ON, which it must
# not: the parent asked, so core-cpp's probes scan and the absent scanner fails them.
#
#   cmake -S tests/consumer-module-scan -B <dir> -DCORE_CPP_SOURCE_DIR=<core-cpp> [-DNESTED_WITH_SYSTEMD=ON|OFF]
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-module-scan LANGUAGES CXX)

if(NOT EXISTS "${CORE_CPP_SOURCE_DIR}/cmake/CoreCppModules.cmake")
    message(FATAL_ERROR
        "consumer-module-scan: CORE_CPP_SOURCE_DIR ('${CORE_CPP_SOURCE_DIR}') is not a core-cpp source tree.")
endif()

# A parent's own standard, as Isoline's is: below C++20 no probe scans, and the case would ask nothing.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Nothing fetched: a check that runs in a ctest must not need the network. Options are normal
# variables, which core-cpp's option() calls honour (CMP0077).
set(CORE_CPP_TESTING OFF)
set(CORE_CPP_WITH_TUI OFF)
set(CORE_CPP_WITH_TUI_OUTPUT OFF)
set(CORE_CPP_FETCH_DEPS OFF)
# What the build under test chose: its default needs libsystemd, which a machine may lack.
if(DEFINED NESTED_WITH_SYSTEMD)
    set(CORE_CPP_WITH_SYSTEMD ${NESTED_WITH_SYSTEMD})
endif()
add_subdirectory("${CORE_CPP_SOURCE_DIR}" core-cpp EXCLUDE_FROM_ALL SYSTEM)
