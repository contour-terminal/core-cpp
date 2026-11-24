// SPDX-License-Identifier: Apache-2.0
//
// core::tui_core as a WASI reactor: no main(), one exported function the host calls after
// _initialize. It runs the headless TUI the way a guest does -- bytes from the host through VtParser,
// events into a Screen over a HeadlessSurface, the frame back out as bytes, diffFrames() between two
// frames, and layout::solve() placing the tree -- and prints one line per check. A failed check
// traps, so the runtime exits non-zero; run-tui-core-reactor.cmake also reads the summary line.

#include <core/tui/Canvas.hpp>
#include <core/tui/Component.hpp>
#include <core/tui/FrameDiff.hpp>
#include <core/tui/HeadlessSurface.hpp>
#include <core/tui/InputEvent.hpp>
#include <core/tui/KeyCode.hpp>
#include <core/tui/Rect.hpp>
#include <core/tui/Screen.hpp>
#include <core/tui/VtParser.hpp>
#include <core/tui/layout/Solve.hpp>
#include <core/tui/layout/Style.hpp>

#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>

namespace
{

/// One line of text input: printable keys append, Enter submits.
class LineInput final: public core::tui::Component
{
  public:
    void render(core::tui::Canvas& canvas) override { canvas.putString(0, 0, _text, {}); }
    [[nodiscard]] bool focusable() const override { return true; }
    [[nodiscard]] core::tui::Size preferredSize() const override { return { .width = 0, .height = 1 }; }

    [[nodiscard]] core::tui::EventResult onEvent(core::tui::InputEvent const& event) override
    {
        auto const* key = std::get_if<core::tui::KeyEvent>(&event);
        if (!key)
            return core::tui::EventResult::Ignored;
        if (key->key == core::tui::KeyCode::Enter)
        {
            _submitted = _text;
            return core::tui::EventResult::Handled;
        }
        if (key->codepoint >= U' ' && key->codepoint < U'\x7f')
        {
            _text += static_cast<char>(key->codepoint);
            invalidate();
            return core::tui::EventResult::Handled;
        }
        return core::tui::EventResult::Ignored;
    }

    [[nodiscard]] std::string const& text() const noexcept { return _text; }
    [[nodiscard]] std::string const& submitted() const noexcept { return _submitted; }

  private:
    std::string _text;
    std::string _submitted;
};

/// A fixed-width side panel, so the row has something to share.
class Sidebar final: public core::tui::Component
{
  public:
    void render(core::tui::Canvas& canvas) override { canvas.putString(0, 0, "side", {}); }
    [[nodiscard]] core::tui::Size preferredSize() const override { return { .width = 8, .height = 1 }; }
};

constexpr auto Columns = 32;
constexpr auto Rows = 4;

/// The guest's state, built by _initialize (the reactor's static constructors) and kept between calls.
struct Guest
{
    std::string frames;
    core::tui::HeadlessSurface surface { core::tui::Size { .width = Columns, .height = Rows },
                                         [this](std::string_view bytes) { frames += bytes; } };
    core::tui::Screen screen { surface };
    core::tui::FlexBox row { core::tui::layout::Style {} };
    LineInput input;
    Sidebar sidebar;
    core::tui::VtParser parser;

    Guest()
    {
        row.addChild(input, { .flex = core::tui::layout::Style { .grow = 1 } });
        row.addChild(sidebar, { .flex = core::tui::layout::Style { .shrink = 0 } });
        screen.root().addChild(row, { .flex = core::tui::layout::Style { .grow = 1 } });
        screen.setFocus(&input);
    }

    /// Feeds bytes as the host would hand them over, delivers each event, and draws a frame.
    /// @return How many events the bytes parsed into.
    int type(std::string_view bytes)
    {
        auto const events = parser.feed(bytes);
        for (auto const& event: events)
            static_cast<void>(screen.dispatchEvent(event));
        screen.draw();
        return static_cast<int>(events.size());
    }
};

Guest guest;

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

int passed = 0;
int failed = 0;

/// Prints one line for a check, and counts it.
/// @param what What holds when the check passes.
/// @param holds Asked once whether it does.
void check(char const* what, std::predicate auto const& holds)
{
    auto const ok = holds();
    std::printf("%s - %s\n", ok ? "ok" : "not ok", what);
    ++(ok ? passed : failed);
}

} // namespace

/// Runs the guest's checks. Exported by name; the reactor has no main().
/// @return The number of failed checks (a failure traps before this returns).
extern "C" __attribute__((export_name("core_tui_reactor_run"))) int coreTuiReactorRun()
{
    // Bytes in: two letters, a cursor key (CSI D), a letter, Enter -- five events.
    auto const events = guest.type("hi\x1b[D!\r");
    check("VtParser parses 'hi', CSI D, '!' and CR into five events", [&] { return events == 5; });
    check("Screen delivers the keys to the focused input, which submits on Enter",
          [] { return guest.input.text() == "hi!" && guest.input.submitted() == "hi!"; });

    // Bytes out: the first row is written as one run after a single cursor move -- the input's text at
    // column 1, the 21 blank cells it leaves, and the sidebar at column 25, where the layout put it.
    check("the frame written to the sink holds the input's text and the sidebar",
          [] { return withoutSgr(guest.frames).contains("\033[1;1Hhi!" + std::string(21, ' ') + "side"); });

    // Two frames: what changed between them, and nothing between a frame and itself.
    auto const before = guest.screen.renderedBuffer();
    static_cast<void>(guest.type("?"));
    auto const& after = guest.screen.renderedBuffer();
    auto const delta = core::tui::diffFrames(before, after);
    check("diffFrames() writes the new cell and not the old ones",
          [&] { return delta.contains("?") && !delta.contains("hi"); });
    check("diffFrames() of a frame and itself is empty",
          [&] { return core::tui::diffFrames(after, after).empty(); });

    // The layout: solve() over the same shape the tree has, and the tree placed as it says.
    auto const tree = core::tui::layout::Node {
        .children = { core::tui::layout::Node { .style = { .grow = 1 } },
                      core::tui::layout::Node { .style = { .shrink = 0 },
                                                .preferred = { .width = 8, .height = 1 } } }
    };
    auto const rects = core::tui::layout::solve(tree, { .x = 0, .y = 0, .width = Columns, .height = Rows });
    check("solve() gives the input what the sidebar leaves", [&] {
        return rects.size() == 3
               && rects[1] == core::tui::Rect { .x = 0, .y = 0, .width = 24, .height = Rows }
               && rects[2] == core::tui::Rect { .x = 24, .y = 0, .width = 8, .height = Rows };
    });
    check("the screen's layout pass placed the tree where solve() says",
          [&] { return guest.input.area() == rects[1] && guest.sidebar.area() == rects[2]; });

    std::printf("tui-core-reactor: %d of %d checks passed\n", passed, passed + failed);
    std::fflush(stdout);
    if (failed != 0)
        std::abort();
    return failed;
}
