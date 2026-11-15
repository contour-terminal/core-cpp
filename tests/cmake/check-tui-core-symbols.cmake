# SPDX-License-Identifier: Apache-2.0
#
# Proves that core::tui_core reaches nothing of the platform layer, the coroutines, the event loop or
# the terminal half of the TUI (docs/design/tui-headless.md), from two sides.
#
# The module table bounds what the target LINKS, and the configure refuses a link its row does not
# list. It cannot see an include: every module's headers sit under one include root, so a file of
# tui_core that includes <core/platform/Clock.hpp> or <core/tui/Terminal.hpp> compiles, and an
# inline function or a template from there lands in the archive, or a call into core::platform
# stays in it as an undefined symbol for the consumer's link to fail on. So this reads
#
#   - the INCLUDES of every file in tui_core's own source and header lists (FILES), refusing
#     <core/platform/...>, <core/async/...>, <core/net/...> and the terminal half's headers, whether
#     included by the include root or by a quoted path relative to the file; and
#   - the archive's SYMBOL TABLE (`nm -C`, every symbol, defined or undefined, demangled), refusing
#     any symbol of those namespaces or of the terminal half's classes, typeinfo and vtables
#     included.
#
# An empty scan is not a clean one: the listing must show a symbol of core::tui::Screen, and the
# file list must name Screen.cpp, before nothing found counts as clean. Where the toolchain has no
# nm (cl), the symbol scan says it did not run and the include scan still does.
#
# Usage: cmake [-DNM=<nm> -DARCHIVE=<libcore-cpp-tui_core.a>] [-DLISTING=<file holding `nm -C` output>]
#              [-DSOURCE_DIR=<src/core/tui> "-DFILES=<file>|<file>|..."]
#              -P tests/cmake/check-tui-core-symbols.cmake
#
# FILES is `|`-separated, as $<JOIN:...,|> writes it, and a relative entry is under SOURCE_DIR.
# LISTING is the self-test's (check-tui-core-symbols-selftest.cmake), which plants listings and files
# rather than building archives that hold a violation.

cmake_minimum_required(VERSION 3.25)

set(problems "")
set(ran "")

# --- the includes ----------------------------------------------------------------

if(DEFINED FILES)
    string(REPLACE "|" ";" files "${FILES}")
    set(sawScreen OFF)
    foreach(file IN LISTS files)
        if(NOT IS_ABSOLUTE "${file}")
            set(file "${SOURCE_DIR}/${file}")
        endif()
        if(NOT EXISTS "${file}")
            list(APPEND problems "${file}: listed in tui_core's sources, but does not exist")
            continue()
        endif()
        if(file MATCHES "/Screen\\.cpp$")
            set(sawScreen ON)
        endif()
        # Both spellings: <core/...> by the include root, and "..." relative to the file, which in
        # src/core/tui/ reaches the terminal half as "Terminal.hpp", "posix/PosixIO.hpp" or
        # "../platform/Clock.hpp" without naming core/. A quoted path is read as one under core/tui/,
        # normalized, unless it names core/ itself.
        file(STRINGS "${file}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*(<core/|\")")
        foreach(line IN LISTS includes)
            if(line MATCHES "<(core/[^>]+)>")
                set(header "${CMAKE_MATCH_1}")
                set(spelled "<${header}>")
            elseif(line MATCHES "\"([^\"]+)\"")
                set(header "${CMAKE_MATCH_1}")
                set(spelled "\"${header}\"")
                if(NOT header MATCHES "^core/")
                    cmake_path(SET header NORMALIZE "core/tui/${header}")
                endif()
            else()
                continue()
            endif()
            if(header MATCHES "^core/(platform|async|net)/"
               OR header MATCHES "^core/tui/(Terminal|TerminalInput|TerminalChannel|ClipboardWriter|SemanticBlockClient|FilesystemImageProvider|ImageLoader)\\.hpp$"
               OR header MATCHES "^core/tui/(posix|windows|testing)/"
               OR (header MATCHES "^core/tui/runtime/" AND NOT header STREQUAL "core/tui/runtime/ModalComponent.hpp"))
                list(APPEND problems "${file}: includes ${spelled} (<${header}>)")
            endif()
        endforeach()
    endforeach()
    if(NOT sawScreen)
        list(APPEND problems
            "FILES does not name Screen.cpp, so it is not tui_core's source list; an empty scan is not a clean one")
    endif()
    list(APPEND ran "includes")
endif()

# --- the symbols -----------------------------------------------------------------

set(symbols "")
set(haveSymbols OFF)
if(DEFINED LISTING)
    if(NOT EXISTS "${LISTING}")
        message(FATAL_ERROR "check-tui-core-symbols: LISTING ('${LISTING}') does not exist.")
    endif()
    file(READ "${LISTING}" symbols)
    set(ARCHIVE "${LISTING}")
    set(haveSymbols ON)
elseif(DEFINED ARCHIVE AND NOT "${NM}" STREQUAL "")
    if(NOT EXISTS "${ARCHIVE}")
        message(FATAL_ERROR "check-tui-core-symbols: ARCHIVE ('${ARCHIVE}') does not exist.")
    endif()
    execute_process(
        COMMAND "${NM}" -C "${ARCHIVE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE symbols
        ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "check-tui-core-symbols: '${NM} -C ${ARCHIVE}' failed (${result}): ${errors}")
    endif()
    set(haveSymbols ON)
elseif(DEFINED ARCHIVE)
    message(STATUS "check-tui-core-symbols: the symbol scan did NOT run -- this toolchain has no nm")
endif()

if(haveSymbols)
    if(NOT symbols MATCHES "core::tui::Screen::")
        list(APPEND problems
            "no symbol of core::tui::Screen in the listing of ${ARCHIVE}, so nm did not read the archive, or "
            "did not demangle it; an empty scan is not a clean one")
    endif()

    # What tui_core may not hold. core::tui::TerminalOutput and its SgrBuilder are the leaf's and are
    # allowed; Terminal, TerminalQueryInput, TerminalInput, TerminalChannel, ClipboardWriter,
    # SemanticBlockClient, the image file loaders (FilesystemImageProvider and ImageLoader.hpp's
    # functions and types; the ImageProvider interface is tui_core's) and the runtime are the
    # terminal half's -- all of the runtime but ModalComponent, the pure Component contract tui_core
    # carries at its runtime/ path, which is taken out of each line before the line is read. A class
    # name must end where the match does, so `typeinfo for core::tui::Terminal` is caught and
    # `core::tui::TerminalOutput::flush()` is not.
    set(forbidden
        "core::platform::"
        "core::async::"
        "core::net::"
        "core::tui::runtime::"
        "core::tui::(Terminal|TerminalQueryInput|TerminalInput|TerminalChannel)([^A-Za-z0-9_]|$)"
        "core::tui::(ClipboardWriter|ClipboardWriterTimeouts|SemanticBlockClient|SemanticBlockResult|CommandBlockInfo)([^A-Za-z0-9_]|$)"
        "core::tui::(FilesystemImageProvider|OwnedImage|ClipboardImage|loadImage|loadImageFromMemory|resizeImage|detectImageMediaType|isImageExtension|readClipboardImage)([^A-Za-z0-9_]|$)")

    string(REPLACE "\n" ";" lines "${symbols}")
    foreach(line IN LISTS lines)
        string(REPLACE "core::tui::runtime::ModalComponent<" "ModalComponent<" scanned "${line}")
        foreach(pattern IN LISTS forbidden)
            if(scanned MATCHES "${pattern}")
                string(STRIP "${line}" line)
                list(APPEND problems "symbol: ${line}")
                break()
            endif()
        endforeach()
    endforeach()
    list(APPEND ran "symbols")
endif()

if(NOT ran)
    message(FATAL_ERROR "check-tui-core-symbols: nothing to scan; pass FILES, ARCHIVE with NM, or LISTING.")
endif()

if(problems)
    list(LENGTH problems count)
    list(JOIN problems "\n  " listed)
    message(FATAL_ERROR
        "check-tui-core-symbols: core::tui_core reaches ${count} thing(s) it may not "
        "(cmake/CoreCppModules.cmake, its row: DEPS base tui_output):\n  ${listed}")
endif()

list(JOIN ran " and " ran)
message(STATUS "check-tui-core-symbols: no platform, async, net or terminal reach in tui_core's ${ran}")
