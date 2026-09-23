# SPDX-License-Identifier: Apache-2.0
#
# Proves tests/cmake/check-read-buffer-guard.cmake: it must be SEEN to refuse each thing it claims,
# and seen to accept a compliant tree -- a check that refused everything would pass every refusal.
#
#   compliant      a guarded out-of-line read passes, and the report counts it
#   missing        an unguarded one is refused, naming the transport
#   late           a guard AFTER the body's first return is refused as MISPLACED, not as missing
#   commented      a guard that is only a comment is refused
#   inclass        an unguarded read defined inside its class body -- `TlsSocket`'s shape -- is
#                  refused: the out-of-line spelling alone would never have seen it
#   delegate       a decorator returning another socket's read(buffer) passes, counted apart
#   notdelegate    an in-class read returning anything else without the guard is refused
#   twoinone       two definitions in one file, the SECOND unguarded, with padding ahead of the
#                  first: upstream's cursor once over-advanced past exactly this and passed it
#   subdirectory   an unguarded transport under `windows/` is refused -- the walk recurses
#   testsexcluded  an unguarded fake in `*_test.cpp` or `*Canary.cpp` is not a transport
#   nofiles        an empty `net/` is the CHECK being broken
#   nosignature    files present, no definition matched: a DIFFERENT empty set
#
# Usage: cmake -DCHECKER=<path to check-read-buffer-guard.cmake> -DWORK_DIR=<scratch directory>
#              -P tests/cmake/check-read-buffer-guard-selftest.cmake
#
# The verdict is `CMake Error` in the output, never the exit code alone.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CHECKER OR NOT EXISTS "${CHECKER}")
    message(FATAL_ERROR "check-read-buffer-guard-selftest: CHECKER ('${CHECKER}') is not set or does not exist.")
endif()
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    message(FATAL_ERROR "check-read-buffer-guard-selftest: WORK_DIR is not set.")
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

## @brief Runs the check over @p tree and requires it refused naming @p phrase, or accepted when
##        @p phrase is ACCEPT; the flattened output goes to `lastOutput` for a follow-up assertion.
function(core_cpp_expect name tree phrase)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${tree}" -P "${CHECKER}"
        OUTPUT_VARIABLE captured ERROR_VARIABLE capturedErrors RESULT_VARIABLE ignored)
    set(output "${captured}${capturedErrors}")
    # Flattened: CMake wraps its diagnostics, so a phrase can straddle two lines of it.
    string(REGEX REPLACE "[\r\n]+" " " output "${output}")
    string(REGEX REPLACE " +" " " output "${output}")
    set(lastOutput "${output}" PARENT_SCOPE)
    set(objected FALSE)
    if(output MATCHES "CMake Error|CMake Warning")
        set(objected TRUE)
    endif()
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

set(guarded "IoAwaitable ExampleSocket::read(std::span<std::byte> buffer)
{
    contract::requireReadBuffer(buffer);
    if (_closed)
        return IoAwaitable { std::unexpected(closedSocket()) };
    return IoAwaitable { readTask(buffer) };
}
")
string(REPLACE "    contract::requireReadBuffer(buffer);\n" "" unguarded "${guarded}")

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(compliant tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.cpp" "${guarded}")
core_cpp_expect(compliant "${tree}" ACCEPT)
if(NOT lastOutput MATCHES "1 ISocket::read definition\\(s\\) across 1 file\\(s\\): 1 guard")
    list(APPEND failures "compliant: passed without counting the one definition it read: ${lastOutput}")
endif()

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(missing tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.cpp" "${unguarded}")
core_cpp_expect(missing "${tree}" "src/core/net/ExampleSocket.cpp: ExampleSocket::read does not call")

math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "    contract::requireReadBuffer(buffer);\n    if (_closed)\n        return IoAwaitable { std::unexpected(closedSocket()) };\n"
               "    if (_closed)\n        return IoAwaitable { std::unexpected(closedSocket()) };\n    contract::requireReadBuffer(buffer);\n"
               late "${guarded}")
core_cpp_make_tree(late tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.cpp" "${late}")
core_cpp_expect(late "${tree}" "only AFTER its first return")

math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "    contract::requireReadBuffer(buffer);" "    // contract::requireReadBuffer(buffer);" commented "${guarded}")
core_cpp_make_tree(commented tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.cpp" "${commented}")
core_cpp_expect(commented "${tree}" "ExampleSocket::read does not call")

set(inClassGuarded "namespace
{
    class TlsLikeSocket final: public ISocket
    {
      public:
        [[nodiscard]] IoAwaitable read(std::span<std::byte> buffer) override
        {
            contract::requireReadBuffer(buffer);
            return IoAwaitable { readPlain(buffer) };
        }
    };
} // namespace
")
math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "            contract::requireReadBuffer(buffer);\n" "" inClass "${inClassGuarded}")
core_cpp_make_tree(inclass tree)
file(WRITE "${tree}/src/core/net/TlsLike.cpp" "${inClass}")
core_cpp_expect(inclass "${tree}" "src/core/net/TlsLike.cpp: an in-class read does not call")

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(delegate tree)
file(WRITE "${tree}/src/core/net/Decorator.hpp" "class Decorator: public ISocket
{
  public:
    [[nodiscard]] IoAwaitable read(std::span<std::byte> buffer) override { return _inner.read(buffer); }
    [[nodiscard]] IoAwaitable write(std::span<std::byte const> buffer) override { return _inner.write(buffer); }
};
")
file(WRITE "${tree}/src/core/net/TlsLike.cpp" "${inClassGuarded}")
core_cpp_expect(delegate "${tree}" ACCEPT)
if(NOT lastOutput MATCHES "2 ISocket::read definition\\(s\\) across 2 file\\(s\\): 1 guard before their first return, 1 delegate")
    list(APPEND failures "delegate: accepted without counting one guarded and one delegate: ${lastOutput}")
endif()

math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "            contract::requireReadBuffer(buffer);\n            return IoAwaitable { readPlain(buffer) };"
               "            return IoAwaitable { readPlain(buffer) };" notDelegate "${inClassGuarded}")
core_cpp_make_tree(notdelegate tree)
file(WRITE "${tree}/src/core/net/TlsLike.cpp" "${notDelegate}")
core_cpp_expect(notdelegate "${tree}" "does not delegate to another socket")

math(EXPR caseCount "${caseCount} + 1")
set(padded "")
foreach(index RANGE 1 200)
    string(APPEND padded "// filler line ${index}, to push the first definition well into the file\n")
endforeach()
string(REPLACE "ExampleSocket" "AlphaSocket" alpha "${guarded}")
string(REPLACE "ExampleSocket" "BetaSocket" beta "${unguarded}")
string(APPEND padded "\n${alpha}\n${beta}")
core_cpp_make_tree(twoinone tree)
file(WRITE "${tree}/src/core/net/TwoSockets.cpp" "${padded}")
core_cpp_expect(twoinone "${tree}" "BetaSocket::read does not call")
if(NOT lastOutput MATCHES "1 of 2 ISocket::read definition")
    list(APPEND failures "twoinone: refused without having SEEN both definitions: ${lastOutput}")
endif()

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(subdirectory tree)
file(WRITE "${tree}/src/core/net/windows/ExampleSocket.cpp" "${unguarded}")
core_cpp_expect(subdirectory "${tree}" "src/core/net/windows/ExampleSocket.cpp: ExampleSocket::read does not call")

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(testsexcluded tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.cpp" "${guarded}")
file(WRITE "${tree}/src/core/net/ExampleSocket_test.cpp" "${unguarded}")
file(WRITE "${tree}/src/core/net/windows/ExampleCanary.cpp" "${unguarded}")
core_cpp_expect(testsexcluded "${tree}" ACCEPT)

math(EXPR caseCount "${caseCount} + 1")
core_cpp_make_tree(nofiles tree)
core_cpp_expect(nofiles "${tree}" "found no non-test file at all")

math(EXPR caseCount "${caseCount} + 1")
string(REPLACE "::read(" "::readSome(" renamed "${guarded}")
core_cpp_make_tree(nosignature tree)
file(WRITE "${tree}/src/core/net/ExampleSocket.cpp" "${renamed}")
core_cpp_expect(nosignature "${tree}" "found NO definition")

list(LENGTH failures failureCount)
if(failureCount GREATER 0)
    set(rendered "")
    foreach(failure IN LISTS failures)
        string(APPEND rendered "\n  - ${failure}")
    endforeach()
    message(FATAL_ERROR
        "check-read-buffer-guard-selftest: ${caseCount} case(s) ran, ${failureCount} failed:${rendered}")
endif()
message(STATUS "check-read-buffer-guard-selftest: ${caseCount} case(s) ran, 0 failed")
