# Portability

core-cpp's CI builds and tests it on:

| Platform | Compilers | Event-loop backend (after Phase B) |
|---|---|---|
| Linux, x86-64 and arm64 | clang 22, GCC 14 and 15 | epoll; poll |
| macOS | AppleClang (Xcode 16), Homebrew LLVM 22 | kqueue; poll |
| Windows | MSVC, clang-cl | IOCP; the event-select backend as a fallback |
| FreeBSD (nightly) | the system clang | kqueue; poll |
| WebAssembly, single-threaded | Emscripten 3.1.56 and the latest release | host-driven |

C++23 is required, with compiler extensions off; CI also builds core-cpp's own sources with
`-DCMAKE_CXX_STANDARD=26`.

## Differences that are handled in one place

- **A platform difference is an implementation of an interface** under a module's private
  `posix/`, `linux/`, `bsd/`, `darwin/`, `windows/` or `emscripten/` directory, selected by CMake,
  never an `#ifdef` in logic. A module's own directory holds only platform-independent code.
- **A missing standard-library facility** (AppleClang's and libc++ 17's gaps) is selected by its
  `__cpp_lib_*` feature-test macro in one header, never by compiler version.
- **Text is UTF-8.** MSVC compiles with `/utf-8`, so a narrow string literal means the same bytes
  on every host.
- **Windows headers stay out of public headers**, because core-cpp's `NOMINMAX` and
  `WIN32_LEAN_AND_MEAN` are private to its own translation units.

## WebAssembly

The WebAssembly subset (see [the module overview](../modules/index.md#the-webassembly-subset))
builds without pthreads, so it links no `Threads::Threads` and forces no `SharedArrayBuffer` on
the page that loads it, and CI asserts that no compile or link command mentions `pthread`. Its
code uses no thread and no blocking wait. The `emscripten` preset compiles with `-fexceptions`,
which core-cpp's test binaries need; a consumer chooses its own exception model.

## Rules

[`platform.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/platform.md)
and
[`build-and-toolchain.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/rules/build-and-toolchain.md)
hold the platform rules and the reasons for them.
