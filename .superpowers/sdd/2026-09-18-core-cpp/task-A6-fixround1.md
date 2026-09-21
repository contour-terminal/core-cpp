# Task A6: fix round 1 (Ruling R42)

The review is `task-A6-review.md`, verdict "Needs fixes". Its Issues section has line numbers and fix details. Fix every item below.

1. **Important 1: file-wide platform guards.**
   - Delete the file-wide `#ifndef _WIN32` … `#endif` guards in `posix/FdPassing_test.cpp` and `posix/UnixSocket_test.cpp`, then run `python scripts/clang-format.py` to un-indent.
   - **Extension (R42):** drop every *file-wide* platform guard in `src/core/net/{posix,windows,linux,bsd}/` sources and headers. That includes `linux/EpollEventSource.{hpp,cpp}`'s `#ifdef __linux__` and `bsd/KqueueEventSource.*`'s kqueue-platform guards. CMake's per-platform lists already select these files, and a guard would silently compile a misfiled source to nothing.
   - Keep any `#if` that selects between variants *inside* one platform family, for example `#ifndef __APPLE__` in FdPassing_test. Name each one in its provenance note.
2. **Important 2: target rows carry their own DEPS.**
   - `core_cpp_module_target()` gets a `DEPS` list. It is authoritative for a target that has a row, and an absent DEPS means the target links no core-cpp target.
   - Each entry must be either a module in the parent row's DEPS or a target of the same module. Refuse anything else when the row is declared.
   - `net_types` gets no DEPS; `net_tls` gets `DEPS net`.
   - `core_cpp_check_layering()` checks each target against its own row, so a same-module link is no longer implicitly allowed.
   - Prove the refusal test-first with a configure-time scenario (e.g. in `check-platform-sources.cmake` or a new hygiene self-test):
     - a row whose target links an undeclared `core::<x>` must be refused by name;
     - `net_types → core::async` must be refused.
   - A7 needs this in place before `tui_output` exists.
3. **Minor 1:** split `testing/InMemoryTransport.cpp` into `testing/posix/InMemoryTransport.cpp` and `testing/windows/InMemoryTransport.cpp`, or `posix/` and `windows/` if a nested testing dir clashes with the layout. Keep the declaration in the public `testing/InMemoryTransport.hpp`.
   - Then `docs/modules/net.md` and the CMakeLists comment name the remaining exceptions: `DefaultEventSource.cpp` until B3, and `EventSourceParity_test.cpp`'s POSIX-only cases.
4. **Minor 2:** fix the provenance notes.
   - `posix/AcceptLoop.cpp` and `windows/WindowsListener.cpp` say "`PeerAddress.hpp` from `detail/`".
   - The `Tls.cpp` note adds "no NOLINT".
   - Each file changed by item 1 notes that its guard was dropped.
5. **Minor 3:** `.agent/guides/consumer-migration.md` gets a row: contour's `net` `Tls.hpp`/`Tls.cpp` → `core::net_tls`. Configure with `CORE_CPP_WITH_TLS ON` and link `core::net_tls`.
6. **Minor 4:** correct the report sentence about `UnixSocket_test.cpp`'s `TempDir`.

**Verification:**
- WSL `clang-debug`, `gcc-release`, `clang-tsan` and `clang-tidy`.
- Windows `clangcl-debug` and `cl-debug`.
- `python scripts/clang-format.py --check` and `mkdocs build --strict`.
- Push, watch the Build run to green, and dispatch `portability.yml` (FreeBSD) and watch it.
- Other sessions share WSL; only `out/build/<preset>` and your own processes are yours.

Append a "Fix round 1" section to `task-A6-report.md`, with RED/GREEN for item 2 and the commands and outputs. Then reply with the short status contract.
