# Provenance

For every file under `src/core/`, `cmake/portable/` and `cmake/FetchTransferBound.cmake`: which
upstream file and commit it came from, or `origin: core-cpp` for code written here. This is the
mechanical record behind the Global Constraints' "Upstream sync discipline" —
[`tests/cmake/check-cmake-hygiene.cmake`](../../tests/cmake/check-cmake-hygiene.cmake)'s
`provenance` rule refuses a file in scope without a row, and a row naming a file that no longer
exists.

An import or port task (A4–A7, B1–B12) appends or bumps rows in the same commit as the import.
Task B12b reads this table mechanically, before v0.1.0, to catch up every row whose upstream has
moved since it was synced. A consumer migration's delta check (`.agent/guides/`) reads it the same
way. `NOTICE` and `CHANGELOG.md` record the same commits at the granularity of a whole import; this
table is the per-file index into them.

Repos: [`contour-terminal/contour`](https://github.com/contour-terminal/contour),
[`contour-terminal/endo`](https://github.com/contour-terminal/endo),
[`LASTRADA-Software/fastcached`](https://github.com/LASTRADA-Software/fastcached). A file adapted
from more than one upstream file (a merge) names its primary upstream in the table and lists the
others in notes.

| core-cpp path | upstream repo | upstream path | synced SHA | notes |
|---|---|---|---|---|
| `cmake/FetchTransferBound.cmake` | LASTRADA-Software/fastcached | `cmake/FetchTransferBound.cmake` | `eb9c9c68da8fadfd43b0b36366919cb462689f48` | verbatim; re-synced together with `CompileCache.cmake` (`cmake/portable/README.md`) |
| `cmake/portable/CompileCache.cmake` | LASTRADA-Software/fastcached | `cmake/portable/CompileCache.cmake` | `eb9c9c68da8fadfd43b0b36366919cb462689f48` | verbatim |
| `cmake/portable/README.md` | origin: core-cpp | - | - | documents the two verbatim files above |
| `src/core/Assert.hpp` | contour-terminal/contour | `src/crispy/Assert.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `Require`/`Guarantee`/`todo`/`unreachable`/`setFailHandler`; `fatal`/`SoftRequire` split out to `src/core/log/Assert.hpp` to close a base→log layering cycle |
| `src/core/Base64.hpp` | contour-terminal/contour | `src/crispy/Base64.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Base64_test.cpp` | contour-terminal/contour | `src/crispy/Base64_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/Config.hpp.in` | origin: core-cpp | - | - | generates `core/Config.hpp` |
| `src/core/Deferred.hpp` | contour-terminal/contour | `src/crispy/Deferred.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Defines.hpp` | contour-terminal/contour | `src/crispy/Defines.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `CRISPY_*` macros renamed `CORE_*` |
| `src/core/Environment.cpp` | contour-terminal/contour | `src/crispy/Environment.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import additions (Task A4): the process-environment writer (`setProcessEnvironmentVariable()`/`unsetProcessEnvironmentVariable()`), and on Windows `LiveEnvironment` reads a variable set to the empty string as set |
| `src/core/Environment.hpp` | contour-terminal/contour | `src/crispy/Environment.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import addition (Task A4): the process-environment writer |
| `src/core/Environment_test.cpp` | contour-terminal/contour | `src/crispy/Environment_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import additions (Task A4): the process-environment writer's cases |
| `src/core/Escape.hpp` | contour-terminal/contour | `src/crispy/Escape.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/FNV.hpp` | contour-terminal/contour | `src/crispy/FNV.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import fix (`48b261a`): the trivially-copyable overload no longer recurses forever for `T != unsigned char` |
| `src/core/FNV_test.cpp` | origin: core-cpp | - | - | written for the fix above |
| `src/core/Flags.hpp` | contour-terminal/contour | `src/crispy/Flags.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Overloaded.hpp` | contour-terminal/contour | `src/crispy/Utils.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `crispy::Overloaded` (defined in `Utils.hpp`) and the ambient global `::Overloaded` merged into one `core::Overloaded` here |
| `src/core/Profiling.hpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Profiling.hpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | `FC_*` macros renamed `CORE_*` |
| `src/core/Profiling_test.cpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Profiling_test.cpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | - |
| `src/core/Ranges.hpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Ranges.hpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | `FastCache::Ranges` renamed `core::ranges`; `FC_RANGES_FORCE_FALLBACK` renamed `CORE_RANGES_FORCE_FALLBACK` |
| `src/core/Ranges_test.cpp` | LASTRADA-Software/fastcached | `src/FastCache/Core/Ranges_test.cpp` | `ee71f868547712892b7d9a2ebff60d49c496e25c` | imported although the A3 brief did not list it: `Ranges.hpp`'s own documentation points to it as the proof that its fallbacks work |
| `src/core/Times.hpp` | contour-terminal/contour | `src/crispy/Times.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import fix (`6b1a4d7`): the postfix `operator++`/`operator--` return the prior position, not the new one |
| `src/core/Times_test.cpp` | contour-terminal/contour | `src/crispy/Times_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/UserInfo.cpp` | contour-terminal/contour | `src/crispy/UserInfo.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/UserInfo.hpp` | contour-terminal/contour | `src/crispy/UserInfo.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Utils.cpp` | contour-terminal/contour | `src/crispy/Utils.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/Utils.hpp` | contour-terminal/contour | `src/crispy/Utils.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `Overloaded` split out to `Overloaded.hpp`; `views::enumerate` is now a function template |
| `src/core/Utils_test.cpp` | contour-terminal/contour | `src/crispy/Utils_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/App.cpp` | contour-terminal/contour | `src/crispy/App.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/App.hpp` | contour-terminal/contour | `src/crispy/App.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CLI.cpp` | contour-terminal/contour | `src/crispy/CLI.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CLI.hpp` | contour-terminal/contour | `src/crispy/CLI.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CLI_test.cpp` | contour-terminal/contour | `src/crispy/CLI_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/cli/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/log/Assert.hpp` | contour-terminal/contour | `src/crispy/Assert.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `fatal()`/`SoftRequire()` moved here from crispy's `Assert.hpp` to close a base→log layering cycle; `Require`/`Guarantee`/`todo`/`unreachable`/`setFailHandler` stay in `src/core/Assert.hpp` |
| `src/core/log/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/log/LogSink.cpp` | contour-terminal/contour | `src/crispy/LogSink.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/log/LogSink.hpp` | contour-terminal/contour | `src/crispy/LogSink.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | - |
| `src/core/log/LogSink_test.cpp` | contour-terminal/contour | `src/crispy/LogSink_test.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | post-import fix (`fe62488`): reads the fixture files through a `contentsOf()` stringstream helper, not `std::istreambuf_iterator` |
| `src/core/log/LogStore.cpp` | contour-terminal/contour | `src/crispy/LogStore.cpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `logstore::` renamed `core::log::` |
| `src/core/log/LogStore.hpp` | contour-terminal/contour | `src/crispy/LogStore.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `logstore::` renamed `core::log::` |
| `src/core/testing/CMakeLists.txt` | origin: core-cpp | - | - | - |
| `src/core/testing/CatchMain.cpp` | origin: core-cpp | - | - | `core::testing_main`: Catch2's `main()`, the exit-code contract and the `LOG` filter |
| `src/core/testing/CatchMain_test.cpp` | origin: core-cpp | - | - | covers the `LOG` filter |
| `src/core/testing/EnvHelper.hpp` | contour-terminal/endo | `src/testing/EnvHelper.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | POSIX writes go through `core::setProcessEnvironmentVariable()`, and `ScopedEnv` reads through `core::LiveEnvironment`, instead of `setenv()`/`unsetenv()`/`getenv()` |
| `src/core/testing/EnvHelper_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/testing/Environment.hpp` | contour-terminal/contour | `src/crispy/testing/Environment.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | `crispy::testing::FakeEnvironment` renamed `core::testing::FakeEnvironment` |
| `src/core/testing/ExitCode.cpp` | origin: core-cpp | - | - | - |
| `src/core/testing/ExitCode.hpp` | origin: core-cpp | - | - | - |
| `src/core/testing/ExitCode_test.cpp` | origin: core-cpp | - | - | - |
| `src/core/testing/ScopedTempDir.hpp` | contour-terminal/endo | `src/testing/ScopedTempDir.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/testing/ScopedTempDir_test.cpp` | contour-terminal/endo | `src/testing/ScopedTempDir_test.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/testing/ScopedWorkingDirectory.hpp` | contour-terminal/endo | `src/testing/ScopedWorkingDirectory.hpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | - |
| `src/core/testing/ScopedWorkingDirectory_test.cpp` | origin: core-cpp | - | - | the upstream file has no test |
| `src/core/testing/SuppressWindowsDialogs.cpp` | contour-terminal/contour | `src/crispy/SuppressWindowsDialogs.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | the out-of-line body added in core-cpp (`26de633`); same four-way merge as `SuppressWindowsDialogs.hpp` below |
| `src/core/testing/SuppressWindowsDialogs.hpp` | contour-terminal/contour | `src/crispy/SuppressWindowsDialogs.hpp` | `6777ff05014f8ff163b071e8b0e942830119db80` | merged with contour `src/coro/testing/SuppressWindowsDialogs.hpp` (same commit), endo `src/testing/SuppressWindowsDialogs.hpp` (`f774a210ce989e5947b8f61d715068b1dc96088c`) and fastcached `src/tests/WindowsErrorPopups.hpp` (`eb9c9c68da8fadfd43b0b36366919cb462689f48`) |
| `src/core/testing/SuppressWindowsDialogsAtStartup.cpp` | contour-terminal/endo | `src/testing/SuppressWindowsDialogsAtStartup.cpp` | `f774a210ce989e5947b8f61d715068b1dc96088c` | verbatim |
