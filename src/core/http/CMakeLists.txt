# SPDX-License-Identifier: Apache-2.0

# core::http: HTTP/1.1 (RFC 9110/9112). The pure layer -- the message vocabulary with its validating
# factories, the framing rules of RFC 9112 section 6.3 as one row table, the incremental head parser
# and the chunked codec -- holds no socket, so every framing rule is tested from a table
# (Framing_test.cpp, over the shared SmugglingCorpus.hpp). Connection puts it on an ISocket: heads
# and streamed bodies with trailers, read through one buffer of maxReadChunk bytes.
core_cpp_add_module(http KIND STATIC
    HEADERS
        Body.hpp
        Connection.hpp
        Framing.hpp
        Message.hpp
    SOURCES
        Body.cpp
        Connection.cpp
        Framing.cpp
        Message.cpp
    PUBLIC_LIBS core::base core::net)

# One binary. SmugglingCorpus.hpp is the test-only corpus the server's cases run too, and
# HttpTestSupport.hpp the helpers the cases share; Connection_test.cpp drives both ends over
# net::testing's in-memory pair on a TestLoop, so nothing in it waits on anything but the loop.
core_cpp_add_test(http
    SOURCES
        Connection_test.cpp
        Framing_test.cpp
        HttpTestSupport.hpp
        Message_test.cpp
        SmugglingCorpus.hpp)

# Connection over a real socket pair on every backend this platform builds: the deadlines and the
# cancellations the in-memory double cannot drive. Its own binary for the label; a deadline defect
# hangs rather than fails, so every case runs under a watchdog, and TIMEOUT is the backstop.
core_cpp_add_test(http NAME http_loopback
    SOURCES
        ConnectionLoopback_test.cpp
        HttpTestSupport.hpp
    SOURCES_POSIX
        posix/HttpTestSupport.cpp
    SOURCES_WINDOWS
        windows/HttpTestSupport.cpp
    LABELS loopback
    TIMEOUT 120)
