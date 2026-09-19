// SPDX-License-Identifier: Apache-2.0
#include <core/tui/Terminal.hpp>

#include <csignal>

#include <unistd.h>

/// @file
/// The three @c Terminal members that differ between the platforms, on POSIX: a size change
/// arrives as SIGWINCH, so initialize() installs a handler that pokes the input's self-pipe and
/// shutdown() puts the previous one back. Everything else is in the shared `Terminal.cpp`.

namespace core::tui
{

namespace
{
    /// The input SIGWINCH pokes, and the handler it displaced. A signal handler reaches its
    /// subject only through file-scope state, so only one Terminal may be initialized at a time.
    TerminalInput* activeInput = nullptr;
    struct sigaction previousSigwinch {};

    void sigwinchHandler(int /*sig*/)
    {
        if (activeInput != nullptr)
            activeInput->notifyResize(0, 0); // Actual dimensions are queried in poll()
    }

    /// Whether a DECRQM answer makes the mode usable: recognized and changeable (set or reset).
    /// A permanent state or anything that is not an answer reads as unusable, as it did when this
    /// was a bool.
    [[nodiscard]] constexpr auto isChangeable(DecModeStatus status) noexcept -> bool
    {
        return status == DecModeStatus::Set || status == DecModeStatus::Reset;
    }
} // namespace

Terminal::~Terminal()
{
    shutdown();
}

auto Terminal::initialize() -> VoidResult
{
    if (_initialized)
        return {};

    // Initialize output first (queries dimensions)
    if (auto result = _output->initialize(); !result)
        return result;

    // In mock mode, skip input initialization, SIGWINCH handler, and cell size query.
    if (_mockMode)
    {
        _initialized = true;
        return {};
    }

    // Initialize input (raw mode, protocols — ECHO off from here)
    if (auto result = _input.initialize(); !result)
        return result;

    // Detect capabilities that require query/response I/O (e.g., XTVERSION).
    // Must run after raw mode is enabled so response bytes aren't echoed.
    _output->detectCapabilities();

    // Install SIGWINCH handler
    activeInput = &_input;
    struct sigaction sa {};
    sa.sa_handler = sigwinchHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, &previousSigwinch);

    // Query cell pixel dimensions (best-effort, non-fatal; unanswered leaves them 0, "unknown")
    if (auto const cellSize = queryCellSize())
    {
        _cellPixelWidth = cellSize->first;
        _cellPixelHeight = cellSize->second;
    }

    // Detect HUD overlay support (DEC mode 2035, Contour terminal)
    _hudSupported = isChangeable(queryDecMode(2035));

    // Detect passive mouse tracking support (DEC mode 2029).
    // When supported, also enable any-motion tracking (mode 1003) for hover tooltips.
    // Non-supporting terminals silently ignored mode 2029 in enableProtocols(),
    // so we only add 1003 when the terminal actually recognized it.
    if (isChangeable(queryDecMode(2029)))
        _input.setAnyMotionTracking(true);

    // Wait for color scheme response so the first prompt renders with correct colors.
    // The original query from enableProtocols() may have been consumed by the raw
    // XTVERSION read in detectCapabilities(), so re-send it here.
    awaitColorScheme();

    _initialized = true;
    return {};
}

void Terminal::shutdown()
{
    if (!_initialized)
        return;

    if (!_mockMode)
    {
        // Restore previous SIGWINCH handler
        sigaction(SIGWINCH, &previousSigwinch, nullptr);
        activeInput = nullptr;

        _input.shutdown();
    }
    _initialized = false;
}

} // namespace core::tui
