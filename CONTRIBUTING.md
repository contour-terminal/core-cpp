# Contributing to core-cpp

core-cpp is shared by six projects, so a change here reaches all of them. This page says what
belongs in core-cpp, what a change must carry, and how pull requests are labelled. The C++ rules
are in the [C++ guidelines](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/cpp-guidelines.md);
the constraints that have each already been a bug are in the
[rulebook](https://github.com/contour-terminal/core-cpp/tree/master/.agent/rules).

## What belongs here

- **Code at least two of the consuming projects need.** A file moves into core-cpp when a second
  project needs it (the graduation rule), and it must carry no PUBLIC compile flag and no
  dependency beyond the standard library and Threads, unless it adds a row to the dependency
  table.
- **Generic code only.** Nothing specific to one consumer enters core-cpp: no terminal-emulator,
  shell, cache or disk-usage concept. A consumer adapts to core-cpp's API through its own code.
- **Fixes to vendored copies go here first.** A consumer never edits its copy of core-cpp; the
  fix lands here, is released, and the consumer moves its pin.

## Toolchain

| Tool | Minimum |
|---|---|
| clang | 22 |
| GCC | 14 |
| AppleClang | Xcode 16 |
| Visual Studio (`cl`, `clang-cl`) | 2022 |
| CMake | 3.25 |
| Ninja | any recent |
| Python | 3 |
| emsdk (WebAssembly only) | 3.1.56 |

Build with a preset, into `out/build/<preset>`:

```sh
cmake --preset clang-debug && cmake --build --preset clang-debug && ctest --preset clang-debug
```

On Windows, run from a Visual Studio developer shell and use `cl-debug` or `clangcl-debug`. The
[building guide](https://contour-terminal.github.io/core-cpp/getting-started/building/) lists
every preset.

## Formatting and static analysis

clang-format and clang-tidy are pinned to one PyPI release each, because successive LLVM
releases format differently and an older clang-tidy is silent about newer checks:

```sh
pip install clang-format==22.1.8 clang-tidy==22.1.8   # or: python scripts/tool-versions.py --install
python scripts/clang-format.py                        # format every C++ source
python scripts/clang-format.py --check                # what CI runs
cmake --preset clang-tidy && cmake --build --preset clang-tidy
```

`scripts/clang-format.py` refuses any clang-format that is not the pinned one. Never format with
another version, and never silence a finding with `NOLINT`: fix it.

## What a change carries

- **Tests.** New or changed behaviour has a test next to the code (`Foo_test.cpp` beside
  `Foo.cpp`), and the test was seen to fail without the change.
- **Small, semantic commits**, each one building and passing on its own, each message ending
  with a `Signed-off-by:` trailer (`git commit -s`).
- **A CHANGELOG entry under `[Unreleased]`** for anything a consumer can observe: public API,
  options, dependencies, behaviour. A breaking change goes under **Breaking** with a migration
  note.
- **Documentation** for what the change affects: the module's page, the options page, and the
  rulebook when the change fixes something that was a bug.
- **A "Consumer impact" section in the pull request**, from the template: for each of contour,
  endo, fastcached, tuidu, Lightweight's `dbtool` and morph, what it must change, or "none".
  Grep their sources before changing a public signature.

## Labels

Every pull request carries exactly one `type/` label, which decides the section of the release
notes it appears in:

| Label | Means |
|---|---|
| `type/feature` | New capability, or a user-visible extension of one |
| `type/bug` | Behaves incorrectly against its stated contract |
| `type/perf` | Throughput, latency or footprint |
| `type/docs` | Documentation, the rulebook, comments |
| `type/chore` | Build, CI, dependencies, releases, repository hygiene |

Add `breaking-change` when a consumer must change code or build configuration, and one
`module/<name>` label per module touched: `module/base`, `module/log`, `module/cli`,
`module/platform`, `module/async`, `module/net`, `module/tui`, `module/testing`.

## Versioning

core-cpp follows Semantic Versioning with `vX.Y.Z` tags. While the major version is 0, a minor
release may break the API, and each break is recorded under **Breaking** with a migration note;
a patch release never breaks. Consumers pin a tag, or temporarily a full commit SHA, never a
branch.

## Reporting a security problem

Do not open a public issue; see the
[security policy](https://github.com/contour-terminal/core-cpp/blob/master/SECURITY.md).

## License

core-cpp is licensed under the Apache License, Version 2.0. By contributing you agree that your
contribution is licensed under the same terms.
