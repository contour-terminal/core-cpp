# Profiling with Tracy

[Tracy](https://github.com/wolfpld/tracy) instrumentation in core-cpp is opt-in and costs
nothing when off. It is gated by `CORE_CPP_WITH_TRACY` (default `OFF`). When it is off, no
Tracy header is included, nothing is linked, and every macro in `<core/Profiling.hpp>` expands
to `(void) 0`.

`<core/Profiling.hpp>` is part of `core::base`: fastcached's profiling header, with its `FC_*`
macros renamed `CORE_*`. The Tracy row of the dependency table pins the Tracy release contour
pins in its `cmake/Tracy.cmake` (0.14.1 at contour `6777ff05`), because the client compiled into
a program and the viewer that reads its captures must be the same version. With the option on,
`core::base` links `Tracy::TracyClient` PUBLIC; a client core-cpp fetches is built with
`TRACY_ENABLE` and `TRACY_ONLY_LOCALHOST`. CI builds and tests the `clang-tracy` preset
(`linux (clang-22-tracy)`).

This guide is adapted from
[fastcached `.agent/guides/profiling-tracy.md`](https://github.com/LASTRADA-Software/fastcached/blob/b5ded89c5ae6ba5b45337335ce774c5ae6986d65/.agent/guides/profiling-tracy.md).

## Building with Tracy

```sh
cmake --preset clang-tracy          # RelWithDebInfo, CORE_CPP_WITH_TRACY=ON
cmake --build --preset clang-tracy
ctest --preset clang-tracy
```

A consumer that profiles its own program with core-cpp inside it sets
`CORE_CPP_WITH_TRACY=ON` before adding core-cpp, and provides or lets core-cpp fetch the same
Tracy version. Two Tracy clients of different versions in one program do not work.

## Adding zones

Instrument through the macros, never through Tracy's own:

```cpp
#include <core/Profiling.hpp>

CORE_ZONE_SCOPED;                           // a zone named by its source location
CORE_ZONE_SCOPED_N("EventLoop::runOnce");   // a zone with a compile-time literal name
CORE_FRAME_MARK;                            // one logical frame or request boundary
CORE_FRAME_MARK_NAMED("tui.render");        // a named frame series
CORE_THREAD_NAME("core-worker-0");          // names the calling OS thread
CORE_PLOT("loop.readyQueue", value);        // a numeric timeline
```

- **A zone never spans a `co_await`.** `CORE_ZONE_SCOPED*` declares a thread-local, stack-shaped
  RAII guard. A coroutine that suspends inside it resumes on a later turn of the loop, maybe on
  another thread, and the guard's destructor then corrupts the profiler's per-thread zone stack.
  Put zones only in synchronous leaf functions, or in `{ }` blocks that contain no `co_await`.
  `CORE_FRAME_MARK` is a stackless event and is safe anywhere, including inside coroutine loops.
  This is a rule, in [`../rules/async-and-net.md`](../rules/async-and-net.md).
- **Macro arguments must have no side effect the program relies on**: when Tracy is off they are
  discarded unevaluated.
- **`CORE_ZONE_SCOPED_N` requires a string literal.** For a runtime label, name the current zone
  with `CORE_ZONE_NAME(pointer, length)` or annotate it with `CORE_ZONE_TEXT(pointer, length)`.

## Capturing and reading a profile

Build the viewer, or a headless capture tool, from the Tracy sources CPM fetched into the build
tree, at the same version as the client:

```sh
# The interactive viewer (needs glfw, freetype, capstone and dbus development packages).
cmake -S <build>/_deps/tracy-src/profiler -B /tmp/tracy-gui -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/tracy-gui

# Headless capture into a .tracy file, to open later.
cmake -S <build>/_deps/tracy-src/capture -B /tmp/tracy-capture -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/tracy-capture
```

The instrumented program listens on TCP port 8086. Start it, connect the viewer (or
`tracy-capture -o out.tracy -a 127.0.0.1`), then drive the workload: an on-demand client records
only from the moment a viewer connects. In the viewer, the Statistics window sorted by self time
finds the hot spots; a gap between a zone and the zone nested in it is time spent waiting.
