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
whose keys are the dotted path of each option, `contour.capture.timeout` for example, prefilled
with every default:

```cpp
auto const syntax = core::cli::Command {
    .name = "tool",
    .options = { core::cli::Option { .name = "verbose", .v = core::cli::Value { false } } },
};
if (auto const flags = core::cli::parse(syntax, argc, argv))
    verbose = flags->get<bool>("tool.verbose");
```

## Open work

- **[core-cpp#13](https://github.com/contour-terminal/core-cpp/issues/13)**: `parse()` throws
  `core::cli::ParserError` or `std::invalid_argument` on a malformed command line, as crispy's
  did. It is to return `std::expected` instead.
