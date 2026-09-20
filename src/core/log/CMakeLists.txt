# SPDX-License-Identifier: Apache-2.0

# core::log: categorised logging, its sinks, and the contract checks that report through it.
#
# The module's root holds only platform-independent code. Its two operating-system differences --
# whether a standard stream is a terminal, and this process's id -- are implementations per
# platform rather than #ifdefs inside the code that uses them (.agent/rules/platform.md), so the
# source lists below choose them and no file in posix/ or windows/ guards itself. Emscripten
# serves both over its own libc and never compiles SOURCES_POSIX, so those files are named in
# SOURCES_EMSCRIPTEN as well.
#
# detail/ProcessId.hpp is private: it is in SOURCES, not HEADERS, so it is in no file set and
# never becomes public API. The terminal queries are declared in LogSink.hpp, which is public,
# because core::cli::App asks them too.
core_cpp_add_module(log KIND STATIC
    HEADERS Assert.hpp LogSink.hpp LogStore.hpp
    SOURCES LogSink.cpp LogStore.cpp detail/ProcessId.hpp
    SOURCES_POSIX posix/ProcessId.cpp posix/TerminalQuery.cpp
    SOURCES_WINDOWS windows/ProcessId.cpp windows/TerminalQuery.cpp
    SOURCES_EMSCRIPTEN posix/ProcessId.cpp posix/TerminalQuery.cpp
    PUBLIC_LIBS core::base)

core_cpp_add_test(log SOURCES LogSink_test.cpp)
