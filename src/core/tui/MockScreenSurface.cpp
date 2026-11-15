// SPDX-License-Identifier: Apache-2.0
#include <core/tui/MockScreenSurface.hpp>

namespace core::tui
{

MockScreenSurface::MockScreenSurface(): _output(std::make_unique<MockTerminalOutput>())
{
}

MockScreenSurface::MockScreenSurface(std::unique_ptr<MockTerminalOutput> output): _output(std::move(output))
{
}

auto MockScreenSurface::columns() const noexcept -> int
{
    return _output->columns();
}

auto MockScreenSurface::rows() const noexcept -> int
{
    return _output->rows();
}

auto MockScreenSurface::output() noexcept -> TerminalOutput&
{
    return *_output;
}

auto MockScreenSurface::queryCursorPosition() -> std::expected<std::pair<int, int>, QueryUnanswered>
{
    return *_output->simulatedCursorPosition(); // A mock always knows its cursor.
}

} // namespace core::tui
