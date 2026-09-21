# Task A10: fix round 1 (Ruling R54)

The review (`task-A10-review.md`) verdicts all 15 findings FIXED, no Critical, and Approved the task. Two Important items remain, both in the in-memory filesystem, and both are the same shape: the fix went in through one door and another was left open. They join Ruling R53's rename seam, so you build and verify once.

1. **Finding 11 is fixed on the write half of the key space only** — `src/core/platform/testing/InMemoryFileSystem.cpp:201,264,355,365,502,518,536,575,582,590`.
   Ten sites convert a key back through the narrow `path` constructor, so on Windows `listDirectory()`, `walkDirectoryRecursive()`, `weaklyCanonical()` and the parent-key derivations do not round-trip a non-ASCII name. It is invisible on POSIX, which is why CI is green, and it is not a regression — the old code threw.
   - Add a `pathFromKey()` helper over `std::u8string_view` and route every site through it, so the key space has one spelling in and one out.
   - Cover it with a listing case using a non-ASCII name.
2. **The streams still hold a raw pointer into the file map** — `InMemoryFileSystem.cpp:91,34,336-347`.
   Every path *through the stream* is now safe, but a `writeFile()` through the filesystem reallocates the string under a live stream — the original defect by another door — and `remove()` or `rename()` leaves `_target` itself dangling.
   - Prefer making the ownership real (a `shared_ptr<std::string>` the stream shares) over documenting the hazard. This is the fake every test uses; a lifetime rule nobody can violate beats one written in a comment.
   - If you make it a documented contract instead, say why in the report, and assert it where the fake can detect a violation.
   - Cover both: a write through the filesystem while a stream is open, and a remove and a rename while one is open.

Plus **Ruling R53**, already sent: the injectable rename primitive and the tests for the two-hop recase path (direct rename failing, second hop failing so the caller gets the second hop's error, rollback failing after that with both spellings named and nothing stranded silently).

## Then

- The presets the constraints name on Windows and WSL, including the sanitizers.
- `python scripts/clang-format.py --check`, `ctest -L hygiene`, `mkdocs build --strict`.
- CHANGELOG for anything a consumer would notice.
- Rebase before pushing; two other agents are on this branch. Push, watch CI and portability to green.
- Append "Fix round 1" to `task-A10-report.md` with RED/GREEN per item.
