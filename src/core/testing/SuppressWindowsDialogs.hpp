// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// Suppresses the Windows dialogs (CRT assert, abort, crash) that block an unattended run.
///
/// A failed assert(), an abort() or a crash in a Windows Debug build opens a modal dialog
/// and waits. Under ctest nobody is there to click it, so a test that should fail in a
/// second holds the run until its timeout instead. suppressWindowsDialogs() sends the
/// reports to stderr, where they stay visible, and lets the process exit.
///
/// core::testing_main calls it first thing in main(), and SuppressWindowsDialogsAtStartup.cpp
/// (the core::testing_dialogs object library) calls it during static initialisation, for an
/// executable whose main() is not core-cpp's.
///
/// This merges four copies that had drifted: contour's crispy and coro copies, endo's
/// testing copy and fastcached's WindowsErrorPopups.hpp. The union is kept: every CRT report
/// type, the abort() message box and fault report, the invalid-parameter handler, and the
/// OS error modes including SEM_NOOPENFILEERRORBOX.

#ifdef _WIN32
    #include <cstdint>
    #include <cstdlib>

    #include <Windows.h>
    #include <crtdbg.h>
#endif

namespace core::testing
{

/// @brief Suppresses every Windows dialog that can block an unattended run.
///
/// - CRT assert, error and warning reports go to stderr instead of a dialog.
/// - abort() shows no message box and asks Windows Error Reporting for nothing.
/// - An invalid argument to a CRT function returns an error instead of opening a dialog.
/// - General-protection faults, critical errors and open-file errors show no OS dialog.
///
/// A no-op on every other platform.
inline void suppressWindowsDialogs() noexcept
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
