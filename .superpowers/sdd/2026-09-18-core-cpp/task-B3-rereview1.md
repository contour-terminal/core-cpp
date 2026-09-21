# Task B3 — re-review of fix round 2 (R101–R103, F3–F8)

Scope: the six commits `870d12b, 7e2e3ae, 8d05bb7, 48af4e6, 7cd86fc, c94a0b9` against
`task-B3-review.md`'s F1–F8 and `task-B3-fixround2.md`'s R101–R103. Not a fresh review of B3.

## Verdicts

| Finding | Verdict |
|---|---|
| **R101 (F1)** — watched direction beats `onError` | **ADDRESSED**, correctly and completely |
| **R102 (F2)** — G1/G3 hand-off to B4 | **ADDRESSED** (documentation-only ask; the paragraph is in `task-B3-report.md` §4) |
| **R103** — checklist wording | **N/A to this lane** — no eighth commit was added (correct); text fix belongs to AGENT.md's owner |
| **F3** — throwing wakeup channel leaks the kernel fd | **ADDRESSED** |
| **F4** — `#475` case could pass vacuously | **ADDRESSED** |
| **F5** — `ScriptedBackend` ignores `Interest` | **ADDRESSED** |
| **F6** — stale `EventSource` comments | **ADDRESSED** (the three in scope; `AGENT.md:27` correctly left to its owning lane) |
| **F7** — `makeDefaultBackend` bypasses `makeBackend` | **ADDRESSED** |
| **F8** — `FdRegistrationFailed` is an exception for a recoverable condition | **ARGUED** — I agree with the argument |

**No new Critical or Important defect found.** Two Minor process slips, both self-reported and
already fixed within the round (`8d05bb7`, `48af4e6`) — see below.

---

## R101 — the headline

**Fix, `IoBackend.hpp:selectReadinessCallback`:** reordered to check a watched direction (with a
non-null callback) first, for `Readable` then `Writable`; only then does a `Failed` bit route to
`onError`; only if `onError` is null does it fall back to whichever direction callback exists.
Traced all four reachable branches by hand:

- `observed` has the watched, populated direction (with or without `Failed` alongside) → that
  direction's callback, always. This is the exact defect case (`POLLIN|POLLHUP` with `onError`
  set) and it now returns `onReadable`.
- `observed` has `Failed` with no populated, observed direction (e.g. a `POLLERR`-only failed
  connect) → `onError`, if set — precisely what R101 asked for ("neither direction set").
- `Failed` with no `onError` and no observed direction → falls back to whichever direction
  callback is non-null, unchanged from before R101 (not part of this defect, not touched).

**I could not construct a combination where a handler watching one direction, with `onError`
set, loses that direction's wakeup to `onError`.** The function returns unconditionally on the
first watched-and-observed direction it finds; `onError` is only ever reached after both direction
checks have failed to match. I did not find a path where a failure bit steals a watched
direction's wakeup, and I don't believe one exists in this function's shape.

**Doc changes match the ruling's four items exactly:**
1. Reordered as above — done.
2. `Readiness::Failed` doc gained a `@warning` block: "Best-effort, and NOT portable. Nothing
   above a backend may depend on it" — done, and the "called **instead of**" clause on `onError`'s
   doc comment is gone, replaced with "a last resort, never a pre-emption" — done.
3. The parity case is exactly the promoted throwaway: `a hangup over buffered data wakes the
   reader on every backend` asserts `reader.readable >= 1` (the property), not which callback
   fired.
4. `EV_EOF` mapping is untouched (confirmed by reading `bsd/KqueueBackend.cpp` — no diff to it
   anywhere in these six commits) and `.agent/rules/async-and-net.md` gained the explicit "do not
   'fix' the divergence by mapping `EV_EOF` to `Failed`" bullet with the `shutdown(WR)` reasoning.

**RED matched exactly.** Ran both levels myself against the fix-round-2 commit in a private
worktree (not arm-removal — I ran the actual committed cases):

- `IoBackend_test.cpp`, case `a hangup over buffered data wakes the reader on every backend`
  (`BackendParity_test.cpp:1104`): `backend=poll` and `backend=epoll` both pass,
  `CHECK(reader.readable >= 1)` → `1 >= 1`. This is the exact assertion the report's RED showed
  failing (`0 >= 1`) before the fix.
- Companion case `a hangup with nothing buffered still wakes somebody`: passes on poll and epoll,
  asserting only `total() >= 1`.

**Companion case design: I agree it is the right call.** I checked what `observed` actually is on
poll/epoll when nothing is buffered and the peer closes: `POLLHUP` with no `POLLIN` (no data means
nothing to signal "readable" for), so `selectReadinessCallback` on a reader with `onError` set
routes to `onError` there — while kqueue reports the same close as a readable `EV_EOF` and routes
to `onReadable`. That is a genuine, already-documented cross-backend divergence (poll/epoll signal
via the failure path, kqueue via the read path, for the *same* event), and pinning "which callback"
would pin the divergence rather than the property every handler needs (get woken). Asserting
`total() >= 1` is consistent with the project's own stated rule ("assert the property, not which
backend does it").

---

## F3 — member order

Verified the actual mechanism, not just the comment. `EpollBackend`/`KqueueBackend`'s
constructors (`EpollBackend.cpp:57`, `KqueueBackend.cpp:56`) initialize only `_epollFd`/`_kq` via
their mem-init list (`::epoll_create1(...)`/`::kqueue()`); `_wakeup` is **not** named in the list,
so it is default-constructed in declaration order, which the fix now puts **before** the kernel
descriptor. `detail::WakeupChannel`'s no-arg constructor (`WakeupChannel.hpp:38`) throws
`std::runtime_error` when `platform::createSystemPipe()` fails. Before the fix, `_epollFd`'s
initializer ran first (successfully allocating a kernel object), then `_wakeup`'s constructor
could throw, unwinding the partially-constructed object with no destructor call for the raw `int`
— genuine leak. After the fix, `_wakeup` constructs first; if it throws, `epoll_create1`/`kqueue()`
is never called at all. This is a correct fix, not just a plausible-looking reorder.

## F4 — the `#475` control

`BackendParity_test.cpp`'s `a handler detached from inside a dispatch is not dispatched in the
same wait` case now opens with a control block: two throwaway pipes, no withdrawal, `REQUIRE(...
dispatched == 2)`. This asserts the premise the rest of the case depends on (one wait dequeues
both peers into one batch) before relying on it. Matches the finding exactly.

## F5 — `ScriptedBackend`

`observableBy()` filters a scripted step through the registration's actual interest: `Interest::
None` → `Readiness::None` always; a direction not in the registration's interest is dropped;
`Failed` survives both muting-independent filters only via the interest check (it is *not*
filtered by direction, but *is* suppressed when muted, matching every real backend). `attach()`
now refuses a second attach of the same handler with `BadHandle`. Three new cases in
`ScriptedBackend_test.cpp`, all in the portable binary, and I read them: they test exactly the
three rules the finding named (silence when muted, silence for the wrong direction, refusal on
double-attach) and nothing more, nothing vacuous.

## F6, F7

Read the diffs directly: `Diagnostics.hpp:33` and `BackendParity_test.cpp:502` now say
`IoBackend`/`PollBackend`; `detail/WaitChunking.hpp:14` was already fixed in `870d12b`.
`posix/DefaultBackend.cpp` and `windows/DefaultBackend.cpp` now route through
`makeBackend(preferredBackendKind())`, matching `linux/` and `bsd/`'s existing shape exactly
(confirmed by reading `linux/DefaultBackend.cpp` side by side). Harmless today since `makeBackend`
never returns null for `Poll`/`Wfmo`, which makes the direct-construction fallback line
unreachable dead code — deliberately, in preparation for B7, and stated as such in both files'
comments.

## F8 — judged, not just accepted

The argument: `WaitFdAwaiter::await_resume()` is `void` (confirmed, `EventLoop.hpp:648,724`), so
there is no return channel for an `expected`. The only way to signal a refused registration
without changing that signature is to throw, and the only alternative to a second exception type
would be reusing `OperationCancelled` — which the report correctly says is *worse*, since it is
exactly the ambiguity `149659a`'s tests exist to keep apart (a plumbing failure and a deliberate
stop must not read as the same event to a caller inspecting why it woke). Changing
`await_resume`'s signature is a cross-cutting change to every awaiter call site in the loop, which
is squarely B4's `runOnce`/park-machinery territory per the report's own §4 hand-off list, not a
fix-round-sized change for B3.

**I agree with this.** The alternative to "argue and hand off" was either a real contract change
(out of scope, risks re-litigating B4's design mid-flight) or silently keeping the status quo
without flagging it (worse — B4 could inherit it by accident rather than by decision). The report
names the tradeoff explicitly in §4 and again in §10's table, so B4 will not discover this cold.

---

## Addendum — the seventh commit (`779f6f9`, outside my range, read only, not built)

The controller asked for an independent view on this while it was landing. My worktree was not
updated (per instruction); this is from reading the committed diff only.

**What it does:** adds `this platform builds every backend the parity matrix expects of it`,
which names the `BackendKind`s each platform must be able to construct (`{Poll, Epoll}` on Linux,
`{Poll, Kqueue}` elsewhere-but-Windows, `{Wfmo}` on Windows) and asserts `makeBackend(kind) !=
nullptr` for each, plus that each expected kind is present in `BackendMatrix`. Separately, it
converts five cases (including both of R101's new hangup cases) from `#ifndef _WIN32` — which
removes them from the Windows binary's case count entirely — to a runtime `SKIP(reason)` on
Windows.

**Does it close the gap, and is the shape right?** Yes to both, and I don't think it collides with
anything in my review. The 29 cases that loop `BackendMatrix` with `if (!backend) continue;` were
one silent regression away from testing only poll everywhere — a `KqueueBackend` that stopped
constructing on macOS would have made every one of those 29 green while exercising nothing R101's
argument depends on, and `the preferred backend is constructible` only guarantees *one* non-null
entry, not the two R101 needs. This new case is the missing guarantee, and it's the correct
minimal fix: one assertion, not 29 rewrites.

I checked the concern that mirrors what I was asked to judge on the companion case (how much a
parity case may leave implicit): is this a "list standing in for a rule," the failure mode
`global-constraints.md` flags repeatedly tonight? I don't think so, for a reason specific to this
case: there is no single property under "which backends does this platform build" other than the
module table itself — it's not an enumeration *approximating* a rule, it *is* the fact, and
`BackendMatrix` itself already carries the same kind of platform-independent enumeration by
design. What matters is whether it's the *good* kind of enumeration, and it is: each row states
why (tied to the module table), it's tied to the one thing it must stay true for (R101's evidence,
named explicitly), and it carries the instruction the anti-pattern write-up asks for — the failure
message says "this is not necessarily a defect… update the table here" and names Task B7's
Windows row by number before it happens, rather than after.

**Second-order thing I looked for and didn't find a problem with:** whether `BackendMatrix`
itself is platform-conditional (which would make the second check — "is the expected kind present
in the matrix" — meaningful) or fixed (which would make it vacuous today). I read
`testing/BackendMatrix.hpp`: it's a single `constexpr std::array` listing all five native kinds
unconditionally on every platform, filtered only at runtime by the `if (!backend) continue`
pattern the new case is defending against. So today the matrix-membership half of the new case
cannot fail — it's pure future-proofing against someone editing the table incorrectly, not active
coverage. Worth knowing, not worth objecting to: it costs nothing and the `makeBackend(kind) !=
nullptr` half is the one doing the real work.

**One thing I checked and it turned out fine on its own:** the working-tree diff I saw mid-edit
had `#include <algorithm>` inserted out of alphabetical order; the committed version
(`779f6f9`) has it correctly sorted first in the `<...>` block. Formatted before landing — not a
finding.

**On the main event, restated as asked:** I could not construct a readiness combination where a
handler with `onError` set, watching one direction, loses that direction's wakeup.
`selectReadinessCallback`'s new order returns on the first watched-and-observed direction
unconditionally, before it ever inspects `Failed`; the only way to reach the `onError` branch is
for both direction checks to fail to match first — i.e., the arriving readiness genuinely doesn't
carry the handler's watched, populated direction. I tried: readable+failed against a
readable-watching handler (routes to `onReadable`), writable+failed against a writable-watching
handler (routes to `onWritable`), failed-alone against a handler watching one direction with
`onError` set (routes to `onError` — the POLLERR-only case it exists for), and failed+the
*other*, unwatched direction against a handler watching only one side (also routes to `onError`,
matching the new unit test's `readerOnly` case). All four are exhaustive over the branches the
function can take; I found no fifth.

## Verified by running

Private worktree, `git worktree add --detach D:/core-cpp-wt/rereviewB3 c94a0b9`, isolated from the
shared checkout (which has B4's uncommitted `EventLoop.*`/`IoBackend.hpp`/`BackendParity_test.cpp`
changes in it right now — none of that reached my worktree; confirmed with `git status` in the
shared tree separately).

**Predicted before running:** zero failures, matching the implementer's claimed 28/28 (15
hygiene), because this is the fix round's own claimed-green commit, not a RED-hunting run.

| Check | Result |
|---|---|
| WSL `clang-debug` configure+build | Clean, 0 warnings, exit 0 |
| `ctest --preset clang-debug` | **28/28 passed, 1 skipped** (`upstream-drift`, by design) — matches the report exactly, including the 15-test hygiene label |
| `core-cpp-net-test` standalone | **861 assertions, 141 cases, all passed** |
| `core-cpp-net_backend-test` standalone | **105 assertions, 22 cases, all passed** |
| `a hangup over buffered data wakes the reader on every backend` | poll and epoll both pass (`reader.readable >= 1` → `1 >= 1`) — the exact case that was RED before R101 |
| `a hangup with nothing buffered still wakes somebody` | passes on poll and epoll (`total() >= 1`) |
| `clang-tidy` 22.1.8, run directly (not through `--quiet`/grep) on the touched, Linux-compiled files (`EpollBackend.cpp`, `linux/DefaultBackend.cpp`, `IoBackend_test.cpp`, `ScriptedBackend_test.cpp`, `BackendParity_test.cpp`) | exit 0; "warnings generated" printed once per TU (5/5, confirming each was actually processed, not silently skipped); 0 unsuppressed warnings — all 54,875 generated diagnostics were in non-user (system/library) code |

Worktree removed after (`git worktree remove --force`).

## Verified by reading GitHub Actions (the platforms I cannot run)

Both fix-round commits are individually on `master` and each has a **completed, all-green Build
run**, which is stronger evidence than the review round had (which could not run macOS at all):

- `7cd86fc` (R101) — Build run `35551974581`: **24/24 jobs success**, including both
  `macos (appleclang)` and `macos (llvm-22)`. This directly confirms the implementer's stated
  hypothesis in the commit message — kqueue already behaved as R101 requires, both new parity
  cases pass on macOS unchanged, and no existing case moved.
- `c94a0b9` (F3/F6/F7/precondition) — Build run `35551997366`: **24/24 jobs success**, both macOS
  legs included.

**Update, resolved after the report was drafted:** the FreeBSD `Portability` run `35552791163`,
dispatched against the shared-checkout `HEAD` at the time (`90cd500`, containing all six of these
commits), finished **success** (`FreeBSD (system clang)` green) shortly after I wrote the
paragraph above. That is the only exercise `KqueueBackend` gets anywhere outside of code-reading,
and it now covers fix round 2, not just the pre-round tree. All three platforms this task cannot
be run on locally (both macOS legs via Build, FreeBSD via Portability) are green for these six
commits.

## What I could not verify

- FreeBSD/kqueue for these six commits specifically (see above — CI run in flight, not mine to
  wait out).
- `posix/DefaultBackend.cpp`'s F7 edit is not compiled by any CI leg or local preset I have access
  to (it's the fallback for a POSIX platform with neither epoll nor kqueue, which nothing in this
  project's CI matrix represents) — it is a one-line mechanical mirror of the already-compiled
  `linux/`/`bsd/` shape, so I judge the risk low, but it is genuinely unexercised anywhere.
- `windows/WfmoBackend.hpp`'s precondition rewording and `windows/DefaultBackend.cpp`'s F7 edit —
  Windows CI (`cl-debug`, `cl-release`, `cl-release-tls`, `clangcl-release`) is green on both
  commits, which covers compilation and the existing Wfmo suite, but I have no Windows machine
  here to independently re-run and read output from, so this is CI's word, not mine.
