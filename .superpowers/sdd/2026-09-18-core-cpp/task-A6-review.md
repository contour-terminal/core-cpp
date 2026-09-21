# Task A6 review: `core::net` import (e45f730..def7325)

### Spec Compliance

**❌ One binding requirement is unmet.** Ruling R40 says `FdPassing_test` and `UnixSocket_test` move
to `posix/` *without their file-wide guards*. Both still carry them. Everything else checked holds.
(The implementer worked from `progress.md:197`, whose summary of R40 leaves out the "without their
guards" clause; see Important 1.)

**How faithfulness was checked.**
- I exported all 72 upstream blobs (`6777ff05`) and all 66 core-cpp blobs (`def7325`) with
  `core.autocrlf=false`. Neither side contains a CR byte.
- I applied the mechanical rewrite: `namespace net`, `coro::`, `net::`, `<coro/…>`, `<net/…>`, and
  `net/platform/{Clock,SystemPipe,WinsockInit,NativeHandle}` to their `core/platform/` headers.
- I diffed each core-cpp file against its provenance row's upstream path. The two
  `PollEventSource.cpp` halves were diffed against the upstream file's `#ifdef` branches.
- Every remaining delta falls into one of three groups:
  - include regrouping by clang-format, and re-wrapped lines;
  - `core::async::`/`core::net::` written as `async::`/`net::` inside `namespace core::net`;
  - a change the provenance notes record.
- There is **no unrecorded behavioural delta** in `src/core/net/`. Two notes are stale and one is
  incomplete; all three are comment-level (Minor 2).

| Requirement | Verdict | Evidence |
|---|---|---|
| Upstream file set imported (72 files at 6777ff05, 9 tests + `testing/`) | ✅ | 61 files kept, less `CMakeLists.txt`, `README.md`, `test_main.cpp`, `testing/TempDir.hpp` and the 7 `platform/{Clock,NativeHandle,SystemPipe.*,WinsockInit.*}` files. `PollEventSource.cpp` is split in two; `NetError.hpp`, `NetError_test.cpp` and `CMakeLists.txt` are new. That makes 66 |
| Mechanical rewrite only, plus recorded deltas | ✅ | See the method above |
| Loop conversions preserve behaviour | ✅ | The `addrinfo` walks (`posix/PosixListener.cpp:78-82`, `posix/SocketsPosix.cpp:77-81`, `windows/SocketsWin32.cpp`, `windows/WindowsListener.cpp`) and the `CMSG_NXTHDR` walk (`posix/PosixSocket.cpp:166-170`) step first with `std::exchange`, so a `continue` still advances. Nothing after the loop reads `ai`. The `iota` rewrites are index-for-index; the `(n+1)/2` and `(n+2)/3` chunk counts in `AsyncBufferedReader_test.cpp` equal the stride loops. `EventLoop.cpp:142-145` `find_if` plus erase matches the first-match `break` |
| `SystemPipe::read` callers handle `ChannelResult` | ✅ | `EventLoop.cpp:109` ignores the result, as upstream did. The loop owns both ends, so end of stream cannot occur, and an empty read or an error just leaves level-triggered readiness. `EventLoop_test.cpp:432` and `EventSourceParity_test.cpp:65` test `bytesRead() == 1`, which is the same predicate as upstream's `*got == 1`: a would-block error and an empty result both read as "not 1". No caller drops an error it used to act on |
| `IClock` → `core::platform::IClock`; `NativeHandle`/`platformRead`/`Write` → `Types.hpp` | ✅ | `EventLoop.hpp:102`, `EventSource.hpp`, the backends. No public net header includes `<windows.h>`; contour's `NativeHandle.hpp` did, and `core/platform/Types.hpp` does not |
| R37: no `core::net::platform` namespace | ✅ | `grep` over `src/core/net`: none. `detail/PeerAddress.hpp:26` is `namespace core::net` |
| R38: `net` row is `PLATFORMS native`, DEPS as needed | ✅ | `cmake/CoreCppModules.cmake:174`. `net` includes only `core/async` (35) and `core/platform` (14) besides itself, so `async platform` is exact |
| R38: `net_types` INTERFACE, header-only, builds under Emscripten | ✅ | `src/core/net/CMakeLists.txt:12-15`. `NetError.hpp`/`IoResult.hpp` include only std. Emscripten CI job 105762130144: `module net: core::net_types only`, `core-cpp.net_types` Passed |
| R38: sub-targets in the table; layering still refuses undeclared links | ⚠️ | Module-level refusal is intact (`cmake/CoreCppTargets.cmake:153`). Target rows carry no DEPS of their own (`CoreCppModules.cmake:29,75`), so the table cannot state that `net_types` needs nothing (Important 2) |
| R38: `Threads::Threads` PUBLIC with contour's comment; no PUBLIC flags | ✅ | `src/core/net/CMakeLists.txt:24-28,91`. Only `core_cpp_apply_toolchain` adds flags, and it adds them PRIVATE |
| `net_tls` only with `CORE_CPP_WITH_TLS`; OpenSSL PRIVATE; no OpenSSL type in a header | ✅ | `CoreCppModules.cmake:176`, `src/core/net/CMakeLists.txt:94-100`. `Tls.hpp` includes only `ISocket.hpp`. The dependency row is `CoreCppDependencies.cmake:192` (`NO_FETCH`) |
| R39: `CORE_CPP_WITH_TLS=ON` in hidden `unix`, not in `emscripten` | ✅ | `CMakePresets.json:25`. `emscripten` inherits `base`, and `CoreCppOptions.cmake:51-54` forces TLS off there anyway |
| R39: `libssl-dev` in every Linux job that configures a unix preset | ✅ | `build.yml:134` (the whole linux matrix), `:398` (sanitizers), `:436` (clang-tidy), `:568` (compile-cache, `gcc-release`), `:652` (coverage). `style`, `emscripten`, `docs.yml`, `downstream.yml` and `release.yml` configure no unix preset, so no job was missed |
| R39: FreeBSD OpenSSL | ✅ | Base-system OpenSSL, noted at `portability.yml:22-23`. Run 35395187310: `OpenSSL: found by find_package(OpenSSL)`, and `core-cpp.net_tls` Passed |
| R40: Epoll → `linux/`, Kqueue → `bsd/`, compiled only through `SOURCES_LINUX`/`SOURCES_BSD` | ✅ | `src/core/net/CMakeLists.txt:76-81`. `DefaultEventSource.cpp` includes them only under its `#ifdef`s |
| R40: `PollEventSource.cpp` split with no code lost; each half keeps its includes; header portable, `_waitRotation` unconditional | ✅ | `posix/PollEventSource.cpp` equals upstream lines 21-83, with one `iota` loop. `windows/PollEventSource.cpp` equals upstream lines 85-272, with one `platform::InvalidHandle`. Every upstream include is kept (`<poll.h>`; `<windows.h>`, `<format>`, `<ranges>`, `Diagnostics`, `WaitChunking`). `PollEventSource.hpp:52` declares `[[maybe_unused]] std::size_t _waitRotation` unconditionally |
| R40: `WaitChunking`, `PeerAddress` → `detail/` | ✅ | `detail/WaitChunking.hpp` and `detail/PeerAddress.hpp`, both byte-identical after the rewrite |
| R40: `FdPassing_test`, `UnixSocket_test` → `posix/` **without their file-wide guards** | ❌ | `posix/FdPassing_test.cpp:2,293` and `posix/UnixSocket_test.cpp:2,294` still wrap the whole file in `#ifndef _WIN32` … `#endif`. CMake selects them correctly (`src/core/net/CMakeLists.txt:115-117`); the guards are simply still there |
| R40: `DefaultEventSource.cpp` keeps its `#ifdef`s | ✅ | Unchanged apart from the include paths |
| R40: no `#ifdef _WIN32` logic in top-level sources, except DefaultEventSource.cpp and EventSourceParity_test | ✅ | None at the root. `testing/InMemoryTransport.cpp` has one outside the root (Minor 1) |
| R40: platform directories private | ✅ | `HEADERS` lists only root headers and `testing/` (`src/core/net/CMakeLists.txt:35-52`). No public header includes a private one |
| R40: every move noted in provenance; layout rule documented | ✅ | Every moved file's row says so. The rule is in `.agent/rules/platform.md:25-30`, `source-map.md`, `docs/modules/index.md`, `docs/design/portability.md` and `docs/modules/net.md:48-56`. Two includer rows are stale (Minor 2) |
| The one intended behavioural change (6dd73ea) matches the spec's `runOnce` turn | ✅ | See "The clock refresh" below |
| Tests: socket tests `loopback`; `no-tsan` justified; no `test_main`; POSIX-only tests chosen by CMake | ✅ | `src/core/net/CMakeLists.txt:118,120`. Contour's presets and CI exclude nothing from TSan at 6777ff05, so no `no-tsan` is needed, and CI's `clang-tsan` job ran `core-cpp.net` and `core-cpp.net_tls` green. Every binary links `core::testing_main` through `core_cpp_add_test` |
| def7325 (`-Wshorten-64-to-32`) is a real fix | ✅ | `posix/FdPassing_test.cpp:44,54` converts `CMSG_SPACE`/`CMSG_LEN` to the field's own type with `decltype`. The value is bounded: the buffer holds 8 descriptors and callers pass at most 2, so the conversion preserves the value. Nothing is muted |
| Hygiene: SPDX, no NOLINT, no pragma, no C-style `for`; provenance row per file with the full SHA | ✅ | All 66 files start with SPDX. `grep` finds no NOLINT, no pragma other than `once`, and no `for (;;)`. The 66 provenance rows equal the 66 files, and every imported row carries `6777ff05014f8ff163b071e8b0e942830119db80` |
| Docs: `net.md` factual and says EventSource is replaced in Phase B; README rows current; build-and-toolchain `--clean-first` rule citing e45f730 | ✅ | `docs/modules/net.md:12-22`, `README.md` module table, `.agent/rules/build-and-toolchain.md:163-169` |
| CHANGELOG, NOTICE, AGENT.md status | ✅ | `CHANGELOG.md` Added and Imported rows, `NOTICE` contour net entry, and the `AGENT.md` status sentence |
| CI green on def7325 | ✅ | Build 35395135341: all 21 jobs succeeded. I read the logs of clang-22, gcc-14, both sanitizers, appleclang, cl-debug, cl-release-tls, emscripten 3.1.56 and clang-tidy: no compiler warning and no clang-tidy finding. FreeBSD 35395187310 passed 15/15 |
| Local Windows/WSL runs, `mkdocs build --strict`, clang-format | ⚠️ | Not re-run, as instructed. CI's `style` job covers clang-format and the platform-sources check, and the Docs run on 3327317 was green |

**The clock refresh (6dd73ea).** It is correct and it is tested.
- `EventLoop.cpp:343` calls `_clock.refresh()` before `computeTimeoutMs()`. `EventLoop.cpp:348`
  calls it right after `_source.wait()` returns, before the post drain, the fd wakes and
  `fireExpiredTimers()`.
- That is Part I §2's turn exactly: step 3, "`clock.refresh()`, then compute the timeout"; step 5,
  "`clock.refresh()`, then fire expired timers".
- On the no-parked path no wait happens, so no refresh is needed. A `ManualClock` or `SteadyClock`
  is unaffected, because `refresh()` is a no-op for both.
- I traced the test (`EventLoop_test.cpp:204-231`) over `CachedClock{ManualClock}`:
  - pump 1: refresh to 100, timeout 400, the wait moves the clock to 350, refresh to 350, the
    delay (due at 500) does not fire;
  - pump 2: refresh, timeout 150, the wait moves the clock to 600, refresh to 600, the delay fires.
- Deleting only the first refresh gives 500 ≠ 400. Deleting only the second leaves `fired` false.
- Moving the refresh to the top of `pumpOnce` would also fail, with 500 ≠ 400, so the test pins
  the placement and not just the presence of the calls. That confirms the report's RED table.

### Strengths

- **The import is faithful.** Across 66 files, what the rewrite leaves is formatting, qualification
  and changes the provenance notes record, and nothing else. The platform swap touched every
  `NativeHandle`, `InvalidHandle`, `IClock`, `SystemPipe` and `ensureWinsockInitialized` site
  without changing behaviour.
- **The loop conversions were done the careful way.** Every linked-list walk steps first with
  `std::exchange`, so a `continue` still advances. The reason was written into
  `build-and-toolchain.md`, where it will stop the next conversion from hanging on a `continue`.
- **The `PollEventSource` split is lossless.** Each half is exactly one branch of the upstream
  `#ifdef`, with its includes. The header stays one type on every platform.
- **The refresh test pins each call and its placement**, not merely that the loop calls
  `refresh()`.
- **The module-table extension is small and consistent.**
  - It reuses the row vocabulary (KIND, PLATFORMS, WHEN) and keeps the KIND check.
  - It gives the Emscripten entry path an explicit status line.
  - It adds 8 row-build scenarios to `check-platform-sources.cmake`.
- **TLS is now built and tested on every POSIX CI leg**: Linux, arm64, the sanitizers, clang-tidy,
  coverage, macOS and FreeBSD. The emscripten legs build and run `net_types` alone.
- **`Tls_test` no longer has a `REQUIRE` above a stop.** The client context is made and checked
  before the server thread starts.

### Issues

#### Critical

None.

#### Important

1. **The two POSIX tests keep their file-wide guards, against Ruling R40.**
   - *Where:* `src/core/net/posix/FdPassing_test.cpp:2,293` and
     `src/core/net/posix/UnixSocket_test.cpp:2,294`.
   - *What:* R40 moves both tests to `posix/` *without their file-wide guards*. Both still wrap the
     whole file in `#ifndef _WIN32` … `#endif // !_WIN32`, and every include is indented under it.
   - *Why it matters:*
     - The layout rule this task wrote down says CMake's source lists choose platform code, never
       an `#ifdef`. Here the guard duplicates `SOURCES_POSIX` (`src/core/net/CMakeLists.txt:115`).
     - If a list entry is ever wrong, the guard silently compiles the file to nothing, and a test
       binary loses its cases without a build error.
     - The ruling names these two files explicitly.
   - *Context:* `progress.md:197`, which the implementer worked from, omits "without their guards".
     So this is a relay gap as much as an implementation miss.
   - *Fix:*
     - Delete the `#ifndef _WIN32` and `#endif` lines in both files, run
       `python scripts/clang-format.py`, and let it un-indent the includes.
     - Rebuild `clang-debug` and `gcc-release`. Windows is unaffected, because neither file is in
       its source list.
     - Optionally, record in provenance that the guard was dropped.
     - The other `posix/` and `windows/` sources keep contour's equally redundant file guards.
       R40 does not require removing those, so they can wait for B3 with the Epoll/Kqueue guards
       (report concern 8).

2. **Target rows carry no DEPS, so the table cannot bound what a sub-target links.**
   - *Where:* `cmake/CoreCppModules.cmake:29,74-75`, `cmake/CoreCppTargets.cmake:141-159`.
   - *What:*
     - `core_cpp_module_target()` takes NAME, MODULE, KIND, PLATFORMS and WHEN, but no DEPS.
     - The documented rule is that "the module's DEPS bound every target of the module".
     - `core_cpp_check_layering()` still allows, for every target, any target of the same module
       plus anything the module's DEPS list.
     - The answer to the dispatch's question is therefore: the layering check still refuses a link
       outside `async platform`, but no row carries its own DEPS.
   - *Why it matters:*
     - `core::net_types` exists to be the dependency-free, everywhere-building vocabulary, yet
       `net_types → core::async`, `→ core::platform` or even `→ core::net` would configure
       without complaint. The last only fails under Emscripten, with a "does not exist (yet)"
       error rather than a layering one.
     - Next task, A7, needs exactly the missing bound. The spec's table says "tui_output: base
       only" (`docs/superpowers/specs/2026-09-18-core-cpp-design.md:55`), and
       `.agent/rules/tui.md:12` makes it a rule.
     - With this mechanism, `tui_output → core::platform` or `→ core::tui` would pass the check.
       R38 asked that the table be extended while "keeping the layering check able to refuse an
       undeclared link". At target granularity it cannot.
   - *Fix:*
     - Give `core_cpp_module_target()` a `DEPS` list and make it authoritative for a target that
       has a row. An absent `DEPS` means "links no core-cpp target".
     - Each entry is either a module in the parent row's DEPS or a target of the same module:
       `net_types` gets none, and `net_tls` gets `DEPS net`.
     - Refuse any other entry when the row is declared.
     - In `core_cpp_add_module()`, pass the target's row to `core_cpp_check_layering()`, and check
       each `core::<x>` against the row's DEPS, so a same-module link is no longer implicitly
       allowed.
     - Prove the refusal with a configure-time scenario, since nothing tests the layering refusal
       today.
     - If the controller prefers, this can be the first step of A7 instead. It has to land before
       `tui_output` exists.

#### Minor

1. **`testing/InMemoryTransport.cpp` is a per-platform `#ifdef` file outside a platform directory.**
   - *Where:* `src/core/net/testing/InMemoryTransport.cpp:4-8,15-28,33-70`.
   - *What:* It holds two complete `makeSocketPair()` bodies, one per `#ifdef _WIN32` branch.
     That is exactly the shape the documented rule says to split
     (`.agent/rules/platform.md:25-30`: "a source with one `#ifdef` branch per platform is split
     into those subdirectories").
   - *Why it matters:*
     - `docs/modules/net.md:48-56` names only `DefaultEventSource.cpp` as an exception, so the doc
       overstates how far the rule holds.
     - The POSIX-only cases of `EventSourceParity_test.cpp` (`:42`, `:929`, `:967`) are also
       unmentioned. That exception is accepted by the controller, but it is undocumented.
   - *Fix:* Either split it into `posix/InMemoryTransport.cpp` and `windows/InMemoryTransport.cpp`
     (the declaration stays in the public `testing/InMemoryTransport.hpp`), or name both exceptions
     in `net.md` and in the `CMakeLists.txt` comment.
2. **Two provenance notes are stale, and one is incomplete.**
   - *Where:* `.agent/reference/provenance.md:120,140,105`.
   - *What:*
     - The rows for `posix/AcceptLoop.cpp` (`:120`) and `windows/WindowsListener.cpp` (`:140`)
       say "`PeerAddress.hpp` from the module's root". After 7b87be3 both include
       `<core/net/detail/PeerAddress.hpp>`.
     - The `Tls.cpp` row (`:105`) says "namespaces and includes", but three `NOLINTNEXTLINE`
       comments were removed there. The `EventLoop.hpp` row records the same change as
       "no `NOLINT`".
   - *Why it matters:* Provenance is what a later delta check (B12b, the C tasks) diffs against.
     A wrong note sends that check looking for a file that is not there.
   - *Fix:* Change the first two notes to "`PeerAddress.hpp` from `detail/`", and add "no `NOLINT`"
     to the `Tls.cpp` note.
3. **The consumer-migration guide does not say that TLS left `core::net`.**
   - *Where:* `.agent/guides/consumer-migration.md`: the contour row and the rename tables.
   - *What:* Contour's `src/net` built `Tls.*` into `net` and required OpenSSL unconditionally. In
     core-cpp, `<core/net/Tls.hpp>` is `core::net_tls`, which exists only with
     `CORE_CPP_WITH_TLS=ON`, and the guide's own CPM snippet sets that option OFF.
   - *Why it matters:* Contour's daemon, the TLS user, will fail to link at C6 until someone adds
     the option and the target link.
   - *Fix:* Add a row: "`net`'s `Tls.hpp`/`Tls.cpp` → `core::net_tls`: configure with
     `CORE_CPP_WITH_TLS ON` and link `core::net_tls`".
4. **The report misdescribes `UnixSocket_test.cpp`.**
   - *Where:* `task-A6-report.md:48`.
   - *What:* It says "contour's `net::testing::TempDir` becomes the local fixture in
     `UnixSocket_test.cpp`". Upstream `UnixSocket_test.cpp:36` already defines its own local
     `TempDir`, and the file is unchanged apart from its includes, which matches its provenance
     row. No net test used `testing/TempDir.hpp`.
   - *Why it matters:* Report accuracy only; no code is affected.
   - *Fix:* Correct the sentence.

### Assessment

**Task quality: Needs fixes.**

The import itself is faithful, well documented and green on every CI leg. The clock refresh is
correct against the spec's turn and well tested.

Two small fixes remain:
- Drop the file-wide guards R40 bans from the two POSIX tests.
- Give target rows their own DEPS so the layering check can bound `net_types`, and next
  `tui_output`. This can move to the start of A7 if the controller prefers.
