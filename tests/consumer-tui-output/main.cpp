// SPDX-License-Identifier: Apache-2.0
//
// Styled output composed by core::tui_output and read back from a destination this program owns,
// which is how dbtool uses it: progress and colour, with no event loop and no libunicode.

#include <core/tui/TerminalOutput.hpp>

#include <cstdio>
#include <string>
#include <string_view>

namespace
{

/// A @c TerminalOutput whose destination is a string, answering as a terminal.
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

} // namespace

int main()
{
    auto output = CapturingOutput {};
    auto style = core::tui::Style {};
    style.bold = true;
    output.writeText("done", style);
    output.flush();

    auto const& bytes = output.captured();
    if (!bytes.contains("done") || !bytes.contains("\033["))
    {
        std::fprintf(stderr, "consumer-tui-output: expected bold \"done\", got %zu byte(s)\n", bytes.size());
        return 1;
    }
    std::printf("consumer-tui-output: %zu byte(s) of styled output\n", bytes.size());
    return 0;
}
