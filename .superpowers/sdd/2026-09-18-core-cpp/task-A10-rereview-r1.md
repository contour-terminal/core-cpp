# Task A10 — re-review of fix round 1 (R53, R54, R56, rules addition)

Base `3cdb0ce`, head `origin/master`. Read the diff
(`D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/review-A10-fix1.diff`) plus the files at head;
verified the streambuf behaviour with a standalone probe rather than through the suite. Nothing in
the working tree, index, HEAD or any branch was touched.

### Finding Verdicts

**R53 — the rename primitive is injected and the two-hop recase is driven: ADDRESSED.**
`src/core/platform/NativeFileSystem.hpp:14-19` adds `RenameFunction` (a `std::function` alias) and
`nativeRename()`; `:37` the constructor `explicit NativeFileSystem(RenameFunction rename =
nativeRename())`; `:94` the one new private member. Checked each constraint the ruling set:

- **No new virtual.** Nothing was added to `FileSystem`, and the diff does not touch
  `FileSystem.hpp` at all. `NativeFileSystem` is still `final`; the seam is a member, not a hook.
- **`instance()` and `NativeFileSystem {}` still compile.** `NativeFileSystem.cpp:42-46` keeps
  `static NativeFileSystem instance;`. A constructor whose only parameter has a default argument
  *is* a default constructor, and `explicit` does not bar empty-brace direct-list-initialization,
  so `NativeFileSystem {}` still value-initializes. `NativeFileSystem::instance()` is the only
  in-tree construction site outside the new tests (grepped `src/`).
- **The default really is today's behaviour.** `NativeFileSystem.cpp:30-35` — `nativeRename()`
  returns a lambda whose body is `fs::rename(from, to, ec)`, and `:33` is now the only direct call
  to `std::filesystem::rename` left in the file. `rename()` calls `_rename` at `:353` and passes it
  down at `:367`.
- **The three cases drive the production path, not a parallel one.**
  `FileSystem_test.cpp:592`, `:613`, `:638` each construct a real `NativeFileSystem { script }`,
  write a real file into a `ScopedTempDir`, and call `backend.rename(...)`. Control goes through
  the production `isCaseOnlyRename()` → `renameViaTemporary()` → the rollback, and the
  `.recase-N` candidate is the production function's own invention (the test recovers it from the
  script at `:648`, it does not assume the spelling). `callCount() == 3` at `:602` pins that the
  refused attempt plus exactly two hops ran — not a shortcut.
- **The middle case would fail if finding 13's fix were reverted.** Confirmed by reading, not by
  taking the report's word: the script is `{ file_exists, forReal, permission_denied }`, so the
  direct attempt reports `file_exists` and the second hop `permission_denied`. `:621-622` assert
  `contains(secondHopFailed->message())` **and** `CHECK_FALSE(contains(refusedAsSameEntry->
  message()))`. The pre-fix code reported the first attempt's `ec`, so both assertions invert.
  The third case additionally asserts the entry really is under the temporary
  (`:652-656` list the directory and read the file back), so the message is checked against the
  world and not only against itself.

**R54 item 1 — one spelling out of the key space: ADDRESSED, with one site still outside it.**
`InMemoryFileSystem.cpp:35-39` is `pathFromKey()`, over `std::u8string_view`, mirroring
`normalizePath()`'s own `reinterpret_cast` idiom in `PathUtils.hpp:64-68`. All ten listed sites are
routed: `:269` `ensureParentDirectories`, `:299` `resolveSymlinks`, `:342` `weaklyCanonical`, `:429`
`createDirectory`'s parent check, `:439` `createDirectories`, `:576`/`:592`/`:610` `listDirectory`,
`:649`/`:656`/`:664` `walkDirectoryRecursive`, `:722-723` `createTempFile`, `:733` `setCurrentPath`,
`:757` `addDirectory`. The two extra sites are real and are fixed: `:770` `addSymlink` now stores
`normalizePath(target)` instead of `target.string()` (the narrowing on the way *in*), and `:244`
turns a `FileEntry::content` symlink target through `pathFromKey()`. Every `_directories.insert()`
now goes through `normalizePath()`, so the way in is uniform too. After the change,
`InMemoryFileSystem.cpp` contains no `std::filesystem::path(<narrow>)` construction at all.

One conversion out of a path still bypasses the discipline — see New Breakage, `:671`.

**R54 item 2 — real ownership, not a documented contract: ADDRESSED for the three cases named.**
`InMemoryFileSystem.hpp:145` makes `_files` a `std::map<std::string, std::shared_ptr<std::string>>`;
`:279-287` `fileAt()` creates storage and never replaces a key's string; `MemoryOutputBuf` (`:47`),
`MemoryReadBuf` (`:99`) and `MemoryIOBuf` (`:191`) all hold the `shared_ptr` and no pointer into the
string. The three cases the ruling required exist and are the right shape:
`FileSystem_test.cpp:707` (write through the filesystem under an open stream, past the old
capacity so a reallocation is forced), `:726` (remove), `:749` (rename, and it asserts the write
lands in the file that now carries the new name and that the size stayed 64 — overwrite, not
append). The implementer took the preferred option rather than the documented-contract one.

Not every writer goes through `fileAt()`, though; see the ownership walk below.

**R56 — `openRead()` shares too: ADDRESSED, and revertible alone.**
`InMemoryFileSystem.cpp:391-401` hands `MemoryIStream` the `shared_ptr` itself, and `MemoryIStream`
(`:175`) now uses `MemoryReadBuf`; the copying `MemoryInputBuf` is gone. It is its own commit,
`9f7925c`, touching `InMemoryFileSystem.cpp`, `FileSystem_test.cpp`, `CHANGELOG.md` and
`provenance.md` and nothing else — at `ab623bc` `openRead()` still snapshotted
(`make_unique<MemoryIStream>(*it->second)`), so reverting `9f7925c` lands back on a self-consistent
state.

The case does fail if snapshot semantics are restored, and for two independent reasons rather than
one: with a private copy of the 64-byte file, `FileSystem_test.cpp:784` reads `'a'` where it checks
`'b'`, and `:788` `seekg(8192)` is refused because `seekoff` clamps to a size of 64. The case also
covers the append-under-an-open-reader (`:787-792`) and the outlives-`remove()` path (`:795-799`).

**The rules addition — `.agent/rules/testing.md`: ADDRESSED.**
`.agent/rules/testing.md:192-213`, "Showing a red by swapping files is safe for behaviour, not for
layout". It states the mechanism (a mixed-object link is silent, then corrupts the heap), the
aggravating factor (a compiler cache rewrites the object with a fresh timestamp regardless), the
remedy (delete the module's directory under `out/build/<preset>/`, or `--clean-first`), and — the
part the ruling actually asked for — the **tell**, all three elements of it: "a failure that only
one toolchain sees, in code the diff did not touch, and that moves or vanishes when tests are run
individually. Suspect the build before the code." A future agent in that situation would recognise
it from the symptom without already knowing the cause, which is the test of the rule.

The file is defensible: the trigger is a test-methodology action ("prove the red"), and it
cross-links `build-and-toolchain.md` for the stale-object half. Observation only, not a finding:
`AGENT.md`'s tripwire bullet for `rules/testing.md` was not extended, so the rule is invisible to a
session that reads only `AGENT.md` — the bullets are explicitly "tripwires, not summaries", so this
is a judgement call rather than an omission.

### The Stream Ownership

**Who owns what.** The file's bytes live in one `std::string` owned by a `shared_ptr`. The map
holds one reference per name; every stream handed out by `openRead()`, `openWrite()` or
`openReadWrite()` holds its own. The name and the bytes are therefore separable, which is the whole
point: `remove()` (`:455`) and `removeAll()` (`:477`) erase the map entry and the string survives
for whoever holds it; `rename()` (`:511`, `:530`) *moves* the `shared_ptr` to the new key, so the
same string object carries the new name and an open stream follows it. That is POSIX's model, and
it is now modelled rather than approximated.

**Can a live stream still see a reallocation?** No — and I checked this against the buffer rather
than against the tests. `MemoryReadBuf` never calls `setg`, so `eback() == gptr() == egptr() ==
nullptr` for the whole life of the buffer. Walking every inherited entry point that could read
bytes:

- `sgetc()` → `gptr() < egptr()` is false → `underflow()` (`:113`), which indexes `contents()` at
  the current position. Live.
- `sbumpc()`/`snextc()` → `gptr() == nullptr` → `uflow()` (`:120`). Live.
- `sgetn()` → `xsgetn()` (`:105`), overridden. Live.
- `in_avail()` → `showmanyc()` (`:127`), overridden, recomputed from `contents().size()`.
- `seekoff`/`seekpos` (`:129`, `:147`) read `contents().size()` each call and only move an integer.
- `sungetc()`/`sputbackc()` → `gptr() == eback()` (both null) → `pbackfail()`, which is **not**
  overridden, so the default returns `eof()` and touches nothing.

So there is no path by which a cached pointer is dereferenced, because there is no cached pointer.
`readable()` (`:165`) is recomputed on every call and guards the shrink direction, so a file that
gets smaller under a reader yields EOF rather than a read past the end. The write half
(`MemoryIOBuf::xsputn`, `:193-203`) reads `position()` before the `resize()` that can reallocate and
indexes through `contents()` afterwards, so it has no stale pointer either. Position handling for a
growing file is correct: nothing is cached, `seekoff(off, end)` anchors on the current size, and the
append case is exercised at `FileSystem_test.cpp:787-792`.

**What can still replace the string a live stream holds.** Three writers bypass `fileAt()` and
assign a *new* `shared_ptr` into the map:

- `copyFile()` — `InMemoryFileSystem.cpp:498`, `_files[dstKey] = std::make_shared<std::string>(...)`.
- `addFile()` — `:742`, same shape.
- `createTempFile()` — `:721`, same shape (the key comes from `_tempCounter`, so a collision needs a
  fixture to have pre-created `/tmp/<prefix>_1` *and* hold a stream on it; effectively unreachable).

None of these is a use-after-free any more — the stream keeps its own reference, so the memory stays
valid. What happens instead is a silent **detach**: the stream keeps reading and writing the
orphaned string while the name now points at a different one. `copyFile()` is the one with a real
native counterpart to disagree with: `std::filesystem::copy_file` with `overwrite_existing` truncates
the destination in place, so a descriptor open on it sees the new bytes; the fake gives it the old
bytes and drops its writes on the floor. `addFile()` is a fixture helper whose natural equivalent is
`writeFile()`, which now *does* write through — so the two disagree with each other.

This is narrow, but it makes `fileAt()`'s own doc comment untrue as written: `InMemoryFileSystem.hpp:
143` says "The one place a file's storage is made", and `.cpp:281` says "Never replaces the string a
key already has" — an invariant a future reader will rely on, stated where three violations of it sit
in the same file. Routing `copyFile()` and `addFile()` through `*fileAt(dstKey) = ...` would make the
sentence true and cost two lines.

`rename()` over an *existing* destination (`:511`) replaces the destination's `shared_ptr` too, but
that one is correct: POSIX `rename()` unlinks the destination, and a descriptor open on it keeps the
old contents. No finding.

### New Breakage in the Fix Diff

**Important — `unget()` and `putback()` now fail on every stream the fake hands out, where the
native backend succeeds.** `src/core/platform/testing/InMemoryFileSystem.cpp:99-172`
(`MemoryReadBuf` leaves the get area empty and does not override `pbackfail()`). Introduced by
`ab623bc` for `openReadWrite()` and by `9f7925c` for `openRead()`; before this round both had a get
area, so a putback after a successful read worked.

Verified by compiling `MemoryReadBuf` verbatim into a standalone probe (g++ 14.3, `-std=c++23
-fsanitize=address,undefined`) alongside the real thing:

```
read c='h' good=1
after unget: good=0 bad=1 fail=1     <- MemoryIStream
after putback: good=0 bad=1          <- MemoryIStream
stringstream after unget: good=1 bad=0
ifstream unget: good=1 bad=0         <- NativeFileSystem::openRead's actual type
fstream  unget: good=1 bad=0         <- NativeFileSystem::openReadWrite's actual type
```

`istream::unget()` sets **badbit**, not failbit, so the stream is poisoned rather than merely
unsuccessful, and nothing in the fake's API reports why. This is precisely the divergence class
`9f7925c`'s own commit message calls "a divergence a test author cannot see, which is what makes a
fake untrustworthy (core-cpp#27)": a consumer's hand-rolled parser that uses one character of
lookahead passes against `NativeFileSystem` and fails against the fake. Nothing in core-cpp itself
uses `unget`/`putback` (grepped `src/`), so no test catches it.

`getline`, `operator>>`, `peek`, `ignore`, `readsome`, `seekg`/`tellg` and `ostream << rdbuf()` are
all unaffected — I exercised them in the same probe and they behave as before. The fix is about six
lines: override `pbackfail(int_type c)` to decrement `_position` when it is non-zero and `c` is
`eof()` or equals `contents()[_position - 1]`. Failing that, the divergence belongs in core-cpp#27's
list and in the CHANGELOG, neither of which mentions it.

**Minor — the walk's sort key is still narrowed out of the key space.**
`src/core/platform/testing/InMemoryFileSystem.cpp:671`:
`[](DirectoryEntry const& e) { return e.path.generic_string(); }`. This is the one remaining
conversion out of a path that does not go through `normalizePath()`, and it is exactly the narrowing
`pathFromKey()` was introduced to eliminate — on Windows it reads back through the ANSI code page.
The line is pre-existing context, not a line this round wrote, but the round's stated goal was "one
way in, one way out", and this is the exception. Impact is bounded: it only orders the results, so
the yielded paths stay correct, but two non-ASCII names that both narrow to `?` sort against each
other arbitrarily (`std::ranges::sort` is not stable), and on an ACP where MSVC's conversion throws
rather than substitutes, the whole walk throws. `normalizePath(e.path)` is the drop-in.

**Minor — `copyFile()` and `addFile()` detach a stream open on their target**, and `fileAt()`'s
"the one place a file's storage is made" is therefore not true.
`src/core/platform/testing/InMemoryFileSystem.cpp:498` and `:742`, against the claims at
`InMemoryFileSystem.hpp:143` and `.cpp:281`. Detailed above; memory-safe, silently wrong,
two lines to fix.

**Minor — the CHANGELOG files two behaviour changes under `### Added`.**
`CHANGELOG.md:245-268` — the key-space fix, the stream-ownership change and the `openRead()`
sharing all sit under `Added` (which starts at `:12`), above `### Breaking` at `:269`. The text is
accurate and unusually candid, but `openRead()` no longer returning a snapshot is a semantics change
a consumer's existing tests can notice — the implementer's own report says so ("it alters observable
behaviour for every consumer whose tests read through this fake") — and the use-after-free repair is
a `Fixed`. A consumer reading `### Breaking` for its migration list sees neither. `library-hygiene.md`
puts 0.x breaks under `Breaking`.

**Minor (formal) — `xsgetn` forms an out-of-range pointer when the file shrank below the reader's
position.** `src/core/platform/testing/InMemoryFileSystem.cpp:108`:
`std::copy_n(contents().data() + _position, count, s)`. `seekoff` clamps `_position` to the size at
seek time, but a later `writeFile()` can truncate the string under the stream, leaving
`_position > size()`. `count` is then 0 so nothing is read, but `data() + _position` is itself UB
per `[expr.add]`. Benign in practice and not flagged by ASan/UBSan in my probe (`gcount=0 eof=1`,
clean). Guard with `if (count == 0) return 0;`.

**Nit — `<sstream>` is now an unused include.** `src/core/platform/testing/InMemoryFileSystem.cpp:16`;
the last user was `MemoryInputBuf`, removed in `9f7925c`.

**Nit — an empty `RenameFunction` is constructible and throws on use.**
`src/core/platform/NativeFileSystem.hpp:37` — `NativeFileSystem { RenameFunction {} }` compiles and
then `std::bad_function_call`s out of `rename()`. "A constructed object is usable"
(`design-principles.md`); a one-line `if (!_rename) _rename = nativeRename();` in the constructor
body closes it. Caller error, so listed as a nit only.

**Nothing in the 15 approved findings regressed.** I re-checked the ones this diff could plausibly
have undone: finding 1's read-write semantics still hold under the rewritten buffer —
`xsputn` overwrites in place from `position()` and extends only past the end
(`InMemoryFileSystem.cpp:193-203`), and the case at `FileSystem_test.cpp:346-375` still walks the
write-past-the-end → `gcount() == 0` → `seekg(0)` → `'x'` path it was written for, which the new
`readable()`-based `xsgetn` answers identically. Finding 13's assertions are strengthened, not
replaced: the case-sensitive-host case at `:456` is untouched and the scripted cases run beside it.
Finding 14's `WriteMode`/`OverwritePolicy` signatures are unchanged. Naming conforms —
`.clang-tidy:194` sets `ConstantCase: camelBack`, so `refusedAsSameEntry` and its neighbours at
`FileSystem_test.cpp:580-588` are correct, not violations.

### Verdict

**Fix round:** Findings remain open — one Important:

- The `unget()`/`putback()` regression (`src/core/platform/testing/InMemoryFileSystem.cpp:99-172`):
  a *new* divergence from `NativeFileSystem`, introduced by this round, in the direction the round
  set out to close, recorded neither in the CHANGELOG nor in core-cpp#27.

R53, R54 item 1, R54 item 2, R56 and the rules addition are each ADDRESSED on their own terms; the
ownership rewrite is sound and I could not find a path that invalidates a live stream. The
remainder above is Minor and can ride with the next round.
