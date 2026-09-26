// SPDX-License-Identifier: Apache-2.0
//
// Indexes a std::vector out of range, and must be seen to be STOPPED there by the MSVC Debug
// runtime's own check: `vector subscript out of range`, which `_ITERATOR_DEBUG_LEVEL=2` compiles into
// operator[] (core-cpp#11).
//
// The cl-debug and clangcl-debug legs exist for that runtime: it traps an invalidated iterator or an
// out-of-range index where it happens. Nothing in core-cpp states the level. It follows from
// `_DEBUG`, the runtime library and the build type, and any of those moving removes every such check
// while the leg stays green. This is the statement: tests/CMakeLists.txt registers it in every MSVC-
// driver Debug build and accepts it only on the runtime's diagnostic (PASS_REGULAR_EXPRESSION), never
// on an exit code, and refuses the line this program prints if the index RETURNED.
//
// The Debug runtime reports the failure through _CrtDbgReport and then ends the process with
// __fastfail, which ctest scores as a crash that no regular expression can override (measured:
// exit 0xc0000409). So a report hook, installed below, is handed the runtime's report first: it
// writes the report's own text to stderr and exits with a plain failure status, before the fast
// fail. core::testing_dialogs is linked as well, so nothing here can open a dialog either way.
// fastcached's `iterator-debug-canary` is the same remedy (fastcached#315).

#include <cstdio>
#include <cstdlib>
#include <print>
#include <vector>

#if defined(_MSC_VER)
    #include <crtdbg.h>
#endif

namespace
{

#if defined(_MSC_VER) && defined(_DEBUG)
/// Receives the Debug runtime's report of a failed check, passes its text on, and ends the process.
/// @param reportType Which kind of report it is.
/// @param message The runtime's own text.
/// @return Never.
int stopAtTheCheck(int reportType, char* message, int* /*returnValue*/)
{
    std::println(stderr, "iterator-debug-canary: the runtime reported ({}): {}", reportType, message);
    std::fflush(stderr);
    std::_Exit(1);
}
#endif

} // namespace

int main(int argc, char* /*argv*/[])
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &stopAtTheCheck);
#endif
    std::println(stderr,
                 "iterator-debug-canary: _ITERATOR_DEBUG_LEVEL is {}",
#if defined(_ITERATOR_DEBUG_LEVEL)
                 _ITERATOR_DEBUG_LEVEL
#else
                 -1
#endif
    );

    auto values = std::vector<int> { 42 };
    // The index is not a constant, so no compiler can see it is out of range and refuse to build.
    auto const index = static_cast<std::size_t>(argc) + 1;
    std::println(stderr, "iterator-debug-canary: about to read index {} of {}", index, values.size());
    std::fflush(stderr);
    auto const value = values[index];

    std::println(stderr, "iterator-debug-canary: the out-of-range index returned {}", value);
    return 0;
}
