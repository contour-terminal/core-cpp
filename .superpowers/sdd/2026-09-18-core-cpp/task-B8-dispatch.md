# Task B8: dialling and resolution, and DNS never runs on a loop

contour's `connect()` calls `getaddrinfo` on the thread that called it. On an event loop's thread
that is a stall of unbounded length — a DNS lookup with a dead resolver is seconds, and every
coroutine on that loop waits for it, including the ones that have nothing to do with the network.
fastcached solved it with an injected resolver and a thread. **This task makes that the only way
core-cpp resolves a name**, and re-implements contour's `connect()` on top rather than beside it.

Read the plan's Task B8 and the design spec's **§2 item 8**. **Read B6's report first** — it owns
`ISocket`, the slot guards and the stop-token rules you inherit, and B7's if IOCP has landed.

## What to build

`IConnector`, `makeConnector(loop, resolver)`, `DialOptions`, `ConnectFlow`, `ReadinessDial`
(non-template, over `IoBackend`), `IocpDial`, `KeepAlive`, `SocketDeadline`, `IAdmissionControl`.
`listen(loop, ListenOptions)` and `adoptListener`. contour's `connect()` re-implemented over
`makeConnector`, keeping its signature where it can.

`IAsyncAddressResolver` and `ThreadedAddressResolver` are the seam and its default. **The seam is
the deliverable, not the thread** — a resolver is the thing a test injects, and
`ThreadedAddressResolver` is merely the implementation that ships.

## Ruling R101 lands on you harder than on anyone

**A dial checks `SO_ERROR`.** That is the portable connect idiom on every platform: the socket
becomes writable when the connect resolves, and `getsockopt(SO_ERROR)` says whether it resolved
into a connection or into `ECONNREFUSED`. It does not consult which callback fired.

This matters because B3's review found the failure mode concretely: on Linux a failed connect
reaches `onError` and the dial completes with the errno, while on macOS the write filter fires with
`EV_EOF`, the backend reports `Writable`, and a dial that trusted the callback **believes the
connect succeeded** and discovers otherwise on its first write. macOS-only, and invisible to a
parity suite that does not assert it.

So, from `task-B3-fixround2.md`'s R101:

**These four landed in `7cd86fc`** -- and an earlier draft of this dispatch said they had not, which
was true when written and false thirty minutes later. A dispatch cannot hold a state; it can only
tell you to check one. **Read `selectReadinessCallback` in `src/core/net/IoBackend.hpp` before you
rely on the ordering, and say in your report what you found.**

- **A watched direction always wins**, on every backend. Your dial is woken on `Writable`.
- **`Readiness::Failed` is best-effort and not portable.** Do not branch on it for correctness.
- **`onError` fires only when `Failed` arrives with neither direction set.** Treat it as a fast
  path that hands you the errno sooner, never as the thing that tells you a connect failed.
- B3's framing, which is the one to write code by: **you never trust `revents` to tell you what
  went wrong — you do the `read`/`write`, or here the `getsockopt`, and let that report.**

A dial written to depend on `onError` is wrong even on the platform where it works.

## Sources

### Correction, made before you were dispatched: the file your central deliverable comes from was missing

`ReadinessDial` — "non-template, over `IoBackend`" — is the heart of this task, and an earlier draft
of this Sources list **did not name the file it comes from**. It would have sent you to
`EpollConnector_test.cpp` to re-derive from scratch something upstream has already factored out.
Verified at the pin with `git ls-tree`:

- **`Net/ReactorDial.hpp` is the readiness dial**, `#if defined(__linux__) || defined(__APPLE__)`,
  **templated on a platform `Traits` triple** (`Traits::Reactor`, `Traits::Handler`).
  `Detail::ReadinessDialOp` holds the per-dial state **in the dialling coroutine's own frame** —
  and the header says why, in a comment worth keeping: its address is stable for exactly as long as
  the backend can reach it, it disappears with the attempt, and a connector holding a slot per dial
  would let a timed-out dial tie one up. **Your job is to de-templatise this file over B3's
  `IoBackend`, not to rewrite it.**
- **`Net/EpollConnector.*` and `Net/KqueueConnector.*` are thin `IConnector` wrappers over it.**
  `KqueueConnector` is `__APPLE__`-only. The earlier draft named `EpollConnector` and **not**
  `KqueueConnector`, which is precisely how the macOS divergence R101 exists to prevent gets written
  back in: deriving a portable dial from the Linux wrapper alone. **Read both wrappers.**
- **`Net/IocpDial.hpp`** exists and is the Windows counterpart. The "What to build" list above names
  `IocpDial`; this list previously did not.

These four files have **no `_test.cpp` at the pin** — do not go looking: `KeepAlive.hpp`,
`SocketDeadline.hpp`, `IAdmissionControl.hpp`, `IAsyncAddressResolver.hpp`. Their coverage is
indirect, through `ConnectFlow_test.cpp` and `PlatformConnector_test.cpp`.

**This is the fifth stale or incomplete source list found in this project.** Treat the list below as
a starting point and run `git ls-tree -r --name-only 0708dd54 src/FastCache/Net/` yourself before
you rely on any of it. Say in your report what you found that this list still gets wrong.

### The list

fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as blobs
(`git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:<path>`):
`Net/{IConnector,ConnectFlow,PlatformConnector,EpollConnector,IocpConnector,KeepAlive,
SocketDeadline,IAdmissionControl,IAsyncAddressResolver,ThreadedAddressResolver,SocketAddress}.*`
and their tests, plus `BlockingConnector*`. **`DialOptions` has no file of its own** — it is
`Net/IConnector.hpp:110`, which the list above already covers; an earlier draft of this dispatch
named `Net/DialOptions.*`, which does not exist. contour's `connect()`, `listen()` and their tests
are already in the tree, in `src/core/net/Sockets.hpp`.

Record the pin and a row per imported file in `.agent/reference/provenance.md`, and every renamed
public symbol in `tools/migrate/renames.json`, **in the same commit as the code** — a row landing
later leaves `ctest -L hygiene` red for whoever builds next, and they will spend an hour proving it
is not theirs. `removed` rows are fully qualified. Base `renames.json` on `git show HEAD:<path>`,
never the worktree: it is generated as well as hand-edited.

## Tests, first

- `ConnectFlow_test`, `PlatformConnector_test`, `SocketAddress_test`, `ThreadedAddressResolver_test`,
  `BlockingConnector_test`.
- fastcached's `EpollConnector_test` → **one** `ReadinessDial_test` over B3's `BackendMatrix`
  (epoll, kqueue, poll); `IocpConnector_test` stays its own where IOCP differs.
- **The case this task exists for**: an injected resolver records the thread that called it, and
  `connect()` asserts that thread is **not** the loop's. Write it against the current `connect()`
  first and watch it fail — that is your RED and it is the whole point of the task.
- **A refused connect completes with the refusal, on every backend.** This is the R101 case in its
  natural habitat: point at a closed port and assert `ConnRefused` reaches the caller. It must pass
  on poll, epoll and kqueue, and it is the case that would have caught the macOS divergence.
- The flow's stop token cancels a dial in flight, and the socket is not leaked when it does.

Write the case, run it, **capture the RED verbatim**, then implement, then the GREEN. Say how many
failures you expect and from which cases before you run.

## What this session has learned that bears on you

- **A dispatch's list is a starting point, never an inventory.** Enumerate what B6 and B7 actually
  left you rather than trusting the paragraph above.
- **It is only a check if you commit to the answer first**, and **a measurement whose answer cannot
  come out wrong is not a measurement** — before reporting a number, ask what input would have made
  it different.
- **A local green is not evidence about something your local run cannot reach.** You cannot run
  macOS or FreeBSD; `Portability` dispatches FreeBSD on demand and macOS runs on every Build. Put
  the hypothesis in the commit message and let CI refute it.
- **Report a defect in another lane's file, do not fix it.**
- **The shared checkout's working tree is not a buildable state.** Build in a throwaway
  `git worktree` at `origin/master` plus your own files.
- **Never a bare `git commit`, `git commit -a` or `git add`.** `git commit --only -- <paths>`;
  a private `GIT_INDEX_FILE` only for hunks of a file whose other hunks are not yours, and then the
  fourth step that is not optional: snapshot `git status --porcelain` before, `git reset --
  <your paths>` after, and **diff the snapshots**. `git show --stat` after every commit.

## Then

- `python scripts/clang-format.py --check <paths>`, `python scripts/python-style.py --all --check`
  if any Python changed, the `clang-tidy` preset, `ctest -L hygiene`, `mkdocs build --strict`.
- WSL `clang-debug`, `gcc-release`, `clang-asan-ubsan`, `clang-tsan`; Windows `cl-debug` and
  `clangcl-release`. **A resolver thread is a thread** — TSan is not optional here.
- `CHANGELOG.md` under **Breaking** with migrations: contour's `connect()` callers are affected,
  and so is anything that assumed resolution happened inline.
- `.agent/rules/async-and-net.md` gains the resolution rule — *a name is never resolved on a loop
  thread* — and the `SO_ERROR` rule, each citing its origin as a full URL.

## Report

`D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B8-report.md`: RED/GREEN per test, the
thread-identity case verbatim, which cases could not run here and which CI run covered them, what
you inherited from B6/B7 versus what you decided, and **what you are leaving for B9 and B10, named
explicitly** — a guarantee that appears in neither the code nor the hand-off list is how one gets
lost. Return only status, the commit range, a one-line test summary, and concerns.
