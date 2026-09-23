# SPDX-License-Identifier: Apache-2.0
#
# Proves tests/cmake/check-cancel-read-declared.cmake: it must be SEEN to refuse each thing it
# claims, and seen to ACCEPT a compliant tree -- a check that refused everything would pass every
# refusal below, and looks exactly like rigour.
#
#   compliant        a transport that declares `cancelRead` passes, and the report counts it
#   missing          one that declares none is refused, naming the file and the class
#   commented        a `cancelRead` only inside a `//` comment is refused
#   blockcommented   the `/* */` spelling of the same thing, which was its own hole upstream
#   neighbour        two transports in one header, the FIRST silent: the second's answer must not
#                    cover it, which is why each class has a bounded region
#   trailinghelper   a `cancelRead` CALL after the last class must not answer for it either
#   indented         a class indented inside a namespace, followed at column zero by code that
#                    names `cancelRead`: a column-zero boundary lets the region run on into it,
#                    which is the case core's `TlsSocket` (declared in Tls.cpp) is shaped like
#   insource         a silent transport declared in a `.cpp` is refused -- sources are read too
#   subdirectory     a silent transport under `windows/` is refused -- the walk recurses
#   testsexcluded    a silent fake in `*_test.cpp` or `*Canary.cpp` is not a transport
#   nofiles          an empty `net/` is the CHECK being broken, not the tree being clean
#   nobase           files present, nothing deriving from `ISocket`: a DIFFERENT empty set
#
# Usage: cmake -DCHECKER=<path to check-cancel-read-declared.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-cancel-read-declared-selftest.cmake
#
# The verdict is `CMake Error` in the output, never the exit code alone.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CHECKER OR NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "check-cancel-read-declared-selftest: CHECKER ('${CHECKER}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "check-cancel-read-declared-selftest: WORK_DIR is not set.")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
set(failures "")
set(caseCount 0)

## @brief Makes an empty tree under WORK_DIR/<name>, with `src/core/net/` present. Files are written
##        into it by the case itself: C++ is full of `;`, which a CMake argument list would split.
## @param name   The case's directory.
## @param outVar Receives the tree root.
function(core_cpp_make_tree name outVar)
    set(tree "${WORK_DIR}/${name}")
    file(MAKE_DIRECTORY "${tree}/src/core/net")
    set(${outVar} "${tree}" PARENT_SCOPE)
endfunction()

## @brief Runs the check over @p tree.
## @param outObjected TRUE when it printed `CMake Error` or `CMake Warning`.
## @param outOutput   Everything it printed, FLATTENED: CMake wraps its diagnostics, so a phrase can
##                    exist in the output and in no single line of it.
function(core_cpp_run_check tree outObjected outOutput)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${tree}" -P "${CHECKER}"
        OUTPUT_VARIABLE captured ERROR_VARIABLE capturedErrors RESULT_VARIABLE ignored)
    set(combined "${captured}${capturedErrors}")
    string(REGEX REPLACE "[\r\n]+" " " combined "${combined}")
    string(REGEX REPLACE " +" " " combined "${combined}")
    set(sawSignal FALSE)
    if(combined MATCHES "CMake Error|CMake Warning")
        set(sawSignal TRUE)
    endif()
    set(${outObjected} ${sawSignal} PARENT_SCOPE)
    set(${outOutput} "${combined}" PARENT_SCOPE)
endfunction()

## @brief Requires the run over @p tree to be refused naming @p phrase, or accepted when @p phrase
##        is ACCEPT, and records a failure under @p name otherwise.
function(core_cpp_expect name tree phrase)
    core_cpp_run_check("${tree}" objected output)
    if(phrase STREQUAL "ACCEPT")
        if(objected)
            set(failures ${failures} "${name}: refused a compliant tree: ${output}" PARENT_SCOPE)
        endif()
        return()
    endif()
    if(NOT objected)
        set(failures ${failures} "${name}: was NOT refused" PARENT_SCOPE)
        return()
    endif()
    string(FIND "${output}" "${phrase}" at)
    if(at EQUAL -1)
        set(failures ${failures} "${name}: refused, but not naming '${phrase}': ${output}" PARENT_SCOPE)
    endif()
endfunction()

set(compliant "#pragma once
namespace core::net
{

class ExampleSocket final: public ISocket
{
  public:
    IoAwaitable read(std::span<std::byte> buffer) override;
    void cancelRead() noexcept override;
    void close() noexcept override;
};

} // namespace core::net
")
string(REPLACE "    void cancelRead() noexcept override;\n" "" silent "${compliant}")

# compliant, with the count asserted: a shrinking set must be visible.
math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(compliant tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.hpp" "${compliant}")
core_cpp_run_check("${tree}" objected output)
if(objected)
    list(APPEND failures "compliant: refused a transport that declares cancelRead: ${output}")
elseif(NOT output MATCHES "1 transport\\(s\\) across 1 file\\(s\\)")
    list(APPEND failures "compliant: passed without reporting the one transport it read: ${output}")
endif()

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(missing tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.hpp" "${silent}")
core_cpp_expect(missing "${tree}" "src/core/net/ExampleSocket.hpp: ExampleSocket derives from ISocket")

math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "    void cancelRead() noexcept override;" "    // cancelRead is inherited on purpose, honest"
               commented "${compliant}")
core_cpp_make_tree(commented tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.hpp" "${commented}")
core_cpp_expect(commented "${tree}" "ExampleSocket derives from ISocket")

math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "    void cancelRead() noexcept override;" "    /** cancelRead is inherited on purpose, honest */"
               blockCommented "${compliant}")
core_cpp_make_tree(blockcommented tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.hpp" "${blockCommented}")
core_cpp_expect(blockcommented "${tree}" "ExampleSocket derives from ISocket")

math(EXPR caseCount "${caseCount} + 1")
set(neighbour "#pragma once
namespace core::net
{

class SilentSocket final: public ISocket
{
  public:
    void close() noexcept override;
};

class AnsweringSocket final: public ISocket
{
  public:
    void cancelRead() noexcept override;
    void close() noexcept override;
};

} // namespace core::net
")
core_cpp_make_tree(neighbour tree)
file(WRITE "${tree}/src/core/net/TwoSockets.hpp" "${neighbour}")
core_cpp_run_check("${tree}" objected output)
if(NOT objected)
    list(APPEND failures "neighbour: the SECOND transport's cancelRead covered the FIRST one's silence")
elseif(NOT output MATCHES "SilentSocket derives from ISocket" OR output MATCHES "AnsweringSocket derives")
    list(APPEND failures "neighbour: refused, but not SilentSocket alone: ${output}")
endif()

math(EXPR caseCount "${caseCount} + 1")
set(trailing "#pragma once
namespace core::net
{

class SilentSocket final: public ISocket
{
  public:
    void close() noexcept override;
};

inline void retireBoth(ISocket& a, ISocket& b) noexcept
{
    a.cancelRead();
    b.cancelRead();
}

} // namespace core::net
")
core_cpp_make_tree(trailinghelper tree)
file(WRITE "${tree}/src/core/net/SilentSocket.hpp" "${trailing}")
core_cpp_expect(trailinghelper "${tree}" "SilentSocket derives from ISocket")

math(EXPR caseCount "${caseCount} + 1")
set(indented "#include <core/net/ISocket.hpp>

namespace core::net
{
namespace
{
    class TlsLikeSocket final: public ISocket
    {
      public:
        void close() noexcept override;
    };
} // namespace

void drain(ISocket& socket) noexcept
{
    socket.cancelRead();
}

} // namespace core::net
")
core_cpp_make_tree(indented tree)
file(WRITE "${tree}/src/core/net/TlsLike.cpp" "${indented}")
core_cpp_expect(indented "${tree}" "TlsLikeSocket derives from ISocket")

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(insource tree)
file(WRITE "${tree}/src/core/net/Transport.cpp" "${silent}")
core_cpp_expect(insource "${tree}" "src/core/net/Transport.cpp: ExampleSocket derives")

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(subdirectory tree)
file(WRITE "${tree}/src/core/net/windows/ExampleSocket.hpp" "${silent}")
core_cpp_expect(subdirectory "${tree}" "src/core/net/windows/ExampleSocket.hpp: ExampleSocket derives")

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(testsexcluded tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.hpp" "${compliant}")
file(WRITE "${tree}/src/core/net/ExampleSocket_test.cpp" "${silent}")
file(WRITE "${tree}/src/core/net/windows/ExampleCanary.cpp" "${silent}")
core_cpp_expect(testsexcluded "${tree}" ACCEPT)

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(nofiles tree)
core_cpp_expect(nofiles "${tree}" "found no non-test file at all")

math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "public ISocket" "public ISomethingElse" noBase "${compliant}")
core_cpp_make_tree(nobase tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.hpp" "${noBase}")
core_cpp_expect(nobase "${tree}" "found no class deriving")

# The count is printed whether or not anything failed: a self-test that stops early must not look
# like one that judged something.
list(LENGTH failures failureCount)
if(failureCount GREATER 0)
    set(rendered "")
    foreach(failure IN LISTS failures)
        string(APPEND rendered "\n  - ${failure}")
    endforeach()
    message(FATAL_ERROR
        "check-cancel-read-declared-selftest: ${caseCount} case(s) ran, ${failureCount} failed:${rendered}")
endif()
message(STATUS "check-cancel-read-declared-selftest: ${caseCount} case(s) ran, 0 failed")
