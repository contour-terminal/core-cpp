# Dispatch: Task A7 (import `core::tui_output` + `core::tui` from endo)

## Where this fits

core-cpp (D:\core-cpp, github.com/contour-terminal/core-cpp, branch `master`) is the shared C++23 library of the Contour projects. Phase A imports code one module per task. A1–A6 are done:
- `core::async`: Task, StopToken and friends;
- `core::platform`: Clock, SystemPipe, Wakeup, `Types.hpp` with NativeHandle/InvalidHandle, and SignalHandler;
- `core::net`: contour's EventLoop over EventSource, and sockets.

A7 imports endo's terminal UI. The only changes are:
- namespaces;
- the platform-directory layout;
- the leaf-target split;
- the SyncGuard fix, test first;
- one fastcached delta.

Moving the TUI runtime onto `core::net::EventLoop` happens in Phase B (B12). Do not start it here.

## Requirements

Read your brief first: `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A7-brief.md`. It is your requirements; use the exact values in it verbatim. It names `global-constraints.md` and the spec, and both are binding. Note the rule there that **WSL is shared with other sessions**.

## Upstream state (checked by the controller)

- **endo:** `origin/master` is still `f774a210`. `src/tui` has no commits past the pin. Read blobs with `git -C D:\endo -c core.autocrlf=false -c core.eol=lf show f774a210:src/tui/<path>`, and refuse CR bytes.
- **fastcached** (read-only; worktree rule: only `show`, never touch `D:\fastcached`'s working tree):
  - `origin/master` is `5389e29a`. Its `vendor/endo/tui` equals endo `f774a210` except `runtime/TuiRuntime.hpp` and `runtime/TuiRuntime_test.cpp`.
  - fastcached commit `6483abd8` (2026-09-18) changes `DelayAwaiter`. `await_ready` becomes a constant `false`, and the elapsed-deadline check moves into `await_suspend`, because MSVC 19.44's ARM64 code generator loses the enclosing `try` block when `await_ready` reads the clock through a virtual `now()`.
  - **Import those two files from fastcached, not endo:** `git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 5389e29a:vendor/endo/tui/runtime/TuiRuntime.hpp` (and `_test.cpp`).
  - Their provenance rows name `LASTRADA-Software/fastcached`, the path `vendor/endo/tui/runtime/…`, the full SHA of `5389e29a` (`git -C D:\fastcached rev-parse 5389e29a`), and a note saying this is endo `f774a210` plus fastcached `6483abd8`.

## Mapping

| endo | core-cpp |
|---|---|
| `namespace tui`, `tui::runtime`, `tui::protocols`, `tui::test`, `tui::runtime::testing` | `core::tui`, `core::tui::runtime`, `core::tui::protocols`, … (namespace = directory; `runtime/` and `completer/` are real sub-namespaces only where endo made them one) |
| `endo::platform::X` used inside tui | `core::platform::X` (A4) |
| `<tui/X.hpp>`, `<platform/X.hpp>`, `<coro/X.hpp>` | `<core/tui/X.hpp>`, `<core/platform/X.hpp>`, `<core/async/X.hpp>` |
| `endo::Generator` | `core::Generator` (in base since A5b) |

## Rulings

**Ruling R40: layout, applied to tui.** The top-level `src/core/tui/` holds only platform-independent code. OS-specific code lives in `posix/` or `windows/` (`linux/`, `bsd/` or `darwin/` if something is that specific), and CMake's per-platform source lists select it. endo's `tui/platform/` directory does not survive:

| endo `tui/platform/` | core-cpp |
|---|---|
| `Terminal.cpp`, `TerminalInput.cpp`, `TerminalOutput.cpp` (POSIX: unistd, termios, ioctl) | `posix/` |
| `TerminalWin32.cpp`, `TerminalInputWin32.cpp`, `TerminalOutputWin32.cpp` | `windows/` |
| `PosixIO.hpp` | `posix/PosixIO.hpp` |
| `Win32Utf.hpp` | `windows/Win32Utf.hpp` |
| `TerminalShared.cpp` (no OS includes) | `detail/`, or top level if it only defines public API |

- No namespace `core::tui::platform` may exist. Inside `core::tui` it would hide `core::platform` (R37).
- Platform directories are in no FILE_SET.
- `TerminalInputWin32_test.cpp` goes to `windows/` as a Windows-only test source. Keep its named-mutex serialisation.
- `tui/runtime/{EventSource,PollEventSource}` stay as they are. B12 deletes them, so do not split them.

**Ruling R41: public headers are platform-clean.**
- `.agent/rules/platform.md` forbids `<windows.h>` in a public header, but endo's `TerminalInput.hpp` includes `<windows.h>` and `<termios.h>` under `#if`. Move the OS types out of the public header:
  - Use a pimpl, or an opaque state struct that `posix/`/`windows/` define.
  - Handles are `core::platform::NativeHandle`.
  - Keep the public API unchanged.
- `TerminalOutput.hpp`'s own `using NativeHandle = void*` under `_WIN32` becomes `core::platform::NativeHandle` from `<core/platform/Types.hpp>`. Check that `core::tui_output` may depend on `core::platform` per the module table (next ruling). If not, keep a tui-local alias without `#if`, and report it.
- `ImageLoader.cpp`'s two `#if _WIN32` blocks: move them behind a small per-platform function in `posix/`/`windows/` if they are logic. If they only pick includes, leave them.
- Record each such change in the provenance notes.

**Targets (Part I §1).**
- `core::tui_output` is a STATIC leaf that depends on **base only**. It holds TerminalOutput, SgrBuilder, SyncGuard, TerminalProtocols, CursorShape, Error, and the `posix/PosixIO`/`windows/Win32Utf` parts it needs.
- Verify, with a hygiene or configure-time check if feasible, that nothing in `tui_output` includes libunicode, `core/async` or `core/net`.
- If TerminalOutput truly needs `core::platform` (only for NativeHandle), prefer a base-level alias, and report it rather than widening the leaf's dependencies silently.
- `core::tui` depends on platform, async, net, libunicode and stb (optional).
- Both targets exist only when `CORE_CPP_WITH_TUI` is ON. They never build under Emscripten (`PLATFORMS native`).
- stb goes behind `CORE_CPP_WITH_IMAGES`. `StbImageImpl.cpp` keeps `-fno-sanitize=undefined` as a PRIVATE per-source option, with no PUBLIC flag.
- The libunicode and stb dependencies resolve through `cmake/CoreCppDependencies.cmake` as they exist since A1. Check that the rows are live and that the libunicode CPM options pass `PEDANTIC_COMPILER OFF`.

**Tests.**
- Step 1 of the brief (SyncGuard through `writeToDestination`, and `isTerminal()`) comes first, with RED shown.
- Every endo tui test comes along, linked to `core::testing_main`, not endo's test main.
- Tests that need a console or TTY SKIP (exit 77) where there is none, and never SUCCEED.
- Tests must be clean under `clang-tsan` and `clang-asan-ubsan`.

**Provenance.** Every new file under `src/core/` needs a row: `contour-terminal/endo`, the upstream path, and the full SHA `f774a210ce989e5947b8f61d715068b1dc96088c` (except the two fastcached files above). Each delta goes in the notes: namespace, layout move, R41 change, and warning fixes.

**Warnings.** Fix them in code. No NOLINT, no pragmas.

**Docs.** Write the following:
- `docs/modules/tui.md`: factual, describing what exists after A7, and saying the runtime moves onto EventLoop in B12.
- CHANGELOG `[Unreleased]`: the import row, and the SyncGuard fix under Fixed.
- NOTICE, if endo tui is missing.
- The AGENT.md module-status sentence.
- The README module rows.

## Local builds (all green before you report)

**Windows.** In PowerShell, run `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`, then configure, build and test `clangcl-debug` (with `--clean-first` after header edits; local fastcache-cc predates fastcached#1531) and `cl-debug`.

**WSL.** For `clang-debug`, `gcc-debug`, `clang-tsan`, `clang-asan-ubsan` and `clang-tidy`:
```
wsl -d Ubuntu-26.04 -- bash -lc 'cd /mnt/d/core-cpp && cmake --preset <p> && cmake --build --preset <p> && ctest --preset <p>'
```
Only `out/build/<preset>` and your own processes are yours; other sessions share WSL. After a `.clang-tidy` edit, rebuild `clang-tidy` with `--clean-first`.

**Format and docs.** Run `python scripts/clang-format.py --check` and `mkdocs build --strict`.

**CI.** Push to `origin master`, then watch the run yourself with `gh run watch <id> -R contour-terminal/core-cpp --exit-status`. Never idle-wait for notifications. The task is done only when `ci-ok` is green. Also run FreeBSD with `gh workflow run portability.yml -R contour-terminal/core-cpp` and watch it. Fix forward if either is red.

## Commits

Make small, semantic commits. The brief names the main subject. Separate commits are welcome, for example:
- the SyncGuard fix (test + fix);
- the R41 header cleanup;
- the fastcached delta.

Each commit ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`.

## Report contract

Write the full report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A7-report.md`. It covers:
- what you implemented;
- TDD evidence (RED and GREEN);
- local results per preset;
- CI run ids and conclusions, including FreeBSD;
- files changed;
- your self-review;
- your concerns.

Then reply in under 15 lines: Status (DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT), commits (short SHA + subject), a one-line test summary, concerns, and the report path.

You never dispatch subagents: no helpers, and no reviewers. Review comes from the controller after your report.

## Addendum (after A6's fix round)

- **Target rows now carry their own DEPS** (A6 fix round 1, Ruling R42). `core_cpp_module_target()` takes a DEPS list that is authoritative for that target.
  - Declare `tui_output` with `DEPS base` only.
  - Declare `tui` with DEPS as needed (base, log, platform, async, net, tui_output) and its dependency targets.
  - The layering check then refuses `tui_output → core::platform` or any other link you did not declare. If TerminalOutput needs `NativeHandle`, R41's base-level alias is the way out.
- **No file-wide platform guards.** Files in `posix/`, `windows/`, `linux/` and `bsd/` carry no file-wide `#ifdef _WIN32`/`#ifndef _WIN32`, because CMake's per-platform source lists select them. Keep only an `#if` that selects variants inside one platform family, and note it in provenance.

## Also fold in (from A6's re-review)

Add a paragraph to `.agent/rules/library-hygiene.md`'s Layering section, next to where the DEPS mechanism is described:
- `DEPS` bounds only a target that is *given* a row. A secondary target without a row falls back to its module's row, which allows the module's other targets.
- A module's targets may link each other by design (Ruling R43): `core::net` → `core::net_types` is legitimate.
- The check sees the links it is given. A `core::` library hidden behind a generator expression, or added by a raw `target_link_libraries()` outside `core_cpp_add_module()`, is invisible to it.
- `core::tui_output` therefore gets its own row with `DEPS base`.
