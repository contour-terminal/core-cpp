# cli

Command-line parsing and an application scaffold. Namespace `core::cli`, directory
`src/core/cli/`, target `core::cli`. It depends on [base](base.md) and [log](log.md) and builds
for WebAssembly.

Imported from contour's crispy `CLI` and `App` at `6777ff05`, where they were `crispy::cli` and
`crispy::App`.

| Header | What it has |
|---|---|
| `<core/cli/CLI.hpp>` | the command syntax (`Command`, `Option`, `Value`), `parse()` into a `FlagStore`, `helpText()` and `usageText()`, and `about::registerProjects()` for a license listing |
| `<core/cli/App.hpp>` | `core::cli::App`, a `main()` scaffold: it parses argv against the syntax an application declares, dispatches to the handler of the command given, and provides `help`, `version` and `license`; it applies `LOG` from its `core::Environment` and installs `--log`/`--log-file` output through [log](log.md) |

## The syntax

A command has options and subcommands. Options are spelled POSIX-style (`--timeout=1.0`,
`-t 1.0`) or naturally (`timeout 1.0`); a boolean needs no value. `parse()` fills a `FlagStore`
whose keys are the dotted path of each option, `tool.fetch.timeout` for the `timeout` option of
the `fetch` subcommand of `tool`, prefilled with every default:

```cpp
auto const syntax = core::cli::Command {
    .name = "tool",
    .options = { core::cli::Option { .name = "verbose", .v = core::cli::Value { false } } },
};
auto const flags = core::cli::parse(syntax, argc, argv);
if (!flags)
{
    std::println(stderr, "tool: {}", flags.error().message);
    return EXIT_FAILURE;
}
verbose = flags->get<bool>("tool.verbose");
```

A malformed command line is a value, never an exception: `parse()` returns
`std::expected<FlagStore, ParseError>`, and a `ParseError` has a `kind` (`ParseErrorKind`: not
enough arguments, an invalid value, an explicit empty value for a non-string option, an unexpected
token, a missing required option, an unknown option), the index of the token at fault and a message
for the user.
A number is read whole: `12abc` is refused rather than read as 12, and `-1` is not an unsigned.

## Verbatim input

A command with a `Verbatim` section and no subcommands takes the rest of the command line as
`FlagStore::verbatim`, unparsed -- a program and its arguments, say. Its options come first, and
the verbatim input starts at the first token that is not spelled like an option, or after a `--`,
which is consumed:

| Command line | Options | `verbatim` |
|---|---|---|
| `tool new --cwd /tmp htop -d 5` | `cwd` | `htop`, `-d`, `5` |
| `tool new --cwd /tmp -- --window 1` | `cwd` | `--window`, `1` |
| `tool new - -x` | | `-`, `-x` |
| `tool new --window 1` | refused: `UnknownOption`, naming `--window` | |

Before the verbatim input starts, a token spelled like an option (`-x`, `-name`, `--name`,
`--name=value`) must name one of the command's options; once it has started, every token is
verbatim, a dash or not. A bare `-` is not spelled like an option: by convention it names standard
input, and so it starts the verbatim input like any other operand. A command without verbatim
input refuses an unknown option-like token with `UnknownOption` too, and any other token it cannot
place with `UnexpectedToken`.
