# Provenance

core-cpp replaces copies of the same code in four projects. Most of it was imported rather than
written here, and every imported file records where it came from, so a consumer migrating onto
core-cpp can compare what it had against what it gets, and so a fix upstream can be found and
carried over.

## What v0.1.0 is made of

Counted from the per-file table, one row per file under `src/core/`, `cmake/portable/` and
`cmake/FetchTransferBound.cmake`:

| Upstream | Commit | Files | What |
|---|---|---|---|
| [endo](https://github.com/contour-terminal/endo) | `f774a210ce989e5947b8f61d715068b1dc96088c` | 212 | the terminal UI (`core::tui`), the generic half of `src/platform` (`core::platform`), `Generator`, and test helpers |
| [contour](https://github.com/contour-terminal/contour) | `6777ff05014f8ff163b071e8b0e942830119db80` | 110 | crispy's generic half (`core::base`, `core::log`, `core::cli`), `src/coro` (`core::async`) and `src/net` (`core::net`) |
| [fastcached](https://github.com/LASTRADA-Software/fastcached) | `0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21` | 103 | the async and networking layer merged into `core::async` and `core::net`: `ParkedWork`, the executors, the event loop's lifetime rules, the socket contract, datagrams, the blocking transports, TLS |
| fastcached | `ee71f868547712892b7d9a2ebff60d49c496e25c` | 4 | `Profiling.hpp` and `Ranges.hpp`, with their tests |
| fastcached | `f6ec49f3446b8bc121eba82c64cde2de759e774a` | 2 | `cmake/portable/CompileCache.cmake` and `cmake/FetchTransferBound.cmake`, verbatim |
| fastcached | `5389e29a5eeca9c2319f43757bd7d6d0ac1c1a13` | 2 | `TuiRuntime.hpp` and its test, from fastcached's vendored endo, where the coroutine runtime was upstreamed |
| core-cpp | -- | 97 | written here: the merge's seams, the host-driven backend, the gates, and the tests that pin one design to another |

The `Imported` section of the [changelog](changelog.md) lists every import by commit, with what was
changed on the way in.

## The per-file record

The canonical record is
[`.agent/reference/provenance.md`](https://github.com/contour-terminal/core-cpp/blob/master/.agent/reference/provenance.md):
for each file, the upstream repository, the upstream path, the commit it was synced at, and what
differs from that upstream beyond the rewrites every import applies (namespace, include paths,
header guard, SPDX line, house style). Three things hold it true:

- **A file without a row is refused.** `core-cpp.cmake-hygiene` fails on a file in scope with no
  row, and on a row naming a file that no longer exists, so the table cannot fall behind the tree.
- **A row names ONE upstream file and a full SHA**, so `git log <sha>.. -- <path>` can answer
  whether upstream has moved since.
- **Drift is reported by a tool, not by reading.** `python scripts/check-upstream-drift.py` fetches
  each upstream and reports, per row, the commits that touched the file since its synced SHA. It is
  `core-cpp.upstream-drift` under `ctest -L hygiene`, and skips (exit 77) on a machine without the
  upstream checkouts rather than passing -- which is every CI runner, so on a push only its
  self-test runs. The question itself is answered nightly, by the `upstream-drift` job of
  `.github/workflows/downstream.yml`, which checks the three upstreams out beside core-cpp and runs
  the checker against them: drift is listed in the job summary, and a malformed row or an unread
  upstream fails it (core-cpp#33). Run for v0.1.0 against fetched upstreams: 433 rows with an
  upstream, 0 drifted.

**A row is not a licence to re-sync by overwriting.** A `-` in its notes means nothing is left to say
beyond the rewrites every import applies, not that the file is byte-identical to upstream; the
record's own preamble lists what those rewrites were.
