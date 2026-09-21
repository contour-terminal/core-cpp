# Dispatch: Task A6 (import `core::net` from contour, as-is)

## Where this fits

core-cpp (D:\core-cpp, github.com/contour-terminal/core-cpp, branch `master`) is the shared C++23 library of the Contour projects. Phase A imports code module by module.
- A1–A5b are done.
- `core::async` (renamed from coro in A5b) holds Task, WhenAll/WhenAny, Cancellation and StopToken.
- `core::platform` (A4) holds the merged Clock, SystemPipe, WinsockInit, Wakeup, `Types.hpp` (NativeHandle, InvalidHandle, platformRead/Write/Close) and PlatformError.

A6 imports contour's `src/net` **as-is apart from namespaces and the platform swap**. The redesign (IoBackend replacing EventSource, the merged fastcached sockets, IOCP) is Phase B: do not start it here.

## Requirements

Read your brief first: `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A6-brief.md`. It is your requirements; use its exact values verbatim. It names `global-constraints.md` and the spec, and both are binding.

## Upstream state (checked by the controller at dispatch)

- `git -C D:\contour fetch origin`: contour `origin/master` is still `6777ff05`. `src/net` and `src/coro` have no commits past the pin, so `6777ff05` is the synced SHA for every provenance row.
- Read the blobs with `git -C D:\contour -c core.autocrlf=false -c core.eol=lf show 6777ff05:src/net/<path>`, and refuse CR bytes.
- The upstream file list is `src/net/CMakeLists.txt` at that SHA: 72 files, with WIN32 vs POSIX source lists.

## Mapping

| contour | core-cpp |
|---|---|
| `namespace net` | `namespace core::net` |
| `coro::` / `<coro/X.hpp>` | `core::async::` / `<core/async/X.hpp>` |
| `net/platform/Clock.hpp` (`net::IClock`, `SteadyClock`, `ManualClock`, default clock) | `core::platform` Clock (A4's merge). Delete the copy. Adapt call sites to the merged API (`now()`, the no-op `refresh()`). Where contour's API differs, record it in provenance notes. |
| `net/platform/SystemPipe.*`, `net/platform/WinsockInit.*` | `core::platform::SystemPipe` / `WinsockInit`. Delete the copies. `SystemPipe::read` now returns `std::expected<ChannelResult, PlatformError>` (A4), so adapt the callers. |
| `net/platform/NativeHandle.hpp` (namespace `net`) | `core::platform` `Types.hpp` (`NativeHandle`, `InvalidHandle`, `platformRead`/`Write`/`Close`). Delete the copy. |
| `net/platform/PeerAddress.hpp` (namespace `net`) | `src/core/net/PeerAddress.hpp`, namespace `core::net` |
| `net/platform/WindowsLoopback.*` | `src/core/net/windows/WindowsLoopback.*` (private: in no FILE_SET) |
| `Tls.cpp`, `Tls_test.cpp` | the `core::net_tls` target (STATIC), built only if `CORE_CPP_WITH_TLS`. OpenSSL is PRIVATE and no OpenSSL type appears in a header. |
| `NetError.hpp`, `IoResult.hpp` | the `core::net_types` target (INTERFACE, header-only) |
| `detail/`, `posix/`, `windows/` | private: in no FILE_SET (Part I §1) |
| `testing/` (InMemoryTransport, EventSourceBackends, ScriptedEventSource) | public DI fakes compiled into `core::net` (Part I §1) |

## Rulings

**Ruling R37: no `core::net::platform` namespace.** Nothing may create namespace `core::net::platform`. Inside `namespace core::net`, an unqualified `platform::X` would resolve to it and hide `core::platform::X`. That is why PeerAddress moves up to `core::net`, and WindowsLoopback moves to `windows/`.

**Ruling R38: `net` is native-only in A6.**
- The module-table row for `net` gets `PLATFORMS native`, with DEPS as the code really needs (async, platform, and base/log if used directly).
- `net_types` is `PLATFORMS any` (header-only) and must still build under Emscripten.
- The WebAssembly subset of net (IoBackend, EventLoop, timers, HostDriven) arrives with B3–B5, which switch the row.
- Read `cmake/CoreCppModules.cmake` and `cmake/CoreCppTargets.cmake` for how a module with several targets (net_types, net, net_tls) is declared. If the table cannot express sub-targets, extend it in the existing style, and keep the layering check able to refuse an undeclared link.
- **Threads.** contour's net links `Threads::Threads` PUBLIC, because its headers expose `std::mutex`/`std::thread`. core-cpp's rule is that there are no PUBLIC *flags*. A PUBLIC *link* to `Threads::Threads` is a usage requirement, and it is allowed where a public header needs it, as for `core::base`. Keep it, and keep contour's comment on why.

**Ruling R39: TLS is built and tested on Linux and macOS too.**
- Today only `cl-release-tls` (Windows, vcpkg) turns on `CORE_CPP_WITH_TLS`, so `Tls_test` would never run on POSIX.
- Set `CORE_CPP_WITH_TLS=ON` in the hidden `unix` preset (or whichever hidden preset the Linux and macOS presets inherit). The `emscripten` preset keeps it OFF, as it is forced OFF there anyway.
- In `.github/workflows/build.yml`, install `libssl-dev` in every Linux job that configures a unix preset (clang, gcc, arm64, C++26, sanitizers, clang-tidy, coverage, compile-cache). macOS already installs `openssl@3` and exports `OPENSSL_ROOT_DIR`.
- WSL already has libssl-dev 3.5.5.
- The FreeBSD `portability.yml` job needs `security/openssl`, or it relies on base OpenSSL: check it.

**Tests.**
- Tests come first, per the brief.
- Every socket test gets the `loopback` label.
- Anything contour marks as unsafe under TSan gets `no-tsan`, with a reason.
- Contour's `test_main.cpp` is not imported: every binary links `core::testing_main` via `core_cpp_add_test`.
- The tests must run clean under `clang-tsan` and `clang-asan-ubsan` in WSL.

**Provenance.** Every new file under `src/core/` needs a row in `.agent/reference/provenance.md`: upstream `contour-terminal/contour`, the upstream path, and SHA `6777ff05014f8ff163b071e8b0e942830119db80`. Put each delta (namespace, platform swap, fixes for `-Wshadow`, `-Wconversion` and clang-tidy) in the notes column.

**Warnings.** Fix every warning in code, never by suppressing it. No NOLINT, no pragmas.

**Docs.**
- `docs/modules/net.md` describes what exists after A6, factually, and says that the EventSource API is replaced in Phase B.
- Add CHANGELOG `[Unreleased]` rows (the 6777ff05 import, `CORE_CPP_WITH_TLS` coverage).
- Update the NOTICE if it lacks contour net.
- Update the AGENT.md module-status sentence.

## Local builds (all must be green before you report)

**Windows.** In PowerShell, run `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`. Then run configure, build and test for `clangcl-debug` and `cl-debug`.
- On clangcl, build with `--clean-first` after a header edit: the local fastcache-cc predates the fix for fastcached#1531.
- `cl-release-tls` needs vcpkg OpenSSL. Build it locally only if vcpkg is set up; otherwise CI covers it. Say which in your report.

**WSL.** For each of `clang-debug`, `gcc-debug`, `clang-tsan`, `clang-asan-ubsan` and `clang-tidy`:
```
wsl -d Ubuntu-26.04 -- bash -lc 'cd /mnt/d/core-cpp && cmake --preset <p> && cmake --build --preset <p> && ctest --preset <p>'
```

**Format and docs.** `python scripts/clang-format.py --check` and `mkdocs build --strict`.

**Push and CI.** Push to `origin master`, then watch CI yourself with `gh run watch <id> -R contour-terminal/core-cpp --exit-status`. Never wait idle for notifications.
- Done means `ci-ok` is green: macOS with TLS, `cl-release-tls`, both emscripten legs (net_types only), and the sanitizers.
- Fix forward if red.
- Also check the next nightly `portability.yml` (FreeBSD) run, or dispatch it with `gh workflow run portability.yml -R contour-terminal/core-cpp` and watch it.

## Commits

Make small semantic commits. The brief names the main subject. Separate commits are welcome, e.g. for the preset/CI TLS change or the platform swap. Each commit ends with the trailer `Signed-off-by: Christian Parpart <christian@parpart.family>`.

## Report contract

Write the full report to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-A6-report.md`. Cover:
- what you implemented;
- the TDD evidence (RED command and output, GREEN command and output);
- the local results per preset;
- the CI run ids and conclusions, including FreeBSD;
- the files changed;
- your self-review;
- your concerns.

Then reply in under 15 lines: Status (DONE | DONE_WITH_CONCERNS | BLOCKED | NEEDS_CONTEXT), commits (short SHA + subject), a one-line test summary, concerns, and the report path.

You never dispatch subagents: no helpers and no reviewers. Review comes from the controller after your report.

## Also fold in (small carries from earlier tasks)

1. **A5b finding.** `.agent/rules/build-and-toolchain.md` gains a rule: after editing `.clang-tidy`, rebuild the `clang-tidy` preset with `--clean-first`. `.clang-tidy` is not a build input, so up-to-date objects skip the check and hide new findings; A5b's e45f730 fixed exactly such a miss. Cite commit e45f730.
2. **A5b review.** Bring the README's module table (status and targets per row) up to date with what exists after A6: base, log, cli, platform, async, testing and net. It also gets the net rows (`core::net_types`, `core::net`, `core::net_tls`).

Put these in their own `docs:` commit.
