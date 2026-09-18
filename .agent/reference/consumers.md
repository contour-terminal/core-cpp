# Consumers

Who consumes core-cpp, how, and where each keeps its pin. Grep these before changing a public
signature; a pull request that changes public API says what each of them must change.

The target state is the design spec's,
[Part I §7](https://github.com/contour-terminal/core-cpp/blob/master/docs/superpowers/specs/2026-09-18-core-cpp-design.md).
**No consumer has migrated yet**: until its pull request merges (Phase C), each still carries
the copy in the "Today" column. The "Expected modules" column is the spec's list for contour and
Lightweight, and for the others is inferred from the copies they carry today; each migration
pull request settles its row.

| Consumer | Default branch | Mechanism | Pin lives in | Expected modules | Today |
|---|---|---|---|---|---|
| [contour](https://github.com/contour-terminal/contour) | `master` | verbatim copy in `vendor/core-cpp`, checked against its `MANIFEST` | the vendored `MANIFEST` header (`# ref`, `# commit`) | `base`, `log`, `cli`, `platform`, `async`, `net`, `testing` | `src/coro`, `src/net`, `src/crispy` (the canonical copies) |
| [endo](https://github.com/contour-terminal/endo) | `master` | CPM | its `CPMAddPackage(NAME core-cpp ...)` | `base`, `log`, `cli`, `platform`, `async`, `net`, `tui`, `testing` | fetches contour's `src/{crispy,vtparser,coro,net}` at configure time; own `src/{platform,tui,testing}` |
| [fastcached](https://github.com/LASTRADA-Software/fastcached) | `master` | CPM | its `CPMAddPackage(NAME core-cpp ...)` | `base`, `platform`, `async`, `net`, `net_tls`, `tui`, `testing` | `vendor/endo` (a verbatim import of endo's TUI and platform and contour's coro), plus its own async and networking layers |
| [tuidu](https://github.com/contour-terminal/tuidu) | `master` | CPM | its `CPMAddPackage(NAME core-cpp ...)` | `base`, `platform`, `async`, `tui`, `testing` | `src/{coro,platform,testing,tui}`, a June snapshot of endo's |
| [Lightweight](https://github.com/LASTRADA-Software/Lightweight) `dbtool` | `master` | CPM, only under `LIGHTWEIGHT_BUILD_TOOLS` | its tools' `CPMAddPackage(NAME core-cpp ...)` | `tui_output` | hand-written ANSI output in `dbtool` |
| [morph](https://github.com/LASTRADA-Software/morph) | `master` | CPM, replacing FetchContent | its `CPMAddPackage(NAME core-cpp ...)` | `base`, `async`, `net` (the WebAssembly subset in its browser builds) | its own timeout scheduler, base64 and wakeup pipe |

## Consequences for core-cpp

- **contour builds offline from its vendored copy** (`CORE_CPP_FETCH_DEPS OFF`), so a dependency
  added without a `find_package` path breaks contour's distribution packagers.
- **morph builds for the browser**, so the WebAssembly subset must stay single-thread safe and
  inside libc++ 17, the one emsdk 3.1.56 ships.
- **fastcached, endo and tuidu include `cmake/portable/CompileCache.cmake` themselves**, the same
  verbatim file core-cpp carries; as a subproject core-cpp sets no launcher and its targets use
  the parent's.
- **Lightweight links only `core::tui_output`**, so that leaf must never gain a dependency on
  libunicode, coroutines or the event loop.
