# net

The event loop, its backends, sockets, dialling, timers, TLS and an HTTP server. Namespace
`core::net`, directory `src/core/net/`, targets `core::net_types` (header-only: `NetError`,
`IoResult`), `core::net` and `core::net_tls` (only with `CORE_CPP_WITH_TLS`).

!!! note "Status"
    Not imported yet. Task A6 of the
    [implementation plan](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/plans/2026-09-18-core-cpp.md)
    imports contour's `src/net` at `6777ff05`; Tasks B2 to B11 merge fastcached's networking
    layer into it.

## Planned design

- **One `EventLoop` per thread, driving exactly one `IoBackend`.** Backends only dispatch; the
  loop resumes coroutines, on its own thread, on every backend.
- **Backends:** epoll (Linux), kqueue (macOS and the BSDs), IOCP (Windows, the default), poll
  everywhere, the Windows event-select backend as a fallback for one release, a scripted and a
  null backend for tests, and a host-driven backend that lets a browser's event loop drive the
  loop under WebAssembly.
- **Sockets** with one frame-free, stop-aware `ISocket` contract; a dialler that never resolves
  names on the loop; UDP and blocking transports; TLS behind an `ITlsContext` seam with no
  OpenSSL type in any header.

The full design is Part I §2 of the
[design spec](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md).
See [Threading](../design/threading.md) and [Coroutines and lifetimes](../design/coroutines-and-lifetimes.md).

Depends on [async](async.md) and [platform](platform.md); `core::net_tls` also on OpenSSL, from the
system, as a private dependency.
