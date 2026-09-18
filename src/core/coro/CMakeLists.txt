# SPDX-License-Identifier: Apache-2.0

# core::coro: the coroutine vocabulary, header-only and needing nothing but the standard library.
# For now it holds only Generator; Task A5 imports contour's Task, cancellation and combinators.
core_cpp_add_module(coro KIND INTERFACE
    HEADERS Generator.hpp)

core_cpp_add_test(coro SOURCES Generator_test.cpp)
