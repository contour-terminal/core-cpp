# SPDX-License-Identifier: Apache-2.0
#
# No OpenSSL type appears in any header, and no file includes an OpenSSL header except the units
# that implement the TLS layer.
#
# ## Why a check, when the build already links OpenSSL PRIVATE
#
# The design spec says of TLS: "No OpenSSL type appears in any header." Linking OpenSSL PRIVATE to
# core::net_tls keeps OpenSSL's headers from core-cpp's CONSUMERS -- but not from any file inside
# core-cpp, and not from a forward declaration, which needs no header at all. fastcached's TLS
# headers forward-declare `struct ssl_st` and `struct ssl_ctx_st` for exactly that reason, and a
# header that names one is a header whose ABI moves with OpenSSL's. So the requirement had nothing
# enforcing it; this is what does, and it landed in the task that created the surface it guards
# (Task B11), so there was never a violation to retrofit it onto.
#
# Its shape is fastcached's `scripts/check-crypto-seam.cmake` (0708dd54), which guards Monocypher
# rather than OpenSSL -- the seam differs, the discipline is the same: permitted units with reasons,
# every permitted row a positive control, and a scan that refuses to report a clean tree it did
# not read.
#
# ## The two rules
#
#   include   A `#include` or `#import` naming a file under `openssl/`, in any file, is refused
#             unless that file is a permitted unit below. Matched case-insensitively: a
#             case-insensitive filesystem resolves <OpenSSL/SSL.h> to the same file.
#   type      A HEADER naming an OpenSSL type -- `SSL`, `SSL_CTX`, `BIO`, `X509`, `EVP_PKEY` and
#             their kin, or a struct tag such as `ssl_st` -- is refused. No header is permitted:
#             the rule is the spec's, and it has no exceptions.
#
# Both read CODE: comments and string literals are removed first, so prose about SSL and a
# diagnostic string naming `SSL_read` are not violations. A type name that is part of a longer
# identifier (`isSSLEnabled`, `ssl_state`) is not one either.
#
# ## The permitted rows are the positive control
#
# Each must EXIST, be ENUMERATED by the walk, and be SEEN including an OpenSSL header. A row that
# has stopped doing any of the three is refused as stale: a moved TLS unit, a pattern that no
# longer matches the spelling it exists for, or a walk that skipped `src/` all look like that, and
# each would otherwise report a clean tree over files it never read.
#
# ## Blind spots, stated
#
# - A header reached through a macro (`#include OPENSSL_HEADER`) is not seen. None exists.
# - A raw string literal (R"(...)") is not recognised as one; a type name inside it would be
#   refused. None exists.
# - A `//` inside a string literal on the same line as code ends that line's scan early; a type
#   named after it on that line is missed.
# - This guards WHO may reach OpenSSL, not whether the TLS layer uses it correctly. That is what
#   the TLS tests and their strict peer are for.
#
# Runs as `cmake -P`; the verdict is `CMake Error` in the output, never the exit code alone.
#
# Usage: cmake -DROOT=<source tree> -P tests/cmake/check-openssl-seam.cmake

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT OR ROOT STREQUAL "")
    get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# The files permitted past the seam, each with the reason it is one of them: `<path>|<reason>`.
# Overridable only so the self-test can stage a tree of its own.
if(NOT DEFINED PERMITTED)
    set(PERMITTED
        "src/core/net/Tls.cpp|the TLS layer itself: every OpenSSL call core::net_tls makes"
        "src/core/net/testing/StrictTlsPeer.cpp|the TLS tests' oracle: OpenSSL driven by hand, which is what makes it independent of the code under test")
endif()

set(permittedPaths "")
foreach(row IN LISTS PERMITTED)
    string(FIND "${row}" "|" bar)
    if(bar EQUAL -1)
        message(FATAL_ERROR "openssl-seam: the permitted row '${row}' has no '|<reason>'.")
    endif()
    string(SUBSTRING "${row}" 0 ${bar} path)
    math(EXPR reasonAt "${bar} + 1")
    string(SUBSTRING "${row}" ${reasonAt} -1 reason)
    string(STRIP "${reason}" reason)
    if(reason STREQUAL "")
        message(FATAL_ERROR
            "openssl-seam: the permitted row for ${path} gives no reason. A file allowed past the seam "
            "is a decision, and without its reason it reads exactly like one somebody forgot.")
    endif()
    list(APPEND permittedPaths "${path}")
    set("seenInclude_${path}" FALSE)
endforeach()

# First-party C++, by directory walk: every root that holds any, and nothing under a build tree.
set(scanRoots src tests examples tools)
set(sourceFiles "")
foreach(scanRoot IN LISTS scanRoots)
    if(IS_DIRECTORY "${ROOT}/${scanRoot}")
        file(GLOB_RECURSE found RELATIVE "${ROOT}"
            "${ROOT}/${scanRoot}/*.hpp" "${ROOT}/${scanRoot}/*.h" "${ROOT}/${scanRoot}/*.hh"
            "${ROOT}/${scanRoot}/*.hxx" "${ROOT}/${scanRoot}/*.ipp" "${ROOT}/${scanRoot}/*.inl"
            "${ROOT}/${scanRoot}/*.cpp" "${ROOT}/${scanRoot}/*.cc" "${ROOT}/${scanRoot}/*.cxx"
            "${ROOT}/${scanRoot}/*.cppm")
        list(APPEND sourceFiles ${found})
    endif()
endforeach()
list(SORT sourceFiles)
if(NOT sourceFiles)
    message(FATAL_ERROR
        "openssl-seam: the walk under ${ROOT} (${scanRoots}) found no C++ at all. That is not a clean "
        "tree but a scan that stopped working -- a ROOT pointing elsewhere, or a moved source root.")
endif()

# What an OpenSSL include looks like, over lowercased code with comments removed.
set(includePattern "(^|\n)[ \t]*#[ \t]*(include|import)[ \t]*[<\"]openssl/")

# What an OpenSSL type looks like in code: the typedefs a caller spells, and the struct tags a
# forward declaration spells. Bounded by non-identifier characters on both sides.
set(typeNames
    "SSL" "SSL_CTX" "SSL_METHOD" "SSL_SESSION" "SSL_CIPHER" "BIO" "BIO_METHOD" "X509" "X509_STORE"
    "X509_STORE_CTX" "X509_NAME" "X509_EXTENSION" "X509_VERIFY_PARAM" "EVP_PKEY" "EVP_PKEY_CTX"
    "EVP_MD" "EVP_MD_CTX" "EVP_CIPHER" "EVP_CIPHER_CTX" "BIGNUM" "BN_CTX" "ASN1_INTEGER"
    "ASN1_STRING" "ASN1_OCTET_STRING" "ASN1_TIME" "OSSL_LIB_CTX" "ENGINE"
    "ssl_st" "ssl_ctx_st" "ssl_method_st" "ssl_session_st" "bio_st" "bio_method_st" "x509_st"
    "x509_store_st" "x509_name_st" "evp_pkey_st" "evp_pkey_ctx_st" "evp_md_st" "evp_cipher_st"
    "bignum_st" "asn1_string_st" "ossl_lib_ctx_st")
list(JOIN typeNames "|" typeAlternation)
set(typePattern "(^|[^A-Za-z0-9_])(${typeAlternation})([^A-Za-z0-9_]|$)")

## @brief Removes string and character literals and comments from @p text, keeping newlines, so a
## line number computed on the result is the line number in the file.
function(core_cpp_seam_code_only text outVar)
    # A quoted include names a file, not a string: spell it angled, so the literal pass below
    # leaves it for the include rule to read.
    string(REGEX REPLACE "(^|\n)([ \t]*#[ \t]*(include|import)[ \t]*)\"([^\"\n]*)\"" "\\1\\2<\\4>"
        text "${text}")
    # Literals first, so a "/*" or "//" inside one cannot open a comment. An escaped quote stays
    # inside its literal.
    string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" "\"\"" text "${text}")
    string(REGEX REPLACE "'([^'\\\\\n]|\\\\.)*'" "''" text "${text}")
    # Block comments, one at a time, keeping every newline they held.
    while(TRUE)
        string(FIND "${text}" "/*" open)
        if(open EQUAL -1)
            break()
        endif()
        string(SUBSTRING "${text}" 0 ${open} before)
        math(EXPR afterOpen "${open} + 2")
        string(SUBSTRING "${text}" ${afterOpen} -1 rest)
        string(FIND "${rest}" "*/" close)
        if(close EQUAL -1)
            set(comment "${rest}")
            set(after "")
        else()
            string(SUBSTRING "${rest}" 0 ${close} comment)
            math(EXPR afterClose "${close} + 2")
            string(SUBSTRING "${rest}" ${afterClose} -1 after)
        endif()
        string(REGEX REPLACE "[^\n]" "" kept "${comment}")
        set(text "${before} ${kept}${after}")
    endwhile()
    # Line comments, `///` included.
    string(REGEX REPLACE "//[^\n]*" "" text "${text}")
    set(${outVar} "${text}" PARENT_SCOPE)
endfunction()

## @brief Sets @p outVar to the 1-based line of offset @p at in @p text.
function(core_cpp_seam_line_of text at outVar)
    string(SUBSTRING "${text}" 0 ${at} before)
    string(REGEX MATCHALL "\n" newlines "${before}")
    list(LENGTH newlines count)
    math(EXPR line "${count} + 1")
    set(${outVar} ${line} PARENT_SCOPE)
endfunction()

set(violations "")
set(headerCount 0)
foreach(relative IN LISTS sourceFiles)
    file(READ "${ROOT}/${relative}" wholeFile)
    # A semicolon in the file would split every list operation below.
    string(REPLACE ";" " " wholeFile "${wholeFile}")
    set(isHeader FALSE)
    if(relative MATCHES "\\.(hpp|h|hh|hxx|ipp|inl)$")
        set(isHeader TRUE)
        math(EXPR headerCount "${headerCount} + 1")
    endif()
    string(TOLOWER "${wholeFile}" lowered)
    string(FIND "${lowered}" "openssl" mentionsOpenssl)
    if(mentionsOpenssl EQUAL -1 AND NOT isHeader)
        continue() # neither rule can fire: no OpenSSL include, and not a header
    endif()

    core_cpp_seam_code_only("${wholeFile}" code)

    if(NOT mentionsOpenssl EQUAL -1)
        string(TOLOWER "${code}" loweredCode)
        set(remaining "${loweredCode}")
        set(consumed 0)
        while(remaining MATCHES "${includePattern}")
            string(FIND "${remaining}" "${CMAKE_MATCH_0}" hitAt)
            math(EXPR absolute "${consumed} + ${hitAt} + 1")
            core_cpp_seam_line_of("${loweredCode}" ${absolute} line)
            if(relative IN_LIST permittedPaths)
                set("seenInclude_${relative}" TRUE)
            else()
                list(APPEND violations "${relative}:${line}: includes an OpenSSL header outside the seam")
            endif()
            string(LENGTH "${CMAKE_MATCH_0}" matchLength)
            math(EXPR skip "${hitAt} + ${matchLength}")
            string(SUBSTRING "${remaining}" ${skip} -1 remaining)
            math(EXPR consumed "${consumed} + ${skip}")
        endwhile()
    endif()

    if(isHeader AND code MATCHES "${typePattern}")
        set(typeName "${CMAKE_MATCH_2}")
        string(FIND "${code}" "${CMAKE_MATCH_0}" hitAt)
        core_cpp_seam_line_of("${code}" ${hitAt} line)
        if(CMAKE_MATCH_1 STREQUAL "\n")
            math(EXPR line "${line} + 1")
        endif()
        list(APPEND violations "${relative}:${line}: a header names the OpenSSL type '${typeName}'")
    endif()
endforeach()

set(stale "")
foreach(permittedPath IN LISTS permittedPaths)
    if(NOT EXISTS "${ROOT}/${permittedPath}")
        list(APPEND stale "${permittedPath}: does not exist")
    elseif(NOT permittedPath IN_LIST sourceFiles)
        list(APPEND stale "${permittedPath}: exists but the walk did not list it, so nothing says the rest of the tree was listed either")
    elseif(NOT seenInclude_${permittedPath})
        list(APPEND stale "${permittedPath}: was read and includes no OpenSSL header -- the row excuses nothing, or the pattern has stopped matching the spelling it exists to find")
    endif()
endforeach()

list(LENGTH sourceFiles fileCount)
list(LENGTH permittedPaths permittedCount)
if(violations OR stale)
    message("")
    foreach(violation IN LISTS violations)
        message("  ${violation}")
    endforeach()
    foreach(entry IN LISTS stale)
        message("  STALE permitted row -- ${entry}")
    endforeach()
    message("")
    message("OpenSSL is reached through <core/net/Tls.hpp> and <core/net/ITlsContext.hpp>, and only the")
    message("permitted units in tests/cmake/check-openssl-seam.cmake may include its headers. A header")
    message("names no OpenSSL type at all -- not even a forward-declared struct tag. For a violation: put")
    message("the OpenSSL code behind the seam and give the header a core-cpp type; adding a permitted row")
    message("is a new place that talks to OpenSSL, and needs a reason in the row.")
    message("")
    message("Scanned ${fileCount} first-party C++ file(s), ${headerCount} of them header(s), under ${ROOT}.")
    list(LENGTH violations violationCount)
    list(LENGTH stale staleCount)
    message(FATAL_ERROR "openssl-seam: ${violationCount} violation(s), ${staleCount} stale permitted row(s)")
endif()

message(STATUS
    "openssl-seam: ${fileCount} first-party C++ file(s), ${headerCount} header(s), none naming an OpenSSL type; "
    "${permittedCount} of ${permittedCount} permitted unit(s) seen including OpenSSL, none other does")
