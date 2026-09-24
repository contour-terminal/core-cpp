# SPDX-License-Identifier: Apache-2.0
#
# A parent that adds core-cpp as a subproject and exports a target of its own that links core-cpp's:
# morph's shape. CMake refuses to install an export set whose targets link one that is in no export
# set, so the parent's install(EXPORT) generates only if core-cpp's targets are in core-cpp's, which
# is what CORE_CPP_INSTALL=ON asks for when core-cpp is not the top-level project.
#
#   cmake -S tests/consumer-install-nested -B <dir> -DCORE_CPP_SOURCE_DIR=<core-cpp> -DNESTED_INSTALL=ON|OFF
#
# ON must generate; OFF must fail with CMake's "not in any export set", which is what proves the
# option is what made the difference. tests/cmake/check-install.cmake runs both.
cmake_minimum_required(VERSION 3.25...3.31)
project(core-cpp-consumer-install-nested VERSION 1.0.0 LANGUAGES CXX)

if(NOT EXISTS "${CORE_CPP_SOURCE_DIR}/cmake/CoreCppModules.cmake")
    message(FATAL_ERROR
        "consumer-install-nested: CORE_CPP_SOURCE_DIR ('${CORE_CPP_SOURCE_DIR}') is not a core-cpp source tree.")
endif()
if(NOT DEFINED NESTED_INSTALL)
    message(FATAL_ERROR "consumer-install-nested: -DNESTED_INSTALL=ON|OFF is required")
endif()

# What CPMAddPackage(SOURCE_DIR ... OPTIONS ...) does, without its download of CPM itself: a check
# that ran in a ctest must not need the network. Options are normal variables, which core-cpp's
# option() calls honour (CMP0077).
set(CORE_CPP_INSTALL ${NESTED_INSTALL})
set(CORE_CPP_WITH_TUI OFF)
set(CORE_CPP_WITH_TUI_OUTPUT OFF)
set(CORE_CPP_FETCH_DEPS OFF)
add_subdirectory("${CORE_CPP_SOURCE_DIR}" core-cpp EXCLUDE_FROM_ALL SYSTEM)

include(GNUInstallDirs)
add_library(parent INTERFACE)
target_link_libraries(parent INTERFACE core::base core::async core::platform core::net)
install(TARGETS parent EXPORT parentTargets)
install(EXPORT parentTargets NAMESPACE parent:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/parent")
