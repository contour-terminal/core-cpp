# SPDX-License-Identifier: Apache-2.0
#
# Every `ISocket::read` core::net hands out refuses an empty buffer, and this is what makes that a
# fact rather than every transport's author having remembered.
#
# `contract::requireReadBuffer` (`core/net/SocketContract.hpp`) is the guard: `0` on this interface
# means THE PEER HAS FINISHED SENDING, and every transport's receive primitive answers `0` for a
# zero-length request, so an empty span would be answered with a graceful close that never happened.
#
# **The guard is purely ADDITIVE, and that is why this check exists.** A transport may simply not
# have the line, and the canary that watches it (`core-cpp.socket-contract-canary.empty-read-buffer`)
# aborts at the first violation, so it can only ever watch ONE transport -- exact about the site it
# knows and silent about the rest, and silence reads like complete coverage. So this walks the tree
# and finds every definition.
#
# Ported from fastcached's `scripts/check-read-buffer-guard.cmake` (0708dd54), renamed into core
# spellings, and extended where the core tree differs:
#
#   - It walks `src/core/net/` RECURSIVELY, sources AND headers: transports live in `posix/`,
#     `windows/` and `testing/`, and `TlsSocket::read` is defined INSIDE its class, in `Tls.cpp`.
#   - It reads both spellings of a definition -- out of line (`Foo::read(...)`) and in a class body
#     (`read(...) override {`) -- and finds each body by counting braces from its opening one, since
#     an in-class body does not end at column zero.
#   - **A DELEGATE passes.** A decorator whose `read` returns another socket's `read(buffer)` hands
#     the same span to a transport that guards it, and guarding twice would add nothing; it is
#     counted and reported apart, so a transport cannot hide among them unseen. A body that returns
#     anything else without the guard is refused.
#
# **Two clauses per guarded site, and the second is not decoration.** The body must call the guard,
# AND that call must stand before the body's first `return`: a guard below the `if (_closed) return`
# every transport opens with is skipped for exactly the socket a caller is most confused about.
#
# **Tests and canaries are out of scope, and that is a decision**: a fixture handing its own fake an
# empty span is the fixture's business. `testing/` is IN scope, because `InMemorySocket` is a
# transport the suite and a consumer's tests read through.
#
# Comments are stripped before anything is measured (both spellings): a comment is not a call site.
# It fails CLOSED on an empty scan, twice -- no file, or no definition matched. It never splits a
# list: whole files are read with `file(READ)` and walked with `string(FIND)`.
#
# Runs as `cmake -P`; the verdict is `CMake Error` in the output, never the exit code alone.
#
# Usage: cmake -DROOT=<source tree> -P tests/cmake/check-read-buffer-guard.cmake

cmake_minimum_required(VERSION 3.25)

# The parameter list both spellings share, and the guard. Matched without the return type, so a
# `[[nodiscard]]` or a reformat of what precedes it cannot silently shrink the set.
set(coreCppReadParameters "read(std::span<std::byte> buffer)")
set(coreCppReadGuardCall "contract::requireReadBuffer(buffer)")

if(NOT DEFINED ROOT OR ROOT STREQUAL "")
    message(FATAL_ERROR "check-read-buffer-guard: ROOT is not set. Invoke this script as: cmake "
                        "-DROOT=<source root> -P ${CMAKE_CURRENT_LIST_FILE}")
endif()

set(netRoot "${ROOT}/src/core/net")
if(NOT IS_DIRECTORY "${netRoot}")
    message(FATAL_ERROR "check-read-buffer-guard: '${netRoot}' is not a directory. Is ROOT the source root?")
endif()

file(GLOB_RECURSE netFiles LIST_DIRECTORIES false "${netRoot}/*.hpp" "${netRoot}/*.cpp")
list(FILTER netFiles EXCLUDE REGEX "(_test|Canary)\\.cpp$")
list(LENGTH netFiles scannedCount)
if(scannedCount EQUAL 0)
    message(FATAL_ERROR
        "check-read-buffer-guard: walked '${netRoot}' and found no non-test file at all. That is the check "
        "being broken, not the tree being clean. Fix the glob in ${CMAKE_CURRENT_LIST_FILE}.")
endif()

set(violations "")
set(guardedCount 0)
set(delegateCount 0)
string(LENGTH "${coreCppReadParameters}" parametersLength)

foreach(file IN LISTS netFiles)
    file(READ "${file}" text)
    string(REGEX REPLACE "//[^\n]*" "" text "${text}")
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" text "${text}")
    file(RELATIVE_PATH shownPath "${ROOT}" "${file}")

    set(cursor 0)
    while(TRUE)
        string(SUBSTRING "${text}" ${cursor} -1 rest)
        string(FIND "${rest}" "${coreCppReadParameters}" signatureAt)
        if(signatureAt EQUAL -1)
            break()
        endif()
        math(EXPR afterSignature "${signatureAt} + ${parametersLength}")

        # Which function this is, for the report and to tell the two spellings apart: the text on
        # the signature's own line in front of it. `Foo::` is out of line; an unqualified `read`
        # is one declared in a class body. Anything else ending in `read` (`readWithFd`, a member
        # CALL such as `_inner.read(buffer)`) is not a definition of this member at all.
        math(EXPR windowStart "${signatureAt} - 160")
        if(windowStart LESS 0)
            set(windowStart 0)
        endif()
        math(EXPR windowLength "${signatureAt} - ${windowStart}")
        string(SUBSTRING "${rest}" ${windowStart} ${windowLength} window)
        set(owner "")
        if(window MATCHES "([A-Za-z_][A-Za-z0-9_]*)::$")
            set(owner "${CMAKE_MATCH_1}")
        elseif(window MATCHES "(^|\n)[ \t]*(\\[\\[nodiscard\\]\\][ \t]*)?IoAwaitable[ \t]+$")
            set(owner "<in-class>")
        endif()
        if(owner STREQUAL "")
            math(EXPR cursor "${cursor} + ${afterSignature}")
            continue()
        endif()

        # A DECLARATION reaches `;` before `{` and has no body to guard.
        string(SUBSTRING "${rest}" ${afterSignature} -1 tail)
        string(FIND "${tail}" "{" braceAt)
        string(FIND "${tail}" ";" semicolonAt)
        if(braceAt EQUAL -1)
            break()
        endif()
        if(NOT semicolonAt EQUAL -1 AND semicolonAt LESS braceAt)
            math(EXPR cursor "${cursor} + ${afterSignature}")
            continue()
        endif()

        # The body, by counting braces from the opening one. Offsets are positions in `rest`.
        math(EXPR bodyStart "${afterSignature} + ${braceAt} + 1")
        string(SUBSTRING "${rest}" ${bodyStart} -1 bodyRest)
        set(depth 1)
        set(scan 0)
        set(bodyEnd -1)
        while(depth GREATER 0)
            string(SUBSTRING "${bodyRest}" ${scan} -1 probe)
            string(FIND "${probe}" "{" openAt)
            string(FIND "${probe}" "}" closeAt)
            if(closeAt EQUAL -1)
                break()
            endif()
            if(NOT openAt EQUAL -1 AND openAt LESS closeAt)
                math(EXPR depth "${depth} + 1")
                math(EXPR scan "${scan} + ${openAt} + 1")
            else()
                math(EXPR depth "${depth} - 1")
                math(EXPR bodyEnd "${scan} + ${closeAt}")
                math(EXPR scan "${scan} + ${closeAt} + 1")
            endif()
        endwhile()
        if(NOT depth EQUAL 0)
            list(APPEND violations
                 "${shownPath}: a read(std::span<std::byte> buffer) body has no matching '}', so this check cannot read it")
            break()
        endif()
        string(SUBSTRING "${bodyRest}" 0 ${bodyEnd} body)

        set(name "${owner}::read")
        if(owner STREQUAL "<in-class>")
            set(name "an in-class read")
        endif()

        string(FIND "${body}" "${coreCppReadGuardCall}" guardAt)
        # `return` rather than `return ` or `co_return`: the substring is in both, and what is
        # wanted is the FIRST way out of the body whatever it is called.
        string(FIND "${body}" "return" returnAt)
        if(guardAt EQUAL -1)
            if(body MATCHES "return[ \t\n]+[A-Za-z0-9_:>.\\-]*read\\(buffer\\)[ \t\n]*;")
                math(EXPR delegateCount "${delegateCount} + 1")
            else()
                list(APPEND violations
                     "${shownPath}: ${name} does not call ${coreCppReadGuardCall} and does not delegate to another socket's read(buffer), so an empty span is answered as EOF there")
            endif()
        elseif(NOT returnAt EQUAL -1 AND guardAt GREATER returnAt)
            list(APPEND violations
                 "${shownPath}: ${name} calls ${coreCppReadGuardCall} only AFTER its first return, so every early exit skips it")
        else()
            math(EXPR guardedCount "${guardedCount} + 1")
        endif()

        # `bodyStart` is already a position in `rest`, so this is one term. Upstream measured the
        # two-term version skipping every second definition in a file.
        math(EXPR cursor "${cursor} + ${bodyStart}")
    endwhile()
endforeach()

math(EXPR definitionCount "${guardedCount} + ${delegateCount}")
list(LENGTH violations violationCount)
math(EXPR definitionCount "${definitionCount} + ${violationCount}")
if(definitionCount EQUAL 0)
    message(FATAL_ERROR
        "check-read-buffer-guard: read ${scannedCount} file(s) under '${netRoot}' and found NO definition of "
        "'${coreCppReadParameters}'. Every transport would 'comply' from here on. The signature has moved or "
        "been reformatted; fix the pattern in ${CMAKE_CURRENT_LIST_FILE}.")
endif()

if(violationCount GREATER 0)
    set(rendered "")
    foreach(violation IN LISTS violations)
        string(APPEND rendered "\n  - ${violation}")
    endforeach()
    message(FATAL_ERROR
        "check-read-buffer-guard: ${violationCount} of ${definitionCount} ISocket::read definition(s) do not "
        "refuse an empty buffer:${rendered}\n\n"
        "Every transport calls contract::requireReadBuffer(buffer) before its first return. `0` on this "
        "interface means the peer has finished sending, so a zero-length read hands the caller a graceful "
        "close that never happened -- see core/net/ISocket.hpp.")
endif()

message(STATUS
    "check-read-buffer-guard: ${definitionCount} ISocket::read definition(s) across ${scannedCount} file(s): "
    "${guardedCount} guard before their first return, ${delegateCount} delegate to another socket's read")
