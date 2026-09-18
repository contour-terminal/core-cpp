// SPDX-License-Identifier: Apache-2.0
#include <core/platform/SignalHandler.hpp>

#include <catch2/catch_test_macros.hpp>

using core::platform::SignalHandler;

TEST_CASE("SignalHandler records an interrupt until it is cleared", "[platform][signal]")
{
    SignalHandler::clearPendingSigint();
    CHECK_FALSE(SignalHandler::hasPendingSigint());

    SignalHandler::simulateSigint();
    CHECK(SignalHandler::hasPendingSigint());
    // Reading the flag does not consume it: a builtin polls it until the interrupt is handled.
    CHECK(SignalHandler::hasPendingSigint());

    SignalHandler::clearPendingSigint();
    CHECK_FALSE(SignalHandler::hasPendingSigint());
}

TEST_CASE("SignalHandler treats Ctrl+C and Ctrl+Break as interrupts, and nothing else", "[platform][signal]")
{
    // The Win32 console control types: CTRL_C_EVENT 0, CTRL_BREAK_EVENT 1, CTRL_CLOSE_EVENT 2,
    // CTRL_LOGOFF_EVENT 5, CTRL_SHUTDOWN_EVENT 6. A close must still reach the default handler,
    // which ends the process as the user asked.
    CHECK(SignalHandler::isInterruptCtrlEvent(0));
    CHECK(SignalHandler::isInterruptCtrlEvent(1));
    CHECK_FALSE(SignalHandler::isInterruptCtrlEvent(2));
    CHECK_FALSE(SignalHandler::isInterruptCtrlEvent(5));
    CHECK_FALSE(SignalHandler::isInterruptCtrlEvent(6));
}
