# Building core-cpp

core-cpp builds with CMake presets. Every tree lives in `out/build/<preset>`, and every preset
turns on the tests, the pedantic warning set and `-Werror`.

```sh
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --preset clang-debug

cmake --workflow --preset ci-clang-debug     # the same three steps in one command
```

## Requirements

- CMake 3.25 or newer, and Ninja.
- A C++23 compiler: clang 22, GCC 14, AppleClang from Xcode 16, or Visual Studio 2022 or newer
  with `cl` or `clang-cl` 22.
- Python 3, for `scripts/clang-format.py` and `scripts/tool-versions.py`.
- For WebAssembly: emsdk 3.1.56 or newer, and node.
- Network access for the first configure, to fetch Catch2 with CPM, unless it is installed.

## Presets

| Preset | Host | Compiler | What it is for |
|---|---|---|---|
| `clang-debug` | Linux, macOS | clang | everyday development |
| `clang-release` | Linux, macOS | clang | optimised build |
| `gcc-debug`, `gcc-release` | Linux | GCC | the second standard library (libstdc++ with GCC) |
| `clang-asan-ubsan` | Linux, macOS | clang | AddressSanitizer and UndefinedBehaviorSanitizer |
| `clang-tsan` | Linux, macOS | clang | ThreadSanitizer |
| `clang-tidy` | Linux, macOS | clang | clang-tidy on every core-cpp target |
| `clang-coverage` | Linux, macOS | clang | source-based coverage; the compiler cache is off |
| `clang-tracy` | Linux, macOS | clang | RelWithDebInfo with the Tracy profiler |
| `appleclang-debug`, `appleclang-release` | macOS | AppleClang | Apple's own compiler and libc++ |
| `cl-debug`, `cl-release` | Windows | MSVC | `cl-debug` runs the tests under the Debug CRT's iterator checks |
| `clangcl-debug`, `clangcl-release` | Windows | clang-cl | |
| `cl-release-tls` | Windows | MSVC | with `core::net_tls` (OpenSSL) |
| `emscripten` | any | emcc | single-threaded WebAssembly; tests run under node |

Every preset has a `ci-<name>` workflow preset. Presets that do not apply to the host are
hidden, so `cmake --list-presets` shows only what can run.

### Windows

Run from a Visual Studio developer shell, which puts `cl`, `clang-cl` and the Windows SDK on the
`PATH`. In PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake --preset clangcl-debug
cmake --build --preset clangcl-debug
ctest --preset clangcl-debug
```

!!! warning "clang-cl trees built from the compiler cache"
    When fastcache-cc serves a clang-cl compile from its cache, Ninja records no header
    dependencies for that object, so a later header edit leaves it stale. Until
    [fastcached#1531](https://github.com/LASTRADA-Software/fastcached/issues/1531) is fixed,
    rebuild a `clangcl-*` tree with `cmake --build --preset clangcl-debug --clean-first` after
    editing a header. `cl` trees and Linux and macOS trees are not affected.

### macOS

The `appleclang-*` presets use Xcode's compiler. The `clang-*` presets work with Homebrew's LLVM
when you name its compiler and its libc++:

```sh
LLVM="$(brew --prefix llvm)"
cmake --preset clang-release -DCMAKE_CXX_COMPILER="$LLVM/bin/clang++" \
      -DCMAKE_EXE_LINKER_FLAGS="-L$LLVM/lib/c++ -L$LLVM/lib/unwind -lunwind -Wl,-rpath,$LLVM/lib/c++ -Wl,-rpath,$LLVM/lib/unwind"
```

### WebAssembly

The `emscripten` preset builds the WebAssembly subset for single-threaded Emscripten, without
`-pthread`, and runs its tests under node:

```sh
source /path/to/emsdk/emsdk_env.sh      # sets EMSDK, which the preset's toolchain file uses
cmake --preset emscripten
cmake --build --preset emscripten
ctest --preset emscripten
```

It turns the TUI off and compiles and links with `-fexceptions`: Catch2 reports a skipped test
and a failed `REQUIRE` by throwing, and without exception catching every such throw aborts the
test binary, which breaks the exit-code contract (a `SKIP` exits 1 instead of 77). CI runs it
with emsdk 3.1.56 and with the latest release.

## The compiler cache

A top-level build goes through the first compiler cache that works:

1. fastcache-cc, when a fastcached daemon answers at `FASTCACHE_ADDR` (default `127.0.0.1:6674`);
2. sccache, only with `-DALLOW_SCCACHE_FALLBACK=ON`;
3. ccache;
4. none.

`-DFASTCACHE_AUTO_INSTALL=ON` downloads fastcache-cc from fastcached's releases, and
`-DFASTCACHE_AUTO_START=ON` starts a daemon in the background. `-DUSE_COMPILER_CACHE=OFF` turns
caching off; only the `clang-coverage` preset does that. The choice is recorded in
`CMakeCache.txt` as `CORE_CPP_CXX_COMPILER_LAUNCHER`, and `build.ninja`'s `LAUNCHER =` lines show
it in effect. The module is a verbatim copy from fastcached; see
[`cmake/portable/README.md`](https://github.com/contour-terminal/core-cpp/blob/master/cmake/portable/README.md).

## Tests

- `ctest --preset <preset>` runs everything; `ctest --preset <preset> -L hygiene` runs the checks
  over the tree and the build contract; `-L canary` runs the programs that must fail.
- Test binaries are `core-cpp-<module>-test`, next to their module in the build tree. Run one
  directly to pass Catch2 options, for example a test name or `--list-tests`.
- Their exit status is 0 (passed), 1 (failed), 77 (every test case skipped) or 2 (nothing ran).

## Sanitizers, clang-tidy and coverage

```sh
cmake --workflow --preset ci-clang-asan-ubsan
TSAN_OPTIONS=halt_on_error=1 cmake --workflow --preset ci-clang-tsan

python scripts/tool-versions.py --install     # clang-format and clang-tidy 22.1.8 from PyPI
cmake --preset clang-tidy && cmake --build --preset clang-tidy

cmake --preset clang-coverage && cmake --build --preset clang-coverage
LLVM_PROFILE_FILE="$PWD/out/build/clang-coverage/profiles/%m-%p.profraw" ctest --preset clang-coverage
llvm-profdata merge -sparse out/build/clang-coverage/profiles/*.profraw -o coverage.profdata
llvm-cov report -instr-profile=coverage.profdata \
    out/build/clang-coverage/src/core/testing/core-cpp-testing-test \
    -ignore-filename-regex='(_deps|_test\.cpp)'
```

Use the `llvm-profdata` and `llvm-cov` of the same major version as the compiler.

## Formatting

```sh
python scripts/clang-format.py           # formats every C++ source
python scripts/clang-format.py --check   # what CI runs
```

The script refuses any clang-format that is not the version `.clang-format-version` pins.
