# SPDX-License-Identifier: Apache-2.0

# core::coro: the coroutine vocabulary, header-only and needing nothing but the standard library.
# Task A5 imports contour's Task, cancellation and combinators; Task B1 adds fastcached's executors.
core_cpp_add_module(coro KIND INTERFACE
    HEADERS
        Generator.hpp
        StopToken.hpp)

# The tests of everything that names a StopToken.
set(_coreCppCoroStopTokenTests
    StopToken_test.cpp)

core_cpp_add_test(coro SOURCES Generator_test.cpp ${_coreCppCoroStopTokenTests})

# The same tests over the StopToken fallback, on every platform, whether or not the standard
# library has std::stop_token. A binary of their own: CORE_CORO_FORCE_STOP_TOKEN_FALLBACK changes
# what StopToken is, so translation units with and without it in one program would give every
# inline function that names a StopToken two definitions.
core_cpp_add_test(coro NAME coro-fallback
    SOURCES ${_coreCppCoroStopTokenTests}
    DEFINITIONS CORE_CORO_FORCE_STOP_TOKEN_FALLBACK)
