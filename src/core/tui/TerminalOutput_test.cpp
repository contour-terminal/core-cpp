// SPDX-License-Identifier: Apache-2.0
#include <core/Environment.hpp>
#include <core/tui/TerminalOutput.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace
{

/// Which kind of stream the test's output pretends to write to.
enum class Destination : std::uint8_t
{
    File,    ///< Not a terminal: no colours, no progress, no synchronised output.
    Terminal ///< A terminal.
};

/// A @c TerminalOutput whose destination is a string this test can read back.
///
/// It overrides only what says where the bytes go, which is the whole point of the two
/// seams under test here: everything else -- the escape-sequence composition, the
/// synchronised-output guard -- must reach this capture unchanged.
class CapturingOutput: public core::tui::TerminalOutput
{
  public:
    explicit CapturingOutput(Destination destination) noexcept: _destination(destination) {}

    [[nodiscard]] bool isTerminal() const noexcept override { return _destination == Destination::Terminal; }

    /// @return Every byte written to the destination so far, in order.
    [[nodiscard]] std::string const& captured() const noexcept { return _captured; }

  protected:
    void writeToDestination(std::string_view bytes) override { _captured.append(bytes); }

  private:
    std::string _captured;
    Destination _destination;
};

} // namespace

TEST_CASE("tui.TerminalOutput: SyncGuard writes through writeToDestination")
{
    auto output = CapturingOutput { Destination::File };
    output.writeRaw("before");

    {
        auto const guard = output.syncGuard();
        // The guard flushes what was buffered before it opens the mode, so the begin sequence
        // brackets everything written from here on and nothing that came before it.
        CHECK(output.captured() == "before\033[?2026h");

        output.writeRaw("inside");
        output.flush();
    }

    // The end sequence goes to the same destination, after the guarded bytes. A guard that wrote
    // to the process's standard output instead would leave the capture at "before" plus "inside"
    // and put both sequences on someone else's screen.
    CHECK(output.captured() == "before\033[?2026hinside\033[?2026l");
}

TEST_CASE("tui.TerminalOutput: a moved-from SyncGuard leaves the mode to its successor")
{
    auto output = CapturingOutput { Destination::File };

    {
        auto first = output.syncGuard();
        auto const second = std::move(first);
        CHECK(output.captured() == "\033[?2026h");
    }

    // Exactly one end sequence: the moved-from guard must not write one of its own.
    CHECK(output.captured() == "\033[?2026h\033[?2026l");
}

TEST_CASE("tui.TerminalOutput: a default-constructed SyncGuard writes nothing")
{
    auto output = CapturingOutput { Destination::File };
    {
        auto const guard = core::tui::SyncGuard {};
    }
    CHECK(output.captured().empty());
}

TEST_CASE("tui.TerminalOutput: isTerminal() reflects the destination")
{
    auto file = CapturingOutput { Destination::File };
    auto terminal = CapturingOutput { Destination::Terminal };

    // Through the base, which is how a renderer holds it: the answer is the destination's, not
    // the process's standard output.
    core::tui::TerminalOutput const& fileRef = file;
    core::tui::TerminalOutput const& terminalRef = terminal;
    CHECK_FALSE(fileRef.isTerminal());
    CHECK(terminalRef.isTerminal());
}

TEST_CASE("tui.TerminalOutput: the default destination's isTerminal() asks the operating system")
{
    // ctest runs a test binary with its output on a pipe, and sets this in the test's environment.
    // Without it the binary was started some other way and standard output may well be a terminal,
    // which is exactly what this case cannot then distinguish from a constant `true`.
    if (!core::defaultEnvironment().get("CTEST_INTERACTIVE_DEBUG_MODE"))
        SKIP("not run by ctest, so standard output is not known to be a pipe");

    auto const output = core::tui::TerminalOutput {};
    CHECK_FALSE(output.isTerminal());
}

TEST_CASE("tui.TerminalOutput: copyToClipboard emits OSC 52 with the base64 of the text")
{
    auto output = CapturingOutput { Destination::File };

    // "man" is three bytes, so it encodes without padding; "ma" and "m" take one and two '='.
    output.copyToClipboard("man");
    output.copyToClipboard("ma");
    output.copyToClipboard("m");
    output.flush();

    CHECK(output.captured() == "\033]52;c;bWFu\033\\\033]52;c;bWE=\033\\\033]52;c;bQ==\033\\");
}
