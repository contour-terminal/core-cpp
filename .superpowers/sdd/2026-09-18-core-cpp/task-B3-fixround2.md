# Task B3: fix round 2 (Rulings R101–R103)

Spec compliance **PASS**, three undeclared divergences all additive or defensible. Task quality
**Approved with changes**: 2 Important, 6 Minor, nothing Critical. Read `task-B3-review.md` in full.

The reviewer cleared the things I most wanted attacked, by running them: **Rule 1 is structurally
impossible to break** rather than merely untested (`detail/ReadyBatch.hpp:129-132` is the only call
site in the module that invokes a `ReadinessCallback`, it raises the guard, and the loop's only
resume site asserts it is down — verified by grep across every backend including `ScriptedBackend`);
the `Interest::None` fix is the right fix, with every exact-count assertion in the parity suite
swept for the same carelessness; `HostDrivenBackend`'s four dispatch requirements all have cases on
every OS including Emscripten; and the deletions are clean. It would also uphold the `slot` ruling
independently. That is the part of this task that is done.

**`870d12b` is outside the review's range** (`e7963de..1709a3c`). That is my bookkeeping error, not
yours; this round's re-review will cover it, and a clean review to date says nothing about it.

---

## Ruling R101 — F1: the defect is the "instead of", and the two backends the reviewer called wrong
## are the two that are right

The reviewer found something real and found it by running it, which is the only reason it was
findable: a peer hangup routes to `onError` on poll and epoll (`readable=0 writable=0 failed=1`,
measured on Linux) and to `onReadable` on kqueue and Wfmo, while `IoBackend.hpp:95-99,145-155` and
`.agent/rules/async-and-net.md` all state it as uniform. Nothing asserts it — `grep '\.failed'` over
the parity suite returns nothing — and `EventLoop::registerFdWaiter` sets `onError = nullptr`
(`EventLoop.cpp:112-120`), so production cannot see it today. **B6 is the first code that will set
it.**

I am taking neither of the two remedies offered, because working through what each costs says the
promise itself is the defect.

**`selectReadinessCallback` (`IoBackend.hpp:176-190`) returns exactly one callback, and `Failed`
with `onError` set wins over a watched direction.** So the moment B6 sets `onError`, a peer hangup
arriving as `POLLIN|POLLHUP` — **a socket with buffered data still unread** — returns `onError`
alone on poll and epoll. The reader is never woken. Those bytes are never read. That is not a
diagnostic divergence; it is data loss, armed and waiting for the first handler that sets the field.

On kqueue and Wfmo the same hangup returns `onReadable`, the reader drains the buffer and then
reads 0. **They are correct.** So remedy 1 — make the other two match poll and epoll — would
propagate the defect to the two platforms that got it right.

And mapping `EV_EOF` to `Readiness::Failed` specifically would be a regression of its own:
`EV_EOF` on a read filter is an **ordinary EOF** — the peer called `shutdown(WR)` — and is not an
error. macOS would begin reporting every normal close as a failure.

Ask what the real users need. A reader needs to be woken so it can read 0. A dial needs to be woken
and then checks `SO_ERROR`, which is the portable connect idiom on every platform and does not
consult the callback that fired. **Neither needs `onError` to be correct** — so `onError` is
diagnostic, never load-bearing, and the "instead of" makes it actively harmful by stealing the
wakeup both of its real users depend on.

So:

1. **A watched direction wins over `onError`.** Reorder `selectReadinessCallback`: `onError` is
   returned only when `Failed` arrives with **neither** direction set, which is precisely the case
   it exists for (a `POLLERR`-only failed connect). Its unit test at `IoBackend_test.cpp:55-85`
   gains the case that distinguishes the two orders, and that case is **red against `HEAD`'s
   function** before you change it.
2. **`Readiness::Failed` is documented as best-effort and not portable.** kqueue and Wfmo do not
   set it for a hangup, by design and correctly; no handler may depend on it; it is a hint. Say this
   in `IoBackend.hpp` where `Failed` is declared and in `.agent/rules/async-and-net.md`, and delete
   the "called **instead of** the two above" clause, which is the sentence that encoded the bug.
3. **The parity case pins what IS portable**: a peer hangup **wakes the watched direction on every
   backend**. That is the property every handler actually depends on and the one nothing asserts
   today. It is the reviewer's throwaway case, promoted, with the assertion changed from which
   callback fired to the property that holds everywhere.
4. **Do not touch `EV_EOF` mapping.** Record in the rules file why not, because the next reader of
   F1 will reach for it.

If working through it says I have this wrong, **stop and report rather than implementing the
version you disagree with** — B6, B7 and B8 are all downstream of this decision and I would rather
re-rule than have it landed under protest.

## Ruling R102 — F2: G1 and G3 are B4's, and the hand-off was the defect

Already sent to B4 with the `PollBackend.cpp:107` `static thread_local` motivation. **Your part is
one paragraph in `task-B3-report.md` §4** saying G1 and G3 are not asserted and naming where they
belong. The reviewer's point stands and is the transferable one: §4 is the document B4 is written
against, and a guarantee the dispatch asked for that appears in neither the code nor the hand-off
list is how one gets lost.

## Ruling R103 — the workflow checklist overstates, and you are right

Steps 4 and 5 read as "every change edits `CHANGELOG.md`", and you showed four same-day commits that
do not — including `c99ca38`, which wrote the words. **The practice is the coherent rule: an entry
for what a reader of the release notes would care about.** `455b21b` has one because a new hygiene
gate is something a maintainer needs to know about; `870d12b` correctly has none.

So **do not add an eighth commit**, and your level-triggered precondition does not earn an entry on
its own. I am having the text corrected by the lane that owns AGENT.md.

**Step 4 survives the correction on its other leg**, and you were right to run it: `mkdocs build
--strict` stays unconditional not because every change touches the CHANGELOG but because it costs
0.43 s and the condition it replaces — *did I touch one of the three files outside `docs/` that are
snippet-included?* — cannot be evaluated without reading `mkdocs.yml`. A condition you cannot check
is more expensive than the check it guards.

Your framing is going into the constraints: **a rule that is visibly not followed stops being read
as a rule, and takes the steps around it down with it.**

## The Minors

Take or argue each in a sentence. Three matter more than the others:

- **F3** — epoll and kqueue leak their kernel fd if `WakeupChannel` throws. Member order, one line.
  It is a constructor that can throw by design, so this is reachable.
- **F5** — `ScriptedBackend::wait` ignores `Interest`. **Do this one early**: B4 is writing mute
  cases and I have told it not to build one on this until you land. Say when it is in.
- **F4** — the `#475` parity case passes vacuously if a kernel reports one of two. Your own lesson
  from last round applies: assert the property, not what your machine happens to do.

F6 (three stale `EventSource` comments in compiled files — leave `AGENT.md:27`, another lane owns
it), F7 (`makeDefaultBackend` bypasses `makeBackend`), F8 (`FdRegistrationFailed` is an exception
for a recoverable condition) as you judge them.

## Then

- RED before each fix; R101's item 1 and item 3 both need one, and say how many failures you expect
  from which cases before you run.
- `python scripts/clang-format.py --check <paths>`, `python scripts/python-style.py --all --check`
  if any Python changed, the `clang-tidy` preset, `ctest -L hygiene`, `mkdocs build --strict`.
- WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`; Windows `cl-debug`,
  `clangcl-release`. **Build in a throwaway worktree** — the shared tree is mid-landing.
- macOS and FreeBSD you cannot run: put the hypothesis in the commit message and let CI refute it.
- Append "Fix round 2" to `task-B3-report.md` with RED/GREEN for R101.
