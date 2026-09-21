# Task A8: fix round 4 (Ruling R50) — the last one for A8

The re-review (`task-A8-rereview-r2.md`) verdicts round 2 and round 3 ADDRESSED, and found five Minor problems plus two worthwhile observations. Fix all seven; then A8 closes.

1. **A `DEST` that is an existing regular file is silently destroyed** (re-review Minor 3, verified).
   - Before round 3, staging inside `DEST` made `file(MAKE_DIRECTORY)` stop the run and leave the file alone. Now the occupant guard cannot see a non-directory, so the file is renamed aside and deleted after a successful swap, reporting success — while `docs/vendoring.md:67-69` and `CHANGELOG.md:193` both promise a `DEST` that is not ours is refused.
   - Fix: `if(EXISTS "${DEST}" AND NOT IS_DIRECTORY "${DEST}")` beside the occupant guard, refusing by name. Add a self-test case.
2. **`FAILED-KEEP-BOTH` recommends adopting a copy that cannot be adopted** (Minor 4).
   - The new copy has no `MANIFEST` at that point — it is written only after the replacement returns OK — so a human who picks it gets a copy that fails `check` and that the next `sync` then refuses to overwrite.
   - Fix: say to move the previous copy back and re-run `sync`, or say plainly that the new copy still needs its manifest. The message must leave a human with a path that works.
3. **Close the crash window between the swap and the manifest** (out-of-scope observation, and the same class as item 2).
   - The backup is deleted as soon as the second rename succeeds, while `MANIFEST` is written afterwards. A kill in between leaves a manifest-less copy with the old one gone, which then fails `check` and blocks the next sync.
   - Fix: keep the backup until the manifest is on disk, then delete it. Say in the report whether any ordering remains where a crash loses the old copy before the new one is complete.
4. **Refuse a leftover backup early** (out-of-scope observation).
   - The refusal currently fires after the clone, the tree walk and every blob is written — minutes in, for a condition visible in a second. Move the `EXISTS "${backupDir}"` check beside the occupant guard, with no change in behaviour.
5. **`docs/vendoring.md:38-39`** still says the clone lands under `DEST`; round 3 moved staging to a sibling.
6. **`docs/vendoring.md:67`** still says "a `DEST` holding a manifest is emptied first", which is the mechanism round 3 replaced. The effect is unchanged; the sentence is not.
7. **The self-test header's taxonomy misses round 3's own messages** (Minor 5).
   - It says the script stops for exactly two kinds of reason; the replacement adds OS rename failures as a third, and two of its messages ("could not move … aside", "could not move the new copy …") have no case and are named nowhere.
   - Fix: name the third kind, and say which of its messages have cases and which cannot be reached from a test, as round 2 did for the others.

**Then:** the vendor self-test, the presets the dispatch names, `clang-format --check`, `mkdocs build --strict`, push, and watch CI and portability to green. Append "Fix round 4" to `task-A8-report.md`, with the mutation evidence for items 1 and 3.
