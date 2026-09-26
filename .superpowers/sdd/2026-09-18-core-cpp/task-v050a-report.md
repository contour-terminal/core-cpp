# Task v0.5.0 lane A report (impl-v040)

Branch `next-050-a` in `D:/core-cpp-wt-v040`, local only (not pushed). It starts at 87e42ed (#53).
The tip is 79ab16c.

## Per issue

| Issue | Outcome | Commit |
|---|---|---|
| #53 TaskKind | fixed | 87e42ed |
| #26 NativeFileSystem narrows paths through the code page | fixed | 42aa08a |
| #27 InMemoryFileSystem divergences | fixed | c157379 |
| #28 SystemPipe sockets inheritable on Windows | fixed | bfebc0c |
| #29 hand-spelled stop-token probe | fixed | 5999958 |
| #6 remove WfmoBackend / BackendKind::Wfmo | fixed (Breaking) | 3238edc |
| #46 WindowsSocket answers BadHandle | obsolete, closed by #6 | 3238edc |
| #50 WFMO residuals | obsolete, closed by #6 | 3238edc |
| #7 Environment unification | fixed (Breaking) | f8dacbd |
| #35 HttpServer refusal destroyed by the close's RST | fixed | ec1f59c |
| (tidy findings on #26 and #35) | fixed | 8fbd033 |
| (Windows clang-tidy leg findings in lane A's tests) | fixed | f5ee1e6 |
| #15 deep synchronous co_await chains | decided: documented limit (option 3, lead ruling) | 79ab16c |
| #47 per-park HandleWatch lookup | closed without code (lead ruling) | - |

### Evidence for each issue

- **#26.** RED on cl-debug. A path the ANSI code page cannot spell made the error path itself throw
  "No mapping for the Unicode character exists". All 21 message sites now spell the path with
  `u8string()`.
- **#27.** RED was 5 failed assertions. The two differences the issue names are fixed: the symlink
  is followed for the execute and permission bits, and `createDirectory` answers "File exists".
  What the fake still does not model is tabled in `docs/modules/platform.md`.
- **#28.** RED: the pipe's read and write handles were inheritable, checked with
  `GetHandleInformation`. The fix makes the socket with `WSA_FLAG_NO_HANDLE_INHERIT` and clears
  inheritance on the accepted socket. #6 applies the same fix to `connectUnix`.
- **#29.** Eleven sites were swept, four of them in lane B's `tui/runtime/TuiRuntime.hpp`, which
  was notified. The new tree-level hygiene rule `hand-spelled-stop-token-probe` has a self-test
  sample. Its RED listed exactly the 11 sites.
- **#6.** RED: a new IoBackend_test case asserts that `BackendKind` names exactly poll, epoll,
  kqueue, iocp, host-driven, scripted and null.
  - **Removed:** `WfmoBackend`, `detail/WaitChunking.hpp`, and the readiness transport that only
    WFMO drove, `WindowsSocket` and `WindowsListener`. The transport had to go in the same commit:
    `WindowsSocket_test` needs a WFMO loop, and nothing else produced these sockets.
  - **Factories:** the Windows factories (`listen`, `listenUnix`, `adoptListener`, `adoptSocket`,
    and the dial through `adoptDialled`) now refuse a loop without a completion port with
    `Unsupported`. `adoptSocket` closes the handle it was given, and `adoptListener` leaves it with
    the caller, as each one documents.
  - **New test:** a Windows case covers each of those refusals.
  - **Tests moved off real sockets:** `ConnectFlow_test` and `SocketDeadline_test` put a real
    socket on a `TestLoop`, which only the readiness transport allowed. They now use the in-memory
    fake.
  - **renames.json:** the three include rows for removed headers became removed-symbol rows.
- **#46.** Obsolete. `WindowsSocket` no longer exists, and `ClosedParkIdle_test` now asserts
  `Cancelled` on every backend, where it could only assert "resolved" before.
- **#50.** Obsolete. Both items were in WFMO-only code, `WaitHandleAwaiter` on a WFMO park and
  `WindowsSocket::cancelRead`, and neither type exists now.
  `CloseResumesThroughLoop_test` no longer skips wfmo.
- **#7.** RED on cl-debug: 9 failed assertions in the new `platform/windows/WideEnvironment_test`.
  The failures were a value outside the code page read back as `?`, UTF-8 written as mojibake,
  `keys()` narrowing U+03A9 to 0xA9, and non-UTF-8 text accepted.
  - **Shape, as the issue specifies:**
    - `core::Environment` is unchanged.
    - `core::platform::ProcessEnvironment : core::Environment`, where `set`, `unset`,
      `exportVariable` and `setAndExport` return `std::expected<void, PlatformError>`.
      `PlatformError::InvalidArgument` is a new enumerator, placed last.
    - `core::platform::WorkingDirectory` is a new seam, with `nativeWorkingDirectory()` and
      `testing::TestWorkingDirectory`.
    - `userName()` joins `homeDirectory()` and `configHome()` as free functions in `UserPaths.hpp`.
    - `testing::TestProcessEnvironment` is the double for both seams.
    - The Windows implementations use `GetEnvironmentVariableW` and `SetEnvironmentVariableW`
      through UTF-8.
  - **One judgement call:** `core::testing::FakeEnvironment` stays. It lives in `core::testing`,
    which links base only and cannot see platform. It is still the read-only double for base-only
    code, and contour uses it. "One double" is met in the sense the issue's problem statement
    needs: a mixed test hands the same `TestProcessEnvironment` to readers and writers.
  - **Also in the commit:** the provenance rows were renamed, the `platform.md` "Open work" entry
    was removed, and `renames.json` now points endo's rows at the new names, with a migration note.
- **#35.** RED on cl-debug: a new `HttpServer_test` case read "HTTP/1.1 413" and then a recv error.
  After the fix it reads EOF.
  - **Port:** `closeLingering`, `LingerBounds`, `LingerEnd` and `LingerOutcome` were ported from
    fastcached 0708dd54 with all three bounds. `serve` calls it on the 413 and 400 paths, bounded
    by the new `HttpLimits::linger` (2 s, 64 KiB, 4 reads, fastcached's connection bounds).
    `serve` has no loop, so each read carries its share of the bound through `setReceiveDeadline`.
  - **Upstream cases:** all seven are ported, including the loopback control that shows the bare
    close's reset.
  - **One test differs from upstream:** core-cpp has no `BlockingListener`, so in the blocking-socket
    case the dialled `BlockingSocket` is the side that lingers, against a silent peer accepted on a
    loop.
- **#15.** Parked. The trampoline fix, candidate 1, would add a completion flag and an atomic
  exchange to every `Task` `co_await`, which is fastcached's hot path. It also rewrites `Task`'s
  transfer next to the ownership machinery. It bounds only synchronous chains.
  - **The limit is already documented:** `docs/modules/async.md`, lines 77 to 100, and the case
    SKIPs where it would overflow.
  - **Ruling needed:** either accept candidate 3, the documented limit, as the decision and close
    the issue, or accept the hot-path cost. Candidate 2 breaks the no-PUBLIC-flag rule.
- **#47.** Recommend closing without code.
  - **What was measured:** during #52, replacing the `_watches` `unordered_map` with a flat map made
    no measurable difference: 17.3 to 18.1 ns/op against 17.3 to 20.7 ns/op.
  - **Limits of that measurement:** it was the container swap, not the watch hint the issue
    proposes, so it bounds the lookup's cost rather than measuring the hint.
  - **Cost of the hint:** a public `ParkEntry` field that six consumers must check.
  - **Recommendation:** revisit only if a profile names `watchHandle` again.

## Consumer impact

Consumers were grepped in the contour, endo (including `D:/endo-worktrees/core-cpp`, the migration
branch), fastcached, tuidu, Lightweight and morph trees and their worktrees.

- **#6:** none. No consumer names `BackendKind::Wfmo`, `WfmoBackend`, `WindowsSocket`,
  `WindowsListener` or `WaitChunking`. The only hits are each project's own pre-migration copies.
  No consumer puts a real socket on a `TestLoop`, `NullBackend` or `ScriptedBackend` loop on
  Windows. That is the only pattern the new refusal breaks: core-cpp's own `ConnectFlow_test` and
  `SocketDeadline_test` did it, and now use the in-memory fake.
- **#7:**
  - **contour:** nothing to change. It uses only `core::Environment` and `FakeEnvironment`, both
    unchanged. The Windows reads are now correct outside the code page.
  - **endo** (migration branch `D:/endo-worktrees/core-cpp` at a08c4250): 34 files name
    `EnvironmentProvider`. What each must change:
    - rename to `ProcessEnvironment` and `<core/platform/ProcessEnvironment.hpp>`, and
      `nativeEnvironmentProvider()` to `nativeProcessEnvironment()` (`shell/Shell.cpp`,
      `shell/ui/Prompt.cpp`);
    - rename `TestEnvironmentProvider` to `TestProcessEnvironment`, and move the initial directory
      and `addValidPath` onto a `TestWorkingDirectory` (`endo-test/TestExecutor.cpp`,
      `shell/completion/Completer_test.cpp`, `WhichCompletion_test.cpp`,
      `shell/DirectoryConfig_test.cpp`, `shell/Shell_test.cpp`, `shell/testing/InjectedShell.hpp`,
      `shell/ui/PromptComponent_test.cpp`, `shell/util/CommandResolver_test.cpp`);
    - pass a `WorkingDirectory&` in place of `changeDirectory` and `currentDirectory`:
      `changeDirectory` in `shell/builtins/Environment.cpp` and `DirectoryConfig_test.cpp`;
      `currentDirectory` in 8 files (`DirectoryConfigBuiltins.cpp`, `builtins/Environment.cpp`,
      `InlineCommands.cpp`, `HistoryCompleter.cpp`, `DirectoryConfig.cpp`,
      `history/RequiredPaths.hpp`, `Shell.cpp`, `ui/PromptComponent.cpp`);
    - rewrite the member calls `env.homeDirectory()`, `env.userName()` and `env.configHome()` as
      `homeDirectory(env)`, `userName(env)` and `configHome(env)`. The calls are in 11, 1 and 6
      files; some of those are already the free function.
    - handle or ignore the `std::expected` from every `set`, `unset`, `exportVariable` and
      `setAndExport`. There are about 80 `set` calls, and `[[nodiscard]]` will flag each one.
  - **tuidu, fastcached, Lightweight's dbtool, morph:** none. None of them uses either interface.
- **#35:** additive. `HttpLimits` gains a defaulted field, so aggregate initialisation still
  compiles. contour is the `HttpServer` user: its refusals now linger for up to 2 s, 64 KiB and 4
  reads before the close.
- **#26, #27, #28, #29, #53:** as reported with each commit. #29 touched lane B's `TuiRuntime.hpp`.

## Gates

The full set ran on ec1f59c. The pinned clang-tidy then found two issues, which 8fbd033 fixes: two
test and constant lines. On 8fbd033, tidy, clang-debug and cl-debug were run again.

**Windows**, `--clean-first`, `-LE tree-level`, each tree deleted afterwards:

| Preset | Built on | Steps | Tests |
|---|---|---|---|
| cl-debug | ec1f59c, and again on 8fbd033 | 644/644, then 652/652 | 49/49 |
| clangcl-release | ec1f59c | 652/652 | 49/49 |
| cl-release | ec1f59c | 652/652 | 49/49 |
| clangcl-debug | ec1f59c | 652/652 | 49/49 |

**WSL**, each tree deleted afterwards:

| Preset | Built on | Steps | Result |
|---|---|---|---|
| clang-debug, every label | ec1f59c, and again on 8fbd033 | 659/659 | 72/72 (text-encoding skipped, as it always is) |
| clang-tidy, pinned 22.1.8, fresh tree | 8fbd033 | 659/659 | clean; on ec1f59c it found the two findings above |
| clang-asan-ubsan | ec1f59c | | 41/41 |
| clang-tsan | ec1f59c | | 41/41 |
| gcc-release | ec1f59c | | 44/44 |

**Tree-level:**

- The pinned format check: 570 files clean on 8fbd033.
- `mkdocs build --strict`: clean on 8fbd033.
- The tree-level hygiene checks, among them provenance, cmake-hygiene and migrate-renames: they
  run in clang-debug's every-label pass and passed there.

**Not run:**

- **The pinned clang-tidy on Windows-only sources**, such as `windows/SocketsWin32.cpp`,
  `windows/WindowsProcessEnvironment.cpp` and `windows/WideEnvironment_test.cpp`. The Linux tidy
  tree does not compile them, and the brief's list is the WSL tidy.
- **emscripten**: it is not in the brief's list. #7 adds `posix/PosixWorkingDirectory.cpp` to the
  wasm subset, and it uses only `chdir` and `std::filesystem::current_path`.

**Disk:** C: had 5.15 GB free at the end, and no build tree of this lane is left. ~/bld-v040 holds
no trees.

## Windows clang-tidy leg (f5ee1e6)

Lane B's new `windows (clang-tidy)` job found problems in this lane's tests. They are fixed without
NOLINT:

- **`operator delete` parameter names, against vcruntime_new.h's `_Block`:** the replaced global
  allocation functions of the three counting tests moved into one translation unit,
  `src/core/testing/ReplacedGlobalAllocation.cpp`. It includes only `<cstddef>`, which was checked
  not to pull in `vcruntime_new.h`, and forwards to `replacedAllocate` and `replacedRelease`, which
  each test defines.
- **`StrandAllocation_test:171`, MismatchedDeallocator:** the same move fixes it.
- **`Strand_test:258`, use of a moved-from object:** the `ResumeTarget` is held behind a
  `unique_ptr`. The comment says why: reading it after the move is the point of the case.
- **This lane's own findings on that leg:**
  - the environment conversions take owned strings (suspicious-stringview-data-usage);
  - `IocpSocket_test` compares with `InvalidSocketValue` (integer sign comparison).

Results:

- The pinned clang-tidy over a clangcl-release compile database, as the job builds it, analysed 270
  sources, 41 of them under `windows/`. Only `StreamSocketOptions` and `UnixListener_test:140`
  remain, and lane B's branch already fixes both.
- In a clangcl-debug tree only, tidy also reports performance-unnecessary-value-param in
  `Ranges.hpp` and `EventLoop.cpp:690`. The cause is MSVC's debug iterators. The job uses release,
  so it does not see them.
- Gates on f5ee1e6:
  - clangcl-release: 49/49
  - tree-level: 28/28
  - WSL clang-tidy: clean
  - WSL clang-debug: 72/72
  - WSL gcc-release: 44/44
- ci-ok: adding `windows (clang-tidy)` to its needs is left for integration, as asked.

## #15: the ruling

The lead accepted option 3 and withdrew the earlier gated-trampoline ruling. 79ab16c records the
decision in `docs/modules/async.md`, where the limit is already described:

- it was decided in 0.5.0;
- why: a trampoline costs an atomic exchange on every `co_await`, fastcached's hot path included,
  and adds a second transfer path next to the frame-ownership code, only to buy synchronous depth
  in GCC debug and emsdk builds at 100000 levels;
- `-mtail-call` would be a PUBLIC flag;
- the targeted fix, if a browser consumer ever hits the limit: the trampoline under
  `__EMSCRIPTEN__` alone, with a plain flag.

`Task_test`'s skip comment now points at that decision. The lead closes #15.

## #47: the reason for closing

While measuring #52, replacing the `_watches` hash map with a flat map made no measurable difference:
17.3 to 18.1 ns/op against 17.3 to 20.7 ns/op. That measurement bounds what the lookup costs; it did
not measure the proposed hint itself. The hint would add a public `ParkEntry` field that six
consumers would have to check. Revisit only if a profile names `watchHandle` again. The lead closes
#47.
