# Parity: fastcached on core-cpp vs fastcached's own reactor (impl-perf, 2026-09-24)

Recorded by the team lead from impl-perf's report, since the lane could not write this file.
The base is fastcached origin/master 6eaea2cb on its own reactor. "before" is fastcached on core-cpp
a55cede. "after" is fastcached on core-cpp at the parity tip, which is the code in v0.2.0 (ec47681).

## Method

- The daemon runs under an LD_PRELOAD shim that counts syscalls. A C load generator drives it in
  4 s windows; each figure is the median of 5, base and candidate interleaved, divided per request.
- Heap allocations are counted with a malloc hook. User and sys CPU come from the server's rusage.
- Every candidate was built with USE_COMPILER_CACHE=OFF, because fastcache-cc served stale objects
  for changed core-cpp headers (fastcached#1597).
- These counts are deterministic and do not depend on machine load. On this WSL host the
  wall-clock getbench spread is 25-60% per scenario, so it cannot resolve a gap of a few percent.

## memcached-text GET, 16 connections (per request; base / before / after)

| Mechanism | base | before | after |
|---|---:|---:|---:|
| epoll_wait, blocking | 0.99 | 0.98 | 0.99 |
| epoll_wait, zero timeout | 1.95 | 0 | 0 |
| epoll_ctl (MOD; ADD and DEL are 0 on every side) | 2.00 | 0 | 0 |
| recv ok + recv EAGAIN + send | 1 + 1.0 + 1 | same | same |
| malloc | 12.9 | 10.1 | 7.1 |
| user CPU us | 5.73 | 6.64 | 6.20 |
| sys CPU us | 18.01 | 17.41 | 17.20 |
| total CPU us | 23.74 | 24.05 | 23.39 |

## redis GET, 16 connections

- zero-timeout epoll_wait: 1.80 / 0 / 0
- epoll_ctl MOD: 2 / 0 / 0
- malloc: 28.6 / 26.1 / 23.1
- user CPU us: 7.78 / 10.09 / 8.28
- total CPU us: 25.74 / 26.91 / 25.03

## 64 connections, total CPU us

- memcached: 21.97 / 22.67 / 21.64
- redis: 24.42 / 25.23 / 24.11

## getbench (interleaved, 5 rounds)

- A/A, base vs base: geomean +0.4%.
- A/B, base vs after: geomean -0.5%.
- Every c1 and c16 row falls inside the range of the A/A rows (-1.9% to +0.8%). The c64 and c256
  rows swing by up to 16% even in the A/A, so they cannot resolve anything.

## Result

Per request, core-cpp makes fewer syscalls, allocates less and spends no more total CPU than
fastcached's reactor. User CPU stays 0.3-1.2 us higher, spread over about six small costs, and the
lower sys time more than offsets it. The biggest single item is tracked as core-cpp#47.
