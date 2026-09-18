# SPDX-License-Identifier: Apache-2.0

# core::base: the headers directly in src/core/, namespace core. It owns core/Config.hpp, which
# CMake generates when it configures core-cpp.
set(_coreCppBaseLibs "")
if(CORE_CPP_USE_THREADS)
    list(APPEND _coreCppBaseLibs Threads::Threads)
endif()

core_cpp_add_module(base KIND STATIC
    HEADERS
        Assert.hpp
        Base64.hpp
        Deferred.hpp
        Defines.hpp
        Environment.hpp
        Escape.hpp
        Flags.hpp
        FNV.hpp
        Overloaded.hpp
        Profiling.hpp
        Ranges.hpp
        Times.hpp
        UserInfo.hpp
        Utils.hpp
        "${CORE_CPP_GENERATED_INCLUDE_DIR}/core/Config.hpp"
    SOURCES
        Environment.cpp
        UserInfo.cpp
        Utils.cpp
    PUBLIC_LIBS ${_coreCppBaseLibs})

core_cpp_add_test(base SOURCES
    Base64_test.cpp
    Environment_test.cpp
    Profiling_test.cpp
    Ranges_test.cpp
    Times_test.cpp
    Utils_test.cpp)
