# SPDX-License-Identifier: Apache-2.0

# core::log: categorised logging, its sinks, and the contract checks that report through it.
#
# The terminal query is the module's one operating-system difference, so it is an implementation
# per platform rather than an #ifdef inside the decision (.agent/rules/platform.md). Emscripten
# serves the POSIX one over its own libc, and never compiles SOURCES_POSIX, so that file is
# named in both lists.
core_cpp_add_module(log KIND STATIC
    HEADERS Assert.hpp LogSink.hpp LogStore.hpp
    SOURCES LogSink.cpp LogStore.cpp
    SOURCES_POSIX posix/TerminalQuery.cpp
    SOURCES_WINDOWS windows/TerminalQuery.cpp
    SOURCES_EMSCRIPTEN posix/TerminalQuery.cpp
    PUBLIC_LIBS core::base)

core_cpp_add_test(log SOURCES LogSink_test.cpp)
