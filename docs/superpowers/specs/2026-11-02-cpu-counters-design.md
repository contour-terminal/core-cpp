# CPU work counters — design

Date: 2026-11-02. Status: draft for approval.

## Intent

Contour checks performance-sensitive changes with Callgrind's instruction counts (`Ir`), because
instruction counts are far more stable than wall time. Valgrind does not run on Apple Silicon macOS
or on Windows, so on those machines there is no comparable measurement today. The goal is a small,
portable core-cpp API that reads the CPU's own hardware counters for the calling program, so any
benchmark (first consumer: Contour's `bench-headless`) can report *instructions* — or the best
available substitute — next to its timings, on every platform, without root and without an
external tool.

Success: `bench-headless throughput` on macOS, Linux and Windows prints a per-workload work count,
and a before/after A/B of a parser change shows a stable, comparable delta.

## Goals

- One portable API in `core::platform` to sample hardware work counters for the calling process,
  and to compute the delta over a measured region.
- Honest capability reporting: the API says *which* measures it can deliver on this machine
  (instructions, cycles, or none), never a fake number.
- Unprivileged only. No root, no admin, no kernel extensions, no private Apple frameworks.
- Injectable (dependency injection) so consumers test their reporting with a fake.

## Non-goals

- Per-function attribution (Callgrind's `callgrind_annotate`, Instruments, perf record). This is a
  region counter, not a profiler.
- Comparability *across* platforms. The numbers are for before/after comparisons on one machine.
- Cache-miss, branch-miss or other PMU events. Instructions and cycles only; the shape must allow
  adding more later by adding a row, not by changing signatures.

## Anti-goals (failure even if it "works")

- Reporting cycles labelled as instructions, or zero where the measure is unavailable.
- An API that fails, throws, or aborts on a machine without counters (CI VMs, containers,
  `perf_event_paranoid` ≥ 3). Unavailable is a normal, reported state.
- `#ifdef` chains in shared logic (core-cpp `platform.md`: OS differences are injected
  implementations in per-OS source files).

## Platform matrix (decided)

| Platform | Instructions | Cycles | Mechanism | Scope |
|---|---|---|---|---|
| macOS (Apple) | yes | yes | `proc_pid_rusage(getpid(), RUSAGE_INFO_V4)` → `ri_instructions`, `ri_cycles` | whole process, user + kernel |
| Linux | when permitted | when permitted | `perf_event_open` (`PERF_COUNT_HW_INSTRUCTIONS`, `PERF_COUNT_HW_CPU_CYCLES`), `exclude_kernel=1`, `exclude_hv=1`, `inherit=1`, pid 0 / cpu −1 | calling thread + threads it creates after the source is opened; user space only |
| Windows | no | no — `ReferenceCycles` instead (see owner decision 2) | `QueryProcessCycleTime` | whole process |
| Other BSDs, Emscripten | no | no | — | — |

Linux "when permitted": works unprivileged with `perf_event_paranoid` ≤ 2 (kernel default) on a
machine whose PMU is exposed. Fails on Debian/Ubuntu defaults of 3/4, and on most CI VMs → reported
as unavailable, with the reason.

Windows instructions: no unprivileged per-process instruction counter is known (hardware-counter
thread profiling needs an administrator to configure the PMCs). Cycles is the Windows measure. If
the builder finds an unprivileged instruction source, it slots in as a new row.

## API shape (decided in outline; exact spelling is the builder's)

In `core::platform`, header `CpuCounters.hpp`:

- `enum class CpuCounter : uint8_t { Instructions, Cycles, ReferenceCycles }` — the measures, as a table the
  backends and printers iterate (data-driven: adding a measure is adding an enumerator plus a
  backend row).
- `struct CpuCounterSample` — one optional `uint64_t` per measure (absent = unavailable), with
  `operator-` giving a delta sample (a measure present in both operands stays present).
- `class ICpuCounterSource` — interface: `[[nodiscard]] CpuCounterSample read()` and
  `[[nodiscard]] bool supports(CpuCounter) const` (a predicate-named bool return is fine per the
  guidelines), plus a human-readable `unavailableReason()` for logs.
- `[[nodiscard]] std::unique_ptr<ICpuCounterSource> makeDefaultCpuCounterSource()` — returns the
  platform's source. Never fails: if nothing is available it returns a source whose `supports()`
  is false for everything and whose reason says why (e.g. "perf_event_open: permission denied
  (perf_event_paranoid=3)"). Opening the Linux file descriptors happens here (RAII; closed in the
  destructor).
- A small helper to measure a callable: `measure(source, fn)` → `CpuCounterSample` delta.
- A test double `core::testing::FakeCpuCounterSource` returning scripted samples.

## Constraints

- core-cpp rules apply: C++23, `std::expected`/`enum class` per the guidelines, Doxygen on public
  API, zero warnings, clang-tidy clean on all CI jobs, no `<Windows.h>` in public headers,
  SPDX header, namespace = directory.
- Per-OS code lives in per-OS sources selected by `cmake/CoreCppTargets.cmake`
  (`SOURCES_LINUX`, `SOURCES_WINDOWS`, …). macOS needs Apple-only code but the table's selector is
  `APPLE OR BSD`; the builder adds an Apple-only selector (`darwin/` is already named in the
  platform rules) rather than an `#ifdef __APPLE__` in a BSD file, and non-Apple BSDs get the
  "unavailable" source.
- Must build on every CI job: Linux x86-64/arm64 (clang, gcc), macOS, Windows x64/arm64,
  Emscripten subset.
- Reading a sample must be cheap enough to wrap a single benchmark run (one syscall per read is
  fine; no per-instruction overhead).

## Tests

- Portable unit tests on `CpuCounterSample` arithmetic and the fake.
- A platform test against `makeDefaultCpuCounterSource()`: for every supported measure, a delta
  over a known busy loop is > 0 and grows with the loop length (2× loop → noticeably more);
  where nothing is supported, the test `SKIP`s with the reported reason. CI VMs will mostly skip
  the Linux instruction path; that is expected and documented.
- A test that the unavailable source never reports a value.

## Release and adoption (decided)

1. core-cpp: implement on `feature/cpu-counters`, PR, merge; CHANGELOG `### Added` entry; release
   **0.12.0** (new public API → minor) via the documented `/draft-release` → `/publish-release`
   flow.
2. Contour: re-vendor 0.12.0 with the documented `CoreCppVendor.cmake` sync command (module list
   unchanged — `platform` is already vendored), as one commit.
3. Contour `bench-headless`: sample the default source around each measured run
   (`runWorkloadOnce`), keep the counters of the fastest run, and append one column per supported
   measure to the throughput and snapshot rows (e.g. `  1.23 Ginstr` / `  2.04 Gcycles`); print the
   unavailable reason once in the header when nothing is supported.
4. Contour docs: `docs/internals/latency.md` and AGENT.md's workflow line on Callgrind gain the
   portable alternative (`bench-headless` counters, with the platform matrix above). Callgrind
   stays the per-function tool on Linux.

## Builder-check findings folded in

Verified facts and settled details (from the builder check; macOS measured on an M1 Max):

- **macOS in VMs reads zero.** XNU's arm64 config leaves `config_pervasive_cpi` out under
  `SOC_IS_VIRTUALIZED`, so `ri_instructions`/`ri_cycles` stay 0 — including on GitHub's
  `macos-15` runners. The macOS source probes at creation (burn some CPU; CPU time advances but
  the counters do not → unavailable, reason "counters not populated (virtualized)"). Same probe on
  Intel Macs. On real Apple Silicon: exactly 4 instructions per loop iteration, kernel work
  included, about 6K instructions per read.
- **Linux scope.** `inherit=1` also follows forked children across `exec`; set
  `inherit_thread=1` (Linux ≥ 5.13) to limit it to threads, and open with
  `PERF_FLAG_FD_CLOEXEC`. On older kernels without `inherit_thread`, fall back to `inherit=1`
  and document that child processes are counted.
- **Linux group reads.** Linux 4.12 rejects `inherit` + `PERF_FORMAT_GROUP`; from 4.13 it works.
  Try one group (leader + member, identical `inherit` settings), and fall back to two separate
  descriptors.
- **Unavailable cases on Linux** reported with their reason: `perf_event_paranoid` ≥ 3, no PMU
  (most CI VMs), and Docker's default seccomp profile, which blocks `perf_event_open`.
- **Windows:** no unprivileged instruction source exists — hardware-counter thread profiling needs
  a kernel driver to configure the counters. `QueryProcessCycleTime` counts constant-rate TSC
  ticks of CPU time, not core cycles.
- **Reasons are per measure:** instructions may open while cycles fail.
- **The fake** is `core::platform::testing::FakeCpuCounterSource` in
  `src/core/platform/testing/` (`core::testing` may link only base and log), with a row in the
  seam table of `.agent/rules/design-principles.md`.
- **Source selection:** add an Apple-only `SOURCES_DARWIN` row and a non-Apple BSD row to
  `cmake/CoreCppTargets.cmake`, with scenarios in `tests/cmake/check-platform-sources.cmake`
  (`SOURCES_BSD` also compiles on macOS, so it cannot hold the factory). The Emscripten
  "unavailable" source goes in `SOURCES_EMSCRIPTEN` and the test in the wasm subset list.
- **Bookkeeping:** an `origin: core-cpp` provenance row per new file, and a row in
  `docs/modules/platform.md`.
- **bench-headless:** open the source in `main` before any worker thread starts. Counters go on
  the in-memory throughput rows (around `runWorkloadOnce`) and on snapshot rows (per frame,
  around `runSnapshotFrames`); real-PTY rows leave the column blank.
- **Platform test thresholds:** a 2× longer loop must give ≥ 1.8× instructions and ≥ 1.3×
  cycles; keep the loop from being optimised away with an asm barrier.

## Owner decisions (2026-11-02)

1. **Scope: the whole process, everywhere.** The source is meant to be opened before the program
   starts worker threads (bench-headless: first thing in `main`). macOS is process-wide by nature;
   Linux uses `inherit=1` + `inherit_thread=1` from the opening thread; Windows uses
   `QueryProcessCycleTime`. The header documents that on Linux threads started *before* the
   source was opened are not counted.
2. **Windows reports `ReferenceCycles`, never `Cycles`.** A third measure,
   `CpuCounter::ReferenceCycles` — CPU time in constant-rate timer ticks — so a reader never
   mistakes it for core cycles or instructions. Windows supports only this measure; macOS and
   Linux do not report it. (Platform matrix row: Windows — Instructions no, Cycles no,
   ReferenceCycles yes.)
3. **Hybrid CPUs on Linux: count per core type and sum.** Enumerate the core PMUs
   (`/sys/bus/event_source/devices/cpu_core`, `cpu_atom`, or arm64 per-cluster PMUs, each with its
   `type`), open one event per PMU with `config | (type << 32)` (`PERF_TYPE_HARDWARE` extended
   type), and sum. On a non-hybrid machine this is one event. Read `TOTAL_TIME_ENABLED` /
   `TOTAL_TIME_RUNNING`; if any event was multiplexed (running < enabled), report that measure as
   unavailable for the sample rather than scaling it.

## Left to the builder

- Exact names, the header split, whether `measure()` is a free function or a member.
- Whether Linux opens instructions and cycles as one perf event group (preferred: one read) or two.
- Number formatting in `bench-headless`.
