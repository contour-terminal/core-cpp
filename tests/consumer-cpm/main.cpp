// SPDX-License-Identifier: Apache-2.0
//
// A consumer's own code, using four core-cpp modules and compiled with the consumer's warning set
// (-Wall -Wextra -Werror, or /W4 /WX): a coroutine flow on core::net's event loop, a loopback echo
// over a real socket, a line through a core::log sink of this program's own, and a screen composed
// onto core::tui's MockTerminalOutput.
//
// The first two are in ../consumer-shared/ConsumerSmoke.hpp, which the vendored consumer uses too;
// what is here is what only a CPM consumer does, which is core::tui.
//
// core-cpp is added with SYSTEM YES, so its headers are outside those flags; this file is not, and
// a core-cpp header that only compiles with core-cpp's own options would fail here.
//
// Every step reports through Checks, so a failure names the step rather than only an exit code.

#include <core/tui/MockTerminalOutput.hpp>
#include <core/tui/TerminalOutput.hpp>

#include <iostream>
#include <string_view>

#include "../consumer-shared/ConsumerSmoke.hpp"

using consumer::smoke::Checks;

namespace
{

/// core::tui: composing onto the mock output, which is what a consumer's own tests render against.
void checkTerminalOutput(Checks& checks)
{
    auto output = core::tui::MockTerminalOutput { 80, 24 };
    output.moveTo(4, 6); // moveTo is 1-based; the mock reports the cursor 0-based.
    checks.expect(output.cursorRow() == 3 && output.cursorCol() == 5,
                  "core::tui::MockTerminalOutput tracked the cursor");

    output.writeHyperlink("core-cpp", "https://example.invalid/core-cpp", core::tui::Style {});
    auto const runs = output.hyperlinkRuns();
    checks.expect(runs.size() == 1 && runs.front().text == "core-cpp"
                      && runs.front().url == "https://example.invalid/core-cpp",
                  "core::tui composed the hyperlink run");
}

} // namespace

int main()
{
    auto checks = Checks {};
    consumer::smoke::checkLoopbackEcho(checks, std::string_view { "hello from a consumer" });
    consumer::smoke::checkLogging(checks, std::string_view { "a consumer" });
    checkTerminalOutput(checks);

    if (checks.failed() != 0)
    {
        std::cout << checks.failed() << " check(s) failed\n";
        return 1;
    }
    std::cout << "consumer-cpm: every check passed\n";
    return 0;
}
