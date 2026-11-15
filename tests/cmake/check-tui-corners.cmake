# SPDX-License-Identifier: Apache-2.0
#
# No widget of core::tui hard-codes a square corner: a frame's corners come from
# BorderChars::fromStyle() (Box.cpp), so a widget drawn in BorderStyle::Rounded shows ╭ ╮ ╰ ╯. This
# scans every source of src/core/tui but the border helper (Box.cpp, Box.hpp) and the tests for the
# four square corners, written as UTF-8, as a \u escape or as \x byte escapes. Borders_test.cpp checks
# the same of each widget it renders; this catches the widget nobody added there yet.
#
# Usage: cmake -DROOT=<core-cpp> -P tests/cmake/check-tui-corners.cmake
#
# Tree-level: its input is the source tree, and the `style` job runs it.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT OR NOT EXISTS "${ROOT}/src/core/tui")
    message(FATAL_ERROR "check-tui-corners: ROOT ('${ROOT}') is not a core-cpp source tree.")
endif()

# ┌ ┐ └ ┘: U+250C, U+2510, U+2514, U+2518.
set(needles
    "┌" "┐" "└" "┘"
    "\\u250C" "\\u2510" "\\u2514" "\\u2518" "\\u250c"
    "\\xe2\\x94\\x8c" "\\xe2\\x94\\x90" "\\xe2\\x94\\x94" "\\xe2\\x94\\x98"
    "\\xE2\\x94\\x8C" "\\xE2\\x94\\x90" "\\xE2\\x94\\x94" "\\xE2\\x94\\x98")

file(GLOB_RECURSE sources RELATIVE "${ROOT}" "${ROOT}/src/core/tui/*.cpp" "${ROOT}/src/core/tui/*.hpp")
set(findings "")
set(scanned 0)
foreach(source IN LISTS sources)
    if(source MATCHES "_test\\.cpp$" OR source MATCHES "/Box\\.(cpp|hpp)$")
        continue()
    endif()
    math(EXPR scanned "${scanned} + 1")
    file(READ "${ROOT}/${source}" content)
    foreach(needle IN LISTS needles)
        string(FIND "${content}" "${needle}" at)
        if(NOT at EQUAL -1)
            string(APPEND findings "\n  ${source}: ${needle}")
        endif()
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
