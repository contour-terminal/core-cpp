# Profiling

core-cpp is instrumented for the [Tracy](https://github.com/wolfpld/tracy) profiler through
macros in `<core/Profiling.hpp>`. With `CORE_CPP_WITH_TRACY` off (the default) every macro
expands to `(void) 0`, no Tracy header is included and nothing is linked.

`CORE_CPP_WITH_TRACY=ON` resolves Tracy 0.14.1, the version contour pins, from the parent
project, `find_package(Tracy)` or CPM, and `core::base` links `Tracy::TracyClient` PUBLIC. A client
core-cpp fetches is built with `TRACY_ENABLE` (off upstream, which compiles the client away) and
`TRACY_ONLY_LOCALHOST`, which keeps its socket and its announcement on the machine.

```cpp
#include <core/Profiling.hpp>

CORE_ZONE_SCOPED;                           // a zone named by its source location
CORE_ZONE_SCOPED_N("EventLoop::runOnce");   // a zone with a literal name
CORE_FRAME_MARK;                            // a frame or request boundary
CORE_THREAD_NAME("worker-0");               // names the calling thread
CORE_PLOT("loop.readyQueue", value);        // a numeric timeline
```

**A zone never spans a `co_await`.** A zone is a stack-shaped, thread-local guard; a coroutine
that suspends inside one resumes later, possibly on another thread, and corrupts the profiler's
zone stack. Put zones in synchronous code; `CORE_FRAME_MARK` is safe anywhere.

Build with the `clang-tracy` preset. A consumer that profiles its own program sets
`CORE_CPP_WITH_TRACY=ON` and uses the same Tracy version, since a client and the viewer that
reads its captures must match. The how-to is
[`profiling-tracy.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/guides/profiling-tracy.md).
