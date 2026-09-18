// SPDX-License-Identifier: Apache-2.0
//
// Fails in one way that can raise a Windows dialog, chosen by its argument, and must be seen to EXIT
// rather than wait for a click. tests/CMakeLists.txt registers one run per way as a WILL_FAIL test with
// a timeout: a run still waiting when the timeout expires is waiting on a dialog nobody will click.
//
// It calls nothing to suppress anything. What it proves is that linking core::testing_dialogs installs
// the suppression in an executable whose main() never asked for it.
//
// Imported from endo (src/testing/WindowsDialogCanary.cpp at f774a210), without two modes. `probe` served
// endo's product-executable variant. `access-violation` ends in an exception status, which ctest reports
// as a failure however WILL_FAIL is set, and ctest's children inherit SEM_NOGPFAULTERRORBOX anyway (endo
// measured it), so under ctest it proved nothing about this suppression.

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string_view>

namespace
{

/// Exit status meaning "this way of failing is not compiled into this build". A Debug build on Windows
/// compiles every way, so there it is unused.
[[maybe_unused]] constexpr int NotExercised = 77;

/// Exit status meaning "the failure was handled and execution continued", distinct from any success.
constexpr int ContinuedAfterFailure = 3;

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::println(stderr, "usage: core-cpp-windows-dialog-canary assert|abort|invalid-parameter");
        return 2;
    }
    auto const mode = std::string_view { argv[1] };

    std::println(stderr, "windows-dialog-canary: failing by {}", mode);
    std::fflush(stderr);

    if (mode == "assert")
    {
#ifdef NDEBUG
        return NotExercised;
#else
        [[maybe_unused]] auto const canaryHolds = false;
        assert(canaryHolds && "windows-dialog-canary asserts on purpose");
        return ContinuedAfterFailure;
#endif
    }
    if (mode == "abort")
        std::abort();
    if (mode == "invalid-parameter")
    {
#ifdef _WIN32
        // A null destination is an invalid parameter to the CRT: a dialog in a Debug CRT, Watson in a
        // Release one, unless a handler was installed.
        char* volatile destination = nullptr;
        if (strcpy_s(destination, 1, "x") != 0)
            return ContinuedAfterFailure;
        return 0;
#else
        return NotExercised;
#endif
    }
    std::println(stderr, "windows-dialog-canary: unknown mode {}", mode);
    return 2;
}
