# SPDX-License-Identifier: Apache-2.0
#
# No widget of core::tui hard-codes a square corner: a frame's corners come from
# BorderChars::fromStyle() (Box.cpp), so a widget drawn in BorderStyle::Rounded shows ╭ ╮ ╰ ╯. This
# scans every source of src/core/tui but the border helper (Box.cpp, Box.hpp) and the tests for the
# four square corners in every spelling C++ has for them (listed below). Borders_test.cpp checks
# the same of each widget it renders; this catches the widget nobody added there yet.
#
# Usage: cmake -DROOT=<core-cpp> -P tests/cmake/check-tui-corners.cmake
#
# Tree-level: its input is the source tree, and the `style` job runs it.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT OR NOT EXISTS "${ROOT}/src/core/tui")
    message(FATAL_ERROR "check-tui-corners: ROOT ('${ROOT}') is not a core-cpp source tree.")
endif()

# ┌ ┐ └ ┘: U+250C, U+2510, U+2514, U+2518. Each as itself, as its UTF-8 bytes in \x escapes, as a \u
# or \U universal character name, as a \x escape in a wide literal (U'\x250C'), or as a number
# (char32_t{0x250C}, 0x0000250c) -- in either case of hex digit.
set(codePoint "(250[cC]|251[048])")
set(patterns
    "(┌|┐|└|┘)"
    "\\\\x[eE]2\\\\x94\\\\x(8[cC]|90|94|98)"
    "\\\\(u|U0000)${codePoint}"
    "\\\\x0*${codePoint}"
    "0[xX]0*${codePoint}")

file(GLOB_RECURSE sources RELATIVE "${ROOT}" "${ROOT}/src/core/tui/*.cpp" "${ROOT}/src/core/tui/*.hpp")
set(findings "")
set(scanned 0)
foreach(source IN LISTS sources)
    if(source MATCHES "_test\\.cpp$" OR source MATCHES "/Box\\.(cpp|hpp)$")
        continue()
    endif()
    math(EXPR scanned "${scanned} + 1")
    file(READ "${ROOT}/${source}" content)
    foreach(pattern IN LISTS patterns)
        string(REGEX MATCHALL "${pattern}" matches "${content}")
        foreach(match IN LISTS matches)
            string(APPEND findings "\n  ${source}: ${match}")
        endforeach()
    endforeach()
endforeach()

if(scanned EQUAL 0)
    message(FATAL_ERROR "check-tui-corners: scanned no file under ${ROOT}/src/core/tui")
endif()
if(findings)
    message(FATAL_ERROR
        "check-tui-corners: a square corner is hard-coded outside the border helper -- draw the frame "
        "with Canvas::drawBox() or BorderChars::fromStyle() of the widget's BorderStyle:${findings}")
endif()
message(STATUS "check-tui-corners: ${scanned} file(s) under src/core/tui hard-code no square corner")
