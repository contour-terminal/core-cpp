# SPDX-License-Identifier: Apache-2.0

# core::http: HTTP/1.1 (RFC 9110/9112). The pure layer here -- the message vocabulary with its
# validating factories, the framing rules of RFC 9112 section 6.3 as one row table, the incremental
# head parser and the chunked codec -- holds no socket, so every framing rule is tested from a table
# (Framing_test.cpp, over the shared SmugglingCorpus.hpp).
core_cpp_add_module(http KIND STATIC
    HEADERS
        Framing.hpp
        Message.hpp
    SOURCES
        Framing.cpp
        Message.cpp
    PUBLIC_LIBS core::base core::net)

# One binary: the pure layer links nothing but the module. SmugglingCorpus.hpp is the test-only
# corpus the server's cases run too.
core_cpp_add_test(http
    SOURCES
        Framing_test.cpp
        Message_test.cpp
        SmugglingCorpus.hpp)
