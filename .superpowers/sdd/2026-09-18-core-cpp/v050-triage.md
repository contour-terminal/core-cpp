# core-cpp 0.5.0: issue sweep (user request 2026-09-26)

The work happens on one branch per lane, local only: no pushes until integration. At integration the team lead merges both into
release/next-050 (from master 29cddd6) and pushes once; CI runs then, and fix rounds follow.
The release is 0.5.0, because #6, #7 and #13 are Breaking (0.x minor). Then every consumer is re-pinned to 0.5.0.

## Lane A: impl-v040, worktree D:/core-cpp-wt-v040, branch next-050-a (async, net, coro, platform, base)
#53 TaskKind (already briefed), #15, #26, #27, #28, #29, #35, #47, #6 (remove WfmoBackend: Breaking; #46 and #50 then close as obsolete), #7 (Environment unification: Breaking)

## Lane B: impl-v050b, worktree D:/core-cpp-wt-v050b, branch next-050-b (tui, cli, testing, tooling, CI, docs)
#16 #17 #18 (probably obsolete since B12 moved TuiRuntime onto EventLoop: verify, don't assume), #19, #20, #21, #22, #49, #13 (cli -> std::expected: Breaking), #10, #11, #12, #23, #25, #33, #34, #36, #37, #38 and #44 (together), #45, #48

## Team lead
- #5 install/export: already done (core_cpp_install, docs/getting-started/install.md). Close with evidence.
- #8: graduation rule, no second consumer needs it. Stays open as a tracker, with a comment.
- #9: the morph strand/executor part was resolved by morph#806 on core::async Strand. Update it; the rest stays open as a tracker.
- #54: external MSVC bug, documented with a workaround. Stays open.
