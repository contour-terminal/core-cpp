# SPDX-License-Identifier: Apache-2.0

# core::async: the coroutine vocabulary, header-only and needing nothing but the standard library.
# Task A5 imports contour's Task, cancellation and combinators; Task B1 adds fastcached's executors.
core_cpp_add_module(async KIND INTERFACE
    HEADERS
        Awaitable.hpp
        Cancellation.hpp
        StopToken.hpp
        Task.hpp
        UniqueCoroHandle.hpp
        WhenAll.hpp
        WhenAny.hpp)

# The tests of everything that names a StopToken.
set(_coreCppAsyncStopTokenTests
    StopToken_test.cpp
    Task_test.cpp
    WhenAll_test.cpp
    WhenAny_test.cpp)

core_cpp_add_test(async SOURCES ${_coreCppAsyncStopTokenTests})

# The same tests over the StopToken fallback, on every platform, whether or not the standard
# library has std::stop_token. A binary of their own: CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK changes
# what StopToken is, so translation units with and without it in one program would give every
# inline function that names a StopToken two definitions.
core_cpp_add_test(async NAME async-fallback
    SOURCES ${_coreCppAsyncStopTokenTests}
    DEFINITIONS CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK)

# Which StopToken this toolchain gets, in the configure log: whether core-cpp.async tests
# std::stop_token or the fallback differs between CI jobs, and nothing else shows it. Each probe
# compiles StopToken.hpp itself and refuses one answer, so exactly one compiles; neither compiling
# means the probe is broken, which is said rather than read as either answer.
if(CORE_CPP_TESTING)
    set(_coreCppStopTokenAnswers "")
    foreach(_coreCppStopTokenProbe IN ITEMS "std::stop_token|!CORE_ASYNC_STOP_TOKEN_IS_STD"
                                            "core-cpp's fallback|CORE_ASYNC_STOP_TOKEN_IS_STD")
        string(REPLACE "|" ";" _coreCppStopTokenProbe "${_coreCppStopTokenProbe}")
        list(GET _coreCppStopTokenProbe 0 _coreCppStopTokenAnswer)
        list(GET _coreCppStopTokenProbe 1 _coreCppStopTokenRefusal)
        try_compile(CORE_CPP_ASYNC_STOP_TOKEN_PROBE
            SOURCE_FROM_CONTENT core-cpp-stop-token-probe.cpp
                "#include <core/async/StopToken.hpp>\n#if ${_coreCppStopTokenRefusal}\n#error refused\n#endif\nint main() { return 0; }\n"
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CORE_CPP_SOURCE_DIR}/src"
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            NO_CACHE)
        if(CORE_CPP_ASYNC_STOP_TOKEN_PROBE)
            list(APPEND _coreCppStopTokenAnswers "${_coreCppStopTokenAnswer}")
        endif()
    endforeach()
    list(LENGTH _coreCppStopTokenAnswers _coreCppStopTokenAnswerCount)
    if(_coreCppStopTokenAnswerCount EQUAL 1)
        message(STATUS "[core-cpp] async: StopToken is ${_coreCppStopTokenAnswers}")
    else()
        message(WARNING "[core-cpp] async: could not tell which StopToken this toolchain gets "
                        "(probes that compiled: '${_coreCppStopTokenAnswers}')")
    endif()
endif()
