// SPDX-License-Identifier: Apache-2.0
#include <core/cli/CLI.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <stdexcept>

// TODO API / impl:
//
// - [ ] int-casts in cli.h are a nightmare. use size_t when applicable then.
// - [ ] Add ValueDef { Value defaultValue; std::string_view placeholder; } and use this where Value{} was
// used.
// - [x] option presence validation (optional, required)
// - [x] option variation parsing: posix
// - [x] usage output
// - [x] help output
// - [x] colorizing the output for usage and detailed help
// - [x] easy accessor for flag values
// - [x] line/word-wrapping; smart indentation at the beginning of the text scope
// - [x] help output: print default, if available (i.e. presence=optional)

// TODO tests:
//
// - [ ] variations of option names and value attachments
//       all of: NAME [VALUE] | --NAME [VALUE] | -NAME [VALUE] | --NAME[=VALUE]
// - [ ] help output printing (colored, non-colored)
// - [ ] help output auto-detecting screen width, via: VT seq, ioctl(TIOCGWINSZ), manual
// - [ ] presence optional vs presence required
// - [x] test option type: BOOL
// - [ ] test option type: INT
// - [ ] test option type: UINT
// - [ ] test option type: FLOAT (also being passed as INT positive / negative)
// - [ ] test option type: STR (can be any arbitrary string)
// - [ ] test option defaults
// - [ ] CONSIDER: supporting positional arguments (free sanding values of single given type)
// - [ ] test command chains up to 3 levels deep (including proper help output, maybe via /bin/ip emul?)
//

using std::optional;
using std::string;

namespace cli = core::cli;

using namespace std::string_view_literals;
using namespace std::string_literals;

TEST_CASE("CLI.option.type.bool")
{
    auto const cmd = cli::Command {
        .name = "contour",
        .helpText = "help here",
        .options = cli::OptionList { cli::Option {
            .name = "verbose"sv, .v = cli::Value { false }, .helpText = "Help text here"sv } },
    };

    SECTION("set")
    {
        auto const args = cli::StringViewList { "contour", "verbose" };
        optional<cli::FlagStore> const flagsOpt = cli::parse(cmd, args);
        REQUIRE(flagsOpt.has_value());
        CHECK(flagsOpt.value().values.at("contour.verbose") == cli::Value { true });
    }

    SECTION("set true")
    {
        auto const args = cli::StringViewList { "contour", "verbose", "true" };
        optional<cli::FlagStore> const flagsOpt = cli::parse(cmd, args);
        REQUIRE(flagsOpt.has_value());
        CHECK(flagsOpt.value().values.at("contour.verbose") == cli::Value { true });
    }

    SECTION("set true")
    {
        auto const args = cli::StringViewList { "contour", "verbose", "false" };
        optional<cli::FlagStore> const flagsOpt = cli::parse(cmd, args);
        REQUIRE(flagsOpt.has_value());
        CHECK(flagsOpt.value().values.at("contour.verbose") == cli::Value { false });
    }

    SECTION("unset")
    {
        auto const args = cli::StringViewList { "contour" };
        optional<cli::FlagStore> const flagsOpt = cli::parse(cmd, args);
        REQUIRE(flagsOpt.has_value());
        CHECK(flagsOpt.value().values.at("contour.verbose") == cli::Value { false });
    }
}

TEST_CASE("CLI.contour-full-test")
{
    auto const cmd = cli::Command {
        "contour",
        "help here",
        cli::OptionList {
            cli::Option { "debug"sv, cli::Value { ""s }, "Help text here"sv },
            cli::Option { "config", cli::Value { "~/.config/contour/contour.yml"s }, "Help text there"sv },
            cli::Option { "profile", cli::Value { ""s }, "Help text over here"sv } },
        cli::CommandList { cli::Command { "capture",
                                          "some capture help text",
                                          {
                                              cli::Option { "logical", cli::Value { false }, "help there" },
                                              cli::Option { "timeout", cli::Value { 1.0 }, "help here" },
                                              cli::Option { "output", cli::Value { ""s } },
                                          } } }
    };

    auto const args = cli::StringViewList { "contour", "capture", "logical", "output", "out.vt" };
    optional<cli::FlagStore> const flagsOpt = cli::parse(cmd, args);
    REQUIRE(flagsOpt.has_value());

    cli::FlagStore const& flags = flagsOpt.value();

    CHECK(flags.values.size() == 8);
    CHECK(flags.values.at("contour") == cli::Value { true }); // command
    CHECK(flags.values.at("contour.debug") == cli::Value { ""s });
    CHECK(flags.values.at("contour.config") == cli::Value { "~/.config/contour/contour.yml"s });
    CHECK(flags.values.at("contour.profile") == cli::Value { ""s });
    CHECK(flags.values.at("contour.capture") == cli::Value { true }); // command
    CHECK(flags.values.at("contour.capture.logical") == cli::Value { true });
    CHECK(flags.values.at("contour.capture.output") == cli::Value { "out.vt"s });
    CHECK(flags.values.at("contour.capture.timeout") == cli::Value { 1.0 });
}

namespace
{
cli::HelpDisplayStyle plainStyle()
{
    auto style = cli::HelpDisplayStyle {};
    style.colors.reset();
    style.hyperlink = false;
    return style;
}

cli::Command commandWithOptions()
{
    return cli::Command {
        .name = "contour",
        .helpText = "Terminal emulator.",
        .options =
            cli::OptionList {
                cli::Option { .name = "config"sv,
                              .v = cli::Value { "~/.config/contour/contour.yml"s },
                              .helpText = "Path to configuration file to load at startup."sv },
                cli::Option { .name = "profile"sv,
                              .v = cli::Value { ""s },
                              .helpText = "Overrides the profile to use in the configuration."sv },
            },
    };
}
} // namespace

// printOptions() sets the cursor to the option column and hands it to wordWrapped() as the
// starting position. `margin - cursor + 1` is unsigned: any margin at or below that column --
// a narrow terminal, or a pty that reports no size at all -- wrapped to about 4294967295, the
// `rightMargin <= 0` guard below it is dead for an unsigned type, and the index walked far off
// the end of the help text.
TEST_CASE("CLI.helpText.narrow-margin")
{
    auto const cmd = commandWithOptions();

    for (auto const margin: { 0u, 1u, 8u, 20u, 40u, 79u, 80u })
    {
        INFO("margin " << margin);
        auto const text = cli::helpText(cmd, plainStyle(), margin);
        CHECK(text.contains("config"));
        CHECK(text.contains("profile"));
    }
}

// wordWrapped() computed the position before the line feed as `linefeed - 1` on a size_t, so a
// help text whose first character is a line feed indexed text[SIZE_MAX].
TEST_CASE("CLI.helpText.leading-linefeed")
{
    auto const cmd = cli::Command {
        .name = "contour",
        .helpText = "Terminal emulator."sv,
        .options =
            cli::OptionList { cli::Option { .name = "config"sv,
                                            .v = cli::Value { ""s },
                                            .helpText = "\nIts help text starts on the next line."sv } },
    };

    auto const text = cli::helpText(cmd, plainStyle(), 80);
    CHECK(text.contains("Its help text starts on the next line."));
}

// The verbatim row's left column was measured against a column width computed from the options
// alone: `columnWidth - leftSize` underflowed, the assert above it is compiled out under NDEBUG,
// and spaces(n) became a string of about four billion characters.
TEST_CASE("CLI.helpText.verbatim-longer-than-the-options")
{
    auto const cmd = cli::Command {
        .name = "contour",
        .helpText = "Terminal emulator."sv,
        .verbatim = cli::Verbatim { "A_PLACEHOLDER_LONGER_THAN_ANY_OPTION", "Extra arguments." },
    };

    auto const text = cli::helpText(cmd, plainStyle(), 80);
    CHECK(text.contains("A_PLACEHOLDER_LONGER_THAN_ANY_OPTION"));
    CHECK(text.contains("Extra arguments."));
}

// The hyperlink scan walks back over the scheme with isalpha(), which is undefined for a char
// whose value is negative -- every continuation byte of a UTF-8 sequence, and help text is
// written by a human.
TEST_CASE("CLI.helpText.non-ascii-help-text")
{
    auto const cmd = cli::Command {
        .name = "contour",
        .helpText = "Terminal emulator."sv,
        .options = cli::OptionList { cli::Option {
            .name = "config"sv,
            .v = cli::Value { ""s },
            .helpText = "Grüße — siehe https://contour-terminal.org/ für mehr."sv } },
    };

    auto style = plainStyle();
    style.hyperlink = true;
    auto const text = cli::helpText(cmd, style, 80);
    CHECK(text.contains("https://contour-terminal.org/"));
}

// The declaration says which failures are a value and which are an exception; these pin it.
TEST_CASE("CLI.parse.failure-modes")
{
    auto const cmd = cli::Command {
        .name = "contour",
        .helpText = "Terminal emulator."sv,
        .options =
            cli::OptionList {
                cli::Option { .name = "count"sv, .v = cli::Value { 0 }, .helpText = "A number."sv },
                cli::Option { .name = "profile"sv,
                              .v = cli::Value { ""s },
                              .helpText = "Which profile."sv,
                              .placeholder = {},
                              .presence = cli::Presence::Required },
            },
    };

    SECTION("a value of the wrong type throws ParserError")
    {
        auto const args = cli::StringViewList { "contour", "profile", "p", "count", "not-a-number" };
        CHECK_THROWS_AS(cli::parse(cmd, args), cli::ParserError);
    }

    SECTION("a missing required option throws std::invalid_argument")
    {
        auto const args = cli::StringViewList { "contour", "count", "1" };
        CHECK_THROWS_AS(cli::parse(cmd, args), std::invalid_argument);
    }
}
