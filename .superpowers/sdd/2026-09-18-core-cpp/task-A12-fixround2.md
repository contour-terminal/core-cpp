# Task A12: fix round 2 (Ruling R70)

The re-review (`task-A12-rereview1.md`) verdicts items 1, 2 and 4 **ADDRESSED** — including the
part I asked it to reason for itself: your sentinel is sound, because `DelayAwaiter::await_resume`
throws on a stopped token and `WhenAny` requests the child stop *before* the loser resumes, so
`timedOut` is unreachable on the cancelled path, and nothing swallows the cancellation between the
timer and its flag the way `AcceptLoop` swallows it between `accept()` and its caller.

Item 3 is **PARTIALLY ADDRESSED**: the five are fixed correctly, and its sweep found a sixth.
That is this round.

## Important

1. **`src/core/net/posix/UnixSocket_test.cpp:121-122` — the sixth give-up site.**
   `connectAndProbe` `co_return`s on a failed write, with the comment "the server already accepted;
   its arm sees this socket close and finishes". Its sibling `echoOnce` (`:85-101`) is a **draining
   loop**: the closed socket makes its read return 0, it takes `continue` rather than `co_return`,
   and goes straight back into `accept()` with `*served` still false. That is a hang, not a
   failure — the shape this round was sent to close.
   - You wrote the correct pairing 300 lines away: `unixProbe` in `Socket_test.cpp:416-420` closes
     the listener on exactly this branch, because it faces the same draining-loop sibling. Do the
     same here.
   - `50c3c2d`'s message claims "every early return that remains ... each now says so". The
     comment you added at this site falsifies it. Say so in the new commit rather than quietly
     fixing it: the claim is what made the site look swept.
   - The lesson for the report, which is the part that generalises: **a sibling's shape decides
     whether an early return is safe.** A sibling that `co_return`s on EOF finishes on its own; a
     sibling that `continue`s on EOF does not. The question is not "does my peer observe my
     close" but "does observing my close end my peer's flow".

## Minor

2. `src/core/net/Socket_test.cpp:464` — `unixEcho`'s client discards its write result, so a failed
   write parks both arms in `read()`. Thinner reachability than item 1, same hang class. Fix it or
   say why the reachability argument holds; do not leave it unexamined.
3. `cmake/CoreCppTargets.cmake` — the 300-second comment names `tests/` as the only registration
   this default does not reach. `core-cpp.async-link-smoke` (`src/core/async/CMakeLists.txt:100`)
   is also a bare `add_test`. Correct the claim. **Do not edit `src/core/async/CMakeLists.txt`** —
   another lane owns it and is mid-task; name the gap in the comment and I will route the bound to
   them.
4. `src/core/net/Socket_test.cpp:252` says "three orders of magnitude"; the reviewer measured about
   two (0.147 s for both sections against a 10 s budget). Say the measured number.
5. `task-A12-report.md:275-278` still says the default timeout is "deliberately not project-wide",
   which `a13070c` contradicts. Correct it. `a13070c` is pushed and Build `35538763899` is running
   on it — watch that to green, since neither the commit nor the default is covered by the runs
   your report cites.

## Then

- The presets the constraints name on Windows and WSL; `python scripts/clang-format.py --check` on
  what you touched, `ctest -L hygiene`.
- **Two hygiene failures in the tree are not yours**: `core-cpp.cmake-hygiene` fails on two
  async-lane provenance rows (`DetachedTask.hpp`, `ParkedWork.hpp`) and `core-cpp.tui` fails on
  `clang-debug`. Both are other lanes' live, uncommitted work. Do not fix them, and do not let
  them stop you reporting your own result.
- Other lanes are in `src/core/async/`, `src/core/tui/`, `tools/migrate/` and `src/core/net/
  NetError.{hpp,_test.cpp}` — that last one is the `net_types` merge, so leave `NetError.hpp` and
  `NetError_test.cpp` alone even though they are in your module.
- Append "Fix round 2" to `task-A12-report.md` with RED/GREEN for item 1 and the sibling-shape
  lesson.
