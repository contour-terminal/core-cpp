// SPDX-License-Identifier: Apache-2.0
//
// A component tree rendered by core::tui_core into bytes this program owns, with no terminal under
// it: the shape a WebAssembly guest uses, linking no platform layer, no coroutines and no event loop.

#include <core/tui/Canvas.hpp>
#include <core/tui/Component.hpp>
#include <core/tui/Screen.hpp>
#include <core/tui/ScreenSurface.hpp>
#include <core/tui/TerminalOutput.hpp>

#include <cstdio>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace
{

/// A @c TerminalOutput whose destination is a string.
class CapturingOutput: public core::tui::TerminalOutput
{
  public:
    [[nodiscard]] bool isTerminal() const noexcept override { return true; }

    [[nodiscard]] std::string const& captured() const noexcept { return _captured; }

  protected:
    void writeToDestination(std::string_view bytes) override { _captured.append(bytes); }

  private:
    std::string _captured;
};

/// A fixed-size surface over a @c CapturingOutput, which no one can ask where the cursor is.
class FixedSurface: public core::tui::ScreenSurface
{
  public:
    [[nodiscard]] auto columns() const noexcept -> int override { return 20; }
    [[nodiscard]] auto rows() const noexcept -> int override { return 3; }
    [[nodiscard]] auto output() noexcept -> core::tui::TerminalOutput& override { return _output; }
    [[nodiscard]] auto queryCursorPosition()
        -> std::expected<std::pair<int, int>, core::tui::QueryUnanswered> override
    {
        return std::unexpected { core::tui::QueryUnanswered::NotAsked };
    }

    [[nodiscard]] std::string const& captured() const noexcept { return _output.captured(); }

  private:
    CapturingOutput _output;
};

/// Draws one word at its top-left corner.
class Greeting: public core::tui::Component
{
  public:
    void render(core::tui::Canvas& canvas) override { canvas.put(0, 0, "headless", {}); }
};

} // namespace

int main()
{
    auto surface = FixedSurface {};
    auto screen = core::tui::Screen { surface };
    auto greeting = Greeting {};
    screen.root().addChild(greeting, { .area = { .x = 0, .y = 0, .width = 20, .height = 1 } });
    screen.draw();

    if (surface.captured().find("headless") == std::string::npos)
    {
        std::fprintf(stderr, "consumer-tui-core: the frame does not hold the component's text\n");
        return 1;
    }
    std::printf("consumer-tui-core: %zu bytes of frame, no terminal\n", surface.captured().size());
    return 0;
}
