# SPDX-License-Identifier: Apache-2.0
#
# Proves tests/cmake/check-openssl-seam.cmake: it must be SEEN to refuse an OpenSSL include past the
# seam and an OpenSSL type in a header, seen to stay QUIET over a healthy tree, and seen to refuse
# every way of not being able to conclude. The accepting cases are what make the refusing ones
# evidence: a check that refused every tree would pass every refusal here.
#
#   clean                          a healthy tree: the TLS unit includes OpenSSL, its header names
#                                  none of its types, a caller includes only the core header
#   quietMentions                  OpenSSL named in comments, strings and longer identifiers only
#   angled, quoted, upperCase,     an include past the seam in each spelling it can take, and in a
#   importDirective, fromHeader    header as well as a source
#   typeInHeader, forwardDecl      a header naming `SSL_CTX*`, and one forward-declaring `ssl_st`
#   staleNoInclude, staleMissing   a permitted row that includes nothing, or names no file
#   empty                          no C++ at all
#
# Usage: cmake -DCHECKER=<path to check-openssl-seam.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-openssl-seam-selftest.cmake
#
# The verdict is `CMake Error` in the output, never the exit code alone.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CHECKER OR NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "check-openssl-seam-selftest: CHECKER ('${CHECKER}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "check-openssl-seam-selftest: WORK_DIR is not set.")
endif()

set(permitted "src/core/net/Tls.cpp|the TLS layer")
set(tlsUnit "#include <core/net/Tls.hpp>\n#include <openssl/ssl.h>\nint f() { return 0\; }\n")
set(tlsHeader "#pragma once\n/// A TLS layer; no OpenSSL type crosses this header.\nnamespace core::net { class ITlsContext\; }\n")
set(caller "#include <core/net/Tls.hpp>\nint g() { return 1\; }\n")

set(caseCount 0)
set(failures "")

## @brief Stages a healthy tree, applies @p file = @p contents on top (NONE to change nothing, or
## DELETE:<path> to remove a file), runs the checker, and requires @p want: ACCEPT or the phrase
## the expected refusal carries.
function(core_cpp_seam_case name file contents want)
    set(root "${WORK_DIR}/${name}")
    file(REMOVE_RECURSE "${root}")
    if(NOT name STREQUAL "empty")
        file(WRITE "${root}/src/core/net/Tls.cpp" "${tlsUnit}")
        file(WRITE "${root}/src/core/net/Tls.hpp" "${tlsHeader}")
        file(WRITE "${root}/src/core/net/Caller.cpp" "${caller}")
    else()
        file(MAKE_DIRECTORY "${root}/src")
    endif()
    if(file MATCHES "^DELETE:(.*)$")
        file(REMOVE "${root}/${CMAKE_MATCH_1}")
    elseif(NOT file STREQUAL "NONE")
        file(WRITE "${root}/${file}" "${contents}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}" "-DPERMITTED=${permitted}" -P "${CHECKER}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    string(REGEX REPLACE "[ \t\r\n]+" " " said "${out} ${err}")

    set(problems "")
    if(want STREQUAL "ACCEPT")
        if(NOT rc EQUAL 0 OR said MATCHES "CMake Error")
            string(APPEND problems " refused a tree it must accept:${said}")
        elseif(NOT said MATCHES "1 of 1 permitted unit")
            string(APPEND problems " accepted without saying it saw the permitted unit:${said}")
        endif()
    else()
        if(rc EQUAL 0 AND NOT said MATCHES "CMake Error")
            string(APPEND problems " accepted a tree it must refuse:${said}")
        else()
            string(FIND "${said}" "${want}" at)
            if(at EQUAL -1)
                string(APPEND problems " refused, but not with '${want}':${said}")
            endif()
        endif()
    endif()

    math(EXPR count "${caseCount} + 1")
    set(caseCount ${count} PARENT_SCOPE)
    if(problems)
        set(failures "${failures}\n  ${name}:${problems}" PARENT_SCOPE)
    endif()
endfunction()

core_cpp_seam_case(clean NONE "" ACCEPT)
core_cpp_seam_case(quietMentions "src/core/net/Notes.hpp"
    "#pragma once\n// SSL_CTX and #include <openssl/ssl.h> in a comment.\n/* BIO* in a block\n   comment X509 */\nconstexpr char const* why = \"SSL_read failed: <openssl/err.h>\"\;\nbool isSSLEnabled = false\; int ssl_state = 0\; int BIOS = 1\;\n"
    ACCEPT)
core_cpp_seam_case(angled "src/core/net/Other.cpp" "#include <openssl/evp.h>\n"
    "src/core/net/Other.cpp:1: includes an OpenSSL header outside the seam")
core_cpp_seam_case(quoted "src/core/net/Other.cpp" "// first\n#include \"openssl/err.h\"\n"
    "src/core/net/Other.cpp:2: includes an OpenSSL header outside the seam")
core_cpp_seam_case(upperCase "src/core/net/Other.cpp" "#include <OpenSSL/SSL.h>\n"
    "includes an OpenSSL header outside the seam")
core_cpp_seam_case(importDirective "tests/Other.cpp" "  #  import <openssl/ssl.h>\n"
    "tests/Other.cpp:1: includes an OpenSSL header outside the seam")
core_cpp_seam_case(fromHeader "src/core/net/Other.hpp" "#pragma once\n#include <openssl/ssl.h>\n"
    "src/core/net/Other.hpp:2: includes an OpenSSL header outside the seam")
core_cpp_seam_case(typeInHeader "src/core/net/Tls.hpp"
    "#pragma once\n\nstruct Holder\n{\n    SSL_CTX* native()\; // the context\n}\;\n"
    "src/core/net/Tls.hpp:5: a header names the OpenSSL type 'SSL_CTX'")
core_cpp_seam_case(forwardDecl "src/core/net/Tls.hpp" "#pragma once\nextern \"C\" struct ssl_st\;\n"
    "src/core/net/Tls.hpp:2: a header names the OpenSSL type 'ssl_st'")
core_cpp_seam_case(staleNoInclude "src/core/net/Tls.cpp" "int f() { return 0\; }\n"
    "STALE permitted row -- src/core/net/Tls.cpp: was read and includes no OpenSSL header")
core_cpp_seam_case(staleMissing "DELETE:src/core/net/Tls.cpp" ""
    "STALE permitted row -- src/core/net/Tls.cpp: does not exist")
core_cpp_seam_case(empty NONE "" "found no C++ at all")

if(failures)
    message(FATAL_ERROR "check-openssl-seam-selftest: ${caseCount} case(s), these failed:${failures}")
endif()
message(STATUS "check-openssl-seam-selftest: all ${caseCount} case(s) passed")
