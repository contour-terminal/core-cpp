# Brief for Task A4

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A4: `core::platform` + `core::testing` helpers

**Files:**
- Create in `src/core/platform/`:
  - Types, PlatformError, Clock (merged), Wakeup + `{linux,posix,windows}/…Wakeup.cpp`, SignalHandler, SystemPipe, WinsockInit, MessageQueue
  - FileSystem, NativeFileSystem, `testing/InMemoryFileSystem`, FileInfoProvider + `linux/LinuxFileInfoProvider`, `windows/WindowsFileInfoProvider`, `testing/MockFileInfoProvider`
  - PathUtils, GlobMatch, EnvironmentProvider + posix/windows implementations + `testing/TestEnvironmentProvider`
  - UserPaths, FileUri, SystemInfo, StringUtils
- Move `Generator.hpp` to `src/core/coro/Generator.hpp`.
- Create: `src/core/testing/{ScopedTempDir,ScopedWorkingDirectory,EnvHelper}.hpp`
- Source: `D:\endo\src\platform` and `D:\endo\src\testing` at `f774a210`. Clock is merged with `D:\contour\src\net\platform\Clock.hpp` and `D:\fastcached\src\FastCache\Core\Clock.hpp`: `now()`, virtual no-op `refresh()`, `CachedClock`, `IWallClock`, `SystemWallClock`, `ManualWallClock`, `WallClockRef`, `SteadyTimePoint`, `SteadyDuration`, `defaultSteadyClock()`, `defaultSystemWallClock()`.
- Tests: endo `platform/{Clock,SystemPipe,FileSystem,Generator,PlatformError,EnvironmentProvider,FileInfoProvider,FileUri,Wakeup,MessageQueue,WindowsPlatform}_test.cpp`, `testing/ScopedTempDir_test.cpp`, fastcached `Core/{Clock,WallClockRef}_test.cpp`. Dedupe the three Clock tests into one.

- [ ] **Step 1:** Import the tests (blobs). Map `endo::platform` → `core::platform`, `endo::testing` → `core::testing`, and `endo::Generator` → `core::coro::Generator`. **Do not** import the `namespace endo { using … }` compatibility aliases. Build: expected FAIL.
- [ ] **Step 2:** Import the sources. Merge Clock (the fastcached `refresh()` contract with the endo/contour `now()` naming). `SystemPipe` returns `PlatformError`. Module rows: `platform DEPS base log coro`, `coro KIND INTERFACE` (for now only `Generator.hpp`).
- [ ] **Step 3:** Build and test on all four local configurations, then push. Expected: CI `ci-ok` green.
- [ ] **Step 3b: WebAssembly subset.** Add a `SOURCES_EMSCRIPTEN` list: Types, NativeHandle, PlatformError, Clock, StringUtils, PathUtils, GlobMatch, FileUri. Tag the tests of those files for the subset. Expected: the CI `emscripten` job passes on both emsdk versions.
- [ ] **Step 4:** Commit `platform: import endo's generic platform layer as core::platform with one merged clock` and `testing: scoped temp dir, working directory and env helpers`.

