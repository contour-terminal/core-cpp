# Changelog

All notable changes to core-cpp are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major version is 0, a minor
release may break the API; every break is listed under **Breaking** with a migration note. A
release tag `vX.Y.Z` equals the version in `project(core-cpp VERSION X.Y.Z)`, and the release
workflow refuses one without a section here.

## [Unreleased]

### Added

- The CMake framework: a module table that enforces the layering between modules, a dependency
  table resolved from the parent project, then `find_package`, then CPM, per-target toolchain
  tables (pedantic warnings, `CORE_CPP_WERROR`, sanitizers, coverage, clang-tidy), and no global
  state unless core-cpp is the top-level project.
- `core::testing` (Windows dialog suppression, usable without a test framework),
  `core::testing_dialogs` and `core::testing_main`, a Catch2 `main()` whose exit status is 0 when
  everything passed, 1 when anything failed or Catch2 reported an error, 77 when every test case
  skipped, and 2 when nothing ran.
- Configure, build, test and workflow presets for clang, GCC, AppleClang, MSVC and clang-cl, the
  sanitizers, clang-tidy, coverage and Tracy, and an `emscripten` preset for single-threaded
  WebAssembly whose tests run under node.
- Checks over the tree: the CMake and C++ hygiene rules with their self-test, the exit-code
  contract, and `tests/cmake/check-release.cmake`, which the release workflow runs on a tag.
- The documentation site, the API reference, the rulebook in `.agent/`, and the CI workflows.
- The module table's `PLATFORMS` column takes `any`, `native` or `wasm-subset`, and a module may
  list `SOURCES_EMSCRIPTEN`; `SOURCES_POSIX` is not compiled under Emscripten, which sets `UNIX`.
- `core::base` (namespace `core`): contract checks (`Require`, `Guarantee`), an injectable
  process environment (`core::Environment`, with `core::testing::FakeEnvironment` in
  `core::testing`), escaping, FNV hashing, type-safe `Flags`, `times()`, the password-database
  entry, string and range utilities, `Overloaded`, `Deferred`, Base64 (`core::base64`), the
  Tracy profiling macros (`CORE_ZONE_*`) and the `core::ranges::Iota`/`FoldLeft` seam. It owns the
  generated `core/Config.hpp`.
- `core::log`: categorised logging (`Category`, `Sink`, `configure()`), its sinks and formatters
  (`ScopedOutput`, `ScopedCapture`), and `fatal()` and `SoftRequire()`, which report through it.
- `core::cli`: the command-line parser (`core::cli::parse`, help and usage text) and the
  application scaffold `core::cli::App`.
- The Tracy dependency, 0.14.1 as contour pins it, resolved when `CORE_CPP_WITH_TRACY` is on:
  `core::base` then links `Tracy::TracyClient` and the `CORE_ZONE_*` macros record zones. A
  fetched client is built with `TRACY_ENABLE` and `TRACY_ONLY_LOCALHOST`. CI builds and tests the
  `clang-tracy` preset.
- `core::testing_main` applies the `LOG` environment variable to `core::log` before it runs the
  tests (`LOG=net` enables the `net` category and writes it to standard output), and so links
  `core::log`.
- `core::platform`, the operating-system layer: one clock seam merged from endo's, contour's and
  fastcached's (`IClock` with `now()` and a virtual no-op `refresh()`, `SteadyClock`,
  `CachedClock`, `ManualClock`, `IWallClock`, `SystemWallClock`, `ManualWallClock`,
  `WallClockRef`, `defaultSteadyClock()`, `defaultSystemWallClock()`), `Types` (`NativeHandle`,
  `isTerminal()`, ...), `PlatformError`, `Wakeup`, `SignalHandler`, `SystemPipe`, `WinsockInit`,
  `MessageQueue`, `FileSystem` and `NativeFileSystem`, `FileInfoProvider`, `EnvironmentProvider`,
  `UserPaths`, `PathUtils`, `GlobMatch`, `FileUri`, `SystemInfo` and `StringUtils`, with the test
  doubles `testing::InMemoryFileSystem`, `testing::MockFileInfoProvider` and
  `testing::TestEnvironmentProvider`, and `nativeEnvironmentProvider()` and
  `nativeFileInfoProvider()`, which give a composition root the private native implementations:
  Windows' own, and one POSIX provider each for Linux, macOS, the BSDs and Emscripten (endo's
  `LinuxFileInfoProvider`, which used nothing Linux-specific, is `PosixFileInfoProvider`).
  Under single-threaded Emscripten its row says
  `wasm-subset`: Types, PlatformError, Clock, StringUtils, PathUtils, GlobMatch, FileUri and the
  POSIX providers build, and their tests run under node.
- `core::async`, header-only and needing nothing but the standard library; fastcached's executors
  arrive with Task B1.
- `core::Generator<T>` in `core::base`: `std::generator` where the standard library has it and is
  not libstdc++, otherwise `core::detail::GeneratorFallback<T>`, which is tested on every
  platform. It needs only the standard library, so it lives in base rather than `core::async`,
  where `core::async::Generator` would read as an asynchronous, `co_await`-able stream.
- `core::async::StopToken`, `StopSource`, `StopCallback<F>` and the `constexpr` tag `NoStopState`
  (`<core/async/StopToken.hpp>`): `std::stop_token`, `std::stop_source`, `std::stop_callback<F>`
  and `std::nostopstate` where the standard library defines `__cpp_lib_jthread`, and otherwise
  core-cpp's implementation with the standard semantics, which keeps plain state under
  single-threaded WebAssembly. libc++ before 20 has `<stop_token>` only behind
  `-fexperimental-library` (emsdk 3.1.56's libc++ 17, FreeBSD 15's base Clang 19, AppleClang 17
  (measured in CI)), and core-cpp adds no compile flag to its consumers, so the fallback runs there,
  with real threads everywhere but WebAssembly. The configure log of a build with tests says which
  branch the toolchain takes.
  `CORE_ASYNC_FORCE_STOP_TOKEN_FALLBACK` selects the fallback everywhere; the test binary
  `core-cpp-async-fallback-test` (ctest `core-cpp.async-fallback`) is built with it, so the fallback
  is tested on every platform, ThreadSanitizer included.
- `core_cpp_add_test()` takes `NAME`, for a module's second test binary, and `DEFINITIONS`, the
  compile definitions of that binary alone.
- contour's coroutine vocabulary in `core::async`: `Task<T>`, lazy and awaited once, whose promise
  carries the `StopToken` it inherits from the awaiting coroutine; `detail::UniqueCoroHandle`;
  `OperationCancelled` and `thisCoroStopToken()` (`Cancellation.hpp`); the `Awaiter` and
  `HasStopToken` concepts (`Awaitable.hpp`); `whenAll()`, which joins `Task<void>`s and rethrows the
  first failure once all have finished; and `whenAny()`, which resolves to the first to finish and
  cancels the others. Their tests also run over the `StopToken` fallback. A `Task`'s symmetric
  transfer is a tail call with Clang and MSVC at every optimisation level, with GCC only when it
  optimises sibling calls, and not in WebAssembly without `-mtail-call`. So awaits that complete
  synchronously grow the stack: at GCC `-O0` both a nested chain and a *loop* of 100000 of them
  overflow an 8 MiB stack, at GCC `-Og`/`-O1` the nested chain does, and under emsdk 3.1.56 the
  nested chain exceeds node's call stack. The deep-chain test is skipped under Emscripten without
  `-mtail-call` and for GCC without `__OPTIMIZE__`
  ([core-cpp#15](https://github.com/contour-terminal/core-cpp/issues/15)).
- `core::testing`: `ScopedTempDir`, `ScopedWorkingDirectory` and `EnvHelper` (`setTestEnv()`,
  `unsetTestEnv()`, `ScopedEnv`).
- `core::setProcessEnvironmentVariable()` and `core::unsetProcessEnvironmentVariable()` in
  `core::base`: the one writer of the process environment, in place of `setenv()`. On POSIX they
  publish a new `environ` block under `LiveEnvironment`'s lock and never free a published one, so
  a reader elsewhere never sees a block change or disappear under it.
- `core::net`, contour's event loop, sockets, TLS and HTTP server, as contour has them but for the
  namespaces and `core::platform` in place of contour's `net/platform/`: `EventLoop` over an
  injected `EventSource` (poll everywhere, epoll on Linux, kqueue on macOS and the BSDs,
  `makeDefaultEventSource()`), `ISocket` and `IListener` with `listen()`, `connect()`,
  `listenUnix()`, `connectUnix()` and `adoptFd()`, descriptor passing on POSIX,
  `AsyncBufferedReader`, `WriteQueue`, `SplitSocket`, `withTimeout()`, an HTTP/1.1 server, the
  diagnostic sink, and the test doubles `testing::ScriptedEventSource`,
  `testing::makeSocketPair()`, `testing::AllBackends` and `testing/CoroTestSupport.hpp`. Its
  error vocabulary, `NetError` and `IoResult`, is the header-only `core::net_types`, which builds
  under Emscripten too; the rest is native only until Phase B, which also replaces the
  `EventSource` API with `IoBackend`. `core::net` links `Threads::Threads` PUBLIC, because its
  headers use `std::mutex`.
- `core::net_tls` (`<core/net/Tls.hpp>`), with `CORE_CPP_WITH_TLS`: a TLS `ISocket` over any other,
  behind `ITlsContext`, in server, client (a pinned CA and a host name, or trust on first use) and
  self-signed form, and `constantTimeEquals()`. It links OpenSSL PRIVATE, and no OpenSSL type
  appears in its header.
- The OpenSSL dependency, taken from the system and never fetched, resolved when
  `CORE_CPP_WITH_TLS` is on.
- Every Linux, macOS and BSD preset turns `CORE_CPP_WITH_TLS` on, and CI installs OpenSSL where
  it builds them, so `core::net_tls` is built and tested on Linux, macOS and FreeBSD as well as in
  `cl-release-tls` on Windows. Those presets now need OpenSSL's development files.
- `core::net::EventLoop` calls its clock's `refresh()` before it computes a wait's timeout and
  after the wait returns, as `core::platform::IClock` asks of whoever owns a loop, so a
  `CachedClock` can drive it. contour's loop did not, because contour's `IClock` had no
  `refresh()`; for `SteadyClock` and `ManualClock` it does nothing.
- A module may declare further targets in the module table, each with a row of its own
  (`core_cpp_module_target()`), where its `PLATFORMS`, `WHEN` or links differ from its module's:
  a native-only module is entered under Emscripten when one of its targets builds there, and
  `core_cpp_add_test(<module> NAME <target>)` links that target and builds where it does. A
  row's `DEPS` are all its target may link, another target of the module or a module its
  module's row lists, and a row without `DEPS` links no core-cpp target; the configure refuses
  a row or a link outside that by name (`core::net_types` links nothing, `core::net_tls` only
  `core::net`).
- `core::tui_output` (`CORE_CPP_WITH_TUI`, native only), the leaf of endo's terminal UI: styled
  output and cursor, screen, scroll-region, sixel, OSC 52 and OSC 8 control through
  `TerminalOutput`, whose `writeToDestination()` a subclass overrides to retarget the stream and
  whose `isTerminal()` says what that stream is; `SyncGuard` (DEC mode 2026), which brackets the
  output it was made from; `buildSgrSequence()`; the protocol sequence constants and the DA1
  reader in `core::tui::protocols`; `CursorShape`; and the module's `Result`/`VoidResult`. It links
  `core::base` and nothing else — no libunicode, no coroutines, not even `core::platform` — so a
  program that only prints styled text takes nothing else with it, and its row in the module table
  is what refuses any other link.
- The libunicode dependency (0.9.3, `unicode::unicode`) when `CORE_CPP_WITH_TUI` is on, and stb
  (`stb_image`, `DOWNLOAD_ONLY`, pinned to a commit because stb publishes no releases) when
  `CORE_CPP_WITH_IMAGES` is on. Both are off under Emscripten. A fetched libunicode is built with
  `BUILD_SHARED_LIBS OFF`, as endo pins it: its target is linked PUBLIC from `core::tui`, so a
  consumer configured for shared libraries would otherwise get a shared libunicode behind a static
  core-cpp. A first configure with `CORE_CPP_WITH_TUI` on fetches libunicode from GitHub and
  libunicode's configure then downloads `UCD.zip` from `www.unicode.org` — core-cpp's only fetch
  outside GitHub.
- `core::tui`, endo's terminal UI (`f774a210`), native only: `TerminalInput` and `VtParser` over
  the Kitty keyboard protocol, SGR mouse reporting, bracketed paste and focus tracking;
  `Terminal`, which pairs input with output and owns the bounded query round-trips on an injected
  clock; `Buffer`, `Canvas` and the diffing `Screen` (inline, full-screen and fixed viewports);
  the components (`InputField`, `List`, `TreeTableView`, `Dialog`, `StatusBar`, `LogPanel`,
  `Spinner`, `ProgressBar`, `Tooltip`, `QuestionComponent`, the completion, command-palette and
  fuzzy-picker popups); `core::tui::completer`; `MarkdownRenderer` and `GenericSyntaxHighlighter`;
  sixel encoding, and with `CORE_CPP_WITH_IMAGES` the stb-backed loader, scaler and
  `FilesystemImageProvider`; and `core::tui::runtime`, whose `TuiRuntime` drives coroutines
  against an `EventSource` (`TerminalEventSource`, `PollEventSource`, `runModal()`,
  `withTimeout()`). Test doubles: `MockTerminalOutput`, `runtime::testing::MockEventSource` and
  `TestHelpers.hpp`. `runtime/TuiRuntime.hpp` and its test come from fastcached's copy
  (`5389e29a`), which carries one fix endo has not taken back: `DelayAwaiter::await_ready()` is a
  constant and an elapsed deadline is decided in `await_suspend()`, because MSVC 19.44's ARM64
  code generator loses the enclosing `try` of a `co_await` on an awaiter whose `await_ready()`
  reads the clock through a virtual `now()`.
- `core::tui` does not link `core::net`: Task B12 moves the runtime onto `core::net::EventLoop`
  and deletes `runtime/EventSource.hpp`, `runtime/PollEventSource.*` and `runtime/WithTimeout.hpp`,
  and the module table's row gains `net` then.
- The global property `CORE_CPP_TARGETS`: every compiled library core-cpp built, by its real
  target name, in the order the module table declares them. A parent project that instruments its
  build reads it and applies the same sanitizers or coverage to core-cpp's code, which is what
  keeps ThreadSanitizer from reporting races between instrumented and uninstrumented code.
  Header-only targets and test binaries are not in it.
- `cmake/CoreCppVendor.cmake`, the vendoring tool of the design spec's Part I §5:
  `cmake -DMODE=sync -DREF=<tag> -DDEST=<dir> [-DREPO=<url or path>] ["-DMODULES=<a;b>"] -P ...`
  copies the file set out of git's own blobs (`-c core.autocrlf=false -c core.eol=lf`), refusing a
  CR byte, a symbolic link and a submodule, and writes a `MANIFEST` of SHA-256 hashes with LF
  endings whatever the host, because the consumer commits that file; `MODE=check` re-hashes a copy
  and refuses a hash mismatch, a missing file, an unlisted file and a manifest that is not one --
  an unparsable line, a missing `# repository` or `# ref` header, a `# commit` that is not 40
  lowercase hex digits, and a `# files` count that is absent, is not a number, is zero or disagrees
  with the lines below it -- needing no git, because a consumer runs it in its own CI. A sync
  assembles the new copy in `DEST/.core-cpp-vendor-staging/` and replaces the old one only once the
  whole copy is legal; every refusal deletes that staging directory on its way out, so a copy a
  refused sync found still passes its own check. It refuses a `DEST` that is not one of ours, and
  it refuses what it cannot copy correctly: a `REF` that is not a tag or a full 40-character SHA,
  a local `REPO` that is not the root of its own repository, a ref whose tree is not core-cpp's,
  and a `MODULES` list that omits a module the ref's own table builds unconditionally. The last
  three are one mistake seen from three sides -- running sync with a *vendored copy's* own script,
  where `REPO` defaults to the copy's directory and git reads the consumer's repository instead.
  `tests/cmake/check-vendor-selftest.cmake` (ctest `core-cpp.vendor-selftest`, label `hygiene`)
  proves every one of those judgements by name against repositories it builds for the purpose, and
  skips rather than fails where git is absent. The file set is the spec's, plus everything else
  directly in `src/core/` -- that module's `CMakeLists.txt` and `Config.hpp.in`, without which the
  copy does not configure. File modes are outside the contract. `docs/vendoring.md` is the
  contract.
- Consumer smoke tests, one project per way core-cpp is consumed, and the `consumer-smoke` CI job
  that runs all three (`ci-ok` requires it): `tests/consumer-cpm` adds core-cpp with CPM and
  asserts that doing so changed none of its own flags, launcher or include directories, that
  `CORE_CPP_TARGETS` names every compiled library and no test binary, that no core-cpp target --
  the header-only ones included, which is where an interface-scoped usage requirement would show --
  carries a PUBLIC or INTERFACE flag, and that no test of core-cpp's was built;
  `tests/consumer-vendored` builds a vendored copy of the commit under test with
  `CORE_CPP_FETCH_DEPS=OFF`, `CORE_CPP_WITH_TUI=OFF` and `CORE_CPP_WITH_TLS=ON` inside a container
  with no network and no git, and registers the verbatim check as one of its own tests;
  `tests/consumer-wasm` builds the WebAssembly subset behind one INTERFACE library, as morph does,
  runs it under node with emsdk 3.1.56, and refuses a build in which any core-cpp target links
  threads -- read off those targets' `LINK_LIBRARIES`, because `find_package(Threads)` inside
  core-cpp creates a target the parent scope cannot see. The loopback echo and the `core::log`
  line the CPM and vendored programs share are `tests/consumer-shared/ConsumerSmoke.hpp`; each
  program keeps only what is its own.

### Fixed

- `core::tui` carries no consumer's name in the code it runs. Beyond the OSC 8 hyperlink id
  below, `detectLanguageFromPath()`'s well-known-filename table no longer has a row for endo's
  `.endo-format`, so that name now answers `LanguageId::None`; the table keeps only names that
  are well known beyond one project, and a consumer that wants its own configuration file
  highlighted passes the language to `highlightLine()` itself. The default theme's path-gradient
  colours and the fuzzy matcher's worked example no longer describe themselves in terms of one
  application either. (`LanguageId::Endo`, `registerEndoHighlighter()` and the `.endo` and `endo`
  token rows are the same finding and are unchanged: renaming them is public API and giving a
  host a seam for its own tokens is a design decision, both pending a ruling.)
- `core::tui`'s assembly highlighter no longer overruns a stack buffer. Its three scanners
  lowercased an identifier, a `%register` or a `.directive` into a 64-character array through a
  helper that took a bare `char*` and wrote `src.size()` bytes; the four other call sites bounded
  the copy themselves and these did not, so a token longer than 64 characters in any rendered
  ```` ```asm ```` fence smashed the caller's frame. The helper now takes a `std::span<char>` and
  returns an oversized identifier unchanged, so the bound is in one place.
- `core::tui`'s three dialogs draw their frame where their text is. Each built its `Rect` as
  `{ .x = startRow, .y = startCol }`, but `Rect::x` is the left column and `Rect::y` the top row,
  while `putString()` takes `(row, col)`; the border and the interior fill therefore landed at the
  transposed position and the contents outside them. The two coincide only on a canvas where the
  dialog is centred at the same offset in both axes, which is why nothing caught it.
- `core::tui::InputDialog::render()` no longer throws on a terminal narrower than its own border.
  `dialogWidth = min(config.width, termCols - 4)` and `inputWidth = dialogWidth - 4` had no floor,
  and the negative width reached `substr()` as a huge `std::size_t`, throwing `std::out_of_range`
  out of a `render()` no caller expects to throw. All three dialogs clamp both to zero.
- `core::tui::Buffer::addHyperlink()` mints the OSC 8 `id=` as the bare hash of the URI. endo's
  copy prefixed it `endo-`, so every consumer's hyperlinks carried another project's name on the
  wire. A behaviour change for anything that reads the id back: it is now `1f2e` where it was
  `endo-1f2e`.
- `core::tui::Completer::addProvider()` sorts stably, so providers of equal priority -- which is
  every provider that does not set one -- keep the order they were registered in.
  `gatherCompletions()` drops a later duplicate by text, so an unstable sort let the standard
  library decide which provider's item a user saw.
- `core::tui::VtParser`'s three sequence buffers are bounded. A bracketed paste, a CSI parameter
  string and a DCS payload each grew for as long as bytes kept arriving without the terminator
  that ends the sequence, and `timeout()` resolves only a bare Escape, so a `ESC[200~` whose
  `ESC[201~` never came grew the process without limit from untrusted bytes on stdin. The caps
  are the new public `VtParser::MaxPasteLength` (4 MiB), `MaxCsiParamLength` (256) and
  `MaxDcsLength` (64 KiB); past one, the parser returns to Ground, emitting the collected text
  for a paste and dropping the other two, which are malformed at that length. Each cap bounds what
  the sequence CARRIES: the terminator's own bytes (`ESC[201~`, `ESC \`) pass through the same
  buffer on their way in, and their room is reserved above the cap, so a paste of exactly
  `MaxPasteLength` bytes and a DCS payload of exactly `MaxDcsLength` bytes still end at their own
  terminator instead of being cut a few bytes into it. `MaxCsiParamLength` needs no such
  reservation: a CSI's final byte is dispatched, never collected.
- `core::tui`'s POSIX SIGWINCH handler saves and restores `errno`, reaches its `TerminalInput`
  through a lock-free `std::atomic` rather than a plain pointer, and cannot block. The write end
  of the resize self-pipe was left blocking (only the read end was made non-blocking), so a pipe
  nobody had drained stalled `::write()` inside a signal context; and a resize arriving between a
  failed syscall and the mainline's `errno` check overwrote the value that check was about to
  read.
- `core::tui::SyncGuard` flushes at both ends of the region, whichever way the guard was made.
  Anything composed inside the region and still buffered was emitted after `CSI ?2026l` and so
  applied outside it -- `Screen::flush()`'s `applyCursorShape()` is the live case -- and
  move-assignment, which ends a region the same way, flushes too. The flush on the way *in* is the
  constructor's rather than `TerminalOutput::syncGuard()`'s, so the natural RAII spelling
  `auto guard = SyncGuard { output };` no longer emits previously buffered bytes inside the
  region it is opening.
- `core::tui::SyncGuard` writes its begin and end sequences (DEC mode 2026) through the
  `TerminalOutput` it brackets, so they follow that output's `writeToDestination()` wherever its
  bytes go. endo's guard wrote them to the process's standard output whatever the output was
  (`src/tui/platform/TerminalOutput.cpp:355-359` at `f774a210`), which put the frame's begin and
  end on a stream that never saw the frame's contents, and left a retargeted output's own stream
  unsynchronised. The guard therefore carries no native handle, and `<core/tui/TerminalOutput.hpp>`
  no longer declares a `void*` handle alias under `_WIN32`. `TerminalOutput::isTerminal()` is new
  beside it: whether the destination is a terminal, answered by the operating system for the
  default one and by the subclass for a retargeted one.
- `core::nextPowerOfTwo()` rounds a 16-, 32- or 64-bit value up to a power of two. crispy's, which
  it was imported from, compared the type's width in bytes against bit counts and so smeared only
  the eight bits below the highest set one: 257 became 511, and 0x10001 became 0x1fe01.
- `core::LiveEnvironment` on Windows reads a variable set to the empty string as set, as it does
  on POSIX; it read as unset.
- `core::Generator` is the same type in every translation unit. endo's, which it was
  imported from, tested `__cpp_lib_generator` before including anything, so whether it was
  `std::generator` depended on what the including file had included first, and a virtual function
  returning one (`FileSystem::walkDirectoryRecursive`) could have two return types in one program.
- `core::platform::SystemPipe` never blocks: both POSIX ends are non-blocking and close-on-exec,
  a write into a full channel reports done, and `send()` uses `MSG_NOSIGNAL`. endo's copy blocked;
  contour's, which an event loop's `post()` uses, already did this.
- `core::platform::SystemPipe::read()` returns a `ChannelResult`, which tells the bytes read, an
  empty channel and the end of the stream apart; only a failed read is a `PlatformError`. endo's
  and contour's copies returned a count, 0 for the end of the stream, and failed a read of an
  empty non-blocking channel with the same error as a broken one.

### Imported

Each file was read as a git blob at the commit named, and none contains a CR byte.

| From | Commit | What |
|---|---|---|
| [fastcached](https://github.com/LASTRADA-Software/fastcached) | `eb9c9c68da8fadfd43b0b36366919cb462689f48` | `cmake/portable/CompileCache.cmake` and `cmake/FetchTransferBound.cmake`, verbatim; the bounded bootstrap download in `cmake/CPM.cmake`; the Windows error-popup suppression, merged into `SuppressWindowsDialogs`; the hook-name `IgnoredRegexp` of `.clang-tidy` |
| [contour](https://github.com/contour-terminal/contour) | `6777ff05014f8ff163b071e8b0e942830119db80` | `.clang-format` and `.clang-tidy`, adapted; two copies of `SuppressWindowsDialogs`, merged; `LICENSE` |
| [endo](https://github.com/contour-terminal/endo) | `f774a210ce989e5947b8f61d715068b1dc96088c` | `SuppressWindowsDialogsAtStartup.cpp`, `WindowsDialogCanary.cpp`, the CPM 0.40.8 pin; a copy of `SuppressWindowsDialogs`, merged; `.github/clang-tidy-matcher.json` |
| [contour](https://github.com/contour-terminal/contour) | `6777ff05014f8ff163b071e8b0e942830119db80` | crispy's generic half, `src/crispy/{Assert,Base64,Deferred,Defines,Environment,Escape,FNV,Flags,Overloaded,Times,UserInfo,Utils}` as `core` (`core::base`), `{LogStore,LogSink}` as `core::log`, `{CLI,App}` as `core::cli`, and `testing/Environment.hpp` as `core::testing`, with their tests (`Base64`, `CLI`, `Environment`, `LogSink`, `Times`, `Utils`); `fatal()` and `SoftRequire()` moved from `Assert.hpp` to `core/log/Assert.hpp`; `gsl::not_null` replaced by a reference |
| [fastcached](https://github.com/LASTRADA-Software/fastcached) | `ee71f868547712892b7d9a2ebff60d49c496e25c` | `src/FastCache/Core/{Profiling,Ranges}.hpp` as `core/{Profiling,Ranges}.hpp` (`FC_*` as `CORE_*`, `FastCache::Ranges` as `core::ranges`), with `Profiling_test.cpp` and `Ranges_test.cpp` |
| [endo](https://github.com/contour-terminal/endo) | `f774a210ce989e5947b8f61d715068b1dc96088c` | `src/testing/{ScopedTempDir,ScopedWorkingDirectory,EnvHelper}.hpp` and `ScopedTempDir_test.cpp` as `core::testing`; `EnvHelper` writes through `core::setProcessEnvironmentVariable()` and reads through `core::LiveEnvironment` on POSIX, not `setenv()`/`getenv()` |
| [endo](https://github.com/contour-terminal/endo) | `f774a210ce989e5947b8f61d715068b1dc96088c` | the generic half of `src/platform` as `core::platform` (Types, PlatformError, Clock, Wakeup, SignalHandler, SystemPipe, WinsockInit, MessageQueue, FileSystem, NativeFileSystem, FileInfoProvider, EnvironmentProvider, UserPaths, PathUtils, GlobMatch, FileUri, SystemInfo, StringUtils, their `posix/`, `linux/` and `windows/` implementations and `testing/` doubles), with their tests (`WindowsPlatform_test.cpp` split into `PathUtils_test`, `Types_test` and `UserPaths_test`); `Generator.hpp` as `core::base` (`core::Generator`; Task A5b moved it out of `core::async`, which it needs nothing of). Process, Pipe, WaitResult, ProcessProvider, ProjectFileTree, InstallPaths and InterruptThrottle stay in endo; the `namespace endo` compatibility aliases were not imported |
| [contour](https://github.com/contour-terminal/contour) | `6777ff05014f8ff163b071e8b0e942830119db80` | `src/net/platform/Clock.hpp`, merged into `core/platform/Clock.hpp`; `src/net/platform/SystemPipe.{hpp,cpp}`, whose non-blocking behaviour is merged into `core/platform/SystemPipe`; `src/net/platform/WinsockInit.{hpp,cpp}`, identical to endo's |
| [contour](https://github.com/contour-terminal/contour) | `6777ff05014f8ff163b071e8b0e942830119db80` | `src/coro/{Awaitable,Cancellation,Task,UniqueCoroHandle,WhenAll,WhenAny}.hpp` and `{Task,WhenAll,WhenAny}_test.cpp` as `core::async`, `coro::` renamed `core::async::`; the `std::stop_token` aliases of `Cancellation.hpp` moved to `StopToken.hpp`, whose fallback replaces their `#error`; no `NOLINT`; two locals renamed for `-Wshadow`; two `WhenAny_test.cpp` helpers compiled only where the case using them is. `test_main.cpp` was not imported (`core::testing_main` replaces it), and `testing/SuppressWindowsDialogs.hpp` had been merged into `core::testing` already |
| [contour](https://github.com/contour-terminal/contour) | `6777ff05014f8ff163b071e8b0e942830119db80` | `src/net` as `core::net`, `core::net_types` and `core::net_tls`, `net::` renamed `core::net::` and `coro::` `core::async::`, with its tests but `test_main.cpp`; `net/platform/{Clock,NativeHandle,SystemPipe,WinsockInit}` replaced by `core::platform`, whose `SystemPipe::read()` returns a `ChannelResult`; `platform/PeerAddress.hpp` moved to `detail/` and `platform/WindowsLoopback.*` to `windows/`, so that no `core::net::platform` namespace hides `core::platform`; platform code in platform subdirectories (epoll in `linux/`, kqueue in `bsd/`, `PollEventSource.cpp` split into `posix/` and `windows/`, `WaitChunking.hpp` in `detail/`); `NetError` split out of `IoResult.hpp` into `NetError.hpp`; `testing/TempDir.hpp` not imported (`core::testing::ScopedTempDir`); no `NOLINT`; the C-style `for` loops written as range-`for`s and `while`s; one lambda parameter renamed for GCC's `-Wshadow`, and a `CMSG_FIRSTHDR()` result checked for GCC's `-Wnull-dereference`; the TLS test makes its client context before its server thread starts |
| [fastcached](https://github.com/LASTRADA-Software/fastcached) | `b461e8b6d367ed22e4bf2935717fa59360a64b7d` | `src/FastCache/Core/Clock.hpp`, merged into `core/platform/Clock.hpp` in camelBack (`Now`/`Refresh` as `now`/`refresh`, `TimePoint`/`Duration` as `SteadyTimePoint`/`SteadyDuration`); `Clock_test.cpp` and `WallClockRef_test.cpp`, merged into `core/platform/Clock_test.cpp` |

The rulebook and CI configuration adapt text from fastcached at
`b5ded89c5ae6ba5b45337335ce774c5ae6986d65`, contour and endo at the commits above, Lightweight at
`f57dc2e0704d885a3c642a63675873919fc2d128` and tuidu at
`30107fbab72310fde5db89e7882eab288f6b541e`; `NOTICE` lists the files.
