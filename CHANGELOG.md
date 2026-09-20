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
  (`ScopedOutput`, `ScopedCapture`), `fatal()` and `SoftRequire()`, which report through it, and
  `isStdOutTerminal()`/`isStdErrTerminal()` — the one place the platform is asked whether a
  standard stream is a terminal, which is what decides colourisation.
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
- `core::async`, header-only and including nothing but the standard library; it links Threads,
  which is what its `StopToken` fallback's `std::mutex`, `std::condition_variable` and
  `std::this_thread::get_id()` need, and nothing at all under single-threaded Emscripten.
  fastcached's executors arrive with Task B1.
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
  assembles the new copy in `DEST.core-cpp-vendor-new`, a sibling of `DEST`, writes its `MANIFEST`
  there, and touches `DEST` itself only once that copy is complete; every refusal deletes the
  sibling on its way out, so a copy a refused sync found still passes its own check with nothing
  new beside it. The replacement is then two directory renames through `DEST.core-cpp-vendor-old`
  (`cmake/CoreCppVendorReplace.cmake`) rather than a file-by-file move into an emptied `DEST`, so
  whichever of the two directories exists when a sync stops -- for any reason, including being
  killed -- is a whole copy that passes its own check, and `DEST` is never half of each nor
  unfinished. A rename that fails -- on Windows an open handle, a lock or a scanner can fail one --
  puts the previous copy back and refuses; if that restore fails too the refusal names both
  directories, deletes neither, and says that either can be adopted by renaming it. A
  `DEST.core-cpp-vendor-old` left by a previous run holds the only copy of what was there, so a
  sync refuses rather than delete it to make room. It refuses a `DEST` that is not one of ours --
  a directory with files and no manifest, or a regular file where a directory belongs -- and
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
- `cmake/portable/CompileCache.cmake` re-synced verbatim from fastcached
  `5a9dca0498f4c37c63a17270550ee51ca87ae0a3` (`cmake/portable/README.md`), fixing the nightly
  `downstream.yml` drift check. Upstream added `FASTCACHE_AUTO_INSTALL_HOST_SYSTEM` and
  `FASTCACHE_AUTO_INSTALL_HOST_PROCESSOR`: empty by default, so `_fc_auto_install_select_row()`
  still asks `CMAKE_HOST_SYSTEM_NAME`/`_PROCESSOR`, but a caller can state the host to fetch
  `fastcache-cc` for instead, which lets `scripts/check-compile-cache-autoinstall.cmake` pin a
  published platform per row rather than stopping at whichever host actually runs the check.
  `cmake/FetchTransferBound.cmake` compared identical at the same commit; no change there.

- `core::platform::NativeFileSystem` takes its rename primitive at construction:
  `RenameFunction`, `nativeRename()` and a constructor defaulting to it, so `instance()` and every
  existing caller are unchanged. It is the one filesystem call the class takes rather than makes,
  and it is injected because `rename()`'s two-hop lettercase retry only runs on a volume that
  refuses a case-only rename outright -- which ext4, APFS, NTFS and UFS all do natively, so the
  retry was unreachable from any test. It moves a consumer's entry through a temporary name and
  can leave it there when both the second hop and the rollback fail, which is not behaviour that
  may ship untested (controller ruling R53).

### Breaking

- `core::async::whenAny()` resolves to `std::optional<std::size_t>` rather than to a `std::size_t`
  that was `core::async::detail::WhenAnyNoWinner` (`SIZE_MAX`) when nothing won. The sentinel was
  part of the documented public result but lived in `detail::`, so handling the empty case meant
  reaching into `detail::`, and a caller who forgot the check indexed a container at `SIZE_MAX`.
  Migration: `auto const i = co_await whenAny(...);` becomes
  `auto const i = co_await whenAny(...); if (i) use(*i);`, and any `== detail::WhenAnyNoWinner`
  becomes `!i.has_value()`. Nothing outside this repository reads the result yet.
- `core::async::whenAll()` and `whenAny()`'s variadic overloads take their tasks by rvalue. The
  constraint was written over `std::remove_cvref_t`, so an lvalue `Task<void>` satisfied it and
  then failed inside `std::vector::push_back` on `Task`'s deleted copy constructor. An lvalue or a
  `const` rvalue is now "no matching overload" at the call. Migration: `whenAll(std::move(task))`,
  which is what every call already had to do to compile.
- `core::platform::FileSystem::openWrite()` takes a `core::platform::WriteMode` and `copyFile()` a
  `core::platform::OverwritePolicy`, in place of the `bool` each took before. A `bool` in an API is
  an anonymous enum whose two values are named after their representation rather than their meaning
  (`.agent/rules/design-principles.md`), and `FileSystem.hpp` is public API for every consumer, so
  this costs nothing now and would be a break once one of them passes `true`. Migration:
  `openWrite(p, true)` becomes `openWrite(p, WriteMode::Append)` and `openWrite(p, false)` becomes
  `openWrite(p, WriteMode::Truncate)`; `copyFile(a, b, true)` becomes
  `copyFile(a, b, OverwritePolicy::Replace)` and `copyFile(a, b, false)` becomes
  `copyFile(a, b, OverwritePolicy::Refuse)`. The defaults are unchanged, so a call that took the
  default needs no edit; an implementation of the interface outside core-cpp mirrors the two
  signatures.
- `core::platform::testing::TestEnvironmentProvider` opens the namespace its directory names,
  alongside its neighbours `InMemoryFileSystem` and `MockFileInfoProvider`; it used to open
  `core::platform`. Migration: spell it `core::platform::testing::TestEnvironmentProvider`.

- `core::Flags::operator&=` intersects instead of clearing, which silently reverses what it
  answers. It called `disable()`, so `f &= X::A` kept everything except `A` while
  `f = f & Flags { X::A }` kept only `A`: the compound operator computed the complement of its
  binary form. Nothing in contour, endo, tuidu or morph uses it, so nothing has to change today,
  but the reversal is invisible at the call site — it compiles either way. Migration: a caller
  that wanted the old meaning spells it `f.disable(X::A)`. An overload taking a `Flags` was added
  too, so the pair is symmetric with `operator|` and `operator|=`.
- `core::FNV`'s byte-wise overload takes only a type with unique object representations, which
  narrows what compiles. It accepted any trivially copyable type and walked its object
  representation, padding included, so two objects with equal members hashed differently
  depending on what their padding held. It now rejects any type with padding bits — a struct with
  interior padding, and `float` and `double`, whose representations have padding bit patterns.
  Migration: hash the members one at a time, or pass
  `std::bit_cast<std::array<unsigned char, sizeof(T)>>(value)`, which is what the overload does
  for the types it still accepts. No consumer instantiates it with such a type: contour's and
  endo's `FNV` uses all go through the `char`, `uint8_t` or `string_view` overloads.
- `core::base64::decodeLength()` answers a different number for the same input: the size of the
  base64 prefix, where it used to size from the whole input including padding and any trailing
  junk. Migration: none for a caller that used it to reserve a buffer for `decode()`, which is
  what it is for — the answer is still an upper bound, just a tight one. A caller that relied on
  the old over-estimate for something else wants its own arithmetic. endo sizes an image buffer
  with it (`GeminiProvider.cpp`) and was over-allocating.
- `core::readFileAsString()` answers a different string for the same file: exactly the bytes on
  disk. It sized from `file_size()` and read in text mode, so on Windows CRLF translation
  delivered fewer bytes than it had reserved and the shortfall stayed behind as trailing NULs;
  and it narrowed the path through `path::string()`, which cannot represent every name a
  filesystem accepts and throws on Windows for the ones it cannot. A missing file now answers
  empty rather than throwing, as its documentation said all along. Migration: a caller that
  trimmed trailing NULs off the result can stop; one that caught `std::filesystem::filesystem_error`
  for a missing path checks for an empty string instead. contour reads a CA certificate and a
  forced-DPI file through it.
- `core::unescape()` and `core::readFileAsString()` are `[[nodiscard]]`. Discarding either is a
  bug — neither has an effect other than its return value — but a consumer that does so and
  builds with `-Werror` stops building. Migration: use the result, or cast it to `void` at the
  one call site that means to throw it away.

### Changed

- `core::async::whenAny()` reports a child that completed even when the awaiting flow's own token
  is stopped afterwards. It threw `OperationCancelled` whenever that token was stopped, so
  `whenAny(readSocket(), timeout())` whose read had completed and consumed bytes lost them if the
  cancellation landed before the last loser unwound; `.agent/rules/async-and-net.md` says the
  opposite, that a receive which already completed with bytes wins. It now throws only where no
  child completed at all. A child that *swallows* its `OperationCancelled` and returns counts as
  one that completed, because nothing can tell the two apart: a loser must let the cancellation
  out, which is what `whenAny`'s contract already asked of it.
- `core::async` links `Threads::Threads` (interface), on the same condition `core::base` uses, so
  a consumer writing `target_link_libraries(app PRIVATE core::async)` links what
  `<core/async/StopToken.hpp>`'s fallback needs. It linked nothing, which failed wherever pthread
  is a library of its own and the fallback branch is taken — libc++ before 20 without
  `-fexperimental-library`, so FreeBSD 15 and AppleClang 17. A single-threaded Emscripten build
  still links nothing.
- `.clang-tidy`'s `readability-identifier-naming` no longer exempts `request_stop`,
  `stop_requested`, `stop_possible` and `get_token` from the *function* naming rule: they are
  members of `std::stop_token` and friends, which `core::async`'s fallback spells as the standard
  does, and a free function of one of those names is not a standard-library hook. The
  `ClassMethod` style is gone with its duplicate of that ~800-character regex; with no
  `ClassMethod` style configured, a static member function falls through to the `Method` style,
  which says the same thing.
- `core::detail::Times2D::operator[]` answers the same type its `value_type` declares: a
  `std::tuple` of both coordinates, in the order iteration yields them (the inner range advances
  fastest). It answered the inner coordinate alone, so subscripting and iterating disagreed on
  what an element of a `Times2D` even is. `operator[]` changed rather than `value_type`, because
  the tuple is what `*it` already yielded and what the one existing test asserts. Nothing in
  core-cpp or any consumer subscripts a `Times2D`. While there: `Times::size()` and
  `Times::operator[]`, which nothing had ever instantiated, spell out the conversions their
  arithmetic implies instead of letting the compiler narrow silently.
- `core::joinHumanReadableQuoted()`'s separator is a `std::string_view`, not a deduced template
  parameter. Deduced, it could never be left out, so the `= ", "` default it declared was
  unusable and `joinHumanReadableQuoted(xs)` did not compile. Nothing calls it yet.

### Fixed

- `core::async::whenAny()` no longer runs the rest of a `request_stop()` on freed memory. Its
  parent→child cancel bridge requested stop on a `StopSource` that the awaiter held as a member;
  a child awaitable that resumes its coroutine from inside its own stop callback — how every
  runtime awaitable delivers cancellation — makes the losers unwind there and then, the last of
  them transfer to the awaiting coroutine, and that frame unwind, destroying the awaiter and with
  it the source whose `request_stop()` is still on the stack. The race state is now held by
  `shared_ptr` and every call into it that can run foreign code holds a reference for that call.
  This was a use-after-free wherever `StopToken` is `std::stop_token`, whose state a raw pointer
  reaches; core-cpp's fallback survived it only because its `request_stop()` happens to hold a
  `shared_ptr` copy of the state.
- `tests/cmake/check-cmake-hygiene.cmake`'s namespace gate checks every namespace a file declares,
  not only the first. Its rule is that every segment of every namespace is lowercase, but it
  stopped after the first declaration, so `namespace core::async { namespace Detail { ... } }`
  passed clean.
- `Task_test.cpp`'s deep-chain case skips on GCC unless the build's optimisation level is known to
  make symmetric transfer a tail call. It keyed on `__OPTIMIZE__`, which GCC defines at `-Og` and
  `-O1` as well, where the 100000-frame chain overflows the stack and kills the process, taking
  every other case in the binary with it — so a build outside core-cpp's presets lost the binary
  rather than getting a red. `src/core/async/CMakeLists.txt` now reads the level off the build's
  own flags and says in the configure log which it decided.
- `<core/async/WhenAll.hpp>` includes `<type_traits>`, which it names; `<core/async/Awaitable.hpp>`
  no longer includes `<utility>`, which it does not; `Task_test.cpp` includes `<stdexcept>` rather
  than relying on Catch2 for it, and not `<string>`, which it does not use.
- `core::cli`'s `--help` no longer reads past the text it is laying out. `wordWrapped()` computed
  the room left on the line as `margin - cursor + 1` in unsigned arithmetic, with a `<= 0` guard
  below it that is dead for an unsigned type; `printOptions()` sets the cursor to the option
  column, so an option column wider than the terminal — an 80-column terminal and an option whose
  rendered text exceeds 65 characters, a narrower terminal, or a pty reporting `ws_col == 0` —
  wrapped it to about 4294967295 and indexed the help text far past its end. It also read
  `text[SIZE_MAX]` for a help text beginning with a line feed, and returned an empty chunk for a
  word longer than the line, which made the caller loop forever. The options column now accounts
  for the verbatim placeholder as well, so a placeholder longer than the longest option no longer
  underflows its padding into a string of about four billion spaces (the `assert` above it is
  compiled out under NDEBUG), and the hyperlink scan's `isalpha()` widens through `unsigned char`,
  which is what it is defined for. The wrapper also advances its index by what a chunk consumed
  rather than by what it emitted: the two differ whenever a chunk is trimmed, and a space before
  a line feed left the trimmed space in front of the index for a skip loop that skips line feeds
  and not spaces, so the same empty chunk came back for ever and `--help` never returned.
  One layout change comes with this: a line whose text reaches exactly to the margin is no longer
  broken onto a second line, so rendered `--help` output differs for lengths that land on it.
- `core::cli::App` keeps the contracts it documents. `installLogging()` assigned the replacement
  over the member holding the previous output, so the previous `ScopedOutput` was destroyed after
  the new one had installed itself: its destructor restores every category to the sink it
  snapshotted, so a second call silently sent every later log line back to the console and left
  each category holding a reference into a destroyed sink. It releases the previous output first
  now — which means a destination that then fails to open leaves logging on the console rather
  than on whatever was installed before; the caller is told, and has nothing to fall back to
  either way. `reparseParameters()` and `parseParametersForTesting()`, both documented "false on
  failure", catch what `cli::parse()` throws rather than letting it escape a function whose
  contract is a bool (`cli::parse()`'s declaration now states what it throws;
  [core-cpp#13](https://github.com/contour-terminal/core-cpp/issues/13) converts this API to
  `std::expected` at the end of the plan). `screenWidth()` rejects a reported width of 0.
  `listDebugTags()` sorts a copy rather than the process-wide category registry, whose order is
  its construction order.
- `core::log` asks the platform whether a standard stream is a terminal, on Windows too.
  `ScopedOutput`'s private `isStdErrTty()` returned `true` unconditionally there, so a redirected
  standard error received SGR escapes — against the header's own contract — and
  `core::cli::App`'s `helpStyle()` and `customizeLogStoreOutput()` each carried a second copy of
  the same branch for standard output. All three now call `core::log::isStdOutTerminal()` or
  `isStdErrTerminal()`, which `core::log` implements once per platform in
  `src/core/log/posix/TerminalQuery.cpp` and `src/core/log/windows/TerminalQuery.cpp` — an
  operating-system difference is an implementation, never an `#ifdef` inside the decision
  (`.agent/rules/platform.md`).
- `core::escape()` and `core::unescape()` round-trip again. 0x7E was outside the printable range,
  so `~` came out as a numeric escape; `escape()` writes a quote as `\"` and `unescape()`
  re-emitted it as `\"`; and an octal escape is three digits of which only those below `\100`
  begin with a zero, but the reader keyed the sequence on `'0'`, so `\101` and everything above it
  came back as literal text. The reader now opens an octal sequence on any octal digit and
  consumes exactly three, which reads the `\0dd` form it used to accept identically (a
  leading zero is octal-neutral). One reading did change: `\1` through `\7` used to come back as
  the two literal characters and now open a three-digit octal run. That is correct for anything
  `escape()` produced, which is what `unescape()` is for; hand-written or third-party escaped text
  that meant a literal backslash before a digit has to spell the backslash `\\`.
- `core::FNV`'s byte-wise overload reads the bytes with `std::bit_cast` rather than a
  `reinterpret_cast` through the object representation, which no constant evaluation may do — so
  the `constexpr` on the overload can now be taken up. (What it accepts narrowed too; see
  **Breaking**.)
- `core::base64::decode()`'s index lambda captures its 256-byte table by reference; by value it
  copied the whole table on every call.
- `core::Utils` stays inside the bounds it is given. `splitKeyValuePairs()` rebuilt its last
  segment with the length-less `std::string_view(char const*)` constructor, which calls `strlen()`:
  it read past the view (AddressSanitizer reports a heap-buffer-overflow) and returned whatever
  followed as part of the value. `toLower()`/`toUpper()` passed a plain `char` to
  `tolower()`/`toupper()`, undefined for any byte with the high bit set — every continuation byte
  of a UTF-8 sequence, and `cli::about::registerProjects()` sorts project titles through them; a
  character wider than a byte goes to `towlower()`/`towupper()` rather than being truncated into
  the narrow functions' domain. (`readFileAsString()` is fixed too; because it answers
  differently, its entry is under **Breaking**.) `eachElement()`'s end iterator was
  `max + 1` computed in `int` and cast back, which for a type narrower than `int` wraps onto
  `begin()` — so the range was empty — and for one as wide as `int` overflows. Windows'
  `threadName()` resized by `len - 1` with `len == 0` on a failed conversion, which threw
  `length_error` before the `LocalFree()` below it ran.
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

- `core::platform::testing::InMemoryFileSystem`'s read-write stream stops handing out stale
  pointers. It cached the get-area pointers into the `std::string` that holds the file and then
  appended to that same string on every write, so a write that grew it past its capacity left every
  one of those pointers naming freed memory -- a heap-use-after-free on the next read. The same
  append also ignored where the stream stood, so `openReadWrite()` could never overwrite in place
  the way the `std::fstream` behind `NativeFileSystem` does; it now carries one position for reading
  and writing, as `std::filebuf` has, and implements `seekoff()`/`seekpos()`.
- `core::platform::MessageQueue` guards its wakeup pointer like every other member. `setWakeup()`
  wrote it with no lock while `push()` and `shutdown()` read and dereferenced it from another
  thread, so a teardown that cleared the pointer could be missed and leave `push()` signalling a
  destroyed `Wakeup`. Registration takes the queue's mutex now, and the signalling happens under it,
  so once `setWakeup(nullptr)` returns nothing is still inside `signal()`.
- `core::platform::SignalHandler::restore()` deregisters the interrupt wakeup as well as the
  callback. It left `interruptWakeup` pointing at the `Wakeup` the caller was about to destroy, and
  Linux's `processSignalFd()`, the SIGINT handler elsewhere and the Windows console control handler
  all reach it through that pointer.
- `core::platform`'s Windows `EnvironmentProvider` reads the environment through
  `core::LiveEnvironment`, and writes it through `core::setProcessEnvironmentVariable()` and
  `core::unsetProcessEnvironmentVariable()`, as the POSIX one already did. Its own
  `GetEnvironmentVariableA()` call could not tell an empty value from a missing name, so it reported
  a variable set to `""` as unset while the other reader of the same Win32 block reported `""`; it
  also ignored the buffer-too-small return and allocated 32 KiB per lookup.
- `core::testing::setTestEnv()` sets an empty value instead of removing the variable. On Windows
  `_putenv_s(name, "")` removes it, so `setTestEnv(name, "")` and `unsetTestEnv(name)` were the same
  call and `ScopedEnv` could not put back a variable whose previous value was empty -- and the
  environment is process-global, so the loss crossed into every later test.
- `core::platform::SystemPipe`'s never-stall guarantee holds on Windows too. Only the read socket
  was made non-blocking, so a producer that outran the loop parked in `send()` indefinitely; the
  write socket is non-blocking now and `write()` answers `WSAEWOULDBLOCK` as done, the way the POSIX
  branch answers `EAGAIN`. `write()` also clamps the byte count to `INT_MAX`, as `read()` already
  did, so a count past it can no longer go negative or wrap into a short write reported as a full
  one. And `makeLoopbackPair()` compares the two ends' addresses and retries: `accept()` returns
  whoever connected, and between the `listen()` and the `accept()` any local process can take the
  ephemeral port, leaving a "pair" whose ends are not each other's.
- `core::platform::FileSystem::isExecutableFile()` classifies a symlink by what it points at. On
  POSIX it accepted any symlink and then read the followed target's permissions, so a symlink to a
  directory was reported as executable on the directory's own search bit -- against the
  declaration's "Directories always return false". A PATH lookup that trusted it ran the directory
  and failed with `EACCES` instead of trying the next entry.
- `core::platform::globMatchFilename()` reaches its bracket arm for the character it exists to
  match. The literal arm was tested first, so `globMatchFilename("[", "[[]")` -- POSIX's own way to
  spell a literal bracket -- answered false. A `[` that no `]` closes stays the literal `[` that
  `fnmatch(3)` reads it as.
- `core::platform::stripTrailingSeparator()` and `isCaseOnlyRename()` keep a spelling the platform's
  native narrow encoding cannot hold. Both went through `path::generic_string()`, which on Windows
  narrows to the ANSI code page: MSVC throws on a path it cannot spell, and where it does not throw
  it substitutes, so two distinct paths come back as one. `InMemoryFileSystem` keys its whole file
  map on the first of them, so one file answered for another.
  `NativeFileSystem::createTempFile()` had the same problem twice, and is `wchar_t` end to end on
  Windows now.
- `core::platform::NativeFileSystem::createDirectory()` names an existing directory rather than
  reporting "No such file or directory", the diagnosis for the other way it fails; and `rename()`
  reports the two-hop recase's own error instead of the first attempt's, and says where a failed
  rollback left the entry.

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
