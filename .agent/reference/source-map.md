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
  CoreCppModules.cmake      the module table, in dependency order, and the walker that enters it
  CPM.cmake                 CPM 0.40.8, bounded and hash-checked download
  FetchTransferBound.cmake  verbatim from fastcached: bounds every dependency transfer
  portable/
    CompileCache.cmake      verbatim from fastcached: fastcache-cc > sccache (opt-in) > ccache
    README.md               provenance of the verbatim files, and how to re-sync them
  CoreCppVendor.cmake       (planned, A8) MODE=sync|check|export of the vendoring contract
src/core/
  Config.hpp.in             generates <core/Config.hpp>: version, skip exit code, WITH_* flags
  *.hpp, *.cpp              core::base (crispy, fastcached Core): Assert, Defines, Environment,
                            Escape, FNV, Flags, Times, UserInfo, Utils, Overloaded, Deferred,
                            Base64, Profiling, Ranges
  log/                      core::log (crispy): LogStore, LogSink; Assert (fatal, SoftRequire)
  cli/                      core::cli (crispy): CLI, App
  platform/                 core::platform (endo platform, one merged Clock): Clock, Types,
                            PlatformError, Wakeup, SignalHandler, SystemPipe, WinsockInit,
                            MessageQueue, FileSystem, FileInfoProvider, EnvironmentProvider,
                            UserPaths, PathUtils, GlobMatch, FileUri, SystemInfo, StringUtils;
                            posix/ linux/ windows/ are private; testing/ holds the fakes
                            (InMemoryFileSystem, MockFileInfoProvider, TestEnvironmentProvider)
  coro/                     core::coro, header-only: Generator (endo); StopToken (std:: or
                            the fallback); Task, UniqueCoroHandle, Cancellation, Awaitable,
                            whenAll/whenAny (contour); executors, AsyncQueue (planned, B1)
  net/                      (planned, A6 and B2-B11) core::net_types, core::net, core::net_tls:
                            EventLoop, IoBackend and backend/, sockets, dialling, timers, TLS
  tui/                      (planned, A7 and B12) core::tui_output (the leaf) and core::tui
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
    check-release.cmake             a release tag equals project(VERSION) and has a CHANGELOG section
    check-release-selftest.cmake    proves each refusal of check-release.cmake
scripts/
  tool-versions.py          prints, installs or checks the pinned clang-format/clang-tidy
  clang-format.py           formats or checks every C++ source with the pinned clang-format
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
- **A platform-specific implementation** goes in the module's `posix/`, `linux/`, `darwin/` or
  `windows/` subdirectory and in the matching `SOURCES_*` list; it is in no file set.
- **A test double** goes in the module's `testing/` subdirectory; it is public and compiled into
  the module.
- **A test** goes next to what it tests, as `Foo_test.cpp`, and in the module's
  `core_cpp_add_test()` call.
- **A check over the tree** is a `cmake -P` script in `tests/cmake/` with a self-test.
