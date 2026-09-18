# base

The generic utilities every other module may use. Namespace `core`, headers directly in
`src/core/`, target `core::base`.

!!! note "Status"
    Not imported yet. Task A3 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    imports it from contour's `src/crispy` at `6777ff05`, with crispy's namespace renamed
    `core`, and adds fastcached's profiling and range headers.

## Planned contents

- From crispy: `Assert`, `Defines`, `Environment`, `Escape`, `FNV`, `Flags`, `Times`,
  `UserInfo`, `Utils`, `Overloaded`, `Deferred`, and `Base64` (namespace `core::base64`).
- From fastcached: `Profiling.hpp` (the `CORE_ZONE_*` and `CORE_FRAME_MARK*` macros, compiled out
  unless `CORE_CPP_WITH_TRACY` is on; see [Profiling](../design/profiling.md)) and `Ranges.hpp`
  (range helpers that select a standard facility by its feature-test macro).

crispy's renderer-side half (ring buffers, LRU caches, the aligned allocator and the like) stays
in contour: it has one consumer. A file moves into core-cpp when a second project needs it.

Depends on Threads, except under single-threaded Emscripten, where it builds without it.
