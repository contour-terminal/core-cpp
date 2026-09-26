# Lane B report: core-cpp 0.5.0 issue sweep (tui, cli, testing, tooling, CI, docs)

Branch `next-050-b` in `D:/core-cpp-wt-v050b`, from `29cddd6` (origin/master after v0.4.3). Not pushed.
Tip: `1edc2c9` (15 commits on 29cddd6). Every commit ends with the Signed-off-by line.

## Per issue

| Issue | Outcome | Commit | Notes |
|---|---|---|---|
| #16 | obsolete | (fed98b4, B12) | `DelayAwaiter` is `net::DelayAwaiter` now (`src/core/tui/runtime/TuiRuntime.hpp:186`, forwards to `_loop.delay`); the runtime's own timer heap is gone. Regression test `TuiRuntime_test.cpp:1169` "core-cpp#16: a cancelled delay leaves nothing behind". |
| #17 | obsolete | (fed98b4, B12) | `blockOn` forwards to `EventLoop::blockOn` (`TuiRuntime.hpp:168`); `pumpOnce`/`PollEventSource` deleted. Tests `TuiRuntime_test.cpp:1190` and `net/EventLoop_test.cpp:1351`. |
| #18 | obsolete | (fed98b4, B12) | All three input awaiters arm a stop-callback → `requestCancelWaiter` (`TuiRuntime.hpp:577,632,692`, `TuiRuntime.cpp:417`). Tests `TuiRuntime_test.cpp:1220,1238`. |
| #19 | obsolete | (fed98b4, B12) | `TerminalEventSource`/`PollEventSource` deleted; handles are waited on by the loop's backend, which chunks. Test `TuiRuntime_test.cpp:987` "More concurrent handle waits than one native wait accepts all resolve". |
| #49 | obsolete | (790b422, v0.2.1) | `InputSource::inputClosed()` + `TuiRuntime::endInput()` (`TuiRuntime.cpp:186-196`); tests `TuiRuntime_test.cpp:836-925`, `posix/TerminalHangup_test.cpp`. |
| #48 | obsolete | (32f5cbe, v0.2.1) | Both halves done: nine completer rename rows + `check_renames_test.py`; provenance row for `SuppressWindowsDialogsAtStartup.cpp` says "not verbatim" (`provenance.md:461`). |
| #21 | fixed | 9455be7 | `ESC DEL`/`ESC BS` → `Backspace+Alt`. RED: 2 cases / 3 assertions failed before the fix. |
| #20 | fixed | 976cf98 | `detail::Utf16ToUtf8` pairs surrogates across reads (replaces private `windows/Win32Utf.hpp`); `VtParser` drops surrogates, overlongs, > U+10FFFF. RED: 6 of 6 invalid sequences produced events before the fix. |
| #37 | fixed | 43141a5 | Citation dropped at the three sites that ship (`SyncRun.hpp`, `async-and-net.md`, `docs/design/coroutines-and-lifetimes.md`). No real origin issue found. Bounded sweep: titles of all 72 fastcached numbers cited outside `.superpowers/` fetched and read against their contexts; no other mismatch (fastcached#405 cited for "a fixture that re-acquires what production binds" is plausible, not conclusive). `task-B1-report.md:329` in the main checkout still cites #178; I did not touch the SDD records. |
| #13 | fixed, **Breaking** | 969a807 | `parse()` → `std::expected<FlagStore, ParseError>`; `ParseErrorKind` {NotEnoughArguments, InvalidValue, EmptyValue, UnexpectedToken, MissingRequiredOption}, token index, message; `ParserError` removed; `from_chars`/`strtod`, whole-token. RED: the old API does not compile against the new cases, and the old parser accepted `12abc` and an unsigned `-1`. |
| #23 | fixed | ec3cd29 | `namespace-directory` skips a leading forward-declaration-only block; `TerminalInput.hpp` forward-declares `platform::Wakeup`. RED: old scanner refuses the new clean fixture. |
| #45 | fixed (cause found) | 8c43406 | Not reproducible with an absolute ROOT at 29cddd6 (five missing rows → five violations). Reproduced with a **relative** ROOT: `IS_DIRECTORY` accepts it, `file(GLOB_RECURSE)` finds nothing, and the scan reports only stale allowlist rows and no real violation -- the recorded shape. ROOT is now made absolute and an empty walk is refused; self-test adds six simultaneous violations of five rules, a relative-ROOT run, and an empty dir. RED on the old scanner; a "first violation only" mutant fails the new case. |
| #25 | fixed | 51cd98e | `MODE=sync` refuses a symlink DEST by name. RED: the old tool accepted a link to an empty directory (and would replace the link). Self-test ran for real on Windows cl-debug (no SKIPPED). |
| #36 | fixed | 675e20e | `.clang-tidy` files (walked up from the source) and the analyser binary are OBJECT_DEPENDS of every analysed compile; `check-tidy-inputs.cmake` + self-test; CI clang-tidy job asserts it. Measured: `touch src/core/tui/.clang-tidy` → 83 compiles, all tui; `touch .clang-tidy` → 483; before: `no work to do`. |
| #12 | fixed | 3ef869c | `scripts/check-open-work.py` (+ self-test): offline grammar in ctest `core-cpp.open-work` (tree-level), `--online` issue state in the style job. It immediately found core-cpp#5 closed with a live entry → 2e8c890 deletes that entry (the vcpkg port is now untracked; see decisions). |
| #33 | fixed | dc5982b | Nightly `upstream-drift` job in `downstream.yml`: full checkouts of contour, endo, fastcached as siblings; drift to the job summary; malformed row or 77 fails. Locally the same checker over the three consumer worktrees: 433 rows, exit 0. |
| #11 | fixed | 45f787e | `tests/IteratorDebugCanary.cpp`, `core-cpp.iterator-debug-canary` in every MSVC-driver Debug build, PASS only on `vector subscript out of range`. The runtime ends with `__fastfail` (0xc0000409, which ctest scores as a crash), so a CRT report hook forwards the report and exits 1. Passed on cl-debug (level 2); built `/MD` the read returns and the FAIL regex matches. |
| #44 | fixed (leg exists, not yet required) | 0e09d6e, 08d05e2 | `windows (clang-tidy)` job via `scripts/tidy-database.py` over the clang-cl compile database; refuses fewer windows/ sources than git tracks, another version, a silent canary. `CXX_CLANG_TIDY` over clang-cl is unusable (634 "exceptions disabled" false errors, measured). Three real Windows findings fixed in 0e09d6e. Final local run: analysed=269 windows=43/43 version=22.1.8 canary=reported failed=4 -- see decisions. Not in `ci-ok`'s needs yet. |
| #38 | partly done, stays open | 08d05e2, 1edc2c9 | clang-tidy half = #44. Sanitiser half measured and recorded as Open work: `clangcl-debug`+ASan now configures but cannot compile (`-MDd not allowed with -fsanitize=address`); `clangcl-release`+ASan does not link (the MSVC-driver path passes no sanitiser flag to the link step; `__asan_init` unresolved). |
| #10 | parked | -- | 361 Doxygen warnings on the last Docs run (platform 68, cli 61, tui 45, log 35, async 31, net 24, base ~97). `WARN_AS_ERROR` can flip only when all are gone, 123 are in lane A's modules under concurrent change, and there is no doxygen on this machine to iterate with. Best done after integration as one task. |
| #22 | parked | -- | The threshold is set by the largest function (`InputField::render`, 200); lowering it at all means refactoring that renderer first, which has thin render-level coverage. Piecemeal work, outside a sweep. |
| #34 | parked | -- | The issue itself says retention must not come first: counts are not comparable across configurations (131 cases in 23 files vanish; the Emscripten source-list class needs a design). Its Open work entry in `testing.md` stays. |

## Consumer impact of the Breaking change (#13)

- **tuidu** (`D:/tuidu-worktrees/core-cpp/src/tuidu/Cli.cpp`, `parseCommandLine()`): declares
  `std::optional<core::cli::FlagStore> parsed;` and assigns `core::cli::parse(...)` inside a
  `try`/`catch (std::exception const&)`. Must become `auto parsed = core::cli::parse(command, argc, argv);`,
  drop the `try`/`catch`, and print `parsed.error().message` where it now prints `e.what()` or
  "failed to parse command line". `*parsed` / `parsed->` keep working.
- **contour** (`D:/contour-worktrees/core-cpp`): uses `core::cli::App` only (`reparseParameters`,
  `parseParametersForTesting`, `run`); signatures unchanged, no change needed. Its
  `vtconformance` `ParserError` is its own enum, unrelated.
- **endo**: uses `App::customizeLogStoreOutput()` only; no change.
- **Lightweight dbtool, fastcached, morph**: no use of `core::cli::parse`; no change.
- Behaviour change visible to all `App` users: a malformed command line now prints
  `<app>: <message>` (e.g. `Option "count" expects an integer, not "x".`) instead of
  "Failed to parse command line parameters." / "Unhandled error caught. ...", and `12abc`,
  an unsigned `-1` and out-of-range numbers are refused.

Other public-header change: `core/tui/TerminalInput.hpp` no longer includes
`core/platform/Wakeup.hpp` (forward declaration). Grepped all consumers: none uses `Wakeup`
through that include alone.

## Gate results

Run on the tree of `1edc2c9` (the last step, squashing a one-line fixup of the #20 header -- a literal U+FFFD spelled `0xFFFD`, which `check-text-encoding` refused -- left the tree byte-identical to what was tested). Every build tree was deleted afterwards.


- `python scripts/clang-format.py --all --check`: 571 files clean (22.1.8). `python-style.py --all --check`: 29 clean.
- clang-tidy preset (WSL, pinned 22.1.8): `tidy-record: exit=0 version=22.1.8 statements=282 steps=517/517 canary=reported findings-printed=0` (incremental after `.clang-tidy` was touched, i.e. near-full re-analysis). `core-cpp.tidy-inputs` passed.
- WSL clang-debug, every label: 100% of 76 passed; `core-cpp.text-encoding` skipped there (WSL git cannot read the Windows worktree) and passes on Windows (883 files).
- WSL clang-asan-ubsan: 42/42 passed. clang-tsan: 42/42 passed. gcc-release: 45/45 passed (the assertion canaries skip under NDEBUG by design).
- Windows cl-debug: build OK; dialog canaries 3/3; 51/51 non-tree-level passed (incl. the new iterator canary); `core-cpp.vendor-selftest` passed (symlink cases ran).
- Windows clangcl-release (fresh tree): build OK; dialog canaries 3/3; 50/50 passed.
- `mkdocs build --strict`: clean. `ctest -L tree-level` (clang-debug): all passed. `check-open-work --online`: 7 entries hold. preset-coverage 18/18, tree-level-coverage 31/31.
- Windows tidy-database end to end: see #44 row (4 files with findings, all in lane A's tests).

## What the lead must decide

1. **The Windows clang-tidy leg is not in `ci-ok`** because of 4 files' findings in lane A's
   area: the replaced `operator delete` in `async/DetachedTaskFrame_test.cpp`,
   `async/StrandAllocation_test.cpp`, `net/CallbackAllocation_test.cpp` (parameter `storage` vs
   vcruntime_new.h's `_Block`, `readability-inconsistent-declaration-parameter-name`),
   `StrandAllocation_test.cpp:171` (`clang-analyzer-unix.MismatchedDeallocator` on the
   malloc-backed replacement), `Strand_test.cpp:257` (`clang-analyzer-cplusplus.Move`, a
   deliberate moved-from check). Whoever owns them fixes them and adds `windows-clang-tidy` to
   `ci-ok`'s needs in the same change.
2. **core-cpp#5 is closed**, so its Open work entry (a vcpkg port) is deleted; file a new issue if the port is still wanted, and re-add the entry.
3. **Lane A**: closing #6 and #7 must delete their Open work entries (`async-and-net.md`, `platform.md`) in the same change, or the style job's `check-open-work --online` goes red. #9 and #8 entries stay (issues open).
4. Conflict surface with lane A: `CHANGELOG.md` `[Unreleased]`; `.agent/rules/async-and-net.md` (one citation paragraph); `src/core/async/SyncRun.hpp` (one doc line); `src/core/net/windows/{StreamSocketOptions,WindowsSocket}.cpp`, `net/windows/UnixListener_test.cpp` (tidy fixes); `tests/CMakeLists.txt`, `.github/workflows/build.yml`.
5. `/code-review` was not run on this lane: the brief forbade dispatching subagents. Self-review only.
