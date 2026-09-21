# Task A10 — re-review of fix round 2 (rulings R58 and R59)

Base `1240035`, head `origin/master`. Read: the round's two commits (`4ec5878`, `f4cdb08`), the
current `src/core/platform/testing/InMemoryFileSystem.cpp` and `src/core/platform/FileSystem_test.cpp`,
the committed `CHANGELOG.md`, and core-cpp#27. The supplied diff file
`review-A10-fix2.diff` carries a diffstat for `CHANGELOG.md` (156 lines) but **no CHANGELOG hunks** —
its body holds only the two source files, so I regenerated the CHANGELOG diff with
`git diff 1240035..origin/master -- CHANGELOG.md`. Worth knowing before the next round's diff is cut.

One focused run, on the already-current `cl-debug` binary (built 22:17, after the last source edit at
22:15): the new case, 16 assertions, all passed on MSVC. Nothing else was run and nothing was mutated.

## Finding Verdicts

| # | Item | Verdict |
|---|---|---|
| 1 | Important — `unget()`/`putback()` set `badbit` | **ADDRESSED** — `pbackfail()` at `D:/core-cpp/src/core/platform/testing/InMemoryFileSystem.cpp:163-193`, with the slot delivered first in `xsgetn()` (`:112-121`), `underflow()` (`:136-137`) and `uflow()` (`:148`), and dropped by `seekoff()` (`:212`) and the read-write `xsputn()` (`:279`). It is not a documented divergence; it is a fix, and it reproduces libstdc++ `filebuf`'s algorithm case for case (see below). |
| 2 | Minor — the walk's narrow sort key | **ADDRESSED** — `InMemoryFileSystem.cpp:747-748`, `normalizePath(e.path)`. `normalizePath(path const&)` goes through `generic_u8string()`, so the key is the same UTF-8 byte string `pathFromKey()` was handed, and the sort order is byte-identical to the map's key order the walk's parents-before-children comment depends on. |
| 3 | Minor — `copyFile()`/`addFile()` detach an open stream | **ADDRESSED** — routed through `fileAt()`: `copyFile()` `:572-576`, `addFile()` `:819`, `createTempFile()` `:798`. See the ruling on `rename()` below. |
| 4 | Minor — three behaviour changes under `Added` | **ADDRESSED** — the three bullets are gone from `Added`; the lifetime change is one `Breaking` entry with a migration (`CHANGELOG.md:249-256`) and the two defects are under `Fixed` (`:396-407`). Nothing breaking is left under `Added`: what remains there is the vendor-sync script, the consumer smoke tests, the CompileCache re-sync and `NativeFileSystem`'s defaulted `RenameFunction` constructor, all additive. Two wording nits below. |
| 5 | Minor — out-of-range pointer in `xsgetn`, stale `<sstream>` | **ADDRESSED** — the copy is guarded at `:126-130` so `contents().data() + _position` is never formed for a zero-length copy; `<sstream>` gone, `<istream>`/`<optional>`/`<ostream>` added (`:11,14,15`), and all three are used. The new `if (n <= 0) return 0;` also removes a pre-existing hazard: a negative `n` used to cast to a huge `size_t` and copy `readable()` bytes. |
| R59 | Keep the model permissive; compare only the guaranteed cases; record the divergence in #27 | **ADDRESSED in code, test and issue — NOT in the CHANGELOG.** The case is rewritten correctly (`FileSystem_test.cpp:862-903`) and core-cpp#27 carries the new row naming libc++. But `CHANGELOG.md:410-412` still says "`std::ifstream` and `std::fstream` accept all of `unget()`, a `putback()` of the character the file holds, **and a `putback()` of one it does not**; the fake now answers as they do". That is the exact claim R59 overturned, left standing in the one document a consumer reads. `f4cdb08` touched only the test. |

## The Put-Back Paths

I walked `pbackfail()` against libstdc++'s `basic_filebuf::pbackfail` / `_M_create_pback` /
`_M_destroy_pback`, because that is the behaviour the class comment claims to reproduce. Notation:
file `"abcdef"`, `p` = `_position`, `S` = the `_pushedBack` slot. The slot conceptually occupies index
`p`, so the remaining stream is `S, contents[p+1], …` — which is why delivering it increments `p`.

| Path | Model | Native | Exact? |
|---|---|---|---|
| `unget()` after one read (`p=1`) | `p→0`, slot cleared, returns `not_eof` → next read `'a'` | `filebuf`: `gbump(-1)` out of the get area, `'a'` | yes |
| `unget()` at the start (`p=0`) | `:165` returns `eof` → `badbit` | at file offset 0, `eback()==gptr()` and `seekoff(-1,cur)` fails → `eof` → `badbit` | yes — both refuse |
| `unget()` after `seekg(3)`, nothing read | `p→2` → next read `'c'` | `seekoff(-1,cur)` then `underflow()` → `'c'` | yes |
| `putback(c)` where the file holds `c` | `holdsIt` true → `p--`, slot cleared, returns `ch` | satisfied out of the get area without reaching `pbackfail` | yes |
| `putback('X')` where it does not | `p--`, `S='X'`; next read yields `'X'` then `contents[p+1]` | `_M_create_pback()` saves `gptr` **before** the put-back and `_M_destroy_pback()` restores it `+1`, i.e. `'X'` then the same byte | yes — same arithmetic, arrived at differently |
| second put-back, different character, none read between | `:181-182` refuses, **without** moving `p` and keeping `S` | `filebuf` reaches the same refusal (`!__testpb` false) but has already done `gbump(-1)`/`seekoff(-1,cur)` and destroyed the pback | refusal matches; the position on the failure path is deliberately tidier, and the standard describes neither |
| second put-back of the byte the file holds at `p-1`, none read between | accepted, `p--`, the pending `S` is dropped | `filebuf` does exactly this too: `seekoff(-1,cur)` destroys the pback, `underflow()` matches `__i`, accepted | yes — including the dropped slot |
| put-back then `seek` (or `tellg`) | `seekoff()` `:212` clears the slot | `seekoff()` destroys the pback, losing the character | yes |
| put-back then write (`openReadWrite`) | `xsputn` writes at `position()`, i.e. over the put-back's index, then `discardPutback()` `:279` | `filebuf` requires a seek between input and output; unspecified | n/a — self-consistent |
| put-back then read past the end | `p=6`, `putback('X')` → `p=5`, `S='X'`; read yields `'X'`, then `readable()==0` → EOF | — | yes; `readable()` = `size - p` counts the slot as replacing `contents[p]`, so `showmanyc()` stays truthful |
| the file shrank under the stream, then `putback('X')` | `holdsIt`'s guard `_position - 1 < contents().size()` (`:177`) is false → slot path, no indexing | — | yes, no out-of-range read |
| **can a different character reach storage?** | **No.** `pbackfail` only assigns `_pushedBack`; the slot is written into the *caller's* buffer in `xsgetn` (`:116`) and returned by `underflow()`/`uflow()`. Nothing in the class writes it to `contents()`, and the read-write `xsputn` writes the caller's bytes and then discards the slot. `fileAfter == "abcdef"` in the case confirms it end to end. | | |

Position arithmetic is exact in every path above. Two consequences of the guard at `:165` worth stating:
`--_position` can never underflow, and `holdsIt`'s `_position - 1` is always ≥ 0 there.

**One divergence I did not find named anywhere.** `unget()` *after the pushed-back character has been
read*: the model returns the file's own byte (`p→p-1`, slot already cleared), where libstdc++'s
`filebuf` still has `gptr()` inside the one-character pback area and returns `'X'` a second time. It is
the same permissiveness R59 sanctioned and it costs a test nothing, but core-cpp#27's row is worded
narrowly as "`putback()` of a character the file does not hold"; widening it to "put-back handling
generally" would make the table airtight.

**Is the rewritten case still load-bearing?** Yes. Against the native backend it now asserts only
`unget()` and `putback()` of the character just read — both satisfied out of a real `filebuf`'s get
area, so every implementation answers alike — plus that neither writes to the file; and it states the
native answers absolutely (`:872-876`) as well as comparing, so two backends wrong alike cannot agree
and pass. Regression coverage, checked case by case: remove `pbackfail()` and `:886`, `:888` and
`:902` go red; forget the `--_position` and `:887` goes red (`'b'` where native says `'a'`); write the
slot into storage and `:890` goes red; fail to deliver the slot and `:903` goes red. It has not been
weakened into always passing.

What the case does **not** reach, and did not before either: the `p==0` refusal, the after-seek
unget, the second put-back, the put-back-then-write, and `openReadWrite()` at all — the script only
uses `openRead()`. The `p==0` refusal is the one I would add an assertion for: dropping the `:165`
guard is silent (no out-of-range access, `readable()` just goes to 0), so nothing would notice.

## New Breakage in the Fix Diff

No Critical or Important breakage. The stream-lifetime guarantees the earlier rounds established are
intact and in two places stronger: `copyFile()`, `addFile()` and `createTempFile()` now assign through
the shared string instead of replacing the map's `shared_ptr`, so `fileAt()`'s invariant holds at every
writer that goes through it, and `copyFile()` onto an open destination really does overwrite in place
(`*fileAt(dstKey) = *it->second`, `:575`). `_files` is a `std::map` (`InMemoryFileSystem.hpp:163`), so
the `fileAt(dstKey)` insertion inside that statement cannot invalidate `it` — worth stating because the
same line over an `unordered_map` would be a use-after-invalidate. `remove()`/`removeAll()` still only
erase map entries, so open streams keep the content alive.

**The `rename()` ruling: the implementer is right.** `:588` and `:607` still do
`_files[dst] = std::move(_files[src])`. That moves the *source's* string object, so a stream open on
the source follows the rename (identity preserved), and it replaces the *destination's* entry, so a
stream open on the destination keeps reading the content it opened — which is precisely POSIX
`rename()` unlinking the destination while an open descriptor holds it. Routing these through
`fileAt()` would be wrong: it would make the destination's readers see the source's bytes appear under
them, which no filesystem does.

Minor and nit, in order of how much they matter:

- **Minor — `CHANGELOG.md:408-412`** states the claim R59 overturned (see the R59 row above). It also
  contradicts core-cpp#27, which now says libc++ refuses. The fix is a sentence: the fake accepts
  `unget()` and a `putback()` of the character the file holds, as every implementation does, and stays
  permissive for a different character, which `[streambuf.virt.pback]` leaves open and libc++ refuses.
- **Minor — `copyFile()` has no test at all**, in either backend: `grep -rn copyFile src/ tests/` finds
  only the declarations and the two implementations. The CHANGELOG's `Breaking` entry
  (`CHANGELOG.md:252`) and core-cpp#27 both now assert "`copyFile()` onto an open destination overwrites
  in place" as modelled behaviour. It is true by inspection, and it is a claim no gate reports on.
- **Nit — `InMemoryFileSystem.cpp:354-357`**, `fileAt()`'s comment ("Never replaces the string a key
  already has") reads as a statement about the whole class, and `rename()` at `:588`/`:607` deliberately
  does replace one. The reasoning is in the report, not at the call site; one line at `rename()` would
  stop the next reader "fixing" it.
- **Nit — `CHANGELOG.md:396-402`** lists "the walk's sort key" among the sites that "turned a key back
  into a path". The sort key converts a path *out* to a string; it is the same key-space bug from the
  other side, but not that sentence's category. The same entry moved the count from "ten sites" to
  "twelve" while the round converted one site, and `pathFromKey()` has 16 call sites at both `1240035`
  and head, so one of the two numbers is wrong.
- **Nit — `FileSystem_test.cpp:112,118,124`**: `ch` is not reset between reads, so a read that does
  nothing (a bad stream) leaves the previous character in the `after*` field. The `*Good` flags catch
  every case where that could hide something, so it costs nothing today.

## Verdict

**Fix round:** All findings addressed, no new Critical/Important breakage — with one Minor left open
from R59's record-keeping: `CHANGELOG.md:410-412` still states that `std::ifstream`/`std::fstream`
accept a `putback()` of a character the file does not hold and that the fake "answers as they do",
which is the claim R59 overturned and which the issue, the test and the code have all moved past.
