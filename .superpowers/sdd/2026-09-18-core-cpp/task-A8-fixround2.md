# Task A8: fix round 2 (Ruling R48)

The re-review (`task-A8-rereview-r1.md`) verdicts all 17 items ADDRESSED and found four Minor problems in the fix itself. They are small, but three are claims that are not true, and one is an escape hatch that does not need to exist. Fix all four, then we close A8.

1. **The self-test header over-claims.** It now says every refusal has a case; six do not, and `cmake/CoreCppVendor.cmake:578-580` is unreachable.
   - Say precisely which refusals have cases and which do not, and why the ones that do not are unreachable from a test.
   - Delete the unreachable branch, or explain in a comment what still reaches it. Prefer deleting: dead code in the tool contour trusts is worse than a missing case.
2. **`src/core/tui/TerminalOutput.cpp:317` still credits `syncGuard()` with the constructor's flush.** Round 1 moved the flush into the constructor; make the comment say what the code now does.
3. **The new hygiene allowlist row is avoidable.** Both callers overwrite the MANIFEST anyway, so the `REMOVE_RECURSE` needs no glob. Remove the glob and the allowlist row with it. An allowlist row is the repository's escape hatch, and one we do not need is one we should not keep.
4. **Two documents still describe the old behaviour.**
   - `docs/vendoring.md:53` says the tool "writes nothing into DEST", which round 1's staging fix made false in the other direction: say what it does write and when it removes it.
   - `docs/vendoring.md:64-67` and `CHANGELOG.md:178-181` omit three `check` refusals that round 1 added. List them.

Also answer, in the report rather than in code: the re-review's residual on item 15 — the fixture table pins none of the real `cmake/CoreCppModules.cmake`'s shape, so a row whose shape the regex misses would fail silently. Say whether a shape-pinning case is worth adding now or belongs with the layering checks; do not build it in this round.

**Then:** the presets the dispatch names, the vendor self-test, `clang-format --check`, `mkdocs build --strict`, push, watch CI and portability to green. Append "Fix round 2" to `task-A8-report.md` with the commands and output.
