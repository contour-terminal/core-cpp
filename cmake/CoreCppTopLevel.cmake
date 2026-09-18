# SPDX-License-Identifier: Apache-2.0
#
# Everything core-cpp does to global state, and the only file that may.
#
# The top-level CMakeLists.txt includes this only when core-cpp is the top-level
# project, after project() and before core_cpp_resolve_dependencies(). As a
# subproject core-cpp changes nothing of its parent's: no launcher, no standard,
# no flags. tests/cmake/check-cmake-hygiene.cmake holds every other file to that.

include_guard(GLOBAL)

# First, so that everything compiled from here on goes through the compiler cache,
# CPM-fetched dependencies included. The module is a verbatim copy; see
# portable/README.md.
include("${CMAKE_CURRENT_LIST_DIR}/portable/CompileCache.cmake")

# The module sets CMAKE_CXX_COMPILER_LAUNCHER as a normal variable, so CMakeCache.txt
# would not otherwise show which launcher this build tree uses. This records it for
# CI and for people; nothing reads it back.
set(CORE_CPP_CXX_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}" CACHE INTERNAL
    "The compiler launcher cmake/portable/CompileCache.cmake selected for this build tree")

# The dependencies core-cpp fetches (Catch2) are compiled with the same standard
# as the code that includes their headers. Catch2 in particular compiles parts of
# itself only from C++17 on, and a test that uses them would fail to link otherwise.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
