# Options

Every option core-cpp declares is `CORE_CPP_`-prefixed, and all of them are in
[`cmake/CoreCppOptions.cmake`](https://github.com/contour-terminal/core-cpp/blob/master/cmake/CoreCppOptions.cmake).
A parent project sets one as a normal variable before adding core-cpp, or in CPM's `OPTIONS`;
core-cpp never writes to the parent's cache.

## core-cpp's options

| Option | Default | Effect |
|---|---|---|
| `CORE_CPP_TESTING` | ON when top-level | Build core-cpp's own tests. Forces `CORE_CPP_CATCH2_MAIN` on |
| `CORE_CPP_CATCH2_MAIN` | `CORE_CPP_TESTING` | Build `core::testing_main`, the Catch2 `main()` with core-cpp's exit-code contract (needs Catch2) |
| `CORE_CPP_BUILD_EXAMPLES` | ON when top-level | Build the examples |
| `CORE_CPP_FETCH_DEPS` | ON | Fetch a dependency with CPM when neither the parent project nor `find_package` provides it. OFF stops the configure instead, naming the option that needed it |
| `CORE_CPP_WITH_TUI` | ON | Build `core::tui`, which needs libunicode. Forces `CORE_CPP_WITH_TUI_OUTPUT` on |
| `CORE_CPP_WITH_TUI_OUTPUT` | `CORE_CPP_WITH_TUI`, on a first configure | Build `core::tui_output`, the styled-output leaf that links `core::base` alone. On with `CORE_CPP_WITH_TUI` off builds the leaf by itself, and fetches no libunicode. The default is taken once, when the cache entry is created: turning `CORE_CPP_WITH_TUI` off in an existing build tree leaves this on |
| `CORE_CPP_WITH_IMAGES` | ON, when `CORE_CPP_WITH_TUI` is | Decode images in `core::tui` (stb_image) |
| `CORE_CPP_WITH_TLS` | OFF | Build `core::net_tls` (OpenSSL, from the system) |
| `CORE_CPP_WITH_TRACY` | OFF | Instrument core-cpp for the Tracy profiler |
| `CORE_CPP_PEDANTIC` | ON when top-level | Compile core-cpp's targets with the pedantic warning set |
| `CORE_CPP_WERROR` | OFF (every preset sets ON) | Treat warnings in core-cpp's targets as errors |
| `CORE_CPP_CLANG_TIDY` | OFF | Run clang-tidy on core-cpp's targets. OFF also clears a `CXX_CLANG_TIDY` they would inherit |
| `CORE_CPP_CLANG_TIDY_EXE` | found on `PATH` | The clang-tidy to run; `.clang-tidy-version` pins 22.1.8 and a mismatch warns |
| `CORE_CPP_SANITIZERS` | empty | A list of `address`, `undefined`, `thread` and `leak`. Top-level builds only: as a subproject it stops the configure |
| `CORE_CPP_COVERAGE` | OFF | Instrument core-cpp's targets for coverage (source-based with clang) |
| `CORE_CPP_MSVC_STATIC_RUNTIME_VARIANTS` | OFF | With an MSVC-ABI compiler (cl, clang-cl), also declare a static-CRT twin of every compiled module but `core::testing_main` (whose Catch2 is built `/MD`): `core::<name>_mt`, target `core-cpp-<name>-mt`, built `/MT` (`/MTd` in Debug) and linking the other twins, for a `/MT` program in a build whose other programs link core-cpp `/MD`. The twins are `EXCLUDE_FROM_ALL`: linking `core::net_mt` builds base, log, platform and net a second time and nothing else. Anything else a twin links (OpenSSL, libunicode) is linked as given. Ignored, with one status line, by every other compiler |

"ON when top-level" means the default is `PROJECT_IS_TOP_LEVEL`: on when core-cpp is the
project being built, off when it is a subproject.

Every flag these options add is private to core-cpp's own targets.

## Under Emscripten

Only the WebAssembly subset builds, and `CORE_CPP_WITH_TUI`, `CORE_CPP_WITH_TUI_OUTPUT`,
`CORE_CPP_WITH_IMAGES` and `CORE_CPP_WITH_TLS` are forced off, by a normal variable that leaves the parent's cache
untouched. Without pthreads, Threads is not linked.

## Compiler-cache options (top-level builds only)

These belong to the compiler-cache module that core-cpp shares, verbatim, with fastcached, endo
and tuidu, so they are deliberately unprefixed: one spelling works in every one of those
projects. They take effect only when core-cpp is the top-level project.

| Option | Default | Effect |
|---|---|---|
| `USE_COMPILER_CACHE` | ON | Use a compiler cache at all |
| `ALLOW_SCCACHE_FALLBACK` | OFF | Let sccache be selected when fastcache-cc is not usable |
| `FASTCACHE_ADDR` | `127.0.0.1:6674`, or the environment's | Where the fastcached daemon answers; empty opts out of fastcache-cc |
| `FASTCACHE_AUTO_INSTALL` | OFF | Download fastcache-cc from fastcached's releases when no launcher is installed |
| `FASTCACHE_AUTO_START` | OFF | Start a fastcached daemon in the background when none answers |

## What the presets set

Every preset sets `CORE_CPP_TESTING`, `CORE_CPP_PEDANTIC` and `CORE_CPP_WERROR` on. The
sanitizer presets set `CORE_CPP_SANITIZERS`, `clang-tidy` sets `CORE_CPP_CLANG_TIDY`,
`clang-coverage` sets `CORE_CPP_COVERAGE` and turns `USE_COMPILER_CACHE` off, `clang-tracy`
sets `CORE_CPP_WITH_TRACY`, and `emscripten` sets `CORE_CPP_WITH_TUI` off. Every preset for
Linux, macOS and the BSDs (each inherits the hidden `unix` preset) and `cl-release-tls` set
`CORE_CPP_WITH_TLS`, so they build and test `core::net_tls` and need OpenSSL's development files
(see [Building](building.md#requirements)); `-DCORE_CPP_WITH_TLS=OFF` on the command line builds
without them.
