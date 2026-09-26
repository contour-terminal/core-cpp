# Task v0.3.0: report

Branch `release/next`, from v0.2.1 (a6d49f2) to `1ae59fd Release 0.3.0` (untagged; the lead
fast-forwards master, tags and publishes).

## Finding -> commit

| Finding | Commits | RED evidence (test-first) |
|---|---|---|
| 1 Host-driven pump use-after-free | 8da125d | the pump-outlives-backend case failed before the PumpTicket liveness cell |
| 2 install()/export (core-cpp#5) | 88593cc, d820516, 8a815b4 | `core-cpp.install` (package, SameMinorVersion, nested consumer) failed before `CoreCppInstall.cmake` |
| 3 Displaced park hangs silently in Release | cc60d68, 06ee357 | watch-read-slot / watch-write-slot canaries ran on in Release; now `core::detail::fail` names direction and handle in every build |
| 4 Spawned roots leak through a nested frame | 4ac6c02, bc53586 | the nested-spawn case failed (root frame not released) before the fix |
| 5 Readiness completion resumes in the callback's position | a0b130b, e2d7399 | ordering case resumed at the back of the ready queue |
| Review round 1 (10 findings) | 1554710, 0e82021 | per-finding cases in EventLoop_test.cpp; old-deque allocation RED under gcc: 2==0, 3==0 |
| Review round 2 (3 findings) | 9a6db41, 9fdedd8, 1a82032 | wake-under-lock UAF (StallingWakeBackend), nested cancel |
| 6 Dialog helper gaps | fe4b642 | WerGetFlags check; abort message required in Debug |
| Review round 3 (1 MEDIUM, 3 LOW) | 31735d5 | throwing-callback teardown at fe4b642: `CHECK(cancelled)` failed (no ASan report: the table lives inside the loop object) |
| CI clang-tidy on 31735d5 | cf221dd | pinned clang-tidy 22.1.8 on EventLoop.cpp + tests: rc 0 |
| Release | 1ae59fd | - |

## Per-spawn cost of item 4

One extra coroutine-frame allocation per `spawn`: the loop-owned `SpawnedRoot` frame that wraps the
flow and hands itself to `_finishedRoots` on completion. The 10k-spawn case bounds nothing beyond
that; there is no per-completion allocation (CallbackAllocation_test: zero allocations on the
callback path).

## Gates

| Gate | Commit | Result |
|---|---|---|
| clang-format (pinned) | cf221dd | clean |
| clang-tidy, pinned, on the changed files | cf221dd | rc 0 |
| WSL clang-asan-ubsan | 31735d5 | 68/68 |
| WSL clang-tsan | 31735d5 | 68/68 |
| WSL clang-debug | cf221dd | 69/69 |
| WSL gcc-release | cf221dd | 69/69 |
| WSL emscripten | cf221dd | 43/43 |
| WSL clang-tidy preset (fresh) | cf221dd | TIDY_PENDING |
| Windows clangcl-debug, cl-debug, cl-release, clangcl-release (--clean-first) | cf221dd | 74/74 each |
| hygiene label (cl-debug, incl. core-cpp.install at 0.3.0) | 1ae59fd | 32/32 |
| mkdocs build --strict | 1ae59fd | rc 0 |
| CI build.yml 36058623097 | cf221dd | success (29 success, 1 skipped) |

The asan/tsan legs built 31735d5; cf221dd differs from it only in one lambda parameter
(`auto const& first`).

## Release commit

1ae59fd `Release 0.3.0`, the same four files as a6d49f2: `project(VERSION 0.3.0)`, CHANGELOG
`## [0.3.0] - 2026-09-24`, README and `docs/getting-started/cpm.md` `v0.3.0`. No fresh
`[Unreleased]`: the repo opens it in a separate commit after the release (e443af4). The other
`0.2.1` mentions are historical ("since 0.2.1") and stay.
