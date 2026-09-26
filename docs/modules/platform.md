# platform

The operating-system layer: clocks, wakeups, signals, pipes, the file system, the environment and
paths, each behind an interface with a test double. Namespace `core::platform`, directory
`src/core/platform/`, target `core::platform`. It links [base](base.md) (for `Generator`, among
other things), and `ws2_32` on Windows.

Imported from the generic half of endo's `src/platform` at `f774a210`, with one clock merged from
endo's, contour's (`src/net/platform/Clock.hpp` at `6777ff05`) and fastcached's
(`src/FastCache/Core/Clock.hpp` at `b461e8b6`), and `SystemPipe` merged with contour's copy.
endo's shell-specific platform code stays in endo: processes and the pipes to them
(`Process`, `Pipe`, `WaitResult`, `ProcessProvider`), the project file tree, install paths and
the interrupt throttle.

| Header | What it has |
|---|---|
| `<core/platform/Clock.hpp>` | the clock seams, see [below](#clocks) |
| `<core/platform/Types.hpp>` | `NativeHandle`, `InvalidHandle`, `ProcessId`, the standard handles, `platformRead()`/`platformWrite()`/`platformClose()`, `isTerminal()`, `nativeHandleToNumber()` |
| `<core/platform/PlatformError.hpp>` | `PlatformError`, the error of this module's fallible operations, and `toString()` |
| `<core/platform/Wakeup.hpp>` | `Wakeup`, a signal one thread raises to wake another out of `poll()` or `WaitForMultipleObjects()`: an eventfd on Linux, a self-pipe on macOS and the BSDs, an event on Windows |
| `<core/platform/SystemPipe.hpp>` | `createSystemPipe()`: an in-process byte channel whose read end an event loop can wait on, on every platform; `ChannelResult`, what one read of it produced |
| `<core/platform/WinsockInit.hpp>` | `ensureWinsockInitialized()`, once per process; a no-op off Windows |
| `<core/platform/SignalHandler.hpp>` | `SignalHandler`: SIGCHLD, SIGTSTP, SIGCONT and SIGINT through signalfd on Linux and handlers elsewhere, Ctrl+C and Ctrl+Break on Windows, and an optional `Wakeup` to raise on an interrupt |
| `<core/platform/MessageQueue.hpp>` | `MessageQueue<T>`, a thread-safe queue that can raise a `Wakeup` on every push |
| `<core/platform/FileSystem.hpp>`, `<core/platform/NativeFileSystem.hpp>` | the `FileSystem` interface, errors as `std::expected`, a lazy recursive walk as a `core::Generator`; `NativeFileSystem` over `std::filesystem` |
| `<core/platform/FileInfoProvider.hpp>` | `FileInfoProvider`, a directory listing with `stat(2)` metadata (`FileEntry`), a single file or a glob pattern |
| `<core/platform/EnvironmentProvider.hpp>` | `EnvironmentProvider`: variables with a set-then-export model, the working directory, `homeDirectory()`, `userName()`, `configHome()` |
| `<core/platform/UserPaths.hpp>` | `homeDirectory()` and `configHome()` over a `core::Environment`, by default the process environment |
| `<core/platform/PathUtils.hpp>` | path spelling: `normalizePath()`, `joinPath()`, `absolutePath()`, `canonicalCasePath()`, `stripTrailingSeparator()`, `isCaseOnlyRename()`, `resolveDevicePath()` |
| `<core/platform/GlobMatch.hpp>` | `globMatchFilename()` (`*`, `?`, `[...]`) and `containsGlobChars()` |
| `<core/platform/FileUri.hpp>` | RFC 3986 percent-encoding and RFC 8089 `file://` URIs |
| `<core/platform/SystemInfo.hpp>` | `hostName()` and `cachedHostName()` |
| `<core/platform/StringUtils.hpp>` | `trimInPlace()` |

The test doubles are in `testing/` and in `core::platform::testing`:
`testing::InMemoryFileSystem` (a `FileSystem` held in maps, with symlinks, permissions and
refused paths), `testing::MockFileInfoProvider` and `testing::TestEnvironmentProvider`, which never
touches the process environment.

`InMemoryFileSystem` answers as `NativeFileSystem` does wherever it models the behaviour: `isExecutableFile`,
`permissions` and `setPermissions` follow a symlink to its target (a dangling link is not
executable and has no permissions to set), and `createDirectory` refuses a path that is already
there, directory or file, with "File exists" (core-cpp#27). What it deliberately does not model,
so a test that depends on one of these belongs against the real filesystem:

| Behaviour | Native | `InMemoryFileSystem` |
|---|---|---|
| `exists()` on a dangling symlink | `false`: the followed status is an error | `true`: the link's own key is there |
| Symlink resolution | the OS, with `ELOOP` at its limit | a bounded textual chain of 32 hops, ending on the last key reached |
| Directory semantics | permissions, ordering and `.`/`..` from the OS | a key set; permissions are consulted by `isExecutableFile` and the refused-path list only |
| `putback()` of a character the file does not hold | may fail; libc++ refuses it, libstdc++ and MSVC accept | always accepted |
| `unget()` after a put-back character was read | hands the put-back character out again | steps back to the file's own byte |
 The native implementations of `FileInfoProvider` and
`EnvironmentProvider` are in the private `posix/` and `windows/` directories, which no consumer
includes; a composition root gets them from `nativeEnvironmentProvider()` and
`nativeFileInfoProvider()`, each a `std::unique_ptr` to the interface:

| Factory | Windows | Linux, macOS, the BSDs | Emscripten |
|---|---|---|---|
| `nativeEnvironmentProvider()` (`<core/platform/EnvironmentProvider.hpp>`) | `GetEnvironmentVariableA`/`SetEnvironmentVariableA`, names case-insensitive | the POSIX provider: reads through `core::LiveEnvironment`, exports through `core::setProcessEnvironmentVariable()` | the POSIX provider, over the environment Emscripten's libc keeps for the module (under node a fixed default set, not the host's) |
| `nativeFileInfoProvider()` (`<core/platform/FileInfoProvider.hpp>`) | `std::filesystem`: the read-only flag as permissions, no blocks, device or inode | the POSIX provider: `lstat(2)` for every field, symlinks as links with their targets, and the blocks, device and inode | the POSIX provider, over Emscripten's virtual filesystem; a relative symlink target reads resolved against the link's directory there (3.1.56 at least) |

The POSIX file-info provider was endo's `LinuxFileInfoProvider`, which used nothing Linux-specific;
it is one implementation, `PosixFileInfoProvider`, for every POSIX system. Each call makes a new
provider.

## Clocks

Logic that schedules against a deadline takes an `IClock&` rather than calling
`std::chrono::steady_clock::now()`, so a test can drive time.

- **`IClock`** answers `now()`, a `SteadyTimePoint`. Its virtual `refresh()` does nothing by
  default; an event loop calls it at fixed points of each turn (after the blocking wait returns,
  and before it computes the next timeout), which is what makes a caching clock correct.
- **`SteadyClock`** reads the OS clock on every call. **`defaultSteadyClock()`** is a process-wide
  one, for default arguments.
- **`CachedClock`** wraps another clock and answers the sample its last `refresh()` took, so a
  turn that reads the clock a thousand times pays for one read. Several loops may share one; it
  never moves backwards.
- **`ManualClock`** moves only on `advance()` and `setNow()`.
- **`IWallClock`**, **`SystemWallClock`**, **`ManualWallClock`** and **`defaultSystemWallClock()`**
  are the same seam for `std::chrono::system_clock`, for inputs that are wall-clock instants (an
  absolute expiry, a log line's date). Internal scheduling never reads the wall clock.
- **`WallClockRef`** is how a type keeps a borrowed `IWallClock`: it binds to a named clock and
  refuses a temporary, and because it is carried by value the refusal survives a forwarding
  constructor, which a deleted `T(IWallClock const&&)` overload does not
  ([fastcached#1028](https://github.com/LASTRADA-Software/fastcached/issues/1028)).

## Behaviour worth knowing

- **`SystemPipe` never blocks.** On POSIX both ends are non-blocking and close-on-exec. A write
  that the full channel refuses reports success, because the bytes already pending wake the reader
  just as well. On Windows the channel is a loopback TCP pair whose read end is mapped to a waitable
  event.
- **`SystemPipe::read()` tells three outcomes apart**, in a `ChannelResult`, before any failure:
  the bytes it read (`bytesRead()`), nothing yet (`empty()`: the channel is empty and the writer still
  there, so wait for readiness and read again) and the end of the stream (`isEndOfStream()`: the
  writer has closed and every byte it wrote has been read). Only a read that fails is a
  `PlatformError` (`IoError`), so a loop draining the channel never mistakes an empty channel for
  a broken one, or a closed writer for either.
- **`Types.hpp` does not include `<Windows.h>`.** `NativeHandle` is `void*` and `ProcessId`
  `unsigned long` there, and the calls into the Windows API are out of line. endo's copy defined
  `STDIN_FILENO` and the `SIG*` numbers on Windows for its process code; core-cpp's does not.
- **The process environment is written in one place.** `PosixEnvironmentProvider` exports through
  `core::setProcessEnvironmentVariable()` (in [base](base.md)), never `setenv()`.
- **`UserPaths` reads through a `core::Environment`**, so a test passes a
  `core::testing::FakeEnvironment`. Its default argument is a `core::LiveEnvironment`, so
  `homeDirectory()` and `configHome()` called without one read the process environment (on
  Windows the operating system's block) through the same body the tests run.
- **`Wakeup`'s constructor throws** `std::runtime_error` when the operating system refuses the
  eventfd, self-pipe or event, which it does only when descriptors, handles or kernel memory are
  exhausted. That is unrecoverable, since no event loop can run without its wakeup channel, so it
  throws rather than returning an error, as the
  [exceptions rule](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/cpp-guidelines.md)
  allows.

## Under Emscripten

The row in the module table says `PLATFORMS wasm-subset`. Under single-threaded Emscripten only
Types, PlatformError, Clock, StringUtils, PathUtils, GlobMatch, FileUri and the POSIX
`EnvironmentProvider` and `FileInfoProvider` (behind `nativeEnvironmentProvider()` and
`nativeFileInfoProvider()`) build (the `SOURCES_EMSCRIPTEN` list), and their tests run under
node. Clock needs no threads; its test of
concurrent `CachedClock` refreshes is compiled only where threads exist. There is no separate
`NativeHandle.hpp`: `NativeHandle` is part of `Types.hpp`, as it is in endo.

**A pipe has no end of file there.** `platformRead()` is `read(2)`, which elsewhere answers 0
for a drained pipe whose writer has closed. Emscripten's pipes behave as if the read end were
always non-blocking, so a read of an empty pipe fails with EAGAIN whether the writer is still
there or not: the drained pipe reads -1 with `errno` EAGAIN, never 0, and nothing tells it from a
pipe that is only empty for now. Code that must also run there learns that the writer is done
some other way, such as a length sent first, a terminator, or the writer's own completion. The
test of that case checks each platform's answer, 0 natively and -1 with EAGAIN under Emscripten.
