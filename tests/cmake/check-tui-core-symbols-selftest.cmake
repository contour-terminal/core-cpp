# SPDX-License-Identifier: Apache-2.0
#
# Watches check-tui-core-symbols.cmake refuse what it exists to refuse, on planted `nm -C` listings
# and planted source trees, and accept clean ones: a checker that passed everything would read
# exactly like a clean archive.
#
# Usage: cmake -DCHECKER=<check-tui-core-symbols.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-tui-core-symbols-selftest.cmake

cmake_minimum_required(VERSION 3.25)

foreach(required IN ITEMS CHECKER WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "check-tui-core-symbols-selftest: ${required} is not set.")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(screen "0000000000000000 T core::tui::Screen::draw()\n")
set(leaf "                 U core::tui::TerminalOutput::flush()\n")

# name | expected outcome | the listing | text the refusal must name
set(cases
    "clean|pass|${screen}${leaf}|"
    "modal-contract|pass|${screen}0000000000000000 V vtable for core::tui::runtime::ModalComponent<core::tui::QuestionResult>\n|"
    "empty|fail||no symbol of core::tui::Screen"
    "platform|fail|${screen}                 U core::platform::SteadyClock::now() const\n|core::platform::SteadyClock"
    "async|fail|${screen}0000000000000000 W core::async::Task<void>::~Task()\n|core::async::Task"
    "net|fail|${screen}                 U core::net::EventLoop::run()\n|core::net::EventLoop"
    "terminal|fail|${screen}                 U core::tui::Terminal::rows() const\n|core::tui::Terminal::rows"
    "runtime|fail|${screen}                 U core::tui::runtime::TuiRuntime::nextEvent()\n|core::tui::runtime::TuiRuntime"
    "terminal-typeinfo|fail|${screen}0000000000000000 V typeinfo for core::tui::Terminal\n|typeinfo for core::tui::Terminal"
    "terminal-vtable|fail|${screen}0000000000000000 V vtable for core::tui::Terminal\n|vtable for core::tui::Terminal"
    "query-input|fail|${screen}                 U core::tui::TerminalQueryInput::~TerminalQueryInput()\n|TerminalQueryInput"
    "clipboard-writer|fail|${screen}                 U core::tui::ClipboardWriter::write(std::basic_string_view<char, std::char_traits<char> >)\n|core::tui::ClipboardWriter::write"
    "clipboard-writer-timeouts|fail|${screen}0000000000000000 W core::tui::ClipboardWriterTimeouts::ClipboardWriterTimeouts()\n|ClipboardWriterTimeouts"
    "semantic-block-client|fail|${screen}0000000000000000 V vtable for core::tui::SemanticBlockClient\n|vtable for core::tui::SemanticBlockClient"
    "filesystem-image-provider|fail|${screen}                 U core::tui::FilesystemImageProvider::FilesystemImageProvider()\n|FilesystemImageProvider"
    "image-loader|fail|${screen}                 U core::tui::loadImage(std::basic_string_view<char, std::char_traits<char> >)\n|core::tui::loadImage"
    "image-loader-type|fail|${screen}0000000000000000 W core::tui::OwnedImage::~OwnedImage()\n|core::tui::OwnedImage"
    "image-provider|pass|${screen}0000000000000000 V typeinfo for core::tui::ImageProvider\n|")

set(failures "")
foreach(case IN LISTS cases)
    string(REPLACE "|" ";" fields "${case}")
    list(LENGTH fields count)
    list(GET fields 0 name)
    list(GET fields 1 expected)
    set(listing "")
    set(needle "")
    if(count GREATER 2)
        list(GET fields 2 listing)
    endif()
    if(count GREATER 3)
        list(GET fields 3 needle)
    endif()

    set(file "${WORK_DIR}/${name}.txt")
    file(WRITE "${file}" "${listing}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DLISTING=${file}" -P "${CHECKER}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(said "${out}${err}")

    if(expected STREQUAL "pass" AND NOT result EQUAL 0)
        list(APPEND failures "${name}: refused a clean listing: ${said}")
    elseif(expected STREQUAL "fail" AND result EQUAL 0)
        list(APPEND failures "${name}: accepted a listing it must refuse")
    elseif(expected STREQUAL "fail" AND needle AND NOT said MATCHES "${needle}")
        list(APPEND failures "${name}: refused, but without naming '${needle}': ${said}")
    endif()
endforeach()

# The include scan, over planted source trees. Each case is a Screen.cpp (which the checker requires
# to see, so a list that is not tui_core's cannot pass as clean) plus one file holding one include.
# name | expected outcome | the include's argument in Other.cpp, as spelled | text the refusal must name
set(includeCases
    "includes-clean|pass|<core/tui/Buffer.hpp>|"
    "includes-leaf|pass|<core/tui/TerminalOutput.hpp>|"
    "includes-modal-contract|pass|<core/tui/runtime/ModalComponent.hpp>|"
    "includes-platform|fail|<core/platform/Clock.hpp>|<core/platform/Clock.hpp>"
    "includes-async|fail|<core/async/Task.hpp>|<core/async/Task.hpp>"
    "includes-net|fail|<core/net/EventLoop.hpp>|<core/net/EventLoop.hpp>"
    "includes-terminal|fail|<core/tui/Terminal.hpp>|<core/tui/Terminal.hpp>"
    "includes-runtime|fail|<core/tui/runtime/TuiRuntime.hpp>|<core/tui/runtime/TuiRuntime.hpp>"
    "includes-posix|fail|<core/tui/posix/PosixIO.hpp>|<core/tui/posix/PosixIO.hpp>"
    "includes-clipboard-writer|fail|<core/tui/ClipboardWriter.hpp>|<core/tui/ClipboardWriter.hpp>"
    "includes-semantic-block-client|fail|<core/tui/SemanticBlockClient.hpp>|<core/tui/SemanticBlockClient.hpp>"
    "includes-filesystem-image-provider|fail|<core/tui/FilesystemImageProvider.hpp>|<core/tui/FilesystemImageProvider.hpp>"
    "includes-image-loader|fail|<core/tui/ImageLoader.hpp>|<core/tui/ImageLoader.hpp>"
    "includes-quoted-clean|pass|\"Buffer.hpp\"|"
    "includes-quoted-modal-contract|pass|\"runtime/ModalComponent.hpp\"|"
    "includes-quoted-terminal|fail|\"Terminal.hpp\"|<core/tui/Terminal.hpp>"
    "includes-quoted-dot-terminal|fail|\"./Terminal.hpp\"|<core/tui/Terminal.hpp>"
    "includes-quoted-posix|fail|\"posix/PosixIO.hpp\"|<core/tui/posix/PosixIO.hpp>"
    "includes-quoted-runtime|fail|\"runtime/TuiRuntime.hpp\"|<core/tui/runtime/TuiRuntime.hpp>"
    "includes-quoted-parent|fail|\"../platform/Clock.hpp\"|<core/platform/Clock.hpp>"
    "includes-quoted-rooted|fail|\"core/net/EventLoop.hpp\"|<core/net/EventLoop.hpp>")
foreach(case IN LISTS includeCases)
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 name)
    list(GET fields 1 expected)
    list(GET fields 2 include)
    list(LENGTH fields count)
    set(needle "")
    if(count GREATER 3)
        list(GET fields 3 needle)
    endif()
    set(dir "${WORK_DIR}/${name}")
    file(WRITE "${dir}/Screen.cpp" "#include <core/tui/Screen.hpp>\n")
    file(WRITE "${dir}/Other.cpp" "// planted\n#include ${include}\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${dir}" "-DFILES=Screen.cpp|Other.cpp" -P "${CHECKER}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(said "${out}${err}")
    if(expected STREQUAL "pass" AND NOT result EQUAL 0)
        list(APPEND failures "${name}: refused a clean tree: ${said}")
    elseif(expected STREQUAL "fail" AND result EQUAL 0)
        list(APPEND failures "${name}: accepted a tree it must refuse")
    elseif(expected STREQUAL "fail" AND NOT said MATCHES "${needle}")
        list(APPEND failures "${name}: refused, but without naming '${needle}': ${said}")
    endif()
endforeach()

# A file list that is not tui_core's -- no Screen.cpp in it -- is refused rather than read as clean.
file(WRITE "${WORK_DIR}/no-screen/Other.cpp" "#include <core/tui/Buffer.hpp>\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${WORK_DIR}/no-screen" "-DFILES=Other.cpp" -P "${CHECKER}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(result EQUAL 0)
    list(APPEND failures "no-screen: accepted a file list that names no Screen.cpp")
endif()

if(failures)
    list(JOIN failures "\n  " listed)
    message(FATAL_ERROR "check-tui-core-symbols-selftest:\n  ${listed}")
endif()
message(STATUS "check-tui-core-symbols-selftest: every planted listing was judged as expected")
