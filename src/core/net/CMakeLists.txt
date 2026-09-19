# SPDX-License-Identifier: Apache-2.0

# core::net: contour's event loop, sockets, TLS and HTTP server (contour 6777ff05), as contour has
# them but for the namespaces and core::platform in place of contour's net/platform/. Phase B
# replaces the EventSource API with IoBackend and merges fastcached's sockets into it. Three targets,
# each held to its row in cmake/CoreCppModules.cmake:
#
#   core::net_types  NetError and IoResult: header-only, and built everywhere, Emscripten included;
#   core::net        everything else: native only until Tasks B3 to B5 bring its WebAssembly subset;
#   core::net_tls    the TLS decorator, only with CORE_CPP_WITH_TLS. OpenSSL is PRIVATE to it, and no
#                    OpenSSL type appears in its header.
core_cpp_add_module(net_types KIND INTERFACE
    HEADERS
        IoResult.hpp
        NetError.hpp)

# Winsock backs the sockets on Windows. PRIVATE, because no Winsock type reaches a public header; a
# static library's private link still reaches the link line of the program that uses it.
set(_coreCppNetPrivateLibs "")
if(WIN32)
    list(APPEND _coreCppNetPrivateLibs ws2_32)
endif()

# Threads::Threads, and PUBLIC for the same reason core::base declares it: net's own headers
# (EventLoop.hpp above all) expose std::mutex and std::thread, so consumers instantiate them too.
# net states it itself rather than inheriting it through core::platform and core::base, because it
# is what net's own headers need. Free on glibc, fatal on FreeBSD, where the symbols live in libthr.
# (contour's src/net/CMakeLists.txt, 6777ff05)
#
# The module's root holds only platform-independent code. What one platform needs lives in posix/,
# linux/ (epoll), bsd/ (kqueue, for Apple and the BSDs) and windows/, and the source lists below
# choose it, so no file there guards itself with an #ifdef of its platform. contour's
# PollEventSource.cpp is split along its #ifdef into posix/ and windows/, and so is the socket pair
# of testing/InMemoryTransport.cpp, into testing/posix/ and testing/windows/. Two exceptions remain:
# DefaultEventSource.cpp keeps its #ifdefs until Task B3 replaces it, and EventSourceParity_test.cpp
# keeps two POSIX-only cases (a closed descriptor's registration, descriptor exhaustion), and the
# includes they need, under #ifndef _WIN32.
core_cpp_add_module(net KIND STATIC
    HEADERS
        AsyncBufferedReader.hpp
        DefaultEventSource.hpp
        Diagnostics.hpp
        EventLoop.hpp
        EventSource.hpp
        HttpServer.hpp
        IListener.hpp
        ISocket.hpp
        PollEventSource.hpp
        Sockets.hpp
        SplitSocket.hpp
        WithTimeout.hpp
        WriteQueue.hpp
        testing/CoroTestSupport.hpp
        testing/EventSourceBackends.hpp
        testing/InMemoryTransport.hpp
        testing/ScriptedEventSource.hpp
    SOURCES
        AsyncBufferedReader.cpp
        DefaultEventSource.cpp
        Diagnostics.cpp
        EventLoop.cpp
        HttpServer.cpp
        WriteQueue.cpp
        detail/PeerAddress.hpp
        detail/WaitChunking.hpp
    SOURCES_POSIX
        detail/WouldBlock.hpp
        posix/AcceptLoop.cpp
        posix/AcceptLoop.hpp
        posix/FdUtils.hpp
        posix/PollEventSource.cpp
        posix/PosixListener.cpp
        posix/PosixListener.hpp
        posix/PosixSocket.cpp
        posix/PosixSocket.hpp
        posix/SocketsPosix.cpp
        posix/UnixListener.cpp
        posix/UnixListener.hpp
        testing/posix/InMemoryTransport.cpp
    SOURCES_LINUX
        linux/EpollEventSource.cpp
        linux/EpollEventSource.hpp
    SOURCES_BSD
        bsd/KqueueEventSource.cpp
        bsd/KqueueEventSource.hpp
    SOURCES_WINDOWS
        testing/windows/InMemoryTransport.cpp
        windows/PollEventSource.cpp
        windows/SocketsWin32.cpp
        windows/WindowsListener.cpp
        windows/WindowsListener.hpp
        windows/WindowsLoopback.cpp
        windows/WindowsLoopback.hpp
        windows/WindowsSocket.cpp
        windows/WindowsSocket.hpp
    PUBLIC_LIBS core::net_types core::async core::platform Threads::Threads
    PRIVATE_LIBS ${_coreCppNetPrivateLibs})

core_cpp_add_module(net_tls KIND STATIC
    HEADERS Tls.hpp
    SOURCES
        Tls.cpp
        detail/ScopeGuard.hpp
    PUBLIC_LIBS core::net
    PRIVATE_LIBS OpenSSL::SSL OpenSSL::Crypto)

# The error vocabulary alone, in every build: under Emscripten this is what proves net_types builds.
core_cpp_add_test(net NAME net_types SOURCES NetError_test.cpp)

# Nearly every case moves bytes over a socket: a real one, a socketpair, or on Windows the loopback
# TCP pair that backs SystemPipe and the in-memory transport alike, hence the label on the binary.
core_cpp_add_test(net
    SOURCES
        AsyncBufferedReader_test.cpp
        EventLoop_test.cpp
        EventSourceParity_test.cpp
        HttpServer_test.cpp
        Socket_test.cpp
        WriteQueue_test.cpp
    SOURCES_POSIX
        posix/FdPassing_test.cpp
        posix/UnixSocket_test.cpp
    LABELS loopback)

core_cpp_add_test(net NAME net_tls SOURCES Tls_test.cpp LABELS loopback)
