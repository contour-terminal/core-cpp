# fastcached parity on core-cpp v0.4.0 (impl-perf, 2026-09-26 00:19-00:25 WSL time)

Setup:
- Candidate: fastcached#1598 9452d2a8, pinned to core-cpp v0.4.0 (e3647c0).
- Base: core-cpp-bench-base at 6eaea2cb, fastcached's own reactor.
- Build and method: both built fresh, Release, clang-22, cache off. The method is unchanged from b0c82a4: LD_PRELOAD shim, C loadgen, base and candidate interleaved, 4 s windows, median of 5.
- Run 1 is clean, with the 1-minute load at 2.0-3.5. Run 2 was contended (load 10.9) and is not used, though its direction matched.

Per request, base / candidate. User CPU in µs is the median with the [min-max] range; total is user+sys in µs; malloc is allocations per request.

| Row | user base | user cand | overlap | total base / cand | malloc base / cand |
|---|---|---|---|---|---|
| mc c1 | 6.06 [5.95-7.04] | 6.14 [5.80-6.86] | yes | 41.25 / 40.23 | 21.17 / 6.000 |
| mc c16 | 5.55 [5.20-5.69] | 6.13 [6.04-6.33] | NO | 23.18 / 23.27 | 13.07 / 6.000 |
| mc c64 | 5.31 [5.14-5.70] | 5.93 [5.82-6.26] | NO | 21.12 / 21.21 | 11.37 / 6.000 |
| redis c1 | 8.55 [8.14-9.37] | 8.60 [8.02-9.00] | yes | 43.55 / 43.28 | 37.45 / 22.000 |
| redis c16 | 8.08 [6.94-8.28] | 8.76 [8.44-9.63] | NO | 26.26 / 26.00 | 28.72 / 22.000 |
| redis c64 | 6.48 [6.33-7.72] | 7.91 [7.15-8.37] | yes | 23.06 / 23.43 | 26.95 / 22.000 |

Syscalls:
- The candidate makes no zero-timeout epoll_wait calls (base 1.05-6.22) and no epoll_ctl calls (base 2.00).
- recv, send and EAGAIN counts are identical.

Profile, mc c16: core-cpp is 8.21% of samples against the reactor's 3.97%. At b0c82a4 it was 7.56% against 3.59%.

Verdict against the b0c82a4 bar:
- Syscalls, malloc and total CPU are at parity or better. Total CPU is within ±1.6% on every row.
- User CPU misses the overlap bar on 3 of 6 rows (mc c16, mc c64, redis c16): about +0.6 µs user time, balanced by less sys time.
- This is not full parity on the bar as set; the remaining user-CPU gap is core-cpp#52.
