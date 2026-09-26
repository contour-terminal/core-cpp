# Source map

The repository's tree, with what each part is for. Parts marked *(planned)* are created by the
task named, from the design spec's module table
([Part I §1](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md))
and the implementation plan.

```
CMakeLists.txt              project(core-cpp VERSION ...): the version literal, then the cmake/
                            modules in order; tests/ only with CORE_CPP_TESTING
CMakePresets.json           configure/build/test/workflow presets; every tree is out/build/<preset>
cmake/
  CoreCppOptions.cmake      every CORE_CPP_* option (Part I §3); Emscripten forces TUI/TLS off
  CoreCppTopLevel.cmake     the ONLY file that touches global state; included only when top-level:
                            compiler cache, fetch bound, CMAKE_CXX_STANDARD, compile commands
  CoreCppToolchain.cmake    per-target policy as tables: pedantic flags (probed), WERROR,
                            sanitizers, coverage, clang-tidy; everything PRIVATE
  CoreCppTargets.cmake      core_cpp_add_module() (targets, FILE_SET HEADERS, layering check,
                            platform source lists) and core_cpp_add_test()
  CoreCppDependencies.cmake the dependency table: parent target, find_package, then CPM
  CoreCppModules.cmake      the module table, in dependency order, the rows of targets whose
                            platforms, option or links differ from their module's, and the walker
  CoreCppInstall.cmake      core_cpp_install(): installs the module targets as the package
                            core-cpp (CORE_CPP_INSTALL), leaving out what links a fetched dependency
  core-cppConfig.cmake.in   the package config: find_dependency() per table row in use, then targets
  CPM.cmake                 CPM 0.40.8, bounded and hash-checked download
  FetchTransferBound.cmake  verbatim from fastcached: bounds every dependency transfer
  portable/
    CompileCache.cmake      verbatim from fastcached: fastcache-cc > sccache (opt-in) > ccache
    README.md               provenance of the verbatim files, and how to re-sync them
  CoreCppVendor.cmake       the vendoring contract: MODE=sync copies a ref's file set out of git's
                            blobs and writes MANIFEST; MODE=check re-hashes a copy, needing no git
  CoreCppVendorReplace.cmake  the one part of a sync that can damage a copy, in a function so a
                            test can call it: two directory renames, and a restore when one fails
src/core/
  Config.hpp.in             generates <core/Config.hpp>: version, skip exit code, WITH_* flags
  *.hpp, *.cpp              core::base (crispy, fastcached Core): Assert, Defines, Environment,
                            Escape, FNV, Flags, Times, UserInfo, Utils, Overloaded, Deferred,
                            Base64, Generator (endo), Profiling, Ranges
  log/                      core::log (crispy): LogStore, LogSink; Assert (fatal, SoftRequire)
  cli/                      core::cli (crispy): CLI, App
  platform/                 core::platform (endo platform, one merged Clock): Clock, Types,
                            PlatformError, Wakeup, SignalHandler, SystemPipe, WinsockInit,
                            MessageQueue, FileSystem, FileInfoProvider, EnvironmentProvider,
                            UserPaths, PathUtils, GlobMatch, FileUri, SystemInfo, StringUtils;
                            posix/ linux/ windows/ are private; testing/ holds the fakes
                            (InMemoryFileSystem, MockFileInfoProvider, TestEnvironmentProvider)
  async/                    core::async, header-only: StopToken (std:: or the fallback); Task,
                            UniqueCoroHandle, Cancellation, Awaitable, whenAll/whenAny (contour);
                            executors, AsyncQueue (planned, B1)
  net/                      core::net_types (NetError, IoResult; header-only, everywhere),
                            core::net (EventLoop over IoBackend, which DISPATCHES readiness to
                            the handlers registered with it; PlatformLoop owns the default
                            backend; sockets, AsyncBufferedReader, WriteQueue, WithTimeout,
                            HttpServer, Diagnostics; native only),
                            core::net_tls (Tls, OpenSSL private); posix/ (PollBackend, sockets)
                            linux/ (EpollBackend) bsd/ (KqueueBackend) windows/ (IocpBackend,
                            sockets) emscripten/ (the browser as a host) detail/ are private but
                            for ParkTable (ParkId, ParkEntry) and WorkerIdentity, which
                            EventLoop.hpp names, and each platform directory has the
                            DefaultBackend.cpp CMake picks one of;
                            testing/ holds the fakes (ScriptedBackend, NullBackend, TestLoop,
                            ManualHostScheduler, makeSocketPair, BackendMatrix, CoroTestSupport),
                            makeSocketPair's halves in testing/posix/ and testing/windows/.
                            HostDrivenCanary.cpp is a process of its own: the host-driven loop's
                            refusal of run() and blockOn() is an abort, which no Catch case can
                            hold. The WebAssembly subset is IoBackend, IHostScheduler,
                            HostDrivenBackend and EventLoop with PlatformLoop and TestLoop over
                            it; IOCP, sockets and dialling (planned, B6-B11)
  tui/                      core::tui_output (endo: TerminalOutput, SyncGuard, SgrBuilder,
                            TerminalProtocols, CursorShape, Error; links base alone; with
                            CORE_CPP_WITH_TUI_OUTPUT, which the full TUI forces on), and
                            core::tui (TerminalInput, VtParser, Terminal, Buffer, Canvas,
                            Screen, the components and popups, completer/, MarkdownRenderer,
                            GenericSyntaxHighlighter, Sixel, images behind
                            CORE_CPP_WITH_IMAGES, runtime/ with TuiRuntime, InputSource,
                            TerminalInputSource, Modal; native only, and only with
                            CORE_CPP_WITH_TUI, and it links core::net since B12 composed the
                            runtime on core::net::EventLoop);
                            posix/ (termios, SIGWINCH, poll, clipboard tools) windows/
                            (console modes, input records, resize event) detail/ are
                            private -- runtime/ has no platform directories at all, because
                            the loop does the waiting on every platform;
                            MockTerminalOutput, runtime/testing/ScriptedInputSource and
                            TestHelpers.hpp are the fakes; .clang-tidy is the one directory
                            override (see its own comment)
  testing/                  core::testing: SuppressWindowsDialogs (no test framework needed),
                            Environment (FakeEnvironment), ScopedTempDir,
                            ScopedWorkingDirectory, EnvHelper (ScopedEnv);
                            core::testing_dialogs: the startup object that installs it;
                            core::testing_main: Catch2's main() with the LOG filter and the
                            exit-code contract
tests/
  CMakeLists.txt            the exit-code fixture, the hygiene checks, the Windows dialog canary
  ExitCodeFixture.cpp       one Catch2 case per outcome, for check-exit-codes.cmake
  WindowsDialogCanary.cpp   must fail fast, never hang on a dialog
  cmake/
    check-exit-codes.cmake          asserts core::testing_main's exit codes from outside
    check-cmake-hygiene.cmake       the rules of Part I §3 as a table, plus an allowlist
    check-cmake-hygiene-selftest.cmake  proves every hygiene rule refuses
    check-platform-sources.cmake    which source lists each platform compiles, Emscripten included
    check-layering.cmake            each module-table row bounds what its target links; refusals by name
    check-release.cmake             a release tag equals project(VERSION) and has a CHANGELOG section
    check-release-selftest.cmake    proves each refusal of check-release.cmake
    check-vendor-selftest.cmake     proves each refusal of cmake/CoreCppVendor.cmake, against git
                                    repositories it builds for the purpose
    check-install.cmake             installs the build, consumes the package, and exports from a
                                    parent with CORE_CPP_INSTALL on and off
  consumer-shared/          ConsumerSmoke.hpp: the loopback echo and the core::log line the CPM
                            and vendored consumer programs both run, so neither carries a copy
  consumer-cpm/             a consumer's own project, added to core-cpp with CPM: asserts that
                            core-cpp changed none of its flags, launcher or targets
  consumer-vendored/        the same for a verbatim copy, built with nothing fetched; CI runs it
                            in a container with no network and no git
  consumer-wasm/            the same for the WebAssembly subset behind one INTERFACE library,
                            run under node
  consumer-install/         a consumer of the INSTALLED package: find_package(core-cpp), core::net,
                            and core::testing_main where the build installed it
  consumer-install-nested/  a parent exporting a target that links core-cpp's, as morph does
scripts/
  tool-versions.py          prints, installs or checks the pinned clang-format/clang-tidy
  clang-format.py           formats or checks the C++ sources named, or --all, with the pinned build
docs/                       the documentation site (mkdocs.yml at the root); Doxyfile for /api/
  superpowers/              the design spec and implementation plan (not part of the site)
.agent/                     the rulebook, guides and this reference (AGENT.md is the index)
.github/
  workflows/build.yml       the CI matrix; ci-ok is the one required check
  workflows/docs.yml        builds the site; on master deploys it and the API reference to Pages
  workflows/release.yml     on a v* tag: check, vendor archive, SHA256SUMS, draft release
  workflows/downstream.yml  nightly: the verbatim fastcached files against fastcached master
  workflows/portability.yml nightly: FreeBSD
  release.yml               release-note categories by type/ label
```

## Where a new file goes

- **A public header** goes in its module's directory and in that module's `HEADERS` list; its
  namespace is `core::<directory>`.
- **A platform-specific implementation** goes in the module's `posix/`, `linux/`, `bsd/` (Apple
  and the BSDs), `darwin/`, `windows/` or `emscripten/` subdirectory and in the matching
  `SOURCES_*` list; it is in no file set. A module's own directory holds only
  platform-independent code: a source with an `#ifdef` per platform is split into those
  subdirectories instead.
- **A test double** goes in the module's `testing/` subdirectory; it is public and compiled into
  the module.
- **A test** goes next to what it tests, as `Foo_test.cpp`, and in the module's
  `core_cpp_add_test()` call.
- **A check over the tree** is a `cmake -P` script in `tests/cmake/` with a self-test.
