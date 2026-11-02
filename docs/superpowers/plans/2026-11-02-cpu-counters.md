# CPU Work Counters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A portable, unprivileged `core::platform` API that reports the calling process's retired
instructions / cycles / reference cycles over a region, released in core-cpp 0.12.0 and shown as
columns in Contour's `bench-headless`.

**Architecture:** One portable header (`CpuCounters.hpp`) defines the measure enum, the sample type
and the `ICpuCounterSource` seam; exactly one per-OS source file implements
`makeDefaultCpuCounterSource()` (macOS `proc_pid_rusage`, Linux `perf_event_open`, Windows
`QueryProcessCycleTime`, everything else "unavailable"), chosen by a single `if/elseif` the way
`core::net` picks its default backend. Contour re-vendors the release and samples the source around
each measured run.

**Tech Stack:** C++23, CMake (core-cpp module system), Catch2, Linux perf_event, Darwin libproc,
Win32.

**Spec:** `docs/superpowers/specs/2026-11-02-cpu-counters-design.md` (core-cpp worktree
`/Users/christianparpart/projects/core-cpp-cpu-counters`, branch `feature/cpu-counters`).

## Global Constraints

- core-cpp rules (`AGENT.md`, `.agent/rules/*.md`) are binding: C++23, no C-style `for`, no
  `NOLINT`/diagnostic pragmas, `enum class` over `bool`, Doxygen `///` on public API, SPDX line 1,
  `#pragma once`, zero warnings, clang-tidy clean (`concurrency-*` is on: no `strerror`; use
  `std::generic_category().message(e)` / `std::system_category().message(e)` as `LogSink.cpp` does).
- Arrays indexed by an enum carry the order `static_assert` the core-cpp rules require.
- OS differences live in per-OS source files; no `#ifdef` in shared logic; no `<Windows.h>` in
  public headers.
- Every new file gets an `origin: core-cpp` provenance row (hygiene test).
- Unprivileged only. Unavailable is a reported state: never 0, never mislabelled, never throws.
- Measures: `Instructions`, `Cycles`, `ReferenceCycles`. Windows reports only `ReferenceCycles`;
  macOS and Linux never report `ReferenceCycles`.
- Scope: whole process. Linux: `inherit=1`, `inherit_thread=1` (≥ 5.13; older kernels fall back to
  `inherit=1` and the source records a note), `exclude_kernel=1`, `exclude_hv=1`, pid 0, cpu −1,
  `PERF_FLAG_FD_CLOEXEC`, separate descriptors (no `PERF_FORMAT_GROUP`).
- Linux hybrid: one event per core PMU, summed. Multiplexed means Σ`timeRunning` < min
  `timeEnabled` − 1 % tolerance → measure absent for that sample, never scaled.
- macOS: probe at creation — after burning ~5 ms of CPU, an instruction delta of 0 means
  unavailable, reason `"counters not populated (virtualized)"`.
- Release version: core-cpp **0.12.0**. Commits `-s`, no AI attribution trailers.

## Review Focus

1. No usable counters (CI VM, container, `perf_event_paranoid` 3): the bench prints the reason
   once and no columns; nothing reports 0. → Task 2 (unavailable source), Task 8 (formatting).
2. macOS VM where `ri_instructions` stays 0: unavailable, not 0. → Task 3 (probe test).
3. Linux hybrid: per-PMU running < enabled is normal and must still sum; true multiplexing drops
   the measure. → Task 4 (`combinePmuReadings` tests).
4. A delta where a measure is missing at either end or went backwards: omitted, not wrapped. →
   Task 2 (`operator-` tests).
5. Linux hybrid with some PMU events failing to open: the measure is unavailable with a reason (a
   partial sum would undercount). → Task 4 (`decideMeasure` test).

---

## Part A — core-cpp (`/Users/christianparpart/projects/core-cpp-cpu-counters`)

### Task 1: One default-source file per platform

**Files:**
- Modify: `src/core/platform/CMakeLists.txt`
- Modify: `.agent/rules/platform.md` (the wasm-subset sentence: `CpuCounters.cpp` joins the subset)

**Interfaces:**
- Produces: CMake variable `_coreCppPlatformCpuCounterSource`, set by one `if/elseif/else` modelled
  on `src/core/net/CMakeLists.txt:79-107` (Ruling R40): `APPLE` → `darwin/DarwinCpuCounters.cpp`,
  `LINUX` → `linux/LinuxCpuCounters.cpp`, `WIN32` → `windows/WindowsCpuCounters.cpp`, `else()`
  (Emscripten, other BSDs, any other POSIX) → `UnavailableCpuCounters.cpp`. The variable is appended
  to `SOURCES` (and to the Emscripten list for the `else()` case).

- [ ] **Step 1:** Add the `if/elseif` block pointing every branch at `UnavailableCpuCounters.cpp`
  for now (Tasks 3–5 each switch their branch to the real file). No source-table rows are added.
- [ ] **Step 2:** Configure + build on this Mac → succeeds (the file is created in Task 2; do Task 1
  and Task 2 in one commit if the build needs the file to exist).

### Task 2: Portable API, unavailable source, fake

**Files:**
- Create: `src/core/platform/CpuCounters.hpp`, `CpuCounters.cpp`, `UnavailableCpuCounters.cpp`,
  `detail/UnavailableCpuCounterSource.hpp`, `testing/FakeCpuCounterSource.hpp`,
  `CpuCounters_test.cpp`
- Modify: `src/core/platform/CMakeLists.txt` (`CpuCounters.cpp` into `_coreCppPlatformSubset` only —
  it is already spliced into `SOURCES`; test into `_coreCppPlatformSubsetTests`; headers into
  `HEADERS`), provenance, `CHANGELOG.md` `[Unreleased]` → `### Added`, `docs/modules/platform.md`,
  `.agent/rules/design-principles.md` seam table (CPU work counters | `core::platform::ICpuCounterSource`
  | `testing::FakeCpuCounterSource`)

**Interfaces:**
- Produces (namespace `core::platform`):
  ```cpp
  enum class CpuCounter : std::uint8_t { Instructions, Cycles, ReferenceCycles };
  inline constexpr std::array AllCpuCounters { CpuCounter::Instructions, CpuCounter::Cycles,
                                               CpuCounter::ReferenceCycles };
  // + the order static_assert: AllCpuCounters[i] == CpuCounter(i)
  [[nodiscard]] constexpr std::string_view toString(CpuCounter counter) noexcept; // "instructions", "cycles", "reference cycles"

  struct CpuCounterSample {
      [[nodiscard]] std::optional<std::uint64_t> get(CpuCounter counter) const noexcept;
      void set(CpuCounter counter, std::uint64_t value) noexcept;
      std::array<std::optional<std::uint64_t>, AllCpuCounters.size()> values {};
      auto operator<=>(CpuCounterSample const&) const = default; // Contour's ThroughputRun defaults <=>
  };
  /// Delta; a measure is present only if present in both and end >= start.
  [[nodiscard]] CpuCounterSample operator-(CpuCounterSample const& end, CpuCounterSample const& start) noexcept;

  class ICpuCounterSource {
  public:
      virtual ~ICpuCounterSource() = default;
      [[nodiscard]] virtual CpuCounterSample read() = 0;
      /// Empty when the measure is supported. The view lives as long as the source.
      [[nodiscard]] virtual std::string_view unavailableReason(CpuCounter counter) const noexcept = 0;
      /// Notes about what is counted (e.g. "child processes are counted (kernel < 5.13)"); may be empty.
      [[nodiscard]] virtual std::string_view note() const noexcept = 0;
      /// Predicate-named, derived — cannot disagree with the reason.
      [[nodiscard]] bool supports(CpuCounter counter) const noexcept { return unavailableReason(counter).empty(); }
  };
  /// Never null, never throws.
  [[nodiscard]] std::unique_ptr<ICpuCounterSource> makeDefaultCpuCounterSource();

  template <std::invocable Work>
  [[nodiscard]] CpuCounterSample measure(ICpuCounterSource& source, Work&& work);
  ```
- `detail::UnavailableCpuCounterSource(std::string reason)` — every reason is `reason`, `read()`
  empty, `note()` empty. Every backend uses it for its "nothing works" path.
- `testing::FakeCpuCounterSource(std::vector<CpuCounterSample> script, std::vector<CpuCounter> supported)`
  — `read()` returns the script in order (last repeated); unsupported reason `"fake: unsupported"`.

- [ ] **Step 1: Failing tests** (`CpuCounters_test.cpp`, tag `[platform][cpucounters]`):
  - `"CpuCounterSample delta keeps measures present in both"`: start {I=100, C=50}, end {I=350,
    C=80} → I=250, C=30, R absent.
  - `"CpuCounterSample delta drops a measure missing at either end"`: start {I=100}, end {C=5} → all
    absent.
  - `"CpuCounterSample delta drops a measure that went backwards"`: start {I=500}, end {I=400} → I
    absent.
  - `"measure() returns the delta around the work and runs it once"`: fake [{I=10},{I=70}] → I=60,
    work ran once.
  - `"an unavailable source supports nothing and reports its reason"`: every `supports()` false,
    `read()` empty, reasons `"no counters here"`.
  - `"toString() names every measure"`.
- [ ] **Step 2: Run, expect FAIL** (no compile): platform test binary with `[cpucounters]`.
- [ ] **Step 3: Implement**; `UnavailableCpuCounters.cpp` defines the factory returning
  `UnavailableCpuCounterSource("CPU counters are not supported on this platform")`.
- [ ] **Step 4: Run, expect PASS**; full platform suite; hygiene/provenance test.
- [ ] **Step 5: Commit** (with Task 1) — `platform: add CpuCounters, a portable CPU work-counter seam`.

### Task 3: macOS backend

**Files:**
- Create: `src/core/platform/darwin/DarwinCpuCounters.cpp`, `detail/CpuCounterProbe.hpp`,
  `CpuCountersNative_test.cpp` (registered for every platform except Emscripten)
- Modify: `src/core/platform/CMakeLists.txt` (`APPLE` branch → the darwin file), provenance

**Interfaces:**
- Produces: `enum class CounterProbe : std::uint8_t { NotPopulated, Populated };`
  `[[nodiscard]] constexpr CounterProbe probeCounters(std::uint64_t instructionDelta) noexcept` —
  `Populated` iff `instructionDelta > 0` (unit-free; `ri_user_time` is Mach ticks on Apple Silicon,
  so CPU time is not used in the decision).

- [ ] **Step 1: Failing tests.**
  - `CpuCounters_test.cpp`: `"the probe calls a zero instruction delta unpopulated"`: 0 →
    `NotPopulated`; 12'000'000 → `Populated`.
  - `CpuCountersNative_test.cpp`: `"supported measures grow with the work"` — for each supported
    measure, best of 3 `measure()` runs of a loop of N and of 2N iterations (N = 20'000'000; the
    loop writes a `volatile std::uint64_t` sink every iteration, on every compiler); require every
    delta > 0, Instructions ratio ≥ 1.8, Cycles/ReferenceCycles ratio ≥ 1.3. If nothing is
    supported: `SKIP(source->unavailableReason(CpuCounter::Instructions))`.
- [ ] **Step 2: Run, expect FAIL** (probe test does not compile).
- [ ] **Step 3: Implement** a source reading `proc_pid_rusage(getpid(), RUSAGE_INFO_V4, …)` →
  `ri_instructions`, `ri_cycles`; at construction burn ~5 ms of CPU and apply `probeCounters` to the
  instruction delta; `NotPopulated` → `UnavailableCpuCounterSource("counters not populated (virtualized)")`;
  syscall failure → unavailable with `std::generic_category().message(errno)`. ReferenceCycles
  reason `"not provided on macOS"`.
- [ ] **Step 4: Run with `-s "[cpucounters]"`** on this Mac and confirm the native case *ran*
  (assertions listed, not skipped); full suite.
- [ ] **Step 5: Commit** — `platform: count instructions and cycles on macOS`.

### Task 4: Linux backend

**Files:**
- Create: `src/core/platform/linux/LinuxCpuCounters.cpp`, `detail/LinuxPerfPlan.hpp`,
  `detail/LinuxPerfPlan.cpp`, `LinuxPerfPlan_test.cpp`
- Modify: `src/core/platform/CMakeLists.txt` (`LINUX` branch → linux file; `LinuxPerfPlan.cpp` and
  its test in `SOURCES` / test `SOURCES` but **not** in the wasm subset — `InMemoryFileSystem` is not
  there), provenance

**Interfaces:**
- Consumes: Task 2 API; `core::platform::FileSystem`, `testing::InMemoryFileSystem`.
- Produces (namespace `core::platform::detail`):
  ```cpp
  struct CorePmu { std::string name; std::uint32_t type; };
  /// Under <sysRoot>/bus/event_source/devices: "cpu" if present, else every PMU directory that has a "cpus" file
  /// (how perf identifies core PMUs: cpu_core/cpu_atom, armv8_*, apple_*_pmu, …); each reads <dir>/type.
  [[nodiscard]] std::vector<CorePmu> discoverCorePmus(FileSystem const& fs, std::filesystem::path const& sysRoot);
  /// With one PMU: the plain event (no extended type — pre-6.6 arm64 rejects it). With several: event | (type << 32).
  [[nodiscard]] std::uint64_t hardwareConfig(std::uint64_t event, CorePmu const& pmu, std::size_t pmuCount) noexcept;
  struct PmuReading { std::uint64_t value; std::uint64_t timeEnabled; std::uint64_t timeRunning; };
  /// Sum of values; nullopt if readings is empty or Σ timeRunning < min timeEnabled * 0.99.
  [[nodiscard]] std::optional<std::uint64_t> combinePmuReadings(std::span<PmuReading const> readings) noexcept;
  struct OpenOutcome { int error; };                 // 0 = opened
  /// Supported only if every per-PMU open succeeded; else the first failure's reason.
  [[nodiscard]] std::string decideMeasure(std::span<OpenOutcome const> outcomes, std::optional<int> paranoidLevel);
  /// Table-driven: EACCES/EPERM → "perf_event_open: permission denied (perf_event_paranoid=<n>)";
  /// ENOENT/ENODEV/EOPNOTSUPP → "no hardware PMU exposed (virtual machine?)"; ENOSYS → "perf_event_open blocked (seccomp?)";
  /// other → "perf_event_open: " + std::generic_category().message(error).
  [[nodiscard]] std::string perfOpenFailureReason(int error, std::optional<int> paranoidLevel);
  ```
  (`decideMeasure` returns an empty string for "supported".)

- [ ] **Step 1: Failing tests** (`LinuxPerfPlan_test.cpp`, `[cpucounters]`):
  - `discoverCorePmus`: `cpu/type=4` → {cpu,4}; `cpu_core/{type=4,cpus}`, `cpu_atom/{type=10,cpus}`,
    `power/type=12` → those two; `armv8_pmuv3_0/{type=8,cpus}`, `apple_icestorm_pmu/{type=9,cpus}` →
    both; nothing → empty.
  - `hardwareConfig(1, {cpu_atom,10}, 2) == (1 | (10ull << 32))`; `hardwareConfig(1, {armv8_pmuv3_0,8}, 1) == 1`.
  - `combinePmuReadings`: {{100,10,10},{50,10,10}} → 150;
    hybrid {{100,10,6},{50,10,4}} → 150; multiplexed {{100,10,3},{50,10,4}} → nullopt; {} → nullopt.
  - `decideMeasure`: {0,0} → ""; {0,EACCES} with paranoid 3 → contains "perf_event_paranoid=3".
  - `perfOpenFailureReason`: ENOENT → contains "no hardware PMU"; ENOSYS → contains "seccomp".
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** the pure functions, then the source: discover PMUs (none found → one
  plain `PERF_TYPE_HARDWARE` event), open per PMU per measure (`PERF_COUNT_HW_INSTRUCTIONS`,
  `PERF_COUNT_HW_CPU_CYCLES`) with the Global-Constraint attrs and `read_format =
  TOTAL_TIME_ENABLED | TOTAL_TIME_RUNNING`; on `EINVAL` with `inherit_thread`, retry without and set
  `note()` to `"child processes are counted (kernel < 5.13)"`; per measure `decideMeasure` (paranoid
  level read via `FileSystem` from `/proc/sys/kernel/perf_event_paranoid`). RAII fds. `read()` uses
  `combinePmuReadings`. Errors via `std::generic_category()`.
- [ ] **Step 4: Run** the pure tests locally; on Linux CI the native test (expect SKIP on most
  runners; run with `-s` and record the reason). On a bare-metal Linux box with
  `perf_event_paranoid` ≤ 2, expect the native case to run and pass.
- [ ] **Step 5: Commit** — `platform: count instructions and cycles on Linux via perf_event`.

### Task 5: Windows backend

**Files:**
- Create: `src/core/platform/windows/WindowsCpuCounters.cpp`, `windows/CpuCounters_test.cpp`
- Modify: `src/core/platform/CMakeLists.txt` (`WIN32` branch → windows file; the test in
  `SOURCES_WINDOWS` of `core_cpp_add_test`), provenance

- [ ] **Step 1: Failing test** `windows/CpuCounters_test.cpp`: `"Windows reports reference cycles"`
  — `REQUIRE(makeDefaultCpuCounterSource()->supports(CpuCounter::ReferenceCycles))` and
  `!supports(Instructions)`, `!supports(Cycles)` (so a silent skip cannot hide a broken backend).
- [ ] **Step 2: Implement** a source supporting only `ReferenceCycles` via
  `QueryProcessCycleTime(GetCurrentProcess(), &value)`; failure → unavailable with
  `std::system_category().message(GetLastError())`; Instructions/Cycles reason
  `"not available to unprivileged processes on Windows"`.
- [ ] **Step 3: Push; CI Windows x64 and arm64** → both tests pass.
- [ ] **Step 4: Commit** — `platform: report reference cycles on Windows`.

### Task 6: Release core-cpp 0.12.0

- [ ] **Step 1:** Confirm every `if/elseif` branch now names its real file and `else()` names
  `UnavailableCpuCounters.cpp`.
- [ ] **Step 2:** `/simplify`, then `/code-review high origin/master..feature/cpu-counters`; address
  findings.
- [ ] **Step 3:** PR; wait (backgrounded) for every CI job green.
- [ ] **Step 4:** Merge (by the owner, or with explicit permission), `/draft-release` 0.12.0 per
  `.agent/guides/releasing.md`, `/publish-release` once the vendor tarball and `SHA256SUMS` are
  attached.

## Part B — Contour (new worktree `../contour-cpu-counters` from `origin/master`)

### Task 7: Re-vendor core-cpp 0.12.0

- [ ] **Step 1:** From the worktree root, with the core-cpp checkout at tag v0.12.0:
  `cmake -DMODE=sync -DREF=v0.12.0 -DREPO=https://github.com/contour-terminal/core-cpp -DDEST=vendor/core-cpp "-DMODULES=base;log;cli;platform;async;net;testing" -P /Users/christianparpart/projects/core-cpp/cmake/CoreCppVendor.cmake`
- [ ] **Step 2:** `cmake -DMODE=check -DDEST=vendor/core-cpp -P vendor/core-cpp/cmake/CoreCppVendor.cmake`
  → clean; build; `ctest -L lint` → PASS; full suite → PASS.
- [ ] **Step 3: Commit** — `vendor: update core-cpp to 0.12.0`.

### Task 8: bench-headless counter columns

**Files:**
- Create: `src/vtbackend/testing/CounterColumns.hpp`, `src/vtbackend/testing/CounterColumns_test.cpp`
- Modify: `src/vtbackend/CMakeLists.txt` (header into `vtbackend_HEADERS`, test source),
  `src/vtbackend/bench-headless.cpp`

**Interfaces:**
- Consumes: `core::platform::{CpuCounter, CpuCounterSample, ICpuCounterSource, makeDefaultCpuCounterSource, measure, toString}`,
  `core::platform::testing::FakeCpuCounterSource`.
- Produces (namespace `vtbackend::testing`):
  ```cpp
  /// For each supported measure, in AllCpuCounters order: std::format(" {:>7} {}", scaled, unit) where scaled is
  /// value/divisor with SI prefix (k/M/G) and two decimals, e.g. "  1.23 Ginstr"; a supported measure absent from the
  /// sample prints std::format(" {:>7} {}", "—", unit). Units: "instr", "cyc", "refcyc".
  [[nodiscard]] std::string formatCounterColumns(core::platform::CpuCounterSample const& delta,
                                                 core::platform::ICpuCounterSource const& source,
                                                 std::uint64_t divisor);
  /// "CPU counters unavailable: <reason>" when no measure is supported; empty otherwise.
  [[nodiscard]] std::string formatCounterAvailability(core::platform::ICpuCounterSource const& source);
  ```

- [ ] **Step 1: Failing tests** (`CounterColumns_test.cpp`): fake supporting Instructions+Cycles,
  delta {I=1'230'000'000, C=450'000'000}, divisor 1 → `"   1.23 Ginstr  450.00 Mcyc"`; divisor 1000 →
  `"   1.23 Minstr  450.00 kcyc"`; I absent → `"       — instr  450.00 Mcyc"`; fake supporting nothing →
  columns `""`, availability `"CPU counters unavailable: fake: unsupported"`; fake supporting one →
  availability `""`.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** `CounterColumns.hpp`, then wire `bench-headless.cpp`:
  - Inject the source through `ContourHeadlessBench`'s constructor; `main` creates it first, before
    any thread. Keep `ThroughputOptions` a literal type (constexpr at ~2304) — the source travels as
    a separate `ICpuCounterSource&` parameter, not inside the options.
  - `ThroughputRun` (~1825) gains `std::optional<core::platform::CpuCounterSample> counters;` (its
    defaulted `<=>` keeps working with Task 2's `<=>`); `measureFromMemory` (~1840) fills it with
    `measure()` around `runWorkloadOnce` (~1857); PTY runs leave it `nullopt` → no columns.
  - Throughput row (~2154-2166): append `formatCounterColumns(*run.counters, source, 1)` when set.
  - Snapshot (~2253): `measureSnapshot` returns `{duration, counters}`; `measure()` around the
    frame loop; columns per frame (divisor = frame count) after the first (`us/frame`) measurement of
    each row only.
  - Print `formatCounterAvailability(source)` once under the throughput and snapshot headers when
    non-empty, and `source.note()` when non-empty.
- [ ] **Step 4: Run** the tests → PASS; release build with `-DLIBTERMINAL_BUILD_BENCH_HEADLESS=ON`;
  `bench-headless throughput size 8 runs 1 kinds utf8 screens primary` on this Mac shows Ginstr and
  Gcyc; the instruction figure is within ~10 % of `/usr/bin/time -l` minus startup for the same
  invocation.
- [ ] **Step 5: Commit** — `bench-headless: report CPU instructions and cycles per workload`.

### Task 9: Documentation, review, PR

**Files:** `docs/internals/profiling.md` (bench-headless section), `AGENT.md` (Workflow: the
Callgrind line).

- [ ] **Step 1:** In `profiling.md`, document the counter columns with the platform matrix
  (macOS/Linux instructions+cycles, Windows reference cycles, VMs/CI usually unavailable, Linux
  needs `perf_event_paranoid` ≤ 2) and the A/B recipe; Callgrind stays the per-function tool.
- [ ] **Step 2:** In `AGENT.md`, extend the perf line: "or compare `bench-headless`'s instruction
  counts before/after (portable; see docs/internals/profiling.md)".
- [ ] **Step 3: Commit** — `docs: measure instruction counts with bench-headless on every OS`.
- [ ] **Step 4:** `/simplify`, `/code-review high` over the branch, PR (label `no changelog` if the
  changelog check requires it for a dev-tool change), one backgrounded CI wait.
