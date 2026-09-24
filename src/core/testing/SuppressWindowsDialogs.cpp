// SPDX-License-Identifier: Apache-2.0
#include <core/testing/SuppressWindowsDialogs.hpp>

#ifdef _WIN32
    #include <cstdint>
    #include <cstdlib>

    #include <Windows.h>
    #include <crtdbg.h>
    #include <werapi.h>
#endif

namespace core::testing
{

#ifdef _WIN32
namespace
{
    /// Asks Windows Error Reporting to show no UI for a fault in this process, where the OS has the
    /// function. Looked up rather than linked: `WerSetFlags` lives in kernel32 on every Windows this
    /// builds for, and a link against wer.lib would be a dependency of every consumer's test binary
    /// for one call. Per process, and it needs no privilege.
    void suppressErrorReportingUi() noexcept
    {
        auto* const kernel = GetModuleHandleW(L"kernel32.dll");
        if (kernel == nullptr)
            return;
        auto* const found = GetProcAddress(kernel, "WerSetFlags");
        if (found == nullptr)
            return;
        using SetFlags = HRESULT(WINAPI*)(DWORD);
        // Through `void (*)()`, the one function pointer type a cast from FARPROC is not warned
        // about as a signature mismatch.
        auto const setFlags = reinterpret_cast<SetFlags>(reinterpret_cast<void (*)()>(found));
        static_cast<void>(setFlags(WER_FAULT_REPORTING_NO_UI));
    }
} // namespace
#endif

void suppressWindowsDialogs() noexcept
{
#ifdef _WIN32
    // Spelled out rather than looped: in a Release CRT these are macros that expand
    // to a constant, and a loop variable they drop would be an unused variable.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);

    // Only the fault report is turned off. The abort message stays: with the report mode above it
    // goes to stderr, not to a dialog, and clearing `_WRITE_ABORT_MSG` as well made abort() silent --
    // a test that died left nothing to say how.
    _set_abort_behavior(0, _CALL_REPORTFAULT);

    _set_invalid_parameter_handler([]([[maybe_unused]] wchar_t const* expression,
                                      [[maybe_unused]] wchar_t const* function,
                                      [[maybe_unused]] wchar_t const* file,
                                      [[maybe_unused]] unsigned int line,
                                      [[maybe_unused]] std::uintptr_t reserved) {});

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // An unhandled structured exception in a process whose error mode something later reset would
    // still reach Windows Error Reporting's dialog; this asks WER itself for no UI.
    suppressErrorReportingUi();
#endif
}

} // namespace core::testing
