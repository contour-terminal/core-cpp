# Task A8: fix round 3 (Ruling R49)

One item, from fix round 2's own concern: `sync`'s replacement phase is not transactional. It empties `DEST`, then renames the staged entries in, so a `file(RENAME)` failure part-way leaves `DEST` half-replaced. It is the last path on which a sync can damage a consumer's copy — plausible on Windows, where a lock or a scanner can fail a rename — and contour's build depends on that copy being either the old one or the new one.

**Fix:** make the replacement recoverable.
- Move the existing copy aside to a sibling of `DEST` before the new entries go in (or stage the whole new tree and swap), so a failure can put it back.
- On any failure during the replacement, restore what was there and refuse, naming the file that failed and the state the copy is in.
- If a restore itself fails, say plainly where both the old and the new trees are, so a human can finish by hand. Never leave the consumer guessing.
- Empty `DEST` and a fresh `DEST` must keep working.

**Prove it:** a self-test case that makes a rename fail part-way (an occupied destination, or a read-only or open file — whatever is portable enough for Windows and Linux) and asserts the copy afterwards still passes its own `check`. If no portable way exists to force the failure, say so in the report and test the restore path by calling it directly rather than skipping the proof.

Then: the vendor self-test, the presets the dispatch names, `clang-format --check`, `mkdocs build --strict`, push, and watch CI and portability. Update `docs/vendoring.md`'s account of what a refusal leaves behind, and the CHANGELOG. Append "Fix round 3" to `task-A8-report.md`.
