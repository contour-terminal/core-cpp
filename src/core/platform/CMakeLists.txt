# SPDX-License-Identifier: Apache-2.0

# core::platform: the operating-system layer, each part behind an interface with a test double in
# testing/. Its row says PLATFORMS wasm-subset: under single-threaded Emscripten only the files in
# the SOURCES_EMSCRIPTEN lists below build (Part I §1), which is why the subset's sources appear in
# both lists.
set(_coreCppPlatformSubset FileUri.cpp GlobMatch.cpp PathUtils.cpp)
set(_coreCppPlatformSubsetTests
    Clock_test.cpp
    EnvironmentProvider_test.cpp
    FileInfoProvider_test.cpp
    FileUri_test.cpp
    GlobMatch_test.cpp
    PathUtils_test.cpp
    PlatformError_test.cpp
    StringUtils_test.cpp
    Types_test.cpp)

# The POSIX providers, which nativeEnvironmentProvider() and nativeFileInfoProvider() return on
# every POSIX system. Emscripten's libc serves them too, over its own environment and virtual
# filesystem, so they are part of the subset.
set(_coreCppPlatformPosixProviders
    posix/PosixEnvironmentProvider.cpp
    posix/PosixEnvironmentProvider.hpp
    posix/PosixFileInfoProvider.cpp
    posix/PosixFileInfoProvider.hpp)

# Sockets back SystemPipe on Windows: a loopback pair mapped to a waitable event.
set(_coreCppPlatformLibs "")
if(WIN32)
    list(APPEND _coreCppPlatformLibs ws2_32)
endif()

core_cpp_add_module(platform KIND STATIC
    HEADERS
        Clock.hpp
        EnvironmentProvider.hpp
        FileInfoProvider.hpp
        FileSystem.hpp
        FileUri.hpp
        GlobMatch.hpp
        MessageQueue.hpp
        NativeFileSystem.hpp
        PathUtils.hpp
        PlatformError.hpp
        SignalHandler.hpp
        StringUtils.hpp
        SystemInfo.hpp
        SystemPipe.hpp
        Types.hpp
        UserPaths.hpp
        Wakeup.hpp
        WinsockInit.hpp
        testing/InMemoryFileSystem.hpp
        testing/MockFileInfoProvider.hpp
        testing/TestEnvironmentProvider.hpp
    SOURCES
        ${_coreCppPlatformSubset}
        NativeFileSystem.cpp
        SignalHandler.cpp
        SystemInfo.cpp
        SystemPipe.cpp
        WinsockInit.cpp
        testing/InMemoryFileSystem.cpp
    SOURCES_POSIX
        ${_coreCppPlatformPosixProviders}
    SOURCES_LINUX
        linux/LinuxWakeup.cpp
    SOURCES_BSD
        posix/PosixWakeup.cpp
    SOURCES_WINDOWS
        windows/WindowsEnvironmentProvider.cpp
        windows/WindowsEnvironmentProvider.hpp
        windows/WindowsFileInfoProvider.cpp
        windows/WindowsFileInfoProvider.hpp
        windows/WindowsTypes.cpp
        windows/WindowsWakeup.cpp
    SOURCES_EMSCRIPTEN
        ${_coreCppPlatformSubset}
        ${_coreCppPlatformPosixProviders}
    PUBLIC_LIBS core::base core::coro
    PRIVATE_LIBS ${_coreCppPlatformLibs})

core_cpp_add_test(platform
    SOURCES
        ${_coreCppPlatformSubsetTests}
        FileSystem_test.cpp
        MessageQueue_test.cpp
        SignalHandler_test.cpp
        SystemInfo_test.cpp
        SystemPipe_test.cpp
        UserPaths_test.cpp
        Wakeup_test.cpp
    SOURCES_EMSCRIPTEN
        ${_coreCppPlatformSubsetTests})
