# core-cpp#31: the header self-check

## The measurement, which was the design question

One translation unit per public header, one object library per header-publishing target. Measured
with fastcache-cc running as it normally does.

| leg | headers | cold | warm | outcome |
|---|---|---|---|---|
| `clang-debug` (WSL, clang 22.1.2) | 159 | ~12.5 s | 4.48 s | 157 compiled, 2 failed |
| `cl-debug` (MSVC, VS 18) | 158 | 11.01 s | 1.26 s | 156 compiled, 2 failed |
| pristine `HEAD`, `clang-debug` | 155 | 12.53 s | — | **155 compiled, 0 failed** |

Against a full build of the tree (346 targets, 2 m 51 s on the same WSL host) the thorough shape
costs about **7 % cold and under 3 % warm**. No trade is needed and none was made: every public
header is compiled, and the check claims exactly that.

The counts differ per leg — 159, 158, 155 — and that is the check working rather than a defect.
The list is each target's `FILE_SET HEADERS`, so a header a platform excludes is excluded here
too; the 155 is pristine `HEAD` without the net lane's four in-flight headers.

## The shape, and why

One generated `.cpp` per header, grouped into one `OBJECT` library per header-publishing target,
built as part of the normal build.

- **The check IS the build.** There is nothing to run afterwards: if the object libraries compile,
  every public header stood alone; if one does not, the compiler names the header and what it
  needed, which is a better diagnostic than a harness would produce.
- **Per target, not one library for everything.** Each unit is compiled with its own target's usage
  requirements, so a header is asked to stand up in the environment a consumer linking *that*
  target gets, not in the union of every module's include paths.
- **The list comes from `FILE_SET HEADERS`, never a glob.** A header added tomorrow is covered
  without anyone remembering; one excluded on a platform is excluded here. `ThreadPoolExecutor.hpp`
  `#error`s under single-threaded Emscripten by design and is in no file set there, so it is not
  compiled there — automatic, not special-cased.
- **Private headers are out of scope** because they are in no file set: `detail/`, `posix/`,
  `windows/` and the rest may assume their includer.
- **Per-leg, not `tree-level`.** Whether a header stands alone is a question for each toolchain.

`core_cpp_add_header_self_check()` refuses an empty target list rather than reporting the same
green a passing run would — a check over nothing is the failure mode this repository keeps finding.

## The self-test, and its RED

`tests/cmake/check-header-self-check-selftest.cmake` builds two fixture projects: one whose headers
all stand alone (must build), and one adding `NeedsNeighbour.hpp`, which names a type it does not
include (must fail, naming the header).

`HEAD` has no such module, so the RED was taken against a **stub** whose
`core_cpp_add_header_self_check()` does nothing — the state of the tree before this task.
**Predicted 1 failure; got exactly 1:**

```
NeedsNeighbour.hpp names a type it does not include and was NOT refused -- the check
compiled it with something included first, or generated no translation unit for it
```

The clean case still passed under the stub, which is what makes the violating case discriminating
rather than merely failing: a generator that refused everything would also have "caught" it.

Registered as `core-cpp.header-self-check-selftest`, labels `core-cpp;hygiene`, **no `tree-level`**,
`TIMEOUT 120` from measurement (7.2 s WSL clang, 1.7 s MSVC).

## The defects the first run found — both the net lane's, neither fixed

Two, and they are the same defect in two places:

| header | module | diagnosis |
|---|---|---|
| `src/core/net/PlatformLoop.hpp:66` | net (Task B4) | `member '_backend' found in multiple base classes of different types` |
| `src/core/net/testing/TestLoop.hpp:61` | net (Task B4) | the same |

`detail::OwnedBackend` declares `std::unique_ptr<IoBackend> _backend` and `EventLoop` declares
`IoBackend& _backend`. `PlatformLoop` derives from both, so the `*_backend` in its mem-initialiser
is ambiguous — name lookup finds both before access control considers that `EventLoop`'s is
private. Qualifying it (`*OwnedBackend::_backend`) is the likely fix, but it is B4's call and B4's
file.

Two things worth stating about them:

- **Both headers are untracked**, so the defect is in-flight work and not on master. The check is
  green at pristine `HEAD` (155/155), which is why landing it does not redden the build.
- **Nothing in the tree includes either header.** They are public, broken, and invisible to the
  entire build — the first consumer to write `#include <core/net/PlatformLoop.hpp>` would have been
  the one to discover it. That is precisely the class of defect the rule was stated for and nothing
  enforced, found nine seconds into the first run.

## Other reds in the tree, none of them this task's

- `core-cpp.cmake-hygiene`: five `src/core/net/` files with no provenance row — the net lane's
  untracked work. Checked each; none names a file of mine.
- `core-cpp.migrate-renames`: three rows for `EventLoop::submit`, `PlatformLoop` and
  `testing::TestLoop` whose own message says *"it waits on task B4"*.

## Then

`mkdocs build --strict` passes (`CHANGELOG.md` and `.agent/rules/cpp-guidelines.md` are both
rendered by snippet). `ctest -L hygiene` is 15 tests, 13 passing, the two above failing for other
lanes' reasons. No Python changed, so `ruff` has nothing to say.

CHANGELOG under **Added** rather than a gate's usual silence: a consumer does not observe the
check, but they do observe the guarantee it makes true — that a public header can be included
first, which is what a migration does.
