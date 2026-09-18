// SPDX-License-Identifier: Apache-2.0
#include <core/testing/SuppressWindowsDialogs.hpp>

#ifdef _WIN32
    #include <cstdint>
    #include <cstdlib>

    #include <Windows.h>
    #include <crtdbg.h>
#endif

namespace core::testing
{

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

    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

    _set_invalid_parameter_handler([]([[maybe_unused]] wchar_t const* expression,
                                      [[maybe_unused]] wchar_t const* function,
                                      [[maybe_unused]] wchar_t const* file,
                                      [[maybe_unused]] unsigned int line,
                                      [[maybe_unused]] std::uintptr_t reserved) {});

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
}

} // namespace core::testing
