# Survey: which test cases vanish on a platform rather than reporting as skipped?

**131 cases across 23 files vanish silently**, in three classes. Class 2 is nearly twice class 1 and
nobody has been looking at it.

**The finding that reframes the ruling: a stated reason does not fix the silence.** 30 of the 46
guarded cases explain themselves in the source — and all 46 are equally invisible in a CI log,
because the reason lives in a file nobody opens while reading a run. The reason serves the person
editing the test; `SKIP` serves the person reading the report. B3 needed the second and no amount of
the first would have helped. Worth separating in the rule, because "document your guards" sounds
like the same instruction and is not.

## My predictions, stated before looking

| | predicted | actual | |
|---|---|---|---|
| files | 8–14 | **14** | correct, at the top |
| cases | 20–40 | **46** | **low by 6** |
| with a stated reason | 40–60% | **65%** | **low by 5 points** |

I was low on both counts. The project documents better than I assumed, and there are more guards
than I assumed.

## Scope: three classes, and why I drew the line there

The harm is **a silent difference between platforms in the same binary's counts**. That gives:

- **Class 1 — a preprocessor guard inside a compiled file.** The binary exists on both platforms;
  its count differs; nothing says why. **This is the defect the ruling names.**
- **Class 2 — a test source excluded by CMake** (`SOURCES_POSIX`/`SOURCES_WINDOWS`/
  `SOURCES_EMSCRIPTEN`). **Identical harm** — same binary, different count, no explanation — and
  *no guard in the file to hang a comment on*. In scope, and the remedy has to be different.
- **Class 3 — a whole binary absent.** Out of scope as a silence, because it shows up as a missing
  row in `ctest -N`, where a human comparing two platforms can see it. Listed for completeness.

You said you could argue it either way. The line I drew is *can a reader of the two runs see that
something is missing* — inside a binary, no; a missing binary, yes.

## Class 1 — 46 cases, 14 files, 16 with no stated reason

| guard | cases |
|---|---|
| `!defined(_WIN32)` | 32 |
| `CORE_CPP_TEST_THREADS` | 9 |
| `defined(_WIN32)` | 2 |
| `defined(__cpp_lib_ranges_iota)` / `..._fold` | 2 |
| `!defined(__EMSCRIPTEN__) \|\| defined(__EMSCRIPTEN_PTHREADS__)` | 1 |

**The 16 with no reason at all:**

| file | line | guard |
|---|---|---|
| `src/core/Environment_test.cpp` | 313, 334 | `!defined(_WIN32)` |
| `src/core/Ranges_test.cpp` | 202, 215 | `defined(__cpp_lib_ranges_iota)`, `..._fold` |
| `src/core/log/LogSink_test.cpp` | 416 | `CORE_CPP_TEST_THREADS` |
| `src/core/net/BackendParity_test.cpp` | 1043, 1211, 1249 | `!defined(_WIN32)` |
| `src/core/platform/Clock_test.cpp` | 217 | `CORE_CPP_TEST_THREADS` |
| `src/core/platform/FileSystem_test.cpp` | 288, 476, 553 | `!defined(_WIN32)` |
| `src/core/platform/PathUtils_test.cpp` | 93 | `defined(_WIN32)` |
| `src/core/platform/SignalHandler_test.cpp` | 88 | `!defined(_WIN32)` |
| `src/core/platform/SystemPipe_test.cpp` | 203, 264 | `!defined(_WIN32)`, `defined(_WIN32)` |

Several are self-evident from the case name (`isExecutableFile agrees with the real filesystem on
the execute bit`). That is an argument about the *file*, not about the run.

**B1's three are confirmed and they are not in the list above** — `async/Task_test.cpp:496` and
`async/WhenAny_test.cpp:423` and `:438`, all `!defined(_WIN32)`, all carrying the reason *"Exception
propagation through a coroutine frame crashes the Catch2 harness…"*. Three cases, exactly the
84 − 81 B1 measured. **Documented, and still silent** — which is the point above, in the one case
where somebody went looking.

**`CORE_CPP_TEST_THREADS` (9 cases) is worth its own look.** It is not a platform macro; it is set
by CMake, so these cases can vanish on a platform that *has* threads if the definition is not
plumbed. `StopToken_test.cpp` has 7, `LogSink_test.cpp` and `platform/Clock_test.cpp` one each.

## Class 2 — 85 cases, 11 files, no guard in the file at all

**`core-cpp.net`** — same binary name, different contents:

| excluded on | file | cases |
|---|---|---|
| Windows | `net/posix/UnixSocket_test.cpp` | 9 |
| Windows | `net/posix/FdPassing_test.cpp` | 7 |
| POSIX | `net/windows/NetworkEvents_test.cpp` | 2 |
| POSIX | `net/windows/WfmoBackend_test.cpp` | 1 |

**`core-cpp.platform` under Emscripten** — `SOURCES_EMSCRIPTEN` lists only the subset, so seven
files' worth vanish: `FileSystem_test` 30, `MessageQueue_test` 12, `SystemPipe_test` 9,
`UserPaths_test` 7, `Wakeup_test` 4, `SignalHandler_test` 3, `SystemInfo_test` 1 — **66 cases**.

`core-cpp.net_backend` sets `SOURCES_EMSCRIPTEN` to the same list as `SOURCES`, so it does **not**
differ. That is the pattern the others could follow where the file compiles everywhere.

**Why this class cannot be fixed the same way.** `SKIP` needs the file compiled, and
`posix/UnixSocket_test.cpp` will not compile on Windows. So the remedy is one of: compile a stub
translation unit on the other platform whose cases are a single `SKIP("POSIX only")`; or record an
expected per-platform count beside the registration. The first keeps the counts comparable, which is
what B3 wanted; the second is cheaper and only tells you the count is *expected* to differ.

## Class 3 — whole binaries, listed not flagged

- **`core-cpp.net`** is inside `if(NOT EMSCRIPTEN)` (`net/CMakeLists.txt:180-202`), with a comment
  saying why.
- **`core-cpp.net_tls`** is registered unconditionally at `net/CMakeLists.txt:206`, but
  `core::net_tls` is declared `WHEN CORE_CPP_WITH_TLS`, which **defaults OFF**
  (`cmake/CoreCppOptions.cmake:20`). Verified: `ctest -N` in this tree lists **no** `net_tls` row,
  and `CMakeCache.txt` has `CORE_CPP_WITH_TLS:BOOL=OFF`. So a default build silently has one fewer
  test binary than the CI leg that sets the option. Visible in `ctest -N`, so out of scope by my
  own line — but it is the one place where the *registration* reads as unconditional and is not.

## Method, and the bug my control caught

Enumerated from `git show HEAD:<path>` over `git ls-tree` — never from a build tree, for the reason
A12 nearly hit: this checkout's `task-c0-win` tree lists 27 tests and is missing everything added
since it was configured.

The probe walks each file keeping a stack of open preprocessor conditions and records every
`TEST_CASE`/`SCENARIO`/`TEMPLATE_TEST_CASE` declared with a non-empty stack. 90 test files, 1627
case macros scanned.

**The first version reported "no stated reason" for all 46, and it was wrong.** It looked in one
place — the lines above the `#if` — and in this repository the reason almost always sits *between*
the `#if` and the case. I caught it by hand-checking two files before believing the number, then
widened the detector to three positions and **controlled it by injecting a known reason into each
position in turn**. The third arm failed the control: `min(len(lines), case - 1)` excluded the line
immediately before the case, which is exactly where these reasons live. Fixed, all four control
cases pass (including "no reason anywhere" staying none).

Had I reported the first run, the headline would have been *"46 vanishing cases and not one explains
itself"* — wrong, and far more interesting than the truth, which is the direction that gets believed.

## What I did not check

Whether each guard is *sound* — I read them as claims, not as engineering. `defined(_WIN32)` on a
case about the execute bit is plainly right; `!defined(_WIN32)` on three `BackendParity` cases may
be a real portability gap or may be correct, and B3 owns that file. I also did not check the
worktree, only `HEAD`, so anything mid-flight in another lane is absent by construction.
