// SPDX-License-Identifier: Apache-2.0
//
// A component tree rendered by core::tui_core into bytes this program owns, with no terminal under
// it: the shape a WebAssembly guest uses, linking no platform layer, no coroutines and no event loop.

#include <core/tui/Canvas.hpp>
#include <core/tui/Component.hpp>
#include <core/tui/FrameDiff.hpp>
#include <core/tui/HeadlessSurface.hpp>
#include <core/tui/Rect.hpp>
#include <core/tui/Responsive.hpp>
#include <core/tui/Screen.hpp>
#include <core/tui/layout/Solve.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace
{

/// Draws one word at its top-left corner.
class Greeting: public core::tui::Component
{
  public:
    void render(core::tui::Canvas& canvas) override { canvas.putString(0, 0, "headless", {}); }
};

/// The bytes with their SGR sequences (`CSI ... m`) removed, so that a check reads the text and the
/// cursor moves that place it, and not how each cell is styled.
/// @param bytes Terminal output.
/// @return @p bytes without its SGR sequences.
std::string withoutSgr(std::string_view bytes)
{
    auto text = std::string {};
    while (!bytes.empty())
    {
        auto const csi = bytes.find("\033[");
        text += bytes.substr(0, csi);
        if (csi == std::string_view::npos)
            break;
        auto const end = bytes.find_first_not_of("0123456789;:", csi + 2);
        auto const isSgr = end != std::string_view::npos && bytes[end] == 'm';
        if (!isSgr)
            text += bytes.substr(csi, 2); // Another sequence: kept, and read on from its parameters.
        bytes.remove_prefix(isSgr ? end + 1 : csi + 2);
    }
    return text;
}

} // namespace

int main()
{
    auto frame = std::string {};
    auto surface = core::tui::HeadlessSurface { core::tui::Size { .width = 20, .height = 3 },
                                                [&frame](std::string_view bytes) { frame += bytes; } };
    auto screen = core::tui::Screen { surface };
    auto greeting = Greeting {};
    screen.root().addChild(greeting, { .area = { .x = 0, .y = 0, .width = 20, .height = 1 } });
    screen.draw();

    // The word stands at the top-left corner, written as one run after the cursor move that reaches it.
    if (!withoutSgr(frame).contains("\033[1;1Hheadless"))
    {
        std::fprintf(stderr, "consumer-tui-core: the frame does not hold the component's text\n");
        return 1;
    }

    // The diff between the frame on the screen and itself is empty: nothing to send.
    if (!core::tui::diffFrames(screen.renderedBuffer(), screen.renderedBuffer()).empty())
    {
        std::fprintf(stderr, "consumer-tui-core: an unchanged frame diffs to bytes\n");
        return 1;
    }

    // The flexbox layout is part of the headless half too: two growing children share a row.
    auto const half = core::tui::layout::Node { .style = { .grow = 1 } };
    auto const row = core::tui::layout::Node { .children = { half, half } };
    auto const rects = core::tui::layout::solve(row, { .x = 0, .y = 0, .width = 20, .height = 3 });
    if (rects.size() != 3 || rects[2] != core::tui::Rect { .x = 10, .y = 0, .width = 10, .height = 3 })
    {
        std::fprintf(stderr, "consumer-tui-core: the layout did not split the row\n");
        return 1;
    }

    // And the layout pass over a component tree: a Responsive at the root, which fills the screen and
    // builds the layout its size chooses, once.
    auto builds = 0;
    auto responsive =
        core::tui::Responsive { { core::tui::Breakpoint { .minColumns = 0, .minRows = 0, .make = [&builds] {
                                                             ++builds;
                                                             return std::make_unique<Greeting>();
                                                         } } } };
    screen.root().addChild(responsive, { .flex = core::tui::layout::Style { .grow = 1 } });
    screen.draw();
    screen.draw();
    if (builds != 1 || responsive.current() == nullptr
        || responsive.current()->area() != core::tui::Rect { .x = 0, .y = 0, .width = 20, .height = 3 })
    {
        std::fprintf(stderr, "consumer-tui-core: the Responsive did not fill the screen with one layout\n");
        return 1;
    }

    std::printf("consumer-tui-core: %zu bytes of frame, no terminal\n", frame.size());
    return 0;
}
