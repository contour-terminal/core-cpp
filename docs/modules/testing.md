# testing

Test support that core-cpp's own tests use and that consumers can link: suppression of the
Windows dialogs that hang an unattended run, and a Catch2 `main()` whose exit status says what
happened. Namespace `core::testing`, directory `src/core/testing/`.

| Target | Kind | Needs | What it is |
|---|---|---|---|
| `core::testing` | static | nothing | `core::testing::suppressWindowsDialogs()` |
| `core::testing_dialogs` | object | `core::testing` | calls it during static initialisation, in every executable that links it |
| `core::testing_main` | static | Catch2 3.8 | `main()` for a Catch2 test binary, with the exit-code contract |

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

## Planned additions

Task A4 adds `ScopedTempDir`, `ScopedWorkingDirectory` and `EnvHelper` from endo. Task A3 adds a
`LOG` filter to `core::testing_main`, so `LOG=net` enables the `net` log category in a test run.
