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
    Awaitable_test.cpp
    StopToken_test.cpp
    Task_test.cpp
    WhenAll_test.cpp
    WhenAny_test.cpp)

# Whether this build's GCC makes the symmetric transfer of a deep co_await chain a tail call. It
# does so only when it optimises sibling calls, from -O2 up, and it defines no macro for the level:
# __OPTIMIZE__ is 1 at -Og and -O1 too, where the 100000-frame chain of Task_test.cpp's deep-chain
# case overflows the stack and takes every other case in the binary with it. So the build says, and
# the case skips wherever the answer is not yes -- including every build outside core-cpp's presets
# (core-cpp#15). Clang and MSVC make the transfer a tail call at every level and are not asked.
set(_coreCppAsyncDefinitions "")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_coreCppAsyncOptimisation "${CMAKE_CXX_FLAGS}")
    if(CMAKE_BUILD_TYPE)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _coreCppAsyncConfiguration)
        string(APPEND _coreCppAsyncOptimisation " ${CMAKE_CXX_FLAGS_${_coreCppAsyncConfiguration}}")
    endif()
    # The last -O on the command line is the one that counts, as it is for the compiler; GCC's
    # default, where there is none at all, is -O0.
    set(_coreCppAsyncLevel "-O0")
    string(REGEX MATCHALL "(^| )-O[0-9A-Za-z]*" _coreCppAsyncLevels "${_coreCppAsyncOptimisation}")
    if(_coreCppAsyncLevels)
        list(POP_BACK _coreCppAsyncLevels _coreCppAsyncLevel)
        string(STRIP "${_coreCppAsyncLevel}" _coreCppAsyncLevel)
    endif()
    set(_coreCppAsyncTailCall "no, so the deep-chain case is skipped")
    if(_coreCppAsyncLevel MATCHES "^-O([23]|fast)$")
        list(APPEND _coreCppAsyncDefinitions CORE_ASYNC_SYMMETRIC_TRANSFER_IS_TAIL_CALL=1)
        set(_coreCppAsyncTailCall "yes")
    endif()
    message(STATUS "[core-cpp] async: GCC at ${_coreCppAsyncLevel}, symmetric transfer tail-calls: "
                   "${_coreCppAsyncTailCall}")
endif()

core_cpp_add_test(async
    SOURCES ${_coreCppAsyncStopTokenTests}
    DEFINITIONS ${_coreCppAsyncDefinitions})

# The same tests over the StopToken fallback, on every platform, whether or not the standard
# library has std::stop_token. A binary of their own: CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK changes
# what StopToken is, so translation units with and without it in one program would give every
# inline function that names a StopToken two definitions.
core_cpp_add_test(async NAME async-fallback
    SOURCES ${_coreCppAsyncStopTokenTests}
    DEFINITIONS CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK ${_coreCppAsyncDefinitions})

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
