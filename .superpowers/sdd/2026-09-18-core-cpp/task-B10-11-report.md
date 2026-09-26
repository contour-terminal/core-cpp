# Tasks B10 and B11: rebased onto the integration tip `3681abd`

| Task | Before the rebase | After the rebase |
|---|---|---|
| B10 | `974d8ef` | **`914d220`** |
| B11 | `02eb8d0` (earlier `454e989`, amended twice for gate fixes) | `7cac6f1`, then **`10682c8`** with the concern-5 ruling |

Neither commit is pushed. The work is in the detached worktree `D:/core-cpp-wt-b11`. Base
`99acaa6` was dropped from the range, because `3681abd` already carries it as `b9428ad`.

## Conflicts, and how each was resolved

- **`provenance.md`:** B8's fix round had rewritten the `Sockets.hpp` row (`IAsyncAddressResolver&`
  became `*`). The result keeps B8's row and B10's note on the `SplitSocket.hpp` row.
- **`async-and-net.md`:** B9 and B11 each inserted a new section before "Profiling zones". Both
  are kept, B9's first.
- **`CHANGELOG.md`:** B9 and B11 each inserted entries at the top of Added and Breaking. Both are
  kept, B9's first.

No source file conflicted. The TLS test's half-close case only asserts that a write fails, so
B9's EPIPE → `SystemError` change does not affect it.

## The concern-5 ruling, folded into `10682c8`

A transport EOF before `close_notify` now reads as `NetErrorCode::ConnReset` ("peer closed
without close_notify"), in both `read` and `waitReadable`. The peer's `close_notify` still reads
as `0`, which is `ISocket`'s clean end of stream.

**Test first, then RED.** `TlsSocket_test` has a new case, "A transport EOF before close_notify
reads as a reset, and close_notify as the end". Its first section is a raw peer that writes and
then ends its transport mid-stream; its second is a peer that sends `close_notify`. The existing
"truncated stream" `waitReadable` section now expects the reset. Predicted: 2 failed assertions
and a green `close_notify` section. Actual:

```
TlsSocket_test.cpp:336: FAILED:  REQUIRE_FALSE( outcome.result->has_value() )  with expansion:  !true
TlsSocket_test.cpp:378: FAILED:  REQUIRE_FALSE( second->has_value() )          with expansion:  !true
test cases:   7 |   5 passed | 2 failed
```

**GREEN:** 254 assertions in 22 cases, three runs in a row. The CHANGELOG has a Breaking entry
with a migration, and the rule is in `async-and-net.md`'s TLS section; `net.md`, `Tls.hpp` and
`StrictTlsPeer.hpp` are updated to match.

## Gates on `10682c8`

| Gate | Build exit | Result |
|---|---|---|
| WSL `clang-debug`, `--clean-first` | 0, **596 steps** | **42/42**, 0 skipped |
| Windows `cl-debug`, `--clean-first` | 0, **590 steps** | **42/42**, 0 skipped |

## Gates after the rebase, before the ruling (`7cac6f1`)

| Gate | Build exit | Result |
|---|---|---|
| WSL `clang-debug`, `--clean-first` | 0, **596 steps** | **42/42**, 0 skipped; `core-cpp-net-test` 3425 assertions / 333 cases; `core-cpp-net_tls-test` 224 / 21 |
| Windows `cl-debug`, `--clean-first` | 0, **590 steps** | **42/42**, 0 skipped |
| `clang-format --all --check` | – | 509 files clean |
| `check-tree-level-coverage.py` | – | 17 of 17 covered |
| `check-renames.py` | – | 667 rows, 0 failures |

The full per-task records (REDs, mutations, the pre-rebase matrix and the concerns) are in
`task-B10-report.md` and `task-B11-report.md`. Their SHAs predate this rebase.
