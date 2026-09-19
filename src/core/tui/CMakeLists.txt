# SPDX-License-Identifier: Apache-2.0

# core::tui_output: the leaf of endo's terminal UI (endo f774a210), the part that composes escape
# sequences and writes them. Its row in cmake/CoreCppModules.cmake says DEPS base, so the layering
# check refuses a link to libunicode's users, to the coroutines or to core::platform here. That is
# the point of the split: a program that only prints styled text and progress (Lightweight's
# dbtool) links this and nothing else.
#
# The module's root holds only platform-independent code. What goes through the operating system --
# the write, the terminal's size, the XTVERSION probe, the terminal test -- is in posix/ and
# windows/, and the source
# lists below choose it, so no file there guards itself with an #ifdef of its platform. In endo
# those two files each carried a full copy of the byte composition as well (`platform/
# TerminalOutput.cpp`, `platform/TerminalOutputWin32.cpp`); here it is shared, and what is left is
# only what actually differs.
core_cpp_add_module(tui_output KIND STATIC
    HEADERS
        CursorShape.hpp
        Error.hpp
        SgrBuilder.hpp
        TerminalOutput.hpp
        TerminalProtocols.hpp
    SOURCES
        SgrBuilder.cpp
        TerminalOutput.cpp
        detail/XtVersion.hpp
    SOURCES_POSIX
        posix/PosixIO.hpp
        posix/TerminalOutput.cpp
    SOURCES_WINDOWS
        windows/TerminalOutput.cpp
        windows/Win32Utf.hpp
    PRIVATE_LIBS core::base)

core_cpp_add_test(tui NAME tui_output
    SOURCES
        TerminalOutput_test.cpp
        TerminalProtocols_test.cpp)
