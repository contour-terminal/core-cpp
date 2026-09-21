# Task A10: fix round 2 (Ruling R58) — the last one

The re-review (`task-A10-rereview-r1.md`) verdicts R53, R54's two items, R56 and the rules addition all ADDRESSED, and the rename seam's middle case does invert when finding 13's fix is reverted. One Important problem came in with the round, plus five Minor.

1. **Important — `unget()` and `putback()` now set `badbit` on every stream the fake hands out**, where `std::ifstream` and `std::fstream` succeed. The reviewer verified it with a standalone probe of `MemoryReadBuf` against the real types.
   - This is a divergence *this round introduced*, in the round whose purpose was to make the fake faithful, and it is in neither the CHANGELOG nor core-cpp#27.
   - Fix it with a `pbackfail()` override rather than documenting it. The position is an integer now, so putting a character back is cheap and exact; a put-back of a *different* character than was read is the case to think about — decide what the real streams do and match them.
   - Cover `unget()`, `putback()` of the same character, and `putback()` of a different one.
2. **Minor — `InMemoryFileSystem.cpp:671`**: the walk's sort key still converts out of a path narrowly, the one conversion the key-space sweep missed.
3. **Minor — `copyFile()` at `:498` and `addFile()` at `:742` detach an open stream**, which falsifies the invariant `fileAt()` states (that a key's string is never replaced). Either route them through `fileAt()` so the invariant holds everywhere, or narrow the invariant's wording to what the code guarantees — prefer the former.
4. **Minor — three behaviour changes are filed under `### Added`** (`CHANGELOG.md:245-268`) when they are `Breaking` or `Fixed`. A consumer scanning `Added` for what is new will not see that something they relied on changed.
5. **Minor — a formally out-of-range pointer in `xsgetn` (`:108`)**, and a stale `<sstream>` include (`:16`).

Then: the presets the constraints name, `clang-format --check`, `ctest -L hygiene`, `mkdocs --strict`, rebase, push, watch Build and Portability, and append "Fix round 2" to `task-A10-report.md` with the RED for item 1. Update core-cpp#27's divergence table with whatever item 1 settles.
