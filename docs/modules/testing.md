# testing

Test support that core-cpp's own tests use and that consumers can link: suppression of the
Windows dialogs that hang an unattended run, and a Catch2 `main()` whose exit status says what
happened. Namespace `core::testing`, directory `src/core/testing/`.

| Target | Kind | Needs | What it is |
|---|---|---|---|
| `core::testing` | static | `core::base` | `core::testing::suppressWindowsDialogs()`, `core::testing::FakeEnvironment` (`<core/testing/Environment.hpp>`), and the scoped fixtures `ScopedTempDir`, `ScopedWorkingDirectory` and `ScopedEnv` |
| `core::testing_dialogs` | object | `core::testing` | calls it during static initialisation, in every executable that links it |
| `core::testing_main` | static | Catch2 3.8, `core::log` | `main()` for a Catch2 test binary, with the `LOG` filter and the exit-code contract |

`core::testing` and `core::testing_dialogs` are always built, including under Emscripten, where
they do nothing. `core::testing_main` is built when `CORE_CPP_CATCH2_MAIN` is on, which
`CORE_CPP_TESTING` forces.

## Windows dialogs

A failed `assert()`, an `abort()`, an invalid argument to a CRT function or a crash in a Windows
Debug build opens a modal dialog and waits for a click. Under ctest nobody clicks, so a test that
should fail in a second holds the run until its timeout; one held an endo test run for 58
minutes.

`suppressWindowsDialogs()` sends CRT assert, error and warning reports to stderr, where they stay
visible, stops `abort()` from showing a message box or asking Windows Error Reporting, makes an
invalid CRT argument return an error, and sets the process error mode so no critical-error,
fault or open-file dialog appears. It is defined out of line, so including its header does not
include `<Windows.h>`, and it is a no-op on other platforms.

**Call it from nowhere.** `core::testing_dialogs` calls it from a static initialiser that runs in
the library initialisation segment, ahead of ordinary static initialisers, and
`core::testing_main` puts that object on the link line of every test executable. An executable
with its own `main()` links `core::testing_dialogs` directly. A call that each `main()` must
remember is how the dialog came back in endo.

## The exit-code contract

| Outcome | Exit status |
|---|---|
| any assertion or test case failed | 1 |
| every test case that ran was skipped | 77 |
| no test case ran and Catch2 said so | 2 |
| nothing failed, but Catch2 still reported an error | 1 |
| otherwise | 0 |

Catch2 3.8's own `main` returns 42 when anything failed and 4 when every test case skipped, and
ctest reads both as a failure, so a binary whose tests all skip, because their environment could
not be arranged, is reported as broken. Registering such a binary with `SKIP_RETURN_CODE 4`
instead makes a test with exactly four failed assertions read as skipped. core-cpp normalises
the status in `main()` and registers every test with `SKIP_RETURN_CODE 77`.

`core::testing::normalisedExitCode(Catch::Totals const&, int)` in `<core/testing/ExitCode.hpp>`
is the mapping; `core::testing::SkipExitCode` is 77, and `core::testing_main` carries it as its
target property `CORE_CPP_SKIP_EXIT_CODE`. `tests/cmake/check-exit-codes.cmake` asserts the
contract from outside the binary, natively and under node.

To use it in your own tests, see
[Using core-cpp with CPM](../getting-started/cpm.md#using-coretesting_main-in-your-own-tests).

## The LOG filter

Before it runs the tests, `core::testing_main`'s `main()` reads `LOG` from the environment and
applies it to [`core::log`](log.md) with `core::log::configure()`: `LOG=net` enables the `net`
category and disables every other one but `error`, and `LOG=all` or `LOG=net.*` work as
`configure()` says. It also gives the categories the standard formatter and enables the console
sink, so what they log appears on standard output, interleaved with Catch2's. An unset or empty
`LOG` changes nothing. This is endo's `test_main` convention.

```sh
LOG=all ctest --preset clang-debug -R core-cpp.log --output-on-failure
```

A category the filter should reach must exist when `main()` runs, which a namespace-scope
`core::log::Category` does. Under node the WebAssembly build sees no host environment, so `LOG`
has no effect there.

## A test double

`core::testing::FakeEnvironment` is a `core::Environment` that holds exactly the variables a
test gives it, for code that takes its environment by reference instead of reading the process's.

## Scoped fixtures

Imported from endo's `src/testing` at `f774a210`. Each undoes what it did when it goes out of
scope, so a failing assertion cannot leak the change into the next test. Prefer a test double
where the code under test takes one (`FakeEnvironment`,
`core::platform::testing::InMemoryFileSystem`, `TestProcessEnvironment`): these fixtures change
state that the whole test binary shares.

| Header | What it has |
|---|---|
| `<core/testing/ScopedTempDir.hpp>` | `ScopedTempDir`, a directory unique to the instance (`mkdtemp`, or the process id and a counter on Windows), removed with its contents on destruction; `path()`, `operator/`, and `string()` in the generic form |
| `<core/testing/ScopedWorkingDirectory.hpp>` | `ScopedWorkingDirectory`, which changes the working directory and changes it back |
| `<core/testing/EnvHelper.hpp>` | `setTestEnv()`, `unsetTestEnv()`, and `ScopedEnv`, which sets a variable and restores the value it replaced, or removes it |

A fixture that cannot be set up throws, and Catch2 reports the test as failed: an empty temporary
path, for one, would quietly put the fixture in the working directory. On POSIX `EnvHelper`
writes through `core::setProcessEnvironmentVariable()` rather than `setenv()`. On Windows it uses
`_putenv_s()`, which updates both the CRT's copy of the environment and the operating system's;
there an empty value removes the variable.
