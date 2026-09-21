# Task B1: fix round 2 (Ruling R106)

Fix round 1 is **verified**. C1, I2, I3, I4 and all six Minors are ADDRESSED — Minor 1 correctly
argued as already-delivered — and **both of your self-corrections were confirmed by compiling
rather than reading**, which is what you asked for and what makes them evidence. The reviewer
temporarily deleted `JoinAwaiter`'s move in a private worktree and reproduced your exact claim:
`return whenAll(...)` compiles through copy elision while `auto a = whenAll(...); return a;` does
not. Your corrected comment says precisely the right thing.

**C1 held under everything it was thrown at.** The reviewer traced
`AbandonState`/`AbandonClaim`/`DetachedTask::abandonOnce`/`ThreadPoolExecutor`'s
`std::deque<detail::Parked>` by hand, ran the promoted arity-2 cases under ASan+UBSan, verified
your undisclosed second half — `submit(ParkedWork)` dropping the claim — **directly in the diff**,
and grepped the rest of `src/core/async` for the same shape. **None left.** That machinery is done.

---

## Ruling R106 — a new Critical, and it is not yours in the sense that matters

**A completed `whenAll`/`whenAny` join over a `ThreadPoolExecutor` can free a sibling child's
coroutine frame while another worker thread is still resuming it.**

**Reproduced on completely unmodified `ceb471d`** — no source changes, the same binary run
repeatedly: **1 failure in 60 ASan runs, 1 in 30 in an earlier ASan sample, 1 in 40 under TSan.**
Roughly 2–3%, in `ThreadPoolExecutor_test.cpp`'s *"A join whose children finish on a pool completes
exactly once"* — **the case I2 itself added.**

Two independently symbolized crashes:

- **ASan**: heap-use-after-free **writing** to a freed `WhenAllPolicy::State` inside the start-phase
  guard release, in `Join.hpp`.
- **TSan**: heap-use-after-free where `Parked::resume()` (`ParkedWork.hpp:306`) **reads a coroutine
  frame another thread is concurrently freeing** via `~JoinAwaiter()`'s unconditional
  `~vector<JoinRunner>()` teardown.

**Root cause as far as the reviewer traced it:** whichever thread's decrement brings `remaining` to
zero **unconditionally destroys every runner in `_runners`**, with no guard against another
still-queued sibling child — submitted to the same pool — being mid-resume on a different worker
thread.

**This is the other half of R98.** I2 closed the *counter* race; the counter reaching zero
correctly is not the same fact as every runner being safe to destroy. A join may span threads was
the premise; the counter was only one of the two things that premise touches.

### Scope — and what it is NOT

**Scoped to `Join.hpp`'s `~JoinAwaiter()` / `_runners` teardown and the cross-thread pool case.**
Explicitly **not** to C1's `ParkedWork`/`AbandonClaim` machinery, which held under direct attack.
Do not repair this by reaching into the refcount; the reviewer went looking for that shape and it
is not there.

### Two things the reviewer could not close, which are yours

1. **The minimal deterministic interleaving was not pinned** — it ran out of budget. A ~2–3%
   probabilistic case is a poor regression test: **find the interleaving and make it deterministic
   if you can**, and if you cannot, say what you tried and label the case honestly rather than
   shipping a test that passes 97% of the time by luck.
2. **Whether `whenAny`'s cancellation path has the identical exposure is unknown** — only `whenAll`
   was reproduced. **Answer it explicitly**, in either direction. An unexamined half is how C1's
   second half survived the first round.

### The field observation that agrees with it

B3 saw `core-cpp.async` **SEGFAULT once in four full-suite runs** on a clean `origin/master` at
`d44e2d6` — which **contains `ceb471d`** — under four concurrent preset builds, with none of its
files in that binary. It did not reproduce in three further runs and B3 reported it rather than
filing it as flake, on the reasoning that *a segfault appearing only under parallel load in a
threaded binary is the shape a real race has, and three green re-runs is what a real race also
looks like.*

**B3 did not say which case crashed, so this is consistency, not identity** — but an
unreproducible field crash and a deliberate 2–3% reproduction landing in the same binary on the
same commit is the strongest corroboration either would get alone. Treat the SEGFAULT question
from my earlier message as **answered by this** unless you find otherwise.

---

## Then

- **RED before the fix**, and the RED here is a rate, not a pass/fail. Say how many runs you expect
  to need and under which sanitizer **before** you run, and report the observed rate both ways.
- Both binaries; `clang-debug`, `gcc-release`, ASan, TSan, `cl-debug`, `clangcl-release`,
  Emscripten. **TSan and ASan are the load-bearing ones and repetition is the instrument** — a
  single green run is not evidence against a 2% defect, which is the whole lesson of this finding.
- `python scripts/clang-format.py --check <paths>`, the `clang-tidy` preset, `ctest -L hygiene`,
  `mkdocs build --strict`.
- A CHANGELOG entry: this is a real use-after-free in shipped-shaped code, even though nothing has
  shipped.
- **Land the parked Windows `SKIP` in the same round** — the re-review has reported, so its hold is
  lifted. Re-run the four toolchains first, as you said you would: *"it applied cleanly" is not the
  same claim as "it still does what I measured."*
- Append "Fix round 2" to `task-B1-report.md` with the RED rate, the GREEN, and your answer on
  `whenAny`.

## One correction of mine, for the record

My dispatch gave the range as `ac0ff76..ceb471d`, which **excludes `ac0ff76`** — the actual C1 fix
commit. The reviewer reviewed it anyway and flagged the off-by-one. That is my error and the same
class as every other range I have written tonight; I mention it so the report's range is right.
