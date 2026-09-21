# Task A8 re-review, round 3 (of fix round 4 / Ruling R50)

Base `b1db039`, head `f7a89f3` (two commits: `6990dcf` code, `f7a89f3` docs/CHANGELOG). Read-only
review; no mutation of the working tree.

### Finding Verdicts

1. **`DEST` an existing regular file is silently destroyed** — ADDRESSED.
   `cmake/CoreCppVendor.cmake:360-364` adds `if(EXISTS "${DEST}" AND NOT IS_DIRECTORY "${DEST}")`
   before the leftover-backup and occupant guards, and before `file(MAKE_DIRECTORY "${stagingDir}")`
   at line 392 — nothing is created or written when it fires. The self-test case
   `refuses-a-destination-that-is-a-file` (`tests/cmake/check-vendor-selftest.cmake:562-567`) writes
   a file at the `copy` path, syncs, asserts the refusal phrase, and then asserts the path is still
   a file with its original content (`:349-361`) — not just "still exists." It also runs
   `core_cpp_selftest_no_siblings`, so no `…-new`/`…-old` is left behind either.
   A symlink is worth flagging as unreached rather than unaddressed: CMake's `IS_DIRECTORY` and
   `EXISTS` resolve through a symlink to its target, so a `DEST` that is a symlink to a file lands
   in this same guard (refused, nothing touched) and a symlink to a directory passes it and is
   treated as the directory it points to for the occupant check. What the two subsequent
   `file(RENAME)` calls and the final `file(REMOVE_RECURSE "${backupDir}")` do to a *symlink* `DEST`
   specifically (rename moves the link, not the target; recent CMake does not recurse through a
   symlink to delete its target) is unchanged by this diff and untested before or after it — not a
   regression, and not one of the seven items, but worth a case in a future round if a consumer's
   `DEST` is ever plausibly a symlink.

2. **`FAILED-KEEP-BOTH` recommends adopting a copy that cannot be adopted** — ADDRESSED.
   `cmake/CoreCppVendorReplace.cmake:94-104`: the message now reads "Both are complete vendored
   copies, manifest included, so rename whichever of the two you want to `${dest}` by hand … the
   one you keep passes `MODE=check` as it stands." This is true only because of item 3's fix: the
   manifest is written into the staged tree before `core_cpp_vendor_replace()` is ever called
   (`cmake/CoreCppVendor.cmake:638-673`, then `:679-680`), so by the time `FAILED-KEEP-BOTH` can
   fire, `newCopy` already has a `MANIFEST`. No case reaches `FAILED-KEEP-BOTH` (documented as such,
   correctly — see item 7), so this is verified by inspection, not by a run.

3. **Crash window between swap and manifest** — ADDRESSED. See Crash Orderings below.

4. **Leftover-backup refusal moved early** — ADDRESSED.
   `cmake/CoreCppVendor.cmake:366-376` now checks `EXISTS "${backupDir}"` immediately after the
   `DEST`-is-a-file guard and before the clone/tree-walk, using the same message text as before.
   `cmake/CoreCppVendorReplace.cmake` no longer contains that check at all (removed, not left as an
   unreachable branch — confirmed by reading the function start at `:45-46`, which goes straight to
   `get_filename_component(destParent ...)`). The docstring at `:41-42` now states the precondition
   is the caller's job. The self-test case `refuses-a-leftover-backup-beside-the-copy`
   (`:637-663`) is unchanged and still drives the guard through the full script (`MODE=sync`), not
   through a direct call to `core_cpp_vendor_replace()`, so moving the check did not strand it
   behind a path no case reaches — it is the same case, now hitting the check earlier in the same
   script run. The other direct caller of `core_cpp_vendor_replace()`
   (`restores-the-copy-when-the-replacement-fails`, `:672-716`) never had a pre-existing backup to
   begin with, so it does not depend on the removed check either.

5. **`docs/vendoring.md:38-39` said the clone lands under `DEST`** — ADDRESSED.
   Now reads "cloned once, bare, into the directory `sync` assembles the new copy in —
   `<dir>.core-cpp-vendor-new`, beside `<dir>` — and removed again before that copy is put in
   place" (`docs/vendoring.md:38-41`), matching `clonePath = "${stagingDir}/repo.git"` and its
   removal at `cmake/CoreCppVendor.cmake:397-398,634-636`.

6. **`docs/vendoring.md:67` described the emptied-first mechanism** — ADDRESSED.
   Now reads "`sync` replaces the copy it finds, whole. `<dir>` is swapped for the new tree rather
   than edited in place …" (`docs/vendoring.md:80-84`), and explicitly lists both `DEST` refusals
   (no-manifest occupant, and now the regular-file case), matching the code.

7. **Self-test header's taxonomy missed round 3's own messages** — ADDRESSED.
   `tests/cmake/check-vendor-selftest.cmake:8-29` now names three kinds of stop reason and, for the
   third (an OS rename refusal), says which of its three messages has a case (`could not move the
   new copy … into <DEST>`, reached via `restores-the-copy-when-the-replacement-fails` calling the
   function directly) and which two cannot be reached from a test, with why for each. I traced the
   case by hand: `dest` exists so the first rename (aside) succeeds, the second rename fails because
   `newCopy` does not exist, and the restore succeeds — landing exactly on the message the header
   names, with state `FAILED` (not `FAILED-KEEP-BOTH`), matching the assertion at
   `check-vendor-selftest.cmake:704-707`.

### Crash Orderings

Sequence after the guards: assemble `stagingDir` → write `stagingDir/MANIFEST` → rename `DEST` to
`backupDir` (skipped if `DEST` doesn't exist) → rename `stagingDir` to `DEST` → delete `backupDir`.

| Killed | `DEST` holds | Next `sync` |
|---|---|---|
| Before the first rename (includes mid-copy and mid-manifest-write, since the manifest write at `:672-673` precedes the `include()`/call at `:679-680`) | The old copy, untouched; `stagingDir` may hold a partial or complete assembly | `file(REMOVE_RECURSE "${stagingDir}")` at `:352` wipes the leftover before any guard runs; `backupDir` does not exist, so the guards pass and the run proceeds normally |
| Between the two renames (`DEST`→`backupDir` done, `stagingDir`→`DEST` not yet) | Neither — `backupDir` holds the complete old copy, `stagingDir` holds the complete new copy (manifest already inside both) | `backupDir`-exists guard (`:370-376`) refuses immediately, before anything is read; both trees intact, nothing lost |
| Between the second rename and `file(REMOVE_RECURSE "${backup}")` (`CoreCppVendorReplace.cmake:65-69`) | The new copy, complete with manifest; `backupDir` (the old copy) still on disk | Same `backupDir`-exists guard refuses immediately; nothing lost, `DEST` already passes its own `check` |
| After the backup is deleted | The new copy, complete | Runs normally |

No ordering leaves `DEST` holding an incomplete copy, and no ordering loses the old copy while the
new one is still incomplete: the new copy is whole (manifest included) before the first rename that
could touch `DEST` happens at all. The one thing a crash can leave behind is a leftover `backupDir`
that blocks the next sync until a human moves or deletes it — which is refused loudly, immediately,
and by name, not lost silently. This matches the report's own table
(`task-A8-report.md`, "Fix round 4" §2-3) exactly; I built it independently from the diff before
reading the report's version.

### New Breakage in the Fix Diff

None found. Specifically checked and unaffected by this diff:
- The root/tree guards (`IS_DIRECTORY "${REPO}"`, bare-repo check, `not a repository root`, `not a
  core-cpp tree`) — untouched, still fire before the new `DEST`/backup guards run (they run even
  earlier, at `:322-348`, before `stagingDir`/`backupDir` are even computed).
- `MODE=check`'s empty-manifest and header refusals (`:193-286`) — untouched by this diff.
- Staging cleanup (`core_cpp_vendor_unstage()`, `:90-94`) — untouched; still called on every
  `FATAL_ERROR` path except `FAILED-KEEP-BOTH`, and `CORE_CPP_VENDOR_STAGING_DIR` is still cleared
  to `""` after a successful replace (`:689`).
- The occupant guard (no-manifest refusal, `:378-390`) — same logic, only relocated below the two
  new guards; its self-test case is unchanged and still passes.
- `git diff --check` over the whole range reports no whitespace errors.
- Manifest hashing correctness: `copied` (built at `:542-616`) never includes `MANIFEST` itself
  (it is generated, not a git blob), so hashing from `stagingDir` instead of `DEST` changes nothing
  about which files are hashed or in what order.

### Verdict

**Fix round: All findings addressed, no new Critical/Important breakage.**

1. DEST-as-file — ADDRESSED
2. FAILED-KEEP-BOTH message — ADDRESSED
3. Crash window between swap and manifest — ADDRESSED
4. Leftover-backup refusal moved early — ADDRESSED
5. docs/vendoring.md clone location — ADDRESSED
6. docs/vendoring.md emptied-first mechanism — ADDRESSED
7. Self-test taxonomy — ADDRESSED
