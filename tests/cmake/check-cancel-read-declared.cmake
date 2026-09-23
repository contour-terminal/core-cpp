# SPDX-License-Identifier: Apache-2.0
#
# Every transport core::net hands out answers `cancelRead` in its own class, and this is what stops
# the next one inheriting silence.
#
# `ISocket::cancelRead` ships with a DEFAULT NO-OP, and that is the right shape for the interface:
# a pure virtual would compel every scripted double to write `{}` with no reason beside it, which is
# "forgot" spelled in the vocabulary of "decided". But a transport whose reads PARK and which
# inherits the no-op cannot retire a parked read at all -- the awaiting coroutine is never resumed
# and its frame never freed, with no signal anywhere. `ISocket.hpp` says so, and until this check
# nothing but that sentence enforced it: `WindowsSocket` inherited it for three tasks, with a
# comment saying so. Reach for the type system when the obligation is DO SOMETHING and for a scan
# when it is SAY WHY.
#
# Ported from fastcached's `scripts/check-cancel-read-declared.cmake` (0708dd54), renamed into core
# spellings (`cancelRead`, `ROOT`), and changed in two ways the core tree needs:
#
#   - It walks `src/core/net/` RECURSIVELY, headers AND sources. Transports here live in
#     `posix/`, `windows/` and `testing/`, and `TlsSocket` is declared inside `Tls.cpp`; a flat
#     header glob would have read two of the seven.
#   - A class's region ends at the next marker at the class's OWN indentation -- its `};`, or the
#     next `class`/`struct` -- rather than at column zero. `TlsSocket` is indented inside a
#     namespace block, and a column-zero boundary lets its region run on into whatever the file
#     declares next, where an unrelated `cancelRead` answers for it.
#
# **This does not detect parking, and nothing textual can.** It asks the narrower question the rule
# is about: did this transport ANSWER, or did it inherit an answer it never gave. A written no-op
# passes, and should -- `BlockingSocket`'s is exactly that, with its reason beside it.
#
# **It DERIVES the set rather than tabulating it**: a hand-kept list of transports is the same
# defect one level up, since the next one joins no list and passes.
#
# **Tests and canaries are out of scope, and that is a decision.** `*_test.cpp` and `*Canary.cpp`
# implement `ISocket` for their own purposes, and a fake whose reads never park owes no
# cancellation. `testing/` is IN scope: `InMemorySocket` and `SocketDecorator` are what core::net
# hands a consumer's tests.
#
# **Comments are stripped before anything is measured** -- both spellings, line comments first so a
# `// ... /*` cannot open a block that swallows real code. A comment is not a declaration.
#
# **It fails CLOSED on an empty scan, twice**: no file found, or no class deriving from `ISocket`
# matched, is the check being broken rather than the tree being clean.
#
# It never splits a list, so a bracket cannot merge two elements: whole files are read with
# `file(READ)` and walked with `string(FIND)`.
#
# Runs as `cmake -P`; the verdict is `CMake Error` in the output, never the exit code alone.
#
# Usage: cmake -DROOT=<source tree> -P tests/cmake/check-cancel-read-declared.cmake

cmake_minimum_required(VERSION 3.25)

# What deriving from the interface looks like, and what answering looks like. The needle omits the
# `final:` / `:` in front so both spellings match one string, and is matched literally.
set(coreCppSocketBase "public ISocket")
set(coreCppCancelName "cancelRead")

if(NOT DEFINED ROOT OR ROOT STREQUAL "")
    message(FATAL_ERROR "check-cancel-read-declared: ROOT is not set. Invoke this script as: cmake "
                        "-DROOT=<source root> -P ${CMAKE_CURRENT_LIST_FILE}")
endif()

set(netRoot "${ROOT}/src/core/net")
if(NOT IS_DIRECTORY "${netRoot}")
    message(FATAL_ERROR "check-cancel-read-declared: '${netRoot}' is not a directory. Is ROOT the source root?")
endif()

file(GLOB_RECURSE netFiles LIST_DIRECTORIES false "${netRoot}/*.hpp" "${netRoot}/*.cpp")
list(FILTER netFiles EXCLUDE REGEX "(_test|Canary)\\.cpp$")

# `list(LENGTH)` rather than a string compare: an unset variable compares against its own NAME.
list(LENGTH netFiles scannedCount)
if(scannedCount EQUAL 0)
    message(FATAL_ERROR
        "check-cancel-read-declared: walked '${netRoot}' and found no non-test file at all. That is the "
        "check being broken, not the tree being clean. Fix the glob in ${CMAKE_CURRENT_LIST_FILE}.")
endif()

set(violations "")
set(transportCount 0)
set(transportNames "")
string(LENGTH "${coreCppSocketBase}" baseLength)

foreach(file IN LISTS netFiles)
    file(READ "${file}" text)
    string(REGEX REPLACE "//[^\n]*" "" text "${text}")
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" text "${text}")

    file(RELATIVE_PATH shownPath "${ROOT}" "${file}")

    set(cursor 0)
    while(TRUE)
        string(SUBSTRING "${text}" ${cursor} -1 rest)
        string(FIND "${rest}" "${coreCppSocketBase}" baseAt)
        if(baseAt EQUAL -1)
            break()
        endif()

        # The class line: from the last newline before the needle. Its leading whitespace is the
        # class's indentation, and its name is for the report. Bounded, so a long file cannot make
        # the regex quadratic.
        set(className "<unknown>")
        set(indent "")
        math(EXPR windowStart "${baseAt} - 160")
        if(windowStart LESS 0)
            set(windowStart 0)
        endif()
        math(EXPR windowLength "${baseAt} - ${windowStart}")
        string(SUBSTRING "${rest}" ${windowStart} ${windowLength} window)
        if(window MATCHES "(^|\n)([ \t]*)(class|struct)[ \t]+([A-Za-z_][A-Za-z0-9_]*)[^;{\n]*$")
            set(indent "${CMAKE_MATCH_2}")
            set(className "${CMAKE_MATCH_4}")
        endif()

        # A forward declaration cannot carry a member. Every offset below is a position in `rest`.
        math(EXPR afterBase "${baseAt} + ${baseLength}")
        string(SUBSTRING "${rest}" ${afterBase} -1 tail)
        string(FIND "${tail}" "{" braceAt)
        string(FIND "${tail}" ";" semicolonAt)
        if(braceAt EQUAL -1)
            break()
        endif()
        if(NOT semicolonAt EQUAL -1 AND semicolonAt LESS braceAt)
            math(EXPR cursor "${cursor} + ${afterBase}")
            continue()
        endif()

        # The region: from the opening brace to whichever marker AT THIS CLASS'S INDENTATION comes
        # first -- its own `};`, or the next `class`/`struct` -- or to the end of the file. A member's
        # own `};` is indented deeper and so is not one. A boundary this misses can only make the
        # region LONGER, never shorter, so it cannot invent a violation.
        math(EXPR regionStart "${afterBase} + ${braceAt} + 1")
        string(SUBSTRING "${rest}" ${regionStart} -1 regionRest)
        set(regionEnd -1)
        foreach(boundary IN ITEMS "\n${indent}};" "\n${indent}class " "\n${indent}struct ")
            string(FIND "${regionRest}" "${boundary}" boundaryAt)
            if(NOT boundaryAt EQUAL -1 AND (regionEnd EQUAL -1 OR boundaryAt LESS regionEnd))
                set(regionEnd ${boundaryAt})
            endif()
        endforeach()
        if(regionEnd EQUAL -1)
            set(region "${regionRest}")
        else()
            string(SUBSTRING "${regionRest}" 0 ${regionEnd} region)
        endif()

        math(EXPR transportCount "${transportCount} + 1")
        string(APPEND transportNames " ${className}")

        string(FIND "${region}" "${coreCppCancelName}" cancelAt)
        if(cancelAt EQUAL -1)
            list(APPEND violations
                 "${shownPath}: ${className} derives from ISocket and does not declare ${coreCppCancelName}, so it INHERITS the default no-op -- an answer it never gave")
        endif()

        math(EXPR cursor "${cursor} + ${regionStart}")
    endwhile()
endforeach()

# The second empty-scan refusal, and a different question from the first: a renamed base leaves the
# file count healthy and the transport count at zero.
if(transportCount EQUAL 0)
    message(FATAL_ERROR
        "check-cancel-read-declared: read ${scannedCount} file(s) under '${netRoot}' and found no class "
        "deriving from '${coreCppSocketBase}'. That is the check being broken, not the tree being clean: "
        "the base may have been renamed or respelled. Fix the needle in ${CMAKE_CURRENT_LIST_FILE}.")
endif()

list(LENGTH violations violationCount)
if(violationCount GREATER 0)
    set(rendered "")
    foreach(violation IN LISTS violations)
        string(APPEND rendered "\n  - ${violation}")
    endforeach()
    message(FATAL_ERROR
        "check-cancel-read-declared: ${violationCount} transport(s) inherit ISocket::cancelRead's default "
        "no-op:${rendered}\n\n"
        "A socket that inherits it cannot retire a parked read: the awaiting coroutine is never resumed "
        "and its frame is never freed. Declare it -- a written no-op WITH ITS REASON is a complete "
        "answer for a transport whose reads never park, and BlockingSocket is the example.")
endif()

message(STATUS
    "check-cancel-read-declared: ${transportCount} transport(s) across ${scannedCount} file(s) declare "
    "${coreCppCancelName}; none inherits the default:${transportNames}")
