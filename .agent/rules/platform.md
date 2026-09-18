# Platform

Rules for `src/core/platform/`, and for any platform-specific code in another module: how an
operating-system difference is expressed, and the Windows facts that have each cost a debugging
session.

The module arrives in Task A4, from endo's generic platform layer with one clock merged from
endo, contour and fastcached (the design spec,
[Part I §1](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md)).
The endo-specific parts stay in endo: process listing, pipes, the project file tree and install
paths.

## A platform difference is an implementation, never an `#ifdef` in logic

1. **Define an abstract interface** in `src/core/platform/`, or in the module that needs it.
2. **Implement it per platform** in the module's private `posix/`, `linux/`, `darwin/` or
   `windows/` subdirectory, and list each file in the matching `SOURCES_POSIX`,
   `SOURCES_LINUX`, `SOURCES_BSD` or `SOURCES_WINDOWS` argument of `core_cpp_add_module`. The
   table in `cmake/CoreCppTargets.cmake` decides which list a platform compiles; `BSD` covers
   macOS, which shares kqueue.
3. **Inject it through a constructor.** The concrete type is named once, at the composition
   root; logic never checks the platform.

Origin: [endo `AGENT.md`, "New Platform Feature"](https://github.com/contour-terminal/endo/blob/f774a210ce989e5947b8f61d715068b1dc96088c/AGENT.md),
which also says: **do not bypass the platform layer.** A direct `::stat`, `std::getenv` or
`CreateFileW` in another module is a seam that tests cannot replace.

- **The environment is read in one place**, `EnvironmentProvider`, so a test sets it with
  `testing::TestEnvironmentProvider` rather than mutating the process's environment, which is
  shared by every thread and every test in the binary.
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
  for exactly this reason.
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

Under single-threaded Emscripten, `core::platform` builds only Types, NativeHandle,
PlatformError, Clock, StringUtils, PathUtils, GlobMatch and FileUri (`SOURCES_EMSCRIPTEN`).
Anything else in the module may use threads, sockets and the filesystem freely; anything on that
list may not. See [`library-hygiene.md`](library-hygiene.md).
