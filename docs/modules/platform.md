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
| `<core/platform/CpuCounters.hpp>` | `ICpuCounterSource`, a portable seam for CPU work counts (`CpuCounter`: instructions, cycles, reference cycles); `CpuCounterSample` and its delta; `measure()`; `makeDefaultCpuCounterSource()`, which never returns null and, where the platform cannot count, reports why per measure. `testing::FakeCpuCounterSource` is the test double |
| `<core/platform/Types.hpp>` | `NativeHandle`, `InvalidHandle`, `ProcessId`, the standard handles, `platformRead()`/`platformWrite()`/`platformClose()`, `isTerminal()`, `nativeHandleToNumber()` |
| `<core/platform/PlatformError.hpp>` | `PlatformError`, the error of this module's fallible operations, and `toString()` |
| `<core/platform/Wakeup.hpp>` | `Wakeup`, a signal one thread raises to wake another out of `poll()` or `WaitForMultipleObjects()`: an eventfd on Linux, a self-pipe on macOS and the BSDs, an event on Windows |
| `<core/platform/SystemPipe.hpp>` | `createSystemPipe()`: an in-process byte channel whose read end an event loop can wait on, on every platform; `ChannelResult`, what one read of it produced |
| `<core/platform/WinsockInit.hpp>` | `ensureWinsockInitialized()`, once per process; a no-op off Windows |
| `<core/platform/SignalHandler.hpp>` | `SignalHandler`: SIGCHLD, SIGTSTP, SIGCONT and SIGINT through signalfd on Linux and handlers elsewhere, Ctrl+C and Ctrl+Break on Windows, and an optional `Wakeup` to raise on an interrupt |
| `<core/platform/MessageQueue.hpp>` | `MessageQueue<T>`, a thread-safe queue that can raise a `Wakeup` on every push |
| `<core/platform/FileSystem.hpp>`, `<core/platform/NativeFileSystem.hpp>` | the `FileSystem` interface, errors as `std::expected`, a lazy recursive walk as a `core::Generator`, and the durability calls a crash-safe replace needs; `NativeFileSystem` over `std::filesystem` and the operating system's own sync calls |
| `<core/platform/FileInfoProvider.hpp>` | `FileInfoProvider`, a directory listing with `stat(2)` metadata (`FileEntry`), a single file or a glob pattern |
| `<core/platform/ProcessEnvironment.hpp>` | `ProcessEnvironment`, a `core::Environment` a shell writes: variables with a set-then-export model, each write a `std::expected`; `isValidEnvironmentName()` |
| `<core/platform/WorkingDirectory.hpp>` | `WorkingDirectory`: `changeDirectory()` and `currentDirectory()`, both over `std::filesystem::path`, so the answer round-trips on Windows whatever it spells |
| `<core/platform/UserPaths.hpp>` | `homeDirectory()`, `userName()` and `configHome()` over a `core::Environment`, by default the process environment |
| `<core/platform/PathUtils.hpp>` | path spelling: `normalizePath()`, `joinPath()`, `absolutePath()`, `canonicalCasePath()`, `stripTrailingSeparator()`, `isCaseOnlyRename()`, `resolveDevicePath()` |
| `<core/platform/GlobMatch.hpp>` | `globMatchFilename()` (`*`, `?`, `[...]`) and `containsGlobChars()` |
| `<core/platform/FileUri.hpp>` | RFC 3986 percent-encoding and RFC 8089 `file://` URIs |
| `<core/platform/SystemInfo.hpp>` | `hostName()` and `cachedHostName()` |
| `<core/platform/MachineId.hpp>` | `readMachineId()`: the 32-digit id in `/etc/machine-id`, read through an injected `FileSystem` |
| `<core/platform/StringUtils.hpp>` | `trimInPlace()` |

The test doubles are in `testing/` and in `core::platform::testing`:
`testing::InMemoryFileSystem` (a `FileSystem` held in maps, with symlinks, permissions and
refused paths), `testing::MockFileInfoProvider`, `testing::TestProcessEnvironment`, which never
touches the process environment and, being a `core::Environment` as well, is the one double for
code that reads and code that writes, and `testing::TestWorkingDirectory`, which never changes
directory.

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
 The native implementations of `FileInfoProvider`,
`ProcessEnvironment` and `WorkingDirectory` are in the private `posix/` and `windows/` directories,
which no consumer includes; a composition root gets them from `nativeFileInfoProvider()`,
`nativeProcessEnvironment()` and `nativeWorkingDirectory()`, each a `std::unique_ptr` to the
interface:

| Factory | Windows | Linux, macOS, the BSDs | Emscripten |
|---|---|---|---|
| `nativeProcessEnvironment()` (`<core/platform/ProcessEnvironment.hpp>`) | reads through `core::LiveEnvironment` and exports through `core::setProcessEnvironmentVariable()`, both over `GetEnvironmentVariableW`/`SetEnvironmentVariableW` in UTF-8; names case-insensitive | the POSIX one: reads through `core::LiveEnvironment`, exports through `core::setProcessEnvironmentVariable()` | the POSIX one, over the environment Emscripten's libc keeps for the module (under node a fixed default set, not the host's) |
| `nativeWorkingDirectory()` (`<core/platform/WorkingDirectory.hpp>`) | `std::filesystem::current_path`, reported in the capitalization the filesystem stores | `chdir(2)` and `std::filesystem::current_path` | the POSIX one, over Emscripten's virtual filesystem |
| `nativeFileInfoProvider()` (`<core/platform/FileInfoProvider.hpp>`) | `std::filesystem`: the read-only flag as permissions, no blocks, device or inode | the POSIX provider: `lstat(2)` for every field, symlinks as links with their targets, and the blocks, device and inode | the POSIX provider, over Emscripten's virtual filesystem; a relative symlink target reads resolved against the link's directory there (3.1.56 at least) |

The POSIX file-info provider was endo's `LinuxFileInfoProvider`, which used nothing Linux-specific;
it is one implementation, `PosixFileInfoProvider`, for every POSIX system. Each call makes a new
provider.

## Replacing a file durably

A rename alone can reach the disk before the data it names, so a writer that replaces a file by
renaming a temporary over it can leave an empty or partial file behind after a power loss.
`FileSystem` carries the four calls that close that gap, in the order a writer makes them:

| Call | POSIX | macOS | Windows |
|---|---|---|---|
| `createNewFile(path)` | `open(O_CREAT \| O_EXCL \| O_NOFOLLOW)`, mode 0600 | as POSIX | `CreateFileW(CREATE_NEW)`, opening a reparse point rather than following it |
| `syncFile(path)` | `fsync()` | `fcntl(F_FULLFSYNC)`, else `fsync()` | `FlushFileBuffers()` |
| `replaceFile(from, to)` | `rename()` | as POSIX | `MoveFileExW(MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH)` |
| `syncDirectory(path)` | `fsync()` on the directory | as POSIX | nothing to flush; answers whether it is a directory |

`createNewFile()` refuses anything already at the path, a symbolic link included, so a link
planted where the temporary goes cannot redirect the write. `replaceFile()` never takes the
two-hop lettercase retry `rename()` does, which would leave a moment with neither name. The four
are virtual with defaults rather than pure, so an implementation written before them still
compiles: the defaults have nothing to flush, replace through `rename()`, and create after a
check rather than atomically. That is right for `InMemoryFileSystem` and for a test double; a
backend that forwards to another `FileSystem` overrides all four, or it answers from the defaults
rather than from the backend it wraps.

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

## System sleep and wake

`ISystemPowerMonitor` tells a program that the machine is about to sleep or has woken up. It is a
hint, not a lock: a liveness check can use it to tell "the peer went silent" from "this machine was
asleep", and so avoid declaring a healthy connection dead on the first tick after a wake.

- **`SystemPowerEvent`** is `WillSleep` or `DidWake`.
- **`ISystemPowerMonitor::subscribe(handler)`** registers a `std::function<void(SystemPowerEvent)>`
  and returns a `PowerSubscription`. The handler is called for every event until the token is
  destroyed.
- **`PowerSubscription`** is move-only and RAII: destroying it (or assigning over it) unsubscribes,
  and a default-constructed one subscribes to nothing. The token keeps the monitor's handler table
  alive, so it may safely outlive the monitor.
- **`makeNativeSystemPowerMonitor()`** returns the platform's monitor as a
  `std::expected<std::unique_ptr<ISystemPowerMonitor>, PlatformError>`. `NotImplemented` means this
  platform (or a build without the backend) has none; `NotificationRegistrationFailed` means the
  platform has one but the operating system or session refused the registration (no system bus, for
  one). The factory does not wait on the operating system's answer where it can avoid it: on Linux
  it opens the bus and queues the match without waiting for the reply, so a hung bus does not hold
  up the caller, and a match the bus refuses later is retried like a lost bus rather than reported.

| Platform | Source | Notes |
| --- | --- | --- |
| macOS | IOKit `IORegisterForSystemPower`, on a private serial dispatch queue | an idle sleep that is asked about and then not taken reports nothing |
| Linux | logind's `PrepareForSleep` signal over sd-bus, on a private thread | needs `CORE_CPP_WITH_SYSTEMD` (on by default) and `pkg-config`; without it the monitor is `NotImplemented`. A lost system bus is reconnected (below) |
| Windows | `PowerRegisterSuspendResumeNotification`, on the power manager's thread | suspend is `WillSleep`, an automatic resume is `DidWake` |
| other | - | `NotImplemented` |

Events are hints, not a protocol:

- **A `DidWake` may arrive without a `WillSleep` before it**, and a monitor that was created
  successfully does not guarantee that any event ever arrives.
- **On Linux a `WillSleep` may arrive late**, after suspend has begun: the monitor takes no
  inhibitor lock.
- **On Linux delivery may pause while the system bus is unavailable.** The monitor's thread notices
  the lost connection, closes it and reconnects: it waits 1 s before the first attempt and doubles
  the wait after each failed one, up to 30 s, and starts over at 1 s once the bus has accepted the
  match again. Events sent while it was away are lost. Destroying the monitor interrupts the wait.

The handler contract:

- **Handlers run on a platform thread that is not the caller's**: the monitor's own on macOS and
  Linux, the power manager's on Windows. Marshal to your own loop (post to it, or set an atomic)
  rather than doing the work in the handler.
- **A handler must not throw.** One that does terminates the process, on every backend and in the
  test double: `PowerHandlerRegistry::dispatch()` is `noexcept`, the same trade
  `core::async::DetachedTask` and `EventLoop::onHostPump` make, since there is no caller to hand
  the exception to.
- **A handler must not create or destroy a monitor.** Destroying one from its handler deadlocks on
  macOS (it waits for its own queue) and on Windows (it takes a process-wide lock the handler
  already holds), and joins its own thread on Linux; creating one on Windows takes that lock again.
- **A handler must never block waiting on a thread that may be destroying its subscription or its
  monitor**: both wait for a running handler to return, on every backend.
- **A handler should return promptly.** On macOS the system does not sleep until every `WillSleep`
  handler has returned, up to IOKit's 30 s timeout.
- **Unsubscribing waits out a running handler.** When the `PowerSubscription` is destroyed or reset on
  another thread, the handler is no longer running once that returns, so a capture by reference may
  then be destroyed. A handler that drops its own subscription does not wait.
- **A capture by value may be destroyed later**, on the dispatching thread, if a dispatch still holds
  the handler when the token goes.
- **A handler subscribed from inside another handler** first sees the event after the one being
  dispatched.
- **The handler must not be empty**; `subscribe()` checks it with `Require()`.

The monitor never inhibits sleep. On macOS it answers IOKit's can-sleep and will-sleep messages so the
system goes on, but only after the `WillSleep` handlers have returned, so a slow handler delays sleep
by its own running time; on Linux and Windows nothing is held back.

```cpp
auto monitor = core::platform::makeNativeSystemPowerMonitor();
if (!monitor)
    return; // no power events here: skip the wake grace period

auto const subscription = (*monitor)->subscribe([&loop](core::platform::SystemPowerEvent event) {
    if (event == core::platform::SystemPowerEvent::DidWake)
        loop.post([&] { livenessCheck.forgiveNextMiss(); }); // marshal onto the loop
});
```

Declare the subscription after whatever its handler captures, so it is destroyed first.

`core::platform::testing::FakeSystemPowerMonitor` (`testing/FakeSystemPowerMonitor.hpp`) is the test
double: `deliver(event)` calls every subscriber on the calling thread and `subscriberCount()` reports
the live subscriptions. Unlike a native monitor, it may be destroyed by one of its handlers.

To implement another monitor, dispatch through `core::platform::detail::PowerHandlerRegistry`, as
every backend and the fake do: it is the supported way, since `PowerSubscription` can be constructed
only through it. It is thread-safe, keeps the handler table alive for as long as a token or a
dispatch needs it, and its `dispatch()` is the `noexcept` boundary of the exception policy above.

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
- **The process environment is written in one place.** `ProcessEnvironment` exports through
  `core::setProcessEnvironmentVariable()` (in [base](base.md)), never `setenv()`, and reports what
  it refused: a name that is empty or holds `=` or NUL, a value that holds NUL, and on Windows text
  that is not UTF-8.
- **`UserPaths` reads through a `core::Environment`**, so a test passes a
  `core::testing::FakeEnvironment`. Its default argument is a `core::LiveEnvironment`, so
  `homeDirectory()`, `userName()` and `configHome()` called without one read the process environment (on
  Windows the operating system's block) through the same body the tests run. A directory variable
  that is not UTF-8 names no directory on Windows, so it counts as unset there
  (`core::pathFromUtf8()`, in [base](base.md#paths-and-utf-8)).
- **`Wakeup`'s constructor throws** `std::runtime_error` when the operating system refuses the
  eventfd, self-pipe or event, which it does only when descriptors, handles or kernel memory are
  exhausted. That is unrecoverable, since no event loop can run without its wakeup channel, so it
  throws rather than returning an error, as the
  [exceptions rule](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/cpp-guidelines.md)
  allows.

## CPU counters

`makeDefaultCpuCounterSource()` returns the platform's `ICpuCounterSource`; it never returns null.
Each measure is either supported or carries a reason (`unavailableReason()`), and `supports()` is
derived from that reason, so the two cannot disagree. All counts are for the calling process and
every thread of it.

| Platform | Instructions | Cycles | ReferenceCycles | Scope |
| --- | --- | --- | --- | --- |
| Linux | yes | yes | no | user mode only (`perf_event_open`) |
| macOS | yes | yes | no | user and kernel together (`proc_pid_rusage`) |
| Windows | no | no | yes | user and kernel together (`QueryProcessCycleTime`) |
| other | no | no | no | - |

- `ReferenceCycles` is a count of constant-rate ticks of CPU time, not core cycles; do not divide
  it into `Instructions` for an IPC.
- Linux needs a hardware PMU and a permissive `perf_event_paranoid`. Create the source before
  starting worker threads: its events follow only threads started afterwards. Each `read()` is
  judged over the interval since the previous one, and a measure the kernel multiplexed away in that
  interval is absent from that sample, not wrong. Only consecutive reads of one source are checked: a
  delta between non-consecutive samples (an outer `measure()` around a nested one on the same source)
  is not, and may undercount. Use one source per measured region and do not nest `measure()` on one
  source.
- `note()` reports a deviation detected at run time from the scope above (Linux before 5.13 also
  counts child processes); it is empty otherwise, on macOS and Windows always.

## Under Emscripten

The row in the module table says `PLATFORMS wasm-subset`. Under single-threaded Emscripten only
Types, PlatformError, Clock, CpuCounters, StringUtils, PathUtils, GlobMatch, FileUri and the
POSIX `ProcessEnvironment`, `FileInfoProvider` and `WorkingDirectory` (behind
`nativeProcessEnvironment()`, `nativeFileInfoProvider()` and `nativeWorkingDirectory()`) build (the `SOURCES_EMSCRIPTEN` list), and their tests run under
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
