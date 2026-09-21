# Task A6 report: `core::net` import (contour, as-is)

Status: **DONE_WITH_CONCERNS**. `ci-ok` is green on def7325 (Build run 35395135341), and so is
FreeBSD (Portability run 35395187310). The concerns are at the end. None of them blocks the task.
Most are contour behaviour I kept unchanged because the dispatch says "as-is".

Base e45f730, head def7325. Everything is pushed to `origin/master`.

## Commits

| SHA | Subject |
|---|---|
| d07e136 | docs: platform-specific code lives in platform subdirectories, in net's backends too (the controller's Ruling R40 commit, pushed along with mine) |
| cb73274 | build: a module's further targets can have rows of their own in the module table |
| ae97516 | net: import contour's event loop, sockets, TLS and HTTP server as core::net |
| 6dd73ea | net: the event loop refreshes its clock before each timeout and after each wait |
| fa74a80 | build: the Linux, macOS and BSD presets build core::net_tls, and CI installs OpenSSL for them |
| 7b87be3 | net: platform code lives in linux/, bsd/, posix/ and windows/, not in the module's root |
| 3327317 | docs: rebuild the clang-tidy preset from clean after editing .clang-tidy, and README's module rows |
| def7325 | net: FdPassing_test sizes its control message in the platform's own types (fix forward, macOS and FreeBSD) |

Every commit ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`.

## What was implemented

### The module table can describe several targets per module (cb73274)

- `core_cpp_module_target(NAME MODULE KIND PLATFORMS [WHEN])` in `cmake/CoreCppModules.cmake`
  gives one of a module's further targets its own row. `core::net_types` is `PLATFORMS any`
  while `net` is `native`, and `core::net_tls` is `WHEN CORE_CPP_WITH_TLS`. The module's DEPS
  still bound every target it has, so the layering check stays at module level.
- `core_cpp_add_modules()` enters a native-only module under Emscripten when one of its target
  rows builds there. The configure then prints
  `module net: core::net_types only (the rest is native only, and this is Emscripten)`.
- `cmake/CoreCppTargets.cmake` adds `core_cpp_row_builds()` and `core_cpp_target_row()`.
  `core_cpp_add_module()` checks a target's KIND against its row. When the row does not build
  here, it returns without creating the target.
- `core_cpp_add_test(<module> NAME <target>)` links that target when it belongs to the module,
  and builds wherever that target's row builds.
- `tests/cmake/check-platform-sources.cmake` gains 8 scenarios for `core_cpp_row_builds()`.

### The import (ae97516)

- All 72 files of contour's `src/net` at 6777ff05 are imported, changed only as follows:
  - the namespace `net` becomes `core::net`, and `coro` becomes `core::async`;
  - the `net/platform/` copies are replaced by `core::platform`: `SystemPipe`, `IClock`,
    `WinsockInit` and `PlatformError`. Per Ruling R37 there is no `core::net::platform`;
  - contour's `testing/TempDir.hpp` (`net::testing::TempDir`) is not imported. No net test used
    it: upstream `UnixSocket_test.cpp:36` already defines a local `TempDir` of its own, which the
    import keeps unchanged. Its users are contour's `vthost` tests, which move to
    `core::testing::ScopedTempDir` at C6 (see concern 9). *(Corrected in fix round 1: this line
    used to say the upstream header became the local fixture in `UnixSocket_test.cpp`.)*
  - hygiene fixes: 14 C-style `for` loops, three of them linked-list walks, converted to
    `while` with `std::exchange`. The `find_if`/`subrange` rewrites are in `EventLoop.cpp`;
  - GCC fixes: `-Wshadow` and `-Wnull-dereference` at `-O3` in `FdPassing_test.cpp`;
  - `Tls_test.cpp` creates the client context and `REQUIRE`s it before it starts the server
    thread. A failed `REQUIRE` after that point would unwind past a joinable `std::thread` and
    terminate the binary (the testing.md "REQUIRE above a stop" rule).
- `NetError.hpp` is split out of `IoResult.hpp`. It forms `core::net_types` (INTERFACE), with a
  new `NetError_test.cpp` (5 cases), which also runs under Emscripten.
- `core::net` is STATIC. It links `core::net_types`, `core::async`, `core::platform` and
  `Threads::Threads` PUBLIC, keeping contour's comment as Ruling R38 says, and `ws2_32`
  PRIVATE on Windows.
- `core::net_tls` is STATIC and TLS-gated: `Tls.hpp`, `Tls.cpp` and `detail/ScopeGuard.hpp`,
  with OpenSSL PRIVATE. The dependency row is
  `core_cpp_dependency(OpenSSL WHEN CORE_CPP_WITH_TLS ... NO_FETCH)`.
- There are three test binaries: `core-cpp.net_types`, `core-cpp.net` and `core-cpp.net_tls`.
  The last two are labelled `loopback`. Contour marks no net test as tsan-unsafe, so none
  carries `no-tsan`.
- Provenance has 66 rows, each with SHA 6777ff05014f8ff163b071e8b0e942830119db80 and a note of
  what changed.
- Docs and rules updated: `docs/modules/net.md` (rewritten), NOTICE, CHANGELOG (Added, Imported),
  source-map, consumer-migration (the SystemPipe, TempDir and private-header rows),
  async-and-net.md status, testing.md, and the AGENT.md status line.

### EventLoop refreshes its clock (6dd73ea)

- `EventLoop::pumpOnce()` now calls `_clock.refresh()` twice. The first call comes before it
  computes the wait timeout. The second comes after `_source.wait()` returns, before it
  dispatches timers. This implements the `IClock::refresh()` contract for caching clocks.
- It is covered by a new test case, "The loop refreshes a caching clock before each timeout and
  after each wait".

### TLS in the unix presets (fa74a80)

- Per Ruling R39, the hidden `unix` preset sets `CORE_CPP_WITH_TLS=ON`.
- `libssl-dev` is installed in every Linux CI job that configures a unix preset: linux,
  sanitizers, clang-tidy, compile-cache and coverage. macOS uses Homebrew's OpenSSL, and
  FreeBSD uses the base system's, which `portability.yml` now notes in a comment.
- `docs/getting-started/building.md` and `options.md` state the OpenSSL requirement.

### The layout rule (7b87be3), implementing Ruling R40

- The epoll backend moves to `linux/`, kqueue to `bsd/`, and the POSIX sockets to `posix/`.
- `PollEventSource.cpp` is split into `posix/` and `windows/`. Its header stays portable, and
  `_waitRotation` is now unconditional.
- `WaitChunking.hpp` and `PeerAddress.hpp` move to `detail/`.
- `FdPassing_test` and `UnixSocket_test` move to `posix/`.
- `DefaultEventSource.cpp` keeps its `#ifdef`s until Task B3.
- `platform.md` states the layout rule.

### Carries (3327317)

- `build-and-toolchain.md`: after editing `.clang-tidy`, rebuild the `clang-tidy` preset with
  `--clean-first`, citing e45f730.
- The README module table lists base, log, cli, platform, async, testing, and net with its three
  targets.

### Fix forward (def7325)

- On macOS and FreeBSD, `msghdr::msg_controllen` and `cmsghdr::cmsg_len` are `socklen_t`.
  `CMSG_SPACE()` and `CMSG_LEN()` return `size_t`, so `-Wshorten-64-to-32 -Werror` refused the
  assignment.
- Both assignments now `static_cast<decltype(field)>`. The provenance note is updated.

## TDD evidence

**RED 1: the tests before the code.** The test files and CMake registration went in before any
header or source. `cmake --build --preset clang-debug` then failed in every test TU:
`fatal error: 'core/net/...hpp' file not found`. That covered core-cpp-net-test,
core-cpp-net_types-test, core-cpp-net_tls-test and `testing/InMemoryTransport.cpp`. **GREEN 1:**
after the import, the build succeeded and `ctest --preset clang-debug` passed 15/15.

**RED 2: the clock refresh.** I reproduced it again on the final tree by deleting both
`_clock.refresh()` lines from `EventLoop.cpp` and running
`out/build/clang-debug/src/core/net/core-cpp-net-test "[clock]"`:

```
EventLoop_test.cpp:222: FAILED:  CHECK( source.recordedTimeouts().back() == 400 )  with expansion: 500 (0x1f4) == 400 (0x190)
EventLoop_test.cpp:228: FAILED:  CHECK( source.recordedTimeouts().back() == 150 )  with expansion: 500 (0x1f4) == 150
EventLoop_test.cpp:229: FAILED:  CHECK( fired )  with expansion: false
test cases:  3 |  2 passed | 1 failed      assertions: 14 | 11 passed | 3 failed
```

Each refresh fails exactly one CHECK of its own when it is deleted alone:

| Refresh deleted | Only failure |
|---|---|
| the one before the timeout | `500 == 400` at line 222 |
| the one after the wait | `fired == false` at line 229 |

**GREEN 2:** `All tests passed (14 assertions in 3 test cases)`.

The first version of this test segfaulted rather than failing. `ScriptedEventSource` threw
"script exhausted" while a flow was still parked, and `~EventLoop` then used freed memory. That
hazard was already in contour (see the concerns). I redesigned the test to use `spawn` plus a
one-pump `blockOn(justReturn())`, which gives the clean RED above.

## Local results (final tree; FdPassing_test re-verified on def7325)

| Preset | Result |
|---|---|
| WSL clang-debug | build OK, 15/15. net 120 cases / 626 assertions, net_types 5 / 56, net_tls 6 / 36 |
| WSL gcc-debug | 15/15 |
| WSL gcc-release | 15/15 (this preset caught the `-O3 -Wnull-dereference`) |
| WSL clang-tsan | 15/15 |
| WSL clang-asan-ubsan | 15/15 |
| WSL clang-tidy (pinned 22.1.8, canary verified) | clean, 15/15 |
| WSL emscripten 3.1.56 | 13/13. net_types only; 0 pthread mentions |
| WSL StopToken-fallback build (the AppleClang configuration) | net 120 / 626, net_tls 6 / 36 |
| Windows cl-debug | 17/17 (net 103 cases / 420 assertions) |
| Windows clangcl-debug (`--clean-first`) | 17/17 |
| Windows cl-release-tls | **not built locally**: VS-bundled vcpkg wants a `builtin-baseline`. CI builds it (green) |
| `python scripts/clang-format.py --check` | 188 files clean |
| `python -m mkdocs build --strict` | clean |
| `ctest -L hygiene` (clang-debug, on def7325) | 5/5 |

Per-commit check: each of cb73274, ae97516, 6dd73ea, fa74a80, 7b87be3 and 3327317 builds and
passes on clang-debug and gcc-release.

def7325 changes only a POSIX test file and a provenance row. I re-ran clang-debug, gcc-release
and clang-tidy on it (15/15 each). It does not affect the Windows builds.

## CI

| Run | Commit | Conclusion |
|---|---|---|
| Build 35393878307 | 3327317 | failure. Only macos (appleclang) and macos (llvm-22) failed: `FdPassing_test.cpp:43/53 -Wshorten-64-to-32` |
| Docs 35393878271 | 3327317 | success |
| Portability 35393912745 (FreeBSD) | 3327317 | failure. Same two lines; OpenSSL was found |
| **Build 35395135341** | **def7325** | **success, `ci-ok` green**. See below |
| **Portability 35395187310 (FreeBSD, system clang)** | **def7325** | **success**. OpenSSL found by find_package; 15/15 including core-cpp.net (kqueue) and core-cpp.net_tls |

Jobs in Build 35395135341:

- linux: clang-22, clang-22-arm64, clang-22-cxx26, clang-22-tracy, gcc-14, gcc-15
- macos: appleclang, llvm-22 (both with TLS)
- windows: cl-debug, cl-release, clangcl-release, cl-release-tls
- sanitizers: clang-asan-ubsan, clang-tsan
- emscripten: emsdk 3.1.56, emsdk latest
- clang-tidy, compile-cache, coverage, style

Dependabot's PR run 35394136482 (branch `dependabot/github_actions/...`) failed on the same two
macOS jobs. Its base is 3327317; a rebase onto def7325 should clear it.

## Files changed (e45f730..def7325)

The range changes 94 files: +12699 / −106.

- `src/core/net/`: 66 files.
  - Portable: the public headers, the portable sources, `testing/`, the tests, and `detail/`
    (PeerAddress, WaitChunking, ScopeGuard, WouldBlock).
  - Platform code: `posix/`, `linux/`, `bsd/` and `windows/`.
  - Also the new `NetError.hpp` and `NetError_test.cpp`, and the module's `CMakeLists.txt`.
- CMake:
  - `cmake/CoreCppModules.cmake`, `cmake/CoreCppTargets.cmake`,
    `cmake/CoreCppDependencies.cmake`
  - `CMakePresets.json`
  - `tests/cmake/check-platform-sources.cmake`
- CI: `.github/workflows/build.yml`, `.github/workflows/portability.yml`.
- Docs:
  - `docs/modules/net.md`, `docs/modules/index.md`
  - `docs/design/portability.md`
  - `docs/getting-started/building.md`, `docs/getting-started/options.md`
  - `README.md`, `CHANGELOG.md`, `NOTICE`, `AGENT.md`
- Agent files:
  - `.agent/reference/provenance.md`, `.agent/reference/source-map.md`
  - `.agent/guides/consumer-migration.md`
  - `.agent/rules/`: async-and-net, build-and-toolchain, cpp-guidelines, design-principles,
    library-hygiene, platform, testing
- Plan and spec: d07e136 edited `docs/superpowers/plans/2026-09-18-core-cpp.md` and
  `docs/superpowers/specs/2026-09-18-core-cpp-design.md`. That commit is the controller's.

## Self-review

- No `NOLINT` and no diagnostic pragma in `src/core/net`. clang-tidy 22.1.8 is clean, and so are
  the format check and hygiene.
- Every imported file has a provenance row, and each row notes every change beyond namespaces
  and includes.
- No OpenSSL type appears in a public header. `core::net_tls` links OpenSSL PRIVATE.
- `core::net_types` builds under Emscripten without `Threads::Threads`: the emscripten legs show
  0 pthread mentions.
- A new header path would silently fail a consumer's include. The consumer-migration guide lists
  every path that changed: the `platform/` copies, `TempDir`, and the headers that moved to
  `detail/` or a platform directory.
- `PollEventSource.hpp` stays portable. `_waitRotation` is used only by the Windows half, so it
  carries `[[maybe_unused]]` (see the concerns).
- The added test would detect a regression: deleting either refresh fails exactly its own CHECK.

## Concerns

1. **Ruling R40 never reached me as a message.** I implemented it from `progress.md` (line 197)
   and the controller's commit d07e136, which I pushed together with my commits. Please confirm
   that the layout matches the ruling you meant.
2. **`[[maybe_unused]] std::size_t _waitRotation`** in `PollEventSource.hpp`. Once it became
   unconditional, Clang's `-Wunused-private-field` flags it in the POSIX half.
   `[[maybe_unused]]` is the pragma-free fix. The alternative is a platform-specific member,
   which the portable-header rule forbids.
3. **Contour behaviour kept as-is:**
   - `Tls.hpp` has a default CN `"contour-daemon"`: a consumer-specific string in core-cpp.
   - The coroutines `connect`, `connectUnix` and `readUntil` take `std::string_view`. The
     cpp-guidelines rule says coroutine parameters go by value.
   - `FdRegistrationFailed` and `EventLoop`'s `std::runtime_error` are exceptions where the
     design principles want `std::expected`.

   These are candidates for Phase B.
4. **An exception out of `blockOn()` while a flow is parked leads to a use-after-free** in
   `~EventLoop`. For example, `ScriptedEventSource`'s "script exhausted" segfaults instead of
   failing. The hazard comes from contour, and I did not fix it (as-is).
5. **The two-reactor TLS tests hang rather than fail** if the client's connect fails: the server
   thread's wait for its peer has no bound. That breaks the testing.md "every wait is bounded"
   rule. The tests came from contour this way.
6. **The `loopback` label covers the whole `core-cpp.net` and `core-cpp.net_tls` binaries**, not
   only the socket cases, because labels attach to a binary.
7. **`cl-release-tls` was not built locally** (vcpkg baseline). CI is green on it.
8. **The Epoll and Kqueue sources keep their internal `#ifdef __linux__` / BSD guards** even though
   they now live in platform directories. The guards are redundant but harmless; removing them
   would be a behaviour-free change for Task B3.
9. **Carry to C6:** contour's vthost tests must use `core::testing::ScopedTempDir` in place of
   `net::testing::TempDir`. The consumer-migration guide records this.

## Fix round 1 (Ruling R42)

Carried out by a new implementer, from `task-A6-fixround1.md` and `task-A6-review.md`. Base
def7325, head c4a083a, pushed to `origin/master`.

### Commits

| SHA | Subject | Item |
|---|---|---|
| c7712a3 | build: a target's row in the module table carries its own DEPS | 2 (Important 2) |
| ae72783 | net: a platform directory's sources carry no file-wide guard of their platform | 1 (Important 1), 4 (guard notes) |
| 10f51d9 | net: makeSocketPair() is split into testing/posix/ and testing/windows/ | 3 (Minor 1) |
| 04a3e19 | docs: provenance names PeerAddress.hpp's detail/ path and Tls.cpp's dropped NOLINTs | 4 (Minor 2) |
| c4a083a | docs: the migration guide says contour's TLS is core::net_tls, behind CORE_CPP_WITH_TLS | 5 (Minor 3) |

Each ends with `Signed-off-by: Christian Parpart <christian@parpart.family>`. Item 6 (Minor 4)
is the corrected sentence under "The import" above; this file is not tracked.

### What changed

**Item 1: no file-wide platform guards (ae72783).**
- The guards are gone from all 22 guarded files in `src/core/net/{posix,windows,linux,bsd}/`:
  - `#ifndef _WIN32` in the 11 `posix/` files, including `FdPassing_test.cpp` and
    `UnixSocket_test.cpp`;
  - `#ifdef _WIN32` in the 7 `windows/` files. Four of them also had one around their leading
    `<winsock2.h>` block, and those went too; the blocks keep their `// clang-format off`;
  - `#ifdef __linux__` in `linux/EpollEventSource.{hpp,cpp}`;
  - the four-platform kqueue `#if` in `bsd/KqueueEventSource.{hpp,cpp}`.
- The pinned clang-format un-indented the includes.
- The `#if`s that choose within one platform family stay:
  - `AcceptLoop.cpp`: `#ifdef __linux__` (`accept4()`) and `#ifndef __linux__` (the `fcntl()`
    fallback);
  - `FdPassing_test.cpp`: `#ifndef __APPLE__`;
  - `PosixSocket.cpp`: `#ifndef MSG_CMSG_CLOEXEC` and `#ifdef SO_NOSIGPIPE`;
  - `FdUtils.hpp`, which never had a guard: `#ifndef MSG_NOSIGNAL` and
    `#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)`;
  - `WindowsListener.cpp`: `#ifndef IO_REPARSE_TAG_AF_UNIX`.
- Each of those files' provenance rows names what it keeps. Every changed file's row says that its
  guard was dropped and which source list selects the file.
- `.agent/rules/platform.md` states the rule, and so does the module's `CMakeLists.txt` comment.
- A check found no other includer that relied on a guard. Every include of a platform header is
  from its own directory, from `DefaultEventSource.cpp` (under its `#ifdef`s), or from the old
  `InMemoryTransport.cpp` (item 3).

**Item 2: target rows carry their own DEPS (c7712a3).**
- `core_cpp_module_target()` takes `DEPS`. Each entry is either another target of the same
  module, by name, or a module that the parent row lists in DEPS. A same-module target is the
  module's own target or one with an earlier row. A module entry lets the target link any target
  of that module. Anything else is refused when the row is declared, and the message names the
  row and the entry.
- A row's DEPS are authoritative. A row without DEPS links no core-cpp target.
- `core_cpp_target_row()` now also returns `_DEPS`, plus `_OWN`, which says whether the target
  has a row of its own.
- `core_cpp_check_layering(<name> <module> <libs>)` looks up the target's row. A target with a
  row of its own may link only what that row's DEPS name, so a same-module link is no longer
  implied for it.
- A target that follows its module's row keeps the module-level rule: the modules the row lists,
  plus the module's other targets. Examples are `core::net` → `core::net_types`, and
  `testing_main`/`testing_dialogs` → `core::testing`. R42 gives `net`'s own row no change, so I
  read "checks each target against its own row" as applying the new bound to targets that have a
  row. The comment in `CoreCppModules.cmake` documents that reading.
- Table: `net_types` has no DEPS, and `net_tls` has `DEPS net`.
- Proof: `tests/cmake/check-layering.cmake`, 12 scenarios.
  - Each scenario is a configure of its own, with `LANGUAGES NONE`, over the real table, plus the
    rows the scenario adds and INTERFACE stand-ins for what it links. It either configures, or it
    must fail with a message that matches a pattern naming the target and the link, or the row and
    the entry.
  - It is registered as `core-cpp.layering` (labels `core-cpp;hygiene`) and runs in CI's `style`
    job ("Module table layering").
- Docs: the comments in `CoreCppModules.cmake` and `CoreCppTargets.cmake`,
  `docs/modules/index.md` (Layering), `.agent/rules/library-hygiene.md` (Layering),
  `source-map.md`, and the CHANGELOG `[Unreleased]` entry for `core_cpp_module_target()`.

**Item 3: `InMemoryTransport.cpp` split (10f51d9).**
- `testing/posix/InMemoryTransport.cpp` (`SOURCES_POSIX`) and
  `testing/windows/InMemoryTransport.cpp` (`SOURCES_WINDOWS`) each hold one branch of contour's
  `makeSocketPair()` with its includes.
- The Windows half includes `<array>` itself; contour's branch got it through
  `WindowsLoopback.hpp`.
- The declaration stays in the public `testing/InMemoryTransport.hpp`.
- The nested directories match the hygiene scan's namespace rule (`core::net::testing` under
  `src/core/net/`). The files stay beside the test doubles rather than among the production
  sockets.
- `docs/modules/net.md` and the module's `CMakeLists.txt` comment now name the two exceptions
  that remain:
  - `DefaultEventSource.cpp`, until B3;
  - `EventSourceParity_test.cpp`'s two POSIX-only cases (a closed descriptor's registration, and
    descriptor exhaustion) and their includes.
- Provenance has two rows in place of one, and the source map lists the halves.

**Item 4: provenance (04a3e19, plus ae72783 for the guard notes).**
- The `posix/AcceptLoop.cpp` and `windows/WindowsListener.cpp` rows say "`PeerAddress.hpp` from
  `detail/`".
- The `Tls.cpp` row adds "no `NOLINT`". It names the three `NOLINTNEXTLINE(readability-identifier-naming)`
  comments on `HandshakeGate`'s `await_*` hooks (contour 6777ff05 lines 163, 165 and 170), which
  `.clang-tidy`'s `MethodIgnoredRegexp` makes unnecessary.

**Item 5: consumer migration (c4a083a).** There is an API-delta row: contour's `net`, which
built `Tls.*` in and always needed OpenSSL, becomes `core::net_tls`. The consumer configures
`CORE_CPP_WITH_TLS ON`, which the CPM snippet has OFF, and links `core::net_tls`. The contour row
says the same for the daemon.

**Item 6: report.** The sentence under "The import" now says that `testing/TempDir.hpp` was not
imported. No net test used it: upstream `UnixSocket_test.cpp:36` defines its own local
`TempDir`, which is unchanged. Its users are the `vthost` tests (C6).

### Item 2: RED, then GREEN

**RED.** The test was written first and run against def7325's mechanism, with nothing else
changed:

```
wsl -d Ubuntu-26.04 -- bash -lc 'cd /mnt/d/core-cpp && cmake -DROOT=/mnt/d/core-cpp -DWORK_DIR=<scratch>/layering -DGENERATOR=Ninja -P tests/cmake/check-layering.cmake'
-- ok    net_types-links-nothing: configures
-- FAIL  net_types-links-async: was not refused
-- FAIL  net_types-links-net: was not refused
-- FAIL  row-links-its-deps: refused (1): ... core_cpp_module_target(net_x): unexpected arguments: DEPS;net;async ...
-- FAIL  row-links-undeclared-module: refused, but not by name ('core-cpp-net_x links core::platform'): ... unexpected arguments: DEPS;async ...
-- FAIL  row-links-undeclared-sibling: refused, but not by name ('core-cpp-net_x links core::net_types'): ... unexpected arguments: DEPS;net ...
-- FAIL  row-without-deps-links-a-module-dep: was not refused
-- FAIL  row-deps-outside-its-module: refused, but not by name ('core_cpp_module_target\(net_x\): DEPS names 'base''): ... unexpected arguments: DEPS;async;base ...
-- FAIL  row-deps-an-undeclared-sibling: refused, but not by name (... DEPS names 'net_y''): ... unexpected arguments: DEPS;net_y ...
-- FAIL  row-deps-itself: refused, but not by name (... DEPS names 'net_x''): ... unexpected arguments: DEPS;net_x ...
-- ok    module-links-its-targets: configures
-- ok    module-links-undeclared-module: refused, naming 'core-cpp-demo links core::log'
CMake Error at tests/cmake/check-layering.cmake:129 (message):
  check-layering: failed: net_types-links-async;net_types-links-net;row-links-its-deps;...
```

- `net_types → core::async` and `net_types → core::net` configured without complaint, as did a
  row without DEPS linking one of its module's DEPS. The six DEPS scenarios were refused only
  because `DEPS` was an unexpected argument.
- The two module-row scenarios passed. They pin the behaviour that is kept.
- The last one is also the first test of the existing module-level refusal, which nothing tested
  before.

**GREEN** (c7712a3):

```
-- ok    net_types-links-nothing: configures
-- ok    net_types-links-async: refused, naming 'core-cpp-net_types links core::async'
-- ok    net_types-links-net: refused, naming 'core-cpp-net_types links core::net[^_]'
-- ok    row-links-its-deps: configures
-- ok    row-links-undeclared-module: refused, naming 'core-cpp-net_x links core::platform'
-- ok    row-links-undeclared-sibling: refused, naming 'core-cpp-net_x links core::net_types'
-- ok    row-without-deps-links-a-module-dep: refused, naming 'core-cpp-net_x links core::async'
-- ok    row-deps-outside-its-module: refused, naming 'core_cpp_module_target\(net_x\): DEPS names 'base''
-- ok    row-deps-an-undeclared-sibling: refused, naming 'core_cpp_module_target\(net_x\): DEPS names 'net_y''
-- ok    row-deps-itself: refused, naming 'core_cpp_module_target\(net_x\): DEPS names 'net_x''
-- ok    module-links-its-targets: configures
-- ok    module-links-undeclared-module: refused, naming 'core-cpp-demo links core::log'
-- check-layering: all 12 scenario(s) configure or are refused as they should
```

The refusal the review asked for, in full:

```
CMake Error at /mnt/d/core-cpp/cmake/CoreCppTargets.cmake:170 (message):
  core-cpp-net_types links core::async, which the 'net_types' row of
  cmake/CoreCppModules.cmake does not allow: its DEPS do not name 'async'
  (it has none, so the target links no core-cpp target).
```

That message was reworded after the GREEN run, before the commit. The first version always
appended "A row without DEPS links no core-cpp target". The test pattern
(`core-cpp-net_types links core::async`) is unaffected, and the committed tree passes, as the
ctest runs below show.

### Local results (head c4a083a; builds at 10f51d9 or later, and the docs commits after it change no build input)

| Preset | Result |
|---|---|
| WSL clang-debug | 16/16, including `core-cpp.layering`. net 120 cases / 626 assertions (unchanged), `[fdpass]` 7 / 64 |
| WSL gcc-release | 16/16 |
| WSL clang-tsan | 16/16 |
| WSL clang-tidy (pinned `~/.local/bin/clang-tidy`, LLVM 22.1.8) | 16/16. The 14 changed TUs were rebuilt through clang-tidy with no finding |
| WSL clang-asan-ubsan | 16/16 |
| WSL emscripten (emcc 3.1.56) | 14/14. `module net: core::net_types only`, `core-cpp.layering` passes, 0 pthread mentions in `build.ninja` |
| Windows clangcl-debug (`--clean-first`, 208 steps) | 18/18. net 103 cases / 420 assertions (unchanged). No warning in the log |
| Windows cl-debug | 18/18. net 103 / 420. No warning |
| `python scripts/clang-format.py --check` | 189 files clean |
| `python -m mkdocs build --strict` | clean |

Commands: the matrix ran as
`wsl -d Ubuntu-26.04 -- bash <script>`, which runs `cmake --preset <p> && cmake --build --preset <p>
&& ctest --preset <p>` for each preset. On Windows, the VS dev shell (`Launch-VsDevShell.ps1
-Arch amd64 -HostArch amd64`) ran the same three steps, with `--clean-first` on clangcl-debug.
Every ctest ended with `100% tests passed`. Every tree was `out/build/<preset>`.

### CI (c4a083a)

| Run | Conclusion |
|---|---|
| **Build 35413031946** | **success, all 21 jobs, `ci-ok` green**. The `style` job's new step "Module table layering" prints `check-layering: all 12 scenario(s) configure or are refused as they should`. `core-cpp.layering` passed in every job that runs ctest, including emscripten 3.1.56 (under node), the Windows jobs, macOS llvm-22, tsan, arm64 and coverage. The macOS jobs compiled `bsd/` and `posix/` without their guards |
| Docs 35413031930 | success |
| **Portability 35413048140 (FreeBSD, system clang, dispatched)** | **success**, 16/16, including `core-cpp.net` (kqueue, now unguarded) and `core-cpp.layering` |

### Fix round 1 concerns

1. **How I read "checks each target against its own row".** A target without a row of its own
   (`core::net`, `core::testing_main`, `core::testing_dialogs`) is still held to its module's
   row. That row allows the module's other targets, as before, so `net` → `net_types` and
   `testing_main` → `testing` still configure.
   - The strict reading would refuse those links. The module row cannot name them in its DEPS,
     because a module's further targets are declared after the module's row, and R42 gives `net`'s
     row no change.
   - The bound R42 asks for holds wherever a target has a row of its own. For A7, `tui_output`
     gets a row with `DEPS base`, and `core::tui` keeps linking it through the module rule.
2. **A row's DEPS can name a sibling that builds on fewer platforms**, e.g. a `PLATFORMS any`
   row with `DEPS net`. The declaration does not refuse that. Under Emscripten it fails at the
   link with "does not exist (yet)", which names the link but not why. Nothing in the table does
   this today.
3. **`testing/posix/` and `testing/windows/`** are platform directories nested in the public
   `testing/` directory, not at the module's root. `.agent/rules/platform.md` describes the
   module's own `posix/` and `windows/`. I kept the halves beside the test doubles because R42
   names this layout first. `docs/modules/net.md` and `source-map.md` say where they are.
