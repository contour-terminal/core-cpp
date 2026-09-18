# core-cpp

core-cpp is the shared C++23 foundation of the Contour Terminal projects: base utilities,
logging, command-line parsing, an operating-system layer, coroutines, an event loop with sockets
and TLS, and a terminal UI, all in namespace `core`.

It exists because this code had spread across contour, endo, fastcached and tuidu as
near-verbatim copies, and the copies drifted: when core-cpp was designed, tuidu's copy was two
and a half months behind and no longer built against contour, and fastcached kept a second
coroutine runtime of its own. core-cpp holds one copy, merges the two coroutine and networking
designs into one, and each project consumes it instead of carrying its own.

!!! note "Status"
    0.1.0 is in development and nothing is tagged yet. The build framework and the
    [`testing`](modules/testing.md) module exist; the [module overview](modules/index.md) says
    when each of the others arrives.

## Where to start

- [Using core-cpp with CPM](getting-started/cpm.md), the way endo, fastcached, tuidu,
  Lightweight and morph consume it.
- [Vendoring](vendoring.md), the way contour consumes it: a verbatim, hash-checked copy that
  builds without fetching anything.
- [Building core-cpp itself](getting-started/building.md), and the [options](getting-started/options.md).
- The [modules](modules/index.md), their layering, and which of them build for WebAssembly.
- The [design notes](design/dependency-injection.md): dependency injection, error handling,
  coroutine lifetimes, threading, portability and profiling.
- The [API reference](https://contour-terminal.github.io/core-cpp/api/), generated from the
  public headers.

## Properties a consumer can rely on

- **It changes nothing of its parent's build.** As a subproject it sets no compiler launcher, no
  C++ standard and no directory-wide flag, and every option is `CORE_CPP_`-prefixed.
- **Every flag is private to core-cpp's own targets**, so linking it changes no flag of yours.
- **Dependencies come from you first**: a target your project already defines, then
  `find_package`, then a CPM fetch only if `CORE_CPP_FETCH_DEPS` allows it.
- **A subset builds for single-threaded WebAssembly** and is tested under node on every change.
- **Versions are SemVer tags**, and while the major version is 0 every breaking change is listed
  in the [changelog](changelog.md) with a migration note.
