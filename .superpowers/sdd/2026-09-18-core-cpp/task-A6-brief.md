# Brief for Task A6

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).

### Task A6: `core::net` import (contour, as-is)

**Files:** `src/core/net/**` from `D:\contour\src\net` at `6777ff05` (72 files).
- Rename `net/platform/{Clock,SystemPipe,WinsockInit}` → use `core::platform` (A4) and delete the copies.
- `Tls.cpp` + `Tls_test.cpp` go to the `core::net_tls` target, built only if `CORE_CPP_WITH_TLS` (OpenSSL PRIVATE).
- `NetError.hpp` + `IoResult.hpp` go to `core::net_types` (INTERFACE).

- [ ] Tests first (all 9 `*_test.cpp` files plus the `testing/` helpers), with `net::` → `core::net::`. Expect FAIL, then import and expect PASS.
- [ ] Label socket tests `loopback`. Run `ctest --preset clang-tsan` in WSL.
- [ ] Commit `net: import contour's event loop, sockets, TLS and HTTP server as core::net`.

