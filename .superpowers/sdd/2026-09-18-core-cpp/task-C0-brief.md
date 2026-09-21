# Brief for Task C0

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


The common steps for every consumer are:
1. Create the worktree with superpowers:using-git-worktrees, using the Global Constraints location. Never pull into or build in the main checkout:
   ```powershell
   git -C D:\<repo> fetch origin
   git -C D:\<repo> worktree add D:\<repo>-worktrees\core-cpp -b <branch> origin/master
   ```
2. Pin with `GIT_TAG v0.1.0` + `VERSION 0.1.0`; iterate locally with `-DCPM_core-cpp_SOURCE=D:/core-cpp`.
3. Build and test with the repo's own presets on Windows and WSL.
4. Run superpowers:requesting-code-review, then open the PR with **contour-workflows:draft-pr**.
5. Drive CI to green with **contour-workflows:fix-ci**.


### Task C0: Migration tooling in core-cpp
**Files:** Create `tools/migrate/renames.json` (the include map, namespace map, and symbol-scoped and member renames, seeded from Part I §2), `tools/migrate/rewrite.py` (mechanical: anchored regexes `(?<![\w:])(crispy|coro|net|tui)::`, `logstore::`, include map; idempotent; `--profile contour|endo|tuidu|fastcached`), `tools/migrate/semantic_rename.py` (libclang bindings over `compile_commands.json`: rewrites only DeclRef/MemberRef/TypeRef whose declaration is under the given paths; unions edits from several compile DBs), and `tools/migrate/*_test.py`.

- [ ] Tests first (`python -m pytest tools/migrate`):
  - `rewrite.py` is idempotent.
  - `crispy::cli::command` → `core::cli::Command`.
  - `std::net` is untouched.
  - `semantic_rename.py` renames `sock.Read(` only where `sock` is a `FastCache::ISocket` (fixture: two classes with `Read`).
- [ ] Commit `tools: rename table and codemods for consumer migration`. Push.

