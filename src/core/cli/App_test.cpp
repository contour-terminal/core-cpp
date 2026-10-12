// SPDX-License-Identifier: Apache-2.0
#include <core/Utf8Path.hpp>
#include <core/Utils.hpp>
#include <core/cli/App.hpp>
#include <core/log/LogStore.hpp>
#include <core/testing/Environment.hpp>
#include <core/testing/ScopedTempDir.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <string>

using std::string;

namespace cli = core::cli;

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace
{
/// The smallest application that has the two logging options installLogging() reads.
class TestApp final: public cli::App
{
  public:
    explicit TestApp(core::Environment const& env):
        cli::App { env, "a9app", "A9 test app", "0.0.0", "Apache-2.0" }
    {
    }

    [[nodiscard]] cli::Command parameterDefinition() const override
    {
        return cli::Command {
            .name = "a9app",
            .helpText = "A test application."sv,
            .options =
                cli::OptionList {
                    cli::Option { .name = "log"sv, .v = cli::Value { ""s }, .helpText = "Log filter."sv },
                    cli::Option {
                        .name = "log-file"sv, .v = cli::Value { ""s }, .helpText = "Where to log."sv },
                    cli::Option { .name = "count"sv, .v = cli::Value { 0 }, .helpText = "A number."sv },
                },
        };
    }
};
} // namespace

// reparseParameters() is documented "false on failure", and parseParametersForTesting() is its
// test-facing alias. cli::parse() used to throw for a malformed command line and neither of them
// caught, so an exception escaped a function whose contract is a bool; it returns the error now
// (core-cpp#13), and these pin that every refusal still reaches the bool.
TEST_CASE("cli::App::reparseParameters answers false for a command line it cannot parse", "[cli][app]")
{
    auto const env = core::testing::FakeEnvironment {};
    auto app = TestApp { env };

    SECTION("a value of the wrong type")
    {
        char const* argv[] = { "a9app", "count", "not-a-number" };
        CHECK(!app.parseParametersForTesting(3, argv));
    }

    SECTION("a value where no option takes one")
    {
        char const* argv[] = { "a9app", "count" };
        CHECK(!app.parseParametersForTesting(2, argv));
    }

    SECTION("a command line it can parse still answers true")
    {
        char const* argv[] = { "a9app", "count", "42" };
        REQUIRE(app.parseParametersForTesting(3, argv));
        CHECK(app.parameters().get<int>("a9app.count") == 42);
    }
}

// installLogging() assigned the replacement over the member holding the previous output, so the
// previous ScopedOutput was destroyed AFTER the new one had installed itself -- and its destructor
// restores every category to the sink it snapshotted, which put all later log lines back on the
// console. It also left every category pointing into a destroyed sink.
TEST_CASE("cli::App::installLogging: a second call leaves the second output installed", "[cli][app]")
{
    auto const tmp = core::testing::ScopedTempDir { "core-cpp-app" };
    auto const env = core::testing::FakeEnvironment {};

    auto* const beforeAnyInstall = &core::log::errorLog.sink();

    constexpr auto Marker = "a9-marker-written-after-the-second-install"sv;

    {
        auto app = TestApp { env };

        auto const install = [&](string const& file) {
            // In UTF-8, as --log-file reads it: generic_string() narrows through the ANSI code page.
            auto const path = core::pathToUtf8(tmp / file);
            char const* argv[] = { "a9app", "log-file", path.c_str() };
            REQUIRE(app.parseParametersForTesting(3, argv));
            REQUIRE(app.installLogging("a9app").has_value());
        };

        install("first.log");
        CHECK(&core::log::errorLog.sink() != beforeAnyInstall);

        install("second.log");
        CHECK(&core::log::errorLog.sink() != beforeAnyInstall);

        // Unqualified on purpose: core::log spells this one as a macro.
        errorLog()("{}", Marker);
    }

    // The app took its output with it: nothing is left pointing into a destroyed sink.
    CHECK(&core::log::errorLog.sink() == beforeAnyInstall);

    // And the line went where the second call said, not back to the console.
    CHECK(core::readFileAsString(tmp / "second.log").contains(Marker));
    CHECK(!core::readFileAsString(tmp / "first.log").contains(Marker));
}

// `contour daemon --background` under C:\Users\Jürgen: the --log-file value is UTF-8, and a daemon
// whose log does not open exits. On Windows it was read through the ANSI code page, so on a CP-1252
// host the log went to a directory with two characters where the `ü` should have been.
TEST_CASE("cli::App::installLogging opens a --log-file beyond ASCII", "[cli][app]")
{
    auto const tmp = core::testing::ScopedTempDir { "core-cpp-app" };
    auto const env = core::testing::FakeEnvironment {};
    auto const path = core::pathToUtf8(tmp.path()) + "/J\xC3\xBCrgen/\xCE\xA9-daemon.log";

    constexpr auto Marker = "a9-marker-beyond-ascii"sv;

    {
        auto app = TestApp { env };
        char const* argv[] = { "a9app", "log-file", path.c_str() };
        REQUIRE(app.parseParametersForTesting(3, argv));
        REQUIRE(app.installLogging("a9app").has_value());

        // Unqualified on purpose: core::log spells this one as a macro.
        errorLog()("{}", Marker);
    }

    CHECK(core::readFileAsString(tmp.path() / std::filesystem::path { u8"Jürgen" }
                                 / std::filesystem::path { u8"Ω-daemon.log" })
              .contains(Marker));
}
