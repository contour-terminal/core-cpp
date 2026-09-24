# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.25...3.31)

# The version literal is the source of truth; a release tag must equal it.
project(core-cpp
    VERSION 0.2.0
    DESCRIPTION "Shared C++23 foundation libraries of the Contour Terminal projects"
    HOMEPAGE_URL "https://github.com/contour-terminal/core-cpp"
    LANGUAGES CXX)

set(CORE_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CORE_CPP_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(CORE_CPP_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

include("${CORE_CPP_SOURCE_DIR}/cmake/CoreCppOptions.cmake")

# Global state (the compiler cache first of all) only when core-cpp is the build
# itself, and before any dependency is resolved, so fetched dependencies are
# compiled through the cache too.
if(PROJECT_IS_TOP_LEVEL)
    include("${CORE_CPP_SOURCE_DIR}/cmake/CoreCppTopLevel.cmake")
endif()

include("${CORE_CPP_SOURCE_DIR}/cmake/CoreCppToolchain.cmake")
include("${CORE_CPP_SOURCE_DIR}/cmake/CoreCppTargets.cmake")
include("${CORE_CPP_SOURCE_DIR}/cmake/CoreCppDependencies.cmake")
include("${CORE_CPP_SOURCE_DIR}/cmake/CoreCppModules.cmake")
include("${CORE_CPP_SOURCE_DIR}/cmake/CoreCppHeaderSelfCheck.cmake")

configure_file("${CORE_CPP_SOURCE_DIR}/src/core/Config.hpp.in"
               "${CORE_CPP_GENERATED_INCLUDE_DIR}/core/Config.hpp" @ONLY)

core_cpp_resolve_dependencies()

if(CORE_CPP_TESTING)
    enable_testing()
endif()

core_cpp_add_modules()

# After every module, because it reads their FILE_SET HEADERS; before tests/, so a header that
# cannot stand alone fails the build rather than a test binary that includes it sideways.
core_cpp_add_header_self_check()

if(CORE_CPP_TESTING)
    add_subdirectory(tests)
endif()
