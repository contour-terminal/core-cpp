# Platform

Rules for `src/core/platform/`, and for any platform-specific code in another module: how an
operating-system difference is expressed, and the Windows facts that have each cost a debugging
session.

The module is endo's generic platform layer with one clock merged from endo, contour and
fastcached (the design spec,
[Part I §1](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md);
the [module page](https://contour-terminal.github.io/core-cpp/modules/platform/) lists what is in
it). The endo-specific parts stay in endo: processes and the pipes to them, process listing, the
project file tree, install paths and the interrupt throttle.

## A platform difference is an implementation, never an `#ifdef` in logic

1. **Define an abstract interface** in `src/core/platform/`, or in the module that needs it.
2. **Implement it per platform** in the module's private `posix/`, `linux/`, `bsd/` (Apple and
   the BSDs), `darwin/`, `windows/` or `emscripten/` subdirectory, and list each file in the
   matching `SOURCES_POSIX`, `SOURCES_LINUX`, `SOURCES_BSD`, `SOURCES_WINDOWS` or
   `SOURCES_EMSCRIPTEN` argument of `core_cpp_add_module`. The table in
   `cmake/CoreCppTargets.cmake` decides which list a platform compiles; `BSD` covers macOS,
   which shares kqueue, and `POSIX` means a native POSIX system, which Emscripten is not,
   although CMake sets `UNIX` there. `tests/cmake/check-platform-sources.cmake` proves the
   selection for each platform.
   - **A module's own directory holds only platform-independent code.** A source with one
     `#ifdef` branch per platform is split into those subdirectories rather than kept whole:
     contour's `PollEventSource.cpp` became `core::net`'s `posix/PollBackend.cpp` and a
     `windows/` WFMO backend (removed in 0.5.0), epoll is in `linux/` and kqueue in `bsd/`, and each of those
     directories holds the `DefaultBackend.cpp` the CMakeLists names exactly one of. A public header
     stays portable: a member only one platform uses is declared on all of them. Origin: user
     direction, 2026-09-18 (core-cpp's Task A6).
   - **A file in a platform subdirectory has no file-wide guard of its platform**, such as an
     `#ifndef _WIN32` around the whole file: its source list already chooses it, and a guard
     would compile a file listed for the wrong platform to nothing, so a test binary loses its
     cases without a build error. An `#if` that chooses between variants within the platform
     family stays (`__linux__`'s `accept4()` in `posix/`, a constant an older SDK lacks), and
     the file's provenance row names it. Origin: Ruling R42, from the Task A6 review.
3. **Inject it through a constructor.** The concrete type is named once, at the composition
   root; logic never checks the platform.

Origin: [endo `AGENT.md`, "New Platform Feature"](https://github.com/contour-terminal/endo/blob/f774a210ce989e5947b8f61d715068b1dc96088c/AGENT.md),
which also says: **do not bypass the platform layer.** A direct `::stat`, `std::getenv` or
`CreateFileW` in another module is a seam that tests cannot replace.

- **The environment is read in one place**, `EnvironmentProvider` (or `core::Environment`, which
  [core-cpp#7](https://github.com/contour-terminal/core-cpp/issues/7) merges with it), so a test
  sets it with `testing::TestEnvironmentProvider` or `core::testing::FakeEnvironment` rather than
  mutating the process's environment, which is shared by every thread and every test in the
  binary. **It is written in one place too:** `core::setProcessEnvironmentVariable()`, never
  `setenv()`, which clang-tidy's `concurrency-mt-unsafe` rejects along with `getenv()`.
- **Time is `IClock`.** Its `now()` is what logic reads, and its virtual `refresh()` is the
  contract that lets `CachedClock` be refreshed by the event loop at fixed points of each turn
  (the design spec, Part I §2, the `runOnce` turn). Tests use `ManualClock`.
- **Private subdirectories are in no file set**, so a platform header never becomes public API.

## What a machine is

- **An architecture is what the compiler built for, not what the kernel is running.** Read it
  from the compiler's predefined macros, not `uname` or `GetNativeSystemInfo`: an x86-64 process
  under Rosetta or WOW64 runs x86-64 code on a machine that truthfully reports `arm64`.
- **The Windows version comes from `RtlGetVersion`**, not `GetVersionEx`, which reports 6.2 for
  every release since Windows 8 unless the caller ships a compatibility manifest.
- **Free space is `std::filesystem::space_info::available`, not `free`**: the difference is the
  root-reserved portion, which an unprivileged process cannot write.
- Origin: [fastcached `.agent/rules/platform-service-and-config.md`, "The daemon host, and what a
  machine IS"](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/rules/platform-service-and-config.md).

## Windows

- **A public header never includes `<Windows.h>`.** core-cpp compiles its own sources with
  `NOMINMAX`, `WIN32_LEAN_AND_MEAN` and `_WIN32_WINNT=0x0A00`, but those are PRIVATE definitions,
  so a consumer's translation unit that includes a core-cpp header has none of them, and
  `<Windows.h>` would define `min`/`max` macros into it. Declare in the header, include
  `<Windows.h>` in the `.cpp`; `core::testing::suppressWindowsDialogs()` was moved out of line
  for exactly this reason, and so were the Windows halves of `core/platform/Types.hpp`
  (`windows/WindowsTypes.cpp`), which spells `HANDLE` and `DWORD` as `void*` and
  `unsigned long` and holds the two spellings equal with a `static_assert` there.
  `Types_test.cpp` fails if the header brings `<Windows.h>` back.
- **`cl` and `clang-cl` are both the MSVC driver** (`CORE_CPP_MSVC_DRIVER`): both take `/`
  options, and both get `/utf-8 /permissive- /Zc:__cplusplus`. clang-cl defines `__clang__` and
  `_MSC_VER` but not `__GNUC__`, and on clang-cl `/Wall` means `-Weverything`, which is why the
  pedantic table uses `/W4` there.
- **A `char` is UTF-8.** See [`build-and-toolchain.md`](build-and-toolchain.md), "What a `char`
  is", for why the process code page is decided for the whole executable and why a narrow
  `std::filesystem::path` can throw.
- **Winsock is initialised before the first socket**, through `core::platform::WinsockInit`.
- **A console input handle is waitable but is not a socket.** IOCP cannot wait on it directly,
  so the IOCP backend bridges it with a thread-pool wait whose callback only posts to the port
  (the design spec, Part I §2, "IOCP readiness bridging"). fastcached kept a second coroutine
  runtime only because its event loop could not park on a console handle.
- **Sockets arrive inheritable, and `SO_REUSEADDR` means something else.** Both are socket
  rules, in [`async-and-net.md`](async-and-net.md).
- **An unattended Windows process must never open a modal dialog.** See
  [`build-and-toolchain.md`](build-and-toolchain.md): the build installs the suppression for
  every test executable through `core::testing_dialogs`.
- **A test that needs a console, a symlink privilege or a second session `SKIP`s** with the
  reason (exit 77 through `core::testing_main`) rather than passing. Tests that share a global
  resource such as the console input serialise on a named mutex.

## The WebAssembly subset

Under single-threaded Emscripten, `core::platform` builds only Types (with `NativeHandle`),
PlatformError, Clock, StringUtils, PathUtils, GlobMatch, FileUri and the POSIX environment and
file-info providers (`SOURCES_EMSCRIPTEN`), and runs only their tests. Its
row in the module table says `PLATFORMS wasm-subset`, so an Emscripten build compiles that list
and nothing else of the module; a module whose row says `any` compiles all of `SOURCES` there,
plus its `SOURCES_EMSCRIPTEN`.
What the list may not use is what single-threaded Emscripten lacks: no `std::thread`, no
blocking wait and no `Threads::Threads`, and no sockets, child processes or signal handlers. The
filesystem is allowed: under Emscripten it is Emscripten's virtual one, which the POSIX file-info
provider lists and `lstat()`s like any other, and its in-memory pipes are what `platformRead()`
reads there. Anything else in the module may use all of these freely. See
[`library-hygiene.md`](library-hygiene.md).

## Open work

- **[core-cpp#7](https://github.com/contour-terminal/core-cpp/issues/7)** — unify
  `core::Environment` (`core::base`, from crispy) with `core::platform::EnvironmentProvider`
  into one injectable seam.
