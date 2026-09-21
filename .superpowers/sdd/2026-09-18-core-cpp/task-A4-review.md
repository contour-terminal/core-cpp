# Task A4 review: `core::platform`, `core::coro` (Generator), `core::testing` helpers

Range `6dda244..48aae7b` (3 commits: `fe95520`, `c7b11d0`, `48aae7b`). Read-only review; no suite
re-run. Upstream blobs were extracted with `git -c core.autocrlf=false -c core.eol=lf show` into the
scratchpad and diffed file by file against the tree at `48aae7b`.

## Spec Compliance

| Requirement | Result |
|---|---|
| Sources are blobs at the pins (endo `f774a210`, contour `6777ff05`, fastcached `b461e8b6`); no CR bytes | ✅ endo and contour `origin/master` are still the pins. fastcached `origin/master` has since moved to `2c63b440`, but `git log b461e8b6..origin/master` touches no Clock file, so there is no delta. No CR in any upstream blob read or in any committed file (`git grep -lI $'\r'` finds nothing). |
| Scope: generic files only; Process, Pipe, WaitResult, ProcessProvider*, ProjectFileTree, InstallPaths, InterruptThrottle stay in endo | ✅ All absent. None of the imported files includes them. |
| Namespace map; no `namespace endo { using … }` aliases | ✅ Every alias block is gone. `grep -rn "namespace endo\|endo::" src/core` finds nothing. Namespace equals directory everywhere (`core::platform`, `core::platform::testing`, `core::coro`, `core::testing`). |
| Import fidelity (checked all ~55 imported files, not just 5 or 6) | ✅ Every difference is one of: the namespace or include map; a NOLINT, loop or tidy fix (`contains`, braced return, `{}` initialisers, `erase_if`, `pthread_sigmask`, the GlobMatch helper split, which is semantically identical: `matched != negate`); or a stated design change (Types, SystemPipe, Generator, environment access, `.recase-`, `strerror`). The split of `WindowsPlatform_test` keeps all 18 upstream cases. Every other test file keeps or grows its case count. |
| One merged Clock (`now()`, virtual no-op `refresh()`, CachedClock, ManualClock `advance`/`setNow`, IWallClock/SystemWallClock/ManualWallClock/WallClockRef, Steady aliases, both defaults) | ✅ Line-for-line faithful to fastcached's semantics: initial sample in the constructor, a CAS that only moves forward, relaxed ordering, the LOAD-BEARING deleted copy/move with its comment, and `WallClockRef` implicit from `const&` and deleted from `const&&`. endo's and contour's `IClock`/`ManualClock`/`defaultSteadyClock` are identical to it apart from wording. |
| Three Clock tests deduped into one, keeping every distinct behaviour | ⚠️ Every CachedClock behaviour is kept: construct, hold, never-backwards, and the bounded concurrent convergence with the same 8×20 000 shape. So are the refresh no-op, the four WallClockRef rows and the borrow test. The retainer/forwarder rows (`FleetHistory`, `SchedulerService`, `CacheEngine`) went with fastcached's types and have no generic stand-in (Minor 1). |
| SystemPipe returns `PlatformError` | ✅ |
| `coro` INTERFACE with Generator; `platform` `wasm-subset` with Types, (NativeHandle), PlatformError, Clock, StringUtils, PathUtils, GlobMatch, FileUri and their tests under node | ✅ `SOURCES_EMSCRIPTEN` holds FileUri/GlobMatch/PathUtils (the rest is header-only), and the tests list is tagged. There is no separate NativeHandle file (it lives in `Types.hpp`, as in endo), and the report and docs say so. |
| R3 no NOLINT / no muting pragma / no `-Wno-` | ✅ `grep -rn NOLINT src` finds nothing. The only pragma in `src/` is A1's `SuppressWindowsDialogsAtStartup.cpp:21`, which predates this task. The diff adds no `-Wno-*` or `/wd`. |
| R4 C-style loops converted | ✅ None are left. I checked the `while` conversions for `continue` (there is none), so the increment always runs. |
| Provenance row for every file | ✅ All 69 added `src/` files have a row, with the full SHA and a change note. |
| consumer-migration rename rows, CHANGELOG, NOTICE, docs pages | ✅ fastcached Clock renames, `net::` clock/handle/pipe rows, the SystemPipe behaviour change, Types' dropped macros, UserPaths, Generator and EnvHelper are all covered. ⚠️ Nothing covers how endo reaches the now-private native providers (Minor 2). |
| Commits and trailers | ✅ The two mandated subjects are present, plus the `base:` writer commit that precedes them. Each ends with the `Signed-off-by` trailer. |
| CI `35356388834` | ✅ The head SHA is `48aae7b` and all 20 jobs plus `ci-ok` passed. `core-cpp.platform`, `core-cpp.coro` and `core-cpp.testing` passed under node on both emsdk 3.1.56 and latest, and on appleclang, llvm-22, gcc-14, the Windows legs, TSan and ASan/UBSan. |

### The implementer's flagged scope additions

- **Process-environment writer (`fe95520`).** Correct, necessary and well-built.
  - It is necessary because R3 and `concurrency-mt-unsafe` rule out `setenv()`, and `Environment.hpp` already forbade it for first-party code.
  - It publishes under the same mutex `LiveEnvironment` reads under. A `deque<string>` and a `deque<vector<char*>>` keep every entry and block address stable. The holder is deliberately immortal, so a late `getenv()` or an `atexit` reader cannot touch freed memory.
  - It copies only pointers from the current block. glibc and Apple libc never free the strings their `setenv` creates, and third-party `setenv`/`unsetenv` edit our block in place, which is memory-safe because we never free it.
  - The one store to `environ` is formally a race with an unsynchronised `getenv()`, exactly as `setenv()` is, and the header is honest about that.
  - The tests cover set/replace/empty/unset/idempotent unset, invalid names, and a held block staying intact. Writing them found and fixed a real Windows bug: an empty value read as unset.
  - The remaining nits are in Minor 6.
- **SystemPipe with contour's non-blocking behaviour.** Correct and necessary. endo's own callers are only TUI tests; endo's `HttpServer` and contour's `EventLoop::post()` already use contour's non-blocking copy. So taking endo's blocking behaviour would have regressed A6. contour's `WindowsLoopback::makeLoopbackPair` is byte-identical to the one inlined here. Three new cases pin non-blocking plus close-on-exec, the full-channel write and the empty read. The one information loss is in Minor 5.
- **Types.hpp without `<windows.h>`.** Correct and rule-driven (`platform.md`). The spelled types are held equal by `static_assert`s in `windows/WindowsTypes.cpp`, and `Types_test` fails if the header brings `<windows.h>` back. The dropped `STDIN_FILENO`/`SIG*` fallbacks were used only by endo's process code, which stays in endo; endo's `src/tui` uses them only in POSIX-only TUs. A migration row covers the change.
- **UserPaths `core::Environment` overloads.** Correct. The tests move from mutating the process to `FakeEnvironment`. The zero-argument overloads are untested (Minor 3).

## Strengths

- Fidelity is high and every deviation is written down: in the provenance notes, the commit message, CHANGELOG `Fixed`, and a migration row.
- The Clock merge keeps fastcached's contract text, including #1028 and #1446, and generalises the fastcached-specific wording without weakening it.
- The concurrent `CachedClock` case is re-expressed without fastcached's `BoundedWait`. It keeps the bounded start gate, uses a joiner and a gate-opener whose destruction order is correct, and is compiled out only where threads do not exist.
- Generator's choice is now include-order-independent, which fixes a latent ODR hazard on MSVC. Every case runs over both `Generator` and the fallback.
- The NOLINT removal in `SignalHandler` (file-scope state) and the loop conversions are mechanical and verifiably equivalent.
- The WebAssembly subset is honest. Its one measured deviation (PIPEFS reports EAGAIN, not EOF, after the writer closes) is compiled out with a reason, not papered over.

## Issues

### Critical

None.

### Important

None.

### Minor

1. **`src/core/platform/Clock_test.cpp:33-48`: the WallClockRef retainer rows were dropped with nothing generic in their place.**
   - fastcached's `WallClockRef_test.cpp` calls these its "rows worth reading". They show that a type which takes `WallClockRef` by value refuses an rvalue clock, including as one of several arguments.
   - `docs/modules/platform.md:60` states that property ("the refusal survives a forwarding constructor"), but core-cpp no longer tests it.
   - Fix: add a test-local `struct Retainer { explicit Retainer(WallClockRef); }` and a multi-argument variant, with the paired `is_constructible` rows.
2. **The native providers are private, and nothing tells endo how to reach them.**
   - `docs/modules/platform.md:40` says "a composition root picks one per platform". But `posix/PosixEnvironmentProvider.hpp`, `windows/WindowsEnvironmentProvider.hpp`, `linux/LinuxFileInfoProvider.hpp` and `windows/WindowsFileInfoProvider.hpp` are outside every FILE_SET, as the spec requires.
   - endo's composition roots include them directly: `Shell.cpp:115,126`, `Prompt.cpp:25,27` and `Registration.cpp:30-36`. They will build only through the `BUILD_INTERFACE` `src/` include path.
   - `.agent/guides/consumer-migration.md` has no row for this. Add a row, or file an issue for a public `nativeEnvironmentProvider()`/`nativeFileInfoProvider()` factory, before C1.
3. **`src/core/platform/UserPaths.hpp:28-34,54-60`: the new zero-argument `homeDirectory()`/`configHome()`, which read `LiveEnvironment`, have no test.** A one-line check against the `core::Environment const&` overload given a `LiveEnvironment` would pin the delegation.
4. **Leftover endo wording.**
   - `src/core/platform/PathUtils.hpp:184` still says "Endo accepts POSIX device paths".
   - `SignalHandler.hpp:14,114`, `FileSystem.hpp:35`, `PathUtils.hpp:27` and `ScopedTempDir.hpp:107-110` speak of "the shell".
   - `6ebb78d` cleaned the contour equivalent; this import leaves endo's.
5. **`src/core/platform/SystemPipe.cpp:108-114`: an empty-channel read (EAGAIN) and a real failure both become `PlatformError::IoError`.**
   - contour's `NetError` carried the errno. Now a caller cannot tell a spurious wakeup from a broken channel.
   - No caller cares today: contour's `EventLoop` ignores the result.
   - A `WouldBlock` enumerator, or a documented "IoError includes would-block" note in the header's `read()` contract, would make this explicit.
6. **Writer nits, all in `src/core/Environment.cpp`.**
   - `publishEnvironment()` (`:114-136`) has no short-circuit when the entry already reads `name=value`. So a repeated identical `export` still costs a fresh block and entry that are never freed. Comparing before publishing is cheap.
   - The `environmentMutex()` comment at `:37` still says it serialises "reads … against one another". It now serialises the writer too.
   - The header does not say the writer is not for use between `fork()` and `exec()`: it takes a mutex and allocates.
7. **`src/core/platform/Types.hpp:119,153`: under Emscripten, a drained pipe whose writer has closed reads -1 (EAGAIN), not 0.** The test is compiled out for that reason (`Types_test.cpp:53-55`), but neither the `platformRead` doc nor the "Under Emscripten" section of `docs/modules/platform.md` says the EOF contract does not hold in the subset.
8. **The fixture throws have no recorded exception to the exceptions rule.** `src/core/testing/ScopedTempDir.hpp:80` throws `std::runtime_error`, and `ScopedWorkingDirectory` can throw through the `std::filesystem` calls. That is sensible for a Catch2 fixture and documented in `docs/modules/testing.md:96`. But `.agent/rules/cpp-guidelines.md:73` says no exception type but `OperationCancelled`, and neither the rulebook nor an issue records the fixture exception. (This is separate from the Wakeup constructor, which is ruled R29/#14.)
9. **`task-A4-report.md:220` still reads `CI_PLACEHOLDER`.** The run is green; the report should say so.

## Assessment

**Task quality: Approved.**

- The import is faithful and complete. The Clock merge preserves every fastcached semantic, and the gap it leaves is a test of a property that holds by construction.
- All four flagged scope additions are correct, necessary and well-built.
- R3, R4, namespace, provenance, migration, CHANGELOG and docs are all satisfied.
- CI is green on every leg, and the WebAssembly subset tests run under node on both emsdk versions.
- The Minor items can go in a follow-up. Items 1 and 2 are worth doing before C1 and A6.
