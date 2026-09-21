# Task A8: re-review of fix rounds 2 and 3

Base `8d1fc18`, head `b1db039`, diff `review-A8-fix23.diff` (6 commits, 8 files). Read once, in
full. Two focused checks were run against the real `cmake/CoreCppVendorReplace.cmake` in a scratch
directory (the working tree, index, HEAD and branches were not touched); both are named where they
are used.

### Finding Verdicts

| # | Finding | Verdict | Where |
|---|---|---|---|
| R48-1a | The self-test header over-claims ("every refusal has a case") | **ADDRESSED** | `tests/cmake/check-vendor-selftest.cmake:8-48` — the header now splits the script's stops into *judgements* (every one has a case and a phrase, bar the two named as unreachable) and *reports of git failing* (named one by one, with why no case provokes them), and calls out `resolving the commit` as the judgement that comes from the git wrapper. `refusalPhrases` (113-136) gained `"already exists"`. See New Breakage #5: round 3's own rename-failure messages are outside both classes. |
| R48-1b | Delete the unreachable branch | **ADDRESSED** | `cmake/CoreCppVendor.cmake:594-596` — the `if(NOT copied AND NOT refusals)` block is gone, replaced by a comment saying why nothing reaches it. The claim holds: `^cmake/` is selected for every `MODULES` list (`:105`), the tree guard has already refused a ref without `cmake/CoreCppModules.cmake`, and if that blob were itself refusable `refusals` is non-empty and `:597` fires first. |
| R48-2 | `~SyncGuard` credits `syncGuard()` with the constructor's flush | **ADDRESSED** | `src/core/tui/TerminalOutput.cpp:317` — "The constructor flushes on the way in for the same reason, so the two ends match." Matches `:300-309`. |
| R48-3 | The new hygiene allowlist row is avoidable | **ADDRESSED** | The selftest's `core_cpp_selftest_empty()` is deleted; both callers now `file(REMOVE_RECURSE)` the whole copy and rewrite `MANIFEST` (`tests/cmake/check-vendor-selftest.cmake:425-433`). No `file(GLOB` remains in that file (grepped), and the row is gone from `tests/cmake/check-cmake-hygiene.cmake` (was 148-149). The tree is *not* free of vendor exemptions: `check-cmake-hygiene.cmake:146-147` still allows `cmake/CoreCppVendor.cmake`, which is the pre-existing row R48 did not ask about and which `core_cpp_vendor_list_files()` (`CoreCppVendor.cmake:168`) still needs. |
| R48-4 | Two documents describe the old behaviour | **ADDRESSED** | `docs/vendoring.md:54-58` now says what `sync` writes (`<dir>.core-cpp-vendor-new`, a sibling) and that every refusal deletes it; `:76-82` and `CHANGELOG.md:181-184` list all three previously omitted `check` refusals, and each matches the code: unparsable line `CoreCppVendor.cmake:232-234`, missing `# repository`/`# ref` `:250-256`, `# commit` not 40 lowercase hex `:257-261`. Two *other* sentences in the same file went stale in round 3 — New Breakage #1 and #2. |
| R49 | Make `sync`'s replacement recoverable | **ADDRESSED** | `cmake/CoreCppVendorReplace.cmake:44-111` (new), called at `cmake/CoreCppVendor.cmake:617-627`. Old copy moves aside (`:61-72`), a failed second rename restores it and refuses (`:92-100`), a failed restore names both trees and deletes neither (`:102-110`), a leftover backup is refused rather than deleted (`:47-55`). Empty and absent `DEST` still work (`:57-58`, `:60-61`). Proof: `check-vendor-selftest.cmake:604-696`, two new cases. |

**Focus 2 — the messages.** `FAILED-KEEP-BOTH` (`CoreCppVendorReplace.cmake:103-109`) names both
paths on their own lines, says `NOTHING HAS BEEN DELETED`, and the caller skips
`core_cpp_vendor_unstage()` for exactly this state (`CoreCppVendor.cmake:620-624`), so neither tree
is removed. Every message is `string(CONCAT)`; I checked all four, and the selftest asserts the
absence of `;` in the one it can reach (`check-vendor-selftest.cmake:674-679`). One gap in "finish
by hand" — New Breakage #4.

**Focus 3 — is the proof honest.** Yes for the restore. `check-vendor-selftest.cmake:656-663`
`include()`s the module by a path derived from `TOOL`, so it is the production file, and calls
`core_cpp_vendor_replace()` with the production argument roles — a real vendored copy as `dest`,
`${copy}.core-cpp-vendor-old` as `backup`, five arguments in the same order. Only `newCopy` differs
from production (a path that was never created, which is how the rename is made to fail); the branch
taken is the production one: backup absent → parent made → `dest` renamed aside → new-copy rename
fails → `movedAside` ON → restore. The assertions are not "the directory is back" but state, both
message phrases, no stray `;`, no sibling left, the `MANIFEST` SHA-256 unchanged, and a full
`MODE=check` over the copy. `FAILED-KEEP-BOTH` being unreached is stated in three places a reader
meets it: the module header (`CoreCppVendorReplace.cmake:28-31`), the selftest header
(`check-vendor-selftest.cmake:42-48`) and the caller's comment (`CoreCppVendor.cmake:620-621`). Note
this is *not* the kind of dead code R48-1b told the round to delete: that branch was unreachable by
construction, this one is reachable in production and only unreachable from a test.

**Focus 5 — the four mutations.** They are the right four: they cover each new guarantee exactly
once (the leftover is refused, not deleted; the restore exists; `movedAside` is tracked; the message
is not a list), and each is reported against its own case. None passes for the wrong reason:
`deletes-a-leftover-backup` is caught by the accept/refuse expectation (the phrase assertion alone
would not have caught it, and `core_cpp_selftest_expect` also rejects any *other* phrase from
`refusalPhrases`); `never-moved-aside` produces state `FAILED` — the expected state — so it is the
`put back` phrase, the restore assertion and the `MODE=check` that turn it red, which is why all
three assertions are needed. Untested by construction, and correctly so: the caller's
`if(NOT replaceState STREQUAL "FAILED-KEEP-BOTH")` guard, since no test reaches that state.

### The Replacement's Orderings

`old` = `DEST` holds the copy it held before; `new` = the copy just assembled; `neither` = `DEST`
does not exist when the run ends.

| Ordering | What the code does | Copy ends up |
|---|---|---|
| First rename fails (nothing moved yet) | `Replace:62-70` returns `FAILED` with "could not move `DEST` aside to `BACKUP` (…), so the copy was not replaced"; the caller unstages the new copy and stops (`Vendor:619-625`) | **old** |
| Second rename fails after the first succeeded, restore succeeds | `Replace:74` fails → `:92` restores → `FAILED` with "The previous copy has been put back"; caller unstages the new copy | **old** (this is the path the direct test drives) |
| The restore itself fails | `Replace:102-110` → `FAILED-KEEP-BOTH`; caller does *not* unstage; the message names `BACKUP` and `newCopy` on separate lines and says nothing was deleted | **neither** — both trees on disk and named. See New Breakage #4 about which of the two a human can actually adopt |
| A leftover `DEST.core-cpp-vendor-old` from a previous crash | `Replace:47-55` refuses before touching anything (`EXISTS backup` is the first statement); the caller unstages `…-new`, the leftover survives. Asserted by `check-vendor-selftest.cmake:609-634` | **old**, and the leftover is kept (it may be the only copy) |
| `DEST` does not exist | `Replace:57-58` creates the parent, `:61` skips rename 1, `:74` renames the new copy in → `OK`. If *that* rename fails: `FAILED` with "…`DEST` does not exist, and no previous copy was lost, because there was none" | **new**; on failure **neither**, with nothing lost |
| `DEST` exists and is empty | The occupant guard (`Vendor:357-366`) finds no occupants; the empty directory is renamed aside and removed after the swap | **new** |
| `DEST` is an ordinary file | The occupant guard cannot see it (`file(GLOB_RECURSE "<file>/*")` is empty, `Vendor:168`), so nothing refuses; `Replace:62` renames the file to `BACKUP`, `:74` puts the copy at `DEST`, `:77` deletes the backup — the user's file is gone, exit 0, no message. **Verified** by calling the real function over a plain-file `DEST`. Before this diff the run stopped at `file(MAKE_DIRECTORY "<file>/.core-cpp-vendor-staging")` and the file survived (also verified) | **new**, and the file that was there is **lost**. New Breakage #3 |
| `DEST` is a symlink (junction) to a directory | `file(RENAME)` moves the *link*, not the target, and `file(REMOVE_RECURSE)` unlinks a symlink rather than following it, so the target keeps the old copy. **Verified** on Windows with a junction: `state=OK`, `DEST` is a real directory with the new copy, `target/MANIFEST` still says "the old copy". POSIX symlinks behave the same way (reasoned, not run here) | **new**, old tree orphaned at the link target, not deleted |
| `DEST` is the process's current working directory | Windows: rename 1 fails on the sharing violation → row 1. POSIX: rename 1 succeeds (the CWD follows the inode), the swap completes | **old** on Windows, **new** on POSIX |
| `DEST` and the backup on different filesystems | Impossible by construction — `…-old` and `…-new` are siblings in `DEST`'s own parent (`Vendor:350-351`, comment at `:68-70`). It can only arise when `DEST` *is* a mount point, and then rename 1 fails as a class → row 1 | **old** |

The only ordering that ends in **neither** with something at stake is the failed restore, and it is
the one the fix deliberately leaves non-destructive: both trees on disk, both named. The absent-`DEST`
rename failure also ends in "neither", but there was no copy to lose.

### New Breakage in the Fix Diff

All five are Minor. None is Critical or Important.

1. **Minor — `docs/vendoring.md:38-39` now describes the old layout.** "a URL, which is cloned once
   into a temporary directory **under `DEST`**". Round 3 moved staging to a sibling, so the bare
   clone lands in `<dir>.core-cpp-vendor-new/repo.git` (`CoreCppVendor.cmake:350` and `:374`), beside
   `DEST` and not inside it. This is the same defect class as R48-4, reintroduced two bullets above
   the one that fixed it.
2. **Minor — `docs/vendoring.md:67` contradicts `:59-66`.** "A `DEST` holding a manifest is emptied
   first" is the mechanism round 3 removed; three lines earlier the page says the old copy is
   renamed aside and "deleted only after both have succeeded". The *effect* (a file the new ref no
   longer has is gone) is still true; the sentence that carries it is not.
3. **Minor — a `DEST` that is an existing regular file is silently destroyed.** Verified, and a
   behaviour change of this diff: before, staging lived inside `DEST` and `file(MAKE_DIRECTORY)`
   stopped the run with "file failed to create directory … No such file or directory", leaving the
   file untouched; now the occupant guard cannot see a non-directory (`CoreCppVendor.cmake:357-366`
   via `:168`) and the replacement renames it aside (`CoreCppVendorReplace.cmake:62`) and deletes it
   after a successful swap (`:77`), reporting success. Both `docs/vendoring.md:67-69` and
   `CHANGELOG.md:193` promise a `DEST` that is not one of ours is refused. A one-line
   `if(EXISTS "${DEST}" AND NOT IS_DIRECTORY "${DEST}")` beside the occupant guard closes it.
4. **Minor — `FAILED-KEEP-BOTH` tells the human to adopt either tree, but one of them cannot be
   adopted.** `CoreCppVendorReplace.cmake:109` says "Rename whichever of those two you want to
   `DEST` by hand, then delete the other." The new copy has no `MANIFEST` at that point — it is
   written only after the replacement returns `OK` (`CoreCppVendor.cmake:654`) — so a human who
   picks it gets a copy that fails `MODE=check` ("MANIFEST does not exist") and that the next sync
   then refuses to overwrite ("holds N file(s) and no MANIFEST"). The message should say to move the
   previous copy back and re-run `sync`, or that the new copy still needs its manifest.
5. **Minor — the self-test header's taxonomy does not cover round 3's own messages.**
   `check-vendor-selftest.cmake:8-21` states the script stops for exactly two kinds of reason
   (judgements, git failures). The replacement adds a third: OS rename failures. Of its four
   messages, `already exists` has a case and a phrase, the restore message has the direct-call case,
   and `FAILED-KEEP-BOTH` is named as unreached at `:46-48` — but "could not move … aside to …"
   (`Replace:64`) and "could not move the new copy … no previous copy was lost" (`Replace:85`) have
   no case and are named nowhere. This is the over-claim R48-1 asked to fix, one round later and
   smaller.

Nothing round 1 fixed is broken: the root guard (`CoreCppVendor.cmake:326-347`) and tree guard are
untouched apart from `CMAKE_CURRENT_LIST_DIR` → `CORE_CPP_VENDOR_SCRIPT_DIR` (captured at `:76`,
before any `include()`, so the `REPO` default is unchanged); the empty-manifest refusals are intact
(`:244-275`); the staging cleanup is asserted more strictly than before, for *both* siblings, at
three call sites (`check-vendor-selftest.cmake:585`, `:593`, `:751`, macro at `:95-101`); and the
`EMPTY_ALL`/`EMPTY_HEADERS` mutations reach the same end state as the deleted helper did, not a
weaker one.

### Out-of-Scope Observations

- **The crash window between the swap and the manifest is still open, and is not new.** The backup
  is deleted as soon as the second rename succeeds (`CoreCppVendorReplace.cmake:77`), while
  `MANIFEST` is written afterwards (`CoreCppVendor.cmake:654`). A kill or a failed write in between
  leaves `DEST` holding a manifest-less new copy with the old one already gone — which then fails
  `check` and blocks the next `sync`'s occupant guard. The same window existed before round 3.
  Closing it means keeping the backup until the manifest is on disk.
- The leftover-backup refusal fires at the very end, after the clone, the tree walk and every blob
  have been written. An `EXISTS "${backupDir}"` pre-check beside the occupant guard
  (`CoreCppVendor.cmake:350-366`) would refuse in a second instead of minutes, without changing
  behaviour.
- `source-glob cmake/CoreCppVendor.cmake` remains in the hygiene allowlist
  (`check-cmake-hygiene.cmake:146-147`). It is not the row R48 asked about and it is still needed —
  `core_cpp_vendor_list_files()` must enumerate a copy to find unlisted files — but "the tree now
  carries no vendor exemption" is not the state of the tree.
- The report's residual answer on R48's item 15 (a fixture pinning the real module table's shape)
  is not in this diff and was not asked to be; nothing here changes the judgement that it belongs
  with the layering checks.

### Verdict

**Fix round:** All findings addressed, no new Critical/Important breakage. Five Minor items are
open: two stale sentences in `docs/vendoring.md` (`:38-39`, `:67`), the silent destruction of a
`DEST` that is a plain file, the `FAILED-KEEP-BOTH` message pointing at a manifest-less tree, and
the self-test header's taxonomy missing the replacement's own rename-failure messages.
