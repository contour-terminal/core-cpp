# platform

The operating-system layer: clocks, wakeups, signals, pipes, the file system, the environment and
paths, each behind an interface with a test double. Namespace `core::platform`, directory
`src/core/platform/`, target `core::platform`.

!!! note "Status"
    Not imported yet. Task A4 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    imports the generic half of endo's `src/platform` at `f774a210`.

## Planned contents

- **One merged clock**, from endo's, contour's and fastcached's: `IClock` with `now()` and a
  `refresh()` an event loop calls at fixed points of each turn, `SteadyClock`, `CachedClock`,
  `ManualClock`, `IWallClock`, `SystemWallClock`, `ManualWallClock`, `WallClockRef`.
- `Wakeup`, `SignalHandler`, `SystemPipe`, `WinsockInit`, `MessageQueue`, `PlatformError`,
  `NativeHandle`.
- `FileSystem` and `NativeFileSystem`, `FileInfoProvider`, `EnvironmentProvider`, `PathUtils`,
  `GlobMatch`, `UserPaths`, `FileUri`, `SystemInfo`, `StringUtils`, with their doubles in
  `testing/`.

endo's shell-specific platform code (processes, pipes to child processes, the project file tree,
install paths) stays in endo.

Depends on [base](base.md), [log](log.md) and [coro](coro.md). Under WebAssembly only Types,
NativeHandle, PlatformError, Clock, StringUtils, PathUtils, GlobMatch and FileUri build.
