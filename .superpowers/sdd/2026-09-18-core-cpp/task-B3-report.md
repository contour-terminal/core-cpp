# Task B3 — `IoBackend` replaces `EventSource`

**Status: DONE_WITH_CONCERNS.** Commits `e7963de..1709a3c` (`e7963de`, `0fd01f5`, `9f7eb5d`, `c4d3567`, `149659a`, `1709a3c`).
Two defects of mine reached master: one fixed by another lane before I found it
(`tests/consumer-shared/ConsumerSmoke.hpp`) and one CI found on macOS and I fixed in `1709a3c`
(§7), and CI is green on the result: `Build` 24/24 and `Portability` on FreeBSD. Five deliberate
deviations from the dispatch are recorded in §6 — **(a) and (d) are the two worth a reviewer's
time.**

---

## 1. What landed

| Commit | What |
|---|---|
| `e7963de` | `net: IoBackend replaces EventSource; backends dispatch, never resume` |
| `0fd01f5` | `net: a host-driven backend so an event loop can run inside the browser's` |
| `9f7eb5d` | `docs(net): the two places outside src/ that still named the deleted event sources` |
| `c4d3567` | `style(net): HostDrivenBackend.cpp includes nothing it does not use` |
| `149659a` | `test(net): the three loop paths a backend's refusal reaches` |
| `1709a3c` | `fix(net): a muted registration's un-muting is asked where the answer is portable` |
| `870d12b` | `test(net): the >64-handle sweep is asked of the backend, not only of its arithmetic` (fix round 1) |

The last four are follow-ups: the sweep outside `src/` that §7 earned, an unused include, the
three cases that give `ScriptedBackend`'s refusal helpers a user (§2), and the macOS failure CI
found (§7).

**Created.** `IoBackend.hpp` (the contract, `ReadinessHandler`, `Interest`, `HandleKind`,
`Readiness`, `selectReadinessCallback`, `BackendKind`, `WaitResult`, the three factories),
`IHostScheduler.hpp`, `HostDrivenBackend.{hpp,cpp}`, `detail/{ReadyBatch,WakeupChannel,WaitTimeout}.hpp`,
`posix/PollBackend.{hpp,cpp}`, `linux/EpollBackend.{hpp,cpp}`, `bsd/KqueueBackend.{hpp,cpp}`,
`windows/WfmoBackend.{hpp,cpp}`, `{posix,linux,bsd,windows,emscripten}/DefaultBackend.cpp`,
`emscripten/EmscriptenHostScheduler.{hpp,cpp}`,
`testing/{ScriptedBackend,NullBackend,BackendMatrix,ManualHostScheduler}.hpp`,
`IoBackend_test.cpp`, `HostDrivenBackend_test.cpp`.

**Deleted.** `EventSource.hpp`, `DefaultEventSource.{hpp,cpp}`, `PollEventSource.hpp`,
`posix/PollEventSource.cpp`, `windows/PollEventSource.cpp`, `linux/EpollEventSource.{hpp,cpp}`,
`bsd/KqueueEventSource.{hpp,cpp}`, `testing/ScriptedEventSource.hpp`,
`testing/EventSourceBackends.hpp`. `detail/WaitChunking.hpp` stayed where it is, as the dispatch
asked.

`EventSourceParity_test.cpp` → `BackendParity_test.cpp` (git records it as a rename, 53% similar).

---

## 2. RED then GREEN, per rule

Every load-bearing arm was removed, the failure captured, and the arm restored. The scripts are in
the session scratchpad (`red.py`, `red2.py`); each break is a single-hunk text replacement so the
"GREEN after" below is the same binary the suite runs.

### `e7963de` — the backends

| Arm removed | Case that failed | RED, verbatim |
|---|---|---|
| `ReadyBatch::withdraw` scrubs the batch (#475) | `a handler withdrawn earlier in the same batch is not dispatched` (pure) and `a handler detached from inside a dispatch is not dispatched in the same wait` (every backend) | `CHECK( batch.dispatch() == 1 )` → `2 == 1`; `CHECK( second.dispatched == 0 )` → `1 == 0`; and on epoll `CHECK( dispatched.dispatched == 1 )` → `2 == 1` |
| `ReadyBatch::add` merges a second report of one handler | `a handler reported twice in one batch is dispatched once` | `CHECK( batch.size() == 1 )` → `2 == 1`; `CHECK( peer.dispatched == 1 )` → `2 == 1` |
| `selectReadinessCallback`'s failure fallback | `a failure with no onError falls back to a watched direction` | `CHECK( selectReadinessCallback(reader, Readiness::Failed) == &onReadable )` → `nullptr == 0x…3050` |
| `toTimeoutMillis` rounds a sub-millisecond wait up to 1 | `a backend timeout converts to the millisecond count a native wait takes` | `CHECK( toTimeoutMillis(…(400us)) == 1 )` → `0 == 1` |
| epoll keeps a muted registration OUT of the set | `a muted registration stays silent when its peer hangs up`, `backend=epoll` | `CHECK( backend->wait(zero).dispatched == 0 )` → `1 == 0` |
| epoll's `dup` failure is reported | `setInterest reports the kernel's refusal when descriptors run out`, `backend=epoll` | `REQUIRE_FALSE( armed.has_value() )` → `!true` |
| **Rule 1** — the loop's refusal to resume during a dispatch | `every backend reports pipe readability`, `backend=poll` | `Assertion '!detail::readinessDispatchInFlight() && "EventLoop::drainReadyQueue reached from inside a backend dispatch: backend callbacks may only enqueue"' failed` → `SIGABRT` |
| `WakeupChannel::signal` writes its byte | `a wake ends a wait, and one raised before the wait is not lost`, on poll **and** epoll | `BackendParity_test.cpp:1394: FAILED` on both, after the bounded 30s wait elapsed |

### `0fd01f5` — the host-driven backend

| Arm removed | Case that failed | RED, verbatim |
|---|---|---|
| The coalescing | `two wakes in one turn schedule exactly one pump` | `CHECK( host.pendingCount() == 1 )` → `3 == 1`; `CHECK( counter.pumps == 1 )` → `3 == 1` |
| The clamp at zero | `armWakeAt asks the host for the deadline's delay, clamped at zero` | `CHECK( host.pending().front().delay == 0 ms )` → `-500 ms == 0 ms` |
| Clearing the schedule BEFORE the pump | `a pump clears its own schedule before it runs, so the turn can re-arm` | `CHECK( host.pendingCount() == 1 )` → `0 == 1` |
| `attach` answering `Unsupported` | `a host-driven backend has no readiness, and says so` | `REQUIRE_FALSE( attached.has_value() )` → `!true` |
| `armWakeAt(nullopt)` scheduling nothing | `armWakeAt …` / `no deadline schedules nothing` | `CHECK( host.pendingCount() == 0 )` → `1 == 0` |

### `149659a` — the loop paths a refusal reaches

`ScriptedBackend` grew `refuseNextAttach`, `refuseNextSetInterest` and `pushFailure` with the first
commit and **nothing used them** — the speculative API this task refused elsewhere (§6a). Each names
a branch of `EventLoop` that had no case, so they got one rather than being deleted:

| Arm removed | Case that failed | RED, verbatim |
|---|---|---|
| `WaitFdAwaiter::await_resume` throws `FdRegistrationFailed` | `a registration the backend refuses fails the await rather than parking it` **and** `a kernel that refuses the interest leaves no registration behind` | `REQUIRE( loop.blockOn(awaitReadableOrRefusal(…)) == -2 )` → `1 == -2` on both: a refusal read as readiness |
| `registerFdWaiter` detaches when `setInterest` is refused | `a kernel that refuses the interest leaves no registration behind` | `CHECK( source.attachedCount() == 0 )` → `1 == 0` |
| `selectReadinessCallback`'s failure fallback | `a hangup resumes a parked reader, because a park watches one direction` | `REQUIRE( loop.blockOn(awaitReadableOrCancel(…)) == 1 )` — the flow was never resumed |

**GREEN.** `core-cpp-net-test`: 851 assertions in 147 cases. `core-cpp-net_backend-test`: 85
assertions in 19 cases. `core-cpp.net_types`, `core-cpp.net_tls` unchanged and passing.
`ctest` on `clang-debug`: **100% passed, 0 failed out of 25**.

Honest caveat on ordering: the two test files were written after the code they test, not before it,
because the contract had to exist for a case to name it. The RED above is therefore
arm-removal — "delete an arm and see which case fails" — and not a first run against nothing. What
that does not prove is that a case would have failed against an *empty* implementation; what it does
prove is that every rule in the interface has a case that dies without it.

---

## 3. How Rule 1 is held, on every backend

Three layers, because the dispatch asked for an assertion on every path rather than one case:

1. **`detail::ReadyBatch::dispatch()` publishes the dispatch.** A thread-local depth counter,
   raised by `ReadinessDispatchGuard` for the length of the walk. Every backend — the four native
   ones and `ScriptedBackend` — dispatches through this one batch, so none of them can opt out.
2. **`EventLoop::drainReadyQueue()` asserts it is not raised.** That is the single place a coroutine
   is resumed, so a backend that resumed inline fails with a stack naming the rule instead of
   corrupting the walk it is inside of. The RED above shows exactly that: making `onParkReady` call
   `drainReadyQueue()` aborts the suite on the first backend it reaches.
3. **A parity case asserts the positive, from the flow's own frame.** `a backend dispatches, and the
   loop resumes` parks a coroutine on a pipe on every backend and has it record
   `detail::readinessDispatchInFlight()` at the instant it resumes. False on all of them.

The structural half is that a `ReadinessCallback` is a `void (*)(ReadinessHandler&) noexcept` and
the loop's is nine lines that call `queueParkedWaiter`. There is nothing in a callback's reach that
resumes.

`.agent/rules/async-and-net.md` gained Rule 1 with its assertion, the batch withdrawal, the
`setInterest` refusal, the one-callback-per-registration rule, the mute rule, the wakeup channel's
ownership and the host-driven contract — each citing fastcached#475, #1054 or #1057 as a full URL.

---

## 4. What the `EventLoop` adaptation cost

**Smaller than the backends, by a wide margin**, so B3 and B4 stay two tasks. `EventLoop.{hpp,cpp}`
is +366/−233 lines, most of that comment, against 2358 lines of backend. What changed:

- `EventSource&` → `IoBackend&`.
- `FdToken` → `ParkId`, the loop's own id (§6c). Each park is a `std::unique_ptr<FdPark>` holding
  the `ReadinessHandler` the backend registers; the handler's `owner` points back at the park, and
  `EventLoop::onParkReady` is the one callback both directions use.
- The post self-pipe is gone: `post()` calls `IoBackend::wake()`. `~EventLoop` no longer detaches a
  token of its own, and the constructor no longer throws — the backend's does.
- `computeTimeoutMs() -> int` became `computeTimeout() -> std::optional<SteadyDuration>`; the
  rounding moved into `detail::toTimeoutMillis`, where the unit is.
- `wakeFdWaiters(vector<FdToken>)` is gone; the backend dispatches into `queueParkedWaiter` directly,
  and `pumpOnce` calls it only for the closed-descriptor list it merges in itself.
- `parkedWaiterCount()` joined `pendingTimerCount()`, because the leak assertions the tests used to
  make against `PollEventSource::attachedCount()` had to move somewhere honest.

**What I left for B4**, and did not invent:

- `runOnce`, `IdlePolicy`, `EventLoopOptions`, `resumeSoon`, `registerPark`/`unregisterPark`,
  `cancelPending`, `ParkedWork`, `IExecutor`. `pumpOnce` still has the five-step shape the spec
  names for `runOnce`, in the same order, so B4 renames rather than restructures.
- `ParkId` is public but narrow — it names an fd wait alone. The spec widens it over every kind of
  parked work; its doc comment says so.
- **`HostDrivenBackend::setPump` is the seam B4 must use.** The spec says the backend asks the host
  to call `loop.runOnce(0)`, and there is no `runOnce` yet, so the backend takes a
  `HostCallback`/`void*` pair instead. B4's `EventLoop` sets it once at construction. A backend with
  no pump set still pumps and still clears its coalescing flag, which is what lets the cases exist.
- **A host-driven loop must refuse `run()` and `blockOn()`** (spec rule 2, "precondition
  violations"). `EventLoop::blockOn` does NOT assert that yet, because with no readiness and no
  timers wired to the host, a `blockOn` on a host-driven backend today spins rather than deadlocks,
  and the assertion belongs beside the `runOnce`/`IdlePolicy` machinery that makes the alternative
  work. **B4 owes it.**
- **Rule 3's G1 and G3 are not asserted anywhere, and this list is why that matters** (review F2,
  Ruling R102). **G1 — exactly one thread calls `wait()`** — and **G3 — a helper thread only ever
  posts, never resumes and never touches a handler** — are both true of what B3 built, and neither
  is checked. The closest thing to G1 in the tree is `PollBackend.cpp:107`'s `static thread_local`
  scratch vector, which makes a second thread in `wait()` merely *not corrupt the first* rather
  than *fail*, and that is not the same guarantee. They belong with the loop, because that is what
  owns the thread the rule is about: **G1 as a Debug assert in `EventLoop::runOnce` with a
  `WILL_FAIL` canary, and G3 asserted where a worker hands work back to the loop.** Named here
  rather than only in a commit message because §4 is the document B4 is written against, and a
  guarantee the dispatch asked for that appears in neither the code nor this list is exactly how
  one gets lost. **B4 owes both.**
- **`FdRegistrationFailed` stays an exception, and B4 owns changing that** (review F8). It is a
  recoverable condition, so `design-principles.md` says `std::expected` — but the awaiter's
  `await_resume()` is `void`, so the only shape available to B3 was to conflate a refused
  registration with `OperationCancelled`, which is strictly worse and is the exact defect `149659a`
  exists to prevent: a plumbing failure and a deliberate stop are different things to report.
  Changing `await_resume`'s signature is a change to the loop's public contract, which is B4's to
  make deliberately rather than B3's to make in a fix round.

---

## 5. What could not run here, and what covers it

| Not runnable locally | Covered by |
|---|---|
| `KqueueBackend` — no BSD or macOS on this machine | `Portability` dispatched on `9f7eb5d` (run 35546044481), green. The kqueue-specific arms — `EV_RECEIPT`'s per-filter receipt, the per-`(descriptor, filter)` duplicate that `ReadyBatch::add` merges, the private `dup()` for a second registration — have no other exercise. |
| macOS — kqueue's other platform, and the one whose poll(2) differs | `Build`'s two macOS jobs. They are what caught §7's divergence, which every other platform hid. |
| The `>MAXIMUM_WAIT_OBJECTS` chunked sweep in `WfmoBackend` | Nothing drives more than 64 handles. `detail/WaitChunking.hpp`'s arithmetic is unit-tested on every platform, as before, and the sweep body is contour's, carried over unchanged. **A gap, and it was a gap before this task too.** |
| A kernel that refuses `epoll_ctl` for a reason other than descriptor exhaustion | Only the `dup`/`EMFILE` path is driven. |

Run locally and green: `clang-debug` (full ctest, 25/25), `gcc-release` (net, 4/4), `emscripten`
(full ctest, 21/21 after the provenance rows; net = 2 binaries), `cl-debug` (27/27),
`clangcl-release` (27/27 but for another lane's `migrate-codemods`, §7), `clang-asan-ubsan`
(net, 4/4) and **`clang-tsan` (net, 4/4)** — which is the one that matters here, because
`a wake ends a wait, and one raised before the wait is not lost` calls `IoBackend::wake()` from a
second thread into a wait in flight, on every backend.

CI on `9f7eb5d`: **`Portability` green** (run 35546044481), which is the FreeBSD job and therefore
the only exercise `KqueueBackend` gets anywhere. `Docs` green.

Gates: pinned `clang-format` 22.1.8 clean over all 85 net sources; the `clang-tidy` preset clean
(it found five things, all fixed: `#ifdef` over `#if defined`, a constant's case, two
`performance-enum-size` findings, an unused using-decl, a container `data()` and a `const` member);
`ctest -L hygiene` green; `mkdocs build --strict` green; `tools/migrate/check-renames.py` green
(484 rows, 0 failures).

---

## 6. Deliberate deviations from the dispatch — please review these

**(a) `ReadinessHandler::slot` is not implemented.** The spec's struct declares
`detail::ReadinessSlotRef slot {}`. I left it out. The slot is defined in the spec only as IOCP's
`lpOverlapped` target ("points to a backend-owned refcounted `ReadinessSlot`, never into the
handler"), and B7 is what gives it a reader; a refcounted type with no user is the speculative
generality `design-principles.md` refuses. The problem it would also have solved here — an entry in
a batch whose handler has been freed — is solved the way fastcached solved it, by withdrawing at
`detach` while the handler is still alive, which its own comment argues needs no generation counter.
**If B7 wants the field, it adds it with its implementation.**

**(b) `defaultHandleKind` is spelled `DefaultHandleKind`.** The spec's `EventLoop` block writes
`HandleKind = defaultHandleKind`; clang-tidy refuses that spelling for a `constexpr` constant under
this repository's naming rule (constants are `CamelCase`). **B4 should use `DefaultHandleKind`.**

**(c) `FdToken` became `ParkId`, a struct, not an `enum class`.** The spec names `ParkId` without a
shape. An `enum class ParkId : std::uint64_t` fails `performance-enum-size`, which is an error here
and which `NOLINT` cannot silence; a struct is also what contour's `FdToken` was, for the reason its
comment gives — an opaque monotonically-allocated id is not an enumeration of named cases. The same
applies to `testing::HandlerId`. Both carry a `std::hash` specialization declared between the type
and its first use.

**(d) The EBADF fixpoint probe is B4's, not mine.** The dispatch asks for "the EBADF fixpoint probe
from `EpollReactor_test`: a closed descriptor in the set must be found and removed rather than
spinning the pump". Read at `0708dd54`, that case is `A chain re-parked during teardown is freed
before the reactor's descriptors close` — it probes whether `~EpollReactor`'s `AbandonParkedWork`
loops to a **fixpoint**, by asking `Attach()` from a dying coroutine frame whether the epoll
descriptor is still open. That is loop teardown (fastcached#1054's other half), which is B4's
`LoopTeardown_test`, and there is no `AbandonParkedWork` in B3 to probe. What I wrote instead is the
readiness-backend half the phrase describes: **`a dead handle in the wait set does not blind a
backend to a live one`**, on every backend. It is a real property with a real origin — Windows
failed the whole `WaitForMultipleObjects` on a dead handle every round, so a flow parked on a LIVE
one hung forever — and it is what a readiness backend can answer. Resuming the flow parked on the
dead descriptor is not something a poller can do at all; that is `notifyHandleClosing`, whose cases
are the `[closehang]` family, unchanged. **Flagging it because it is a substitution, not the case
you named.**

**(e) Tests that hardcoded a backend now use `makeDefaultBackend()`.** `HttpServer_test`,
`WriteQueue_test`, `Tls_test`, `AsyncBufferedReader_test`, `posix/FdPassing_test` and
`posix/UnixSocket_test` constructed a `PollEventSource` directly. `PollBackend` is POSIX-only now,
so they had to change; `makeDefaultBackend()` is the honest translation (on Windows it is what they
had), and it means those suites run on epoll and kqueue where they ran on poll. That is the change
`BackendParity_test`'s own preamble argues for — but it is a widening of what they exercise, not a
rename, and a failure it surfaces would be a real defect rather than a port error.

---

## 7. What I got wrong

**`tests/consumer-shared/ConsumerSmoke.hpp` still included the deleted header.** `e7963de` reached
master red on `consumer-smoke`, because I grepped `src/` for `EventSource` and not the tree. That
fixture compiles only in CI — the CPM, vendored and WebAssembly legs each build it — so every local
preset stayed green over the one job whose whole purpose is proving a consumer can build against us.
Another lane found and fixed it (`13bdcd4`) before I did. `9f7eb5d` is my sweep of what that miss
implies: `.agent/rules/platform.md` still named `posix/PollEventSource.cpp` as its worked example,
and `tests/consumer-wasm/CMakeLists.txt` still said `core::net` is native-only. I also syntax-checked
`ConsumerSmoke.hpp` against the new headers afterwards; it compiles.

**A parity case asked a question with no portable answer, and CI found it on macOS.** `a muted
registration is dispatched to by no backend` armed a second registration on a descriptor the first
was still watching and then asked whether the second was dispatched to. The failing section is
`backend=poll` — **not kqueue**, which passed on macOS in the same run and on FreeBSD in the same
nightly. Linux's and FreeBSD's
poll(2) fill in every matching `pollfd`, so it was; macOS's reports the descriptor once, so it was
not. Every local preset and the FreeBSD nightly stayed green; both macOS compilers went red on
`CHECK( muted.readable >= 1 )`. The divergence is not a defect — `IoBackend` promises neither
answer, the sibling case two below says so in its own comment, and `two registrations on one handle`
is careful to assert `first || second` for exactly this reason. I wrote the careless form one case
away from the comment explaining why it is careless. `1709a3c` detaches the control first and asks
the question of a descriptor with one registration on it, where every multiplexer agrees.

**`tools/migrate/renames.json` lost edits in both directions, and my flow is half the cause.** My
rows vanished between my edit and my staging; separately, a hand edit of another lane's was dropped
by *my* write. One mechanism explains both: I add rows with a script that reads the whole file,
parses it, inserts and writes it back, so anything committed between my read and my write is
silently reverted — R87's hazard one layer up from the index, in the file itself. The fix is not
"hand-edit instead of generating" (a stale editor buffer fails identically) but R87's own rule:
base the transform on `git show HEAD:<path>`, never on the worktree copy. What I did in the end was
re-run the transform against the current file immediately before staging, then stage only my rows
as a HEAD-to-mine patch through `git apply --cached`, so the other lane's rows stayed in the working
tree and out of my commit. Worth knowing for whoever edits that file next: `git diff` alone did not
show the loss, because both versions were larger than HEAD.

---

## 8. Concerns

1. **CI is green on `1709a3c`.** `Build` run 35546989595: **24 of 24 jobs, zero non-success** —
   both macOS legs, both `emscripten` legs, all three `consumer-smoke` legs, all four Windows legs,
   both sanitizers, `clang-tidy`, `style`, `coverage`, `compile-cache`, arm64 and cxx26.
   `Portability` (FreeBSD — kqueue's other implementation, and the only one anywhere) run
   35547462130: success. The single-variable experiment came back clean: the run before the fix
   (`aae25fc`) failed on exactly the two macOS jobs and nothing else, 803 of 804 assertions, and
   **both cleared without a line of kqueue changing** — which measures the diagnosis in §7 as well
   as arguing it. Every local preset is green on the same tree: `clang-debug` 25/25, `emscripten`
   21/21, `cl-debug` 27/27, plus `gcc-release`, `clang-asan-ubsan`, `clang-tsan` and
   `clangcl-release`.
2. **Not mine, and live in the tree while I worked:** `tools/migrate/rewrite.py` had an undefined
   `LITERAL` (`core-cpp.migrate-codemods` red on `clangcl-release`; another lane fixed it in
   `c973592`); `.agent/reference/provenance.md` carried a malformed `ThreadPoolExecutor.hpp` row
   (`core-cpp.upstream-drift` red; fixed by its lane); `core-cpp.tui` failed in
   `MarkdownRenderer_test.cpp:1592`. I staged around all three and touched none of them.
3. **`KqueueBackend::Registration::owned` is order-dependent, and deliberately so.** Reviewing a
   hypothesis the controller raised about the macOS failure (it was `backend=poll`, not kqueue —
   §7) surfaced a real property of the dup path worth stating here rather than leaving implied by a
   flag. `armed` means "has filters in the kernel", not "is live": `setInterest(h, Interest::None)`
   routes to `disarm()`, which clears it, so a muted registration is invisible to the duplicate
   scan. A registration muted at attach time therefore runs its scan later, when it first arms, and
   may take the `dup()` a registration attached live would have taken at once. That is correct —
   the dup exists to make two registrations on one handle independent, and taking it when it is
   needed is the right time — but it means `owned` is not a function of attach order alone, and
   kqueue is the only backend with the mechanism at all, so the parity suite cannot see it. Stated
   here because B7 will meet it.
4. **`WfmoBackend`'s >64-handle sweep now has a case — concern withdrawn, and it found two things.**
   `870d12b`, after the controller overruled my "it is going away" argument. Measured cost: **21ms**
   for all four boundary indices, 80 registrations each, so the objection was wrong on the facts as
   well as on the principle. Two findings came out of writing it. **(a)** Coverage of a late chunk
   is defended TWICE — sweeping every chunk per wait, and rotating the chunk a sweep starts on —
   and removing either alone leaves the case green, because the other still gets there. Only
   removing both reddens it, on exactly the two indices past the boundary. So it pins the property,
   not one of its two defences, which is worth knowing before anyone deletes one as redundant.
   **(b)** `WfmoBackend` requires LEVEL-triggered handles: its wait is a detector followed by a
   rescan, and `WaitForMultipleObjects` CONSUMES an auto-reset event when it returns it, so an
   auto-reset handle is eaten by the detector, invisible to the rescan, and dispatched to nobody.
   Nothing can enforce it — a HANDLE does not say which it is — and it holds for the handles the
   backend actually gets, since a WSAEVENT from `WSAEventSelect` is manual-reset. The header now
   states it. Writing the fixture with auto-reset events is how it was found.
5. **`core::net` is now `wasm-subset`, which is a real widening of what the `emscripten` job
   builds.** It compiles `HostDrivenBackend.cpp`, `emscripten/*` and the header-only logic, and links
   no `Threads::Threads` there (gated on `CORE_CPP_USE_THREADS`, the same gate `core::base` uses).
   `tests/consumer-wasm` still links only `core::base`, `core::async` and `core::net_types`, so the
   consumer leg is unaffected — but B5, which is meant to link `core::net` there, inherits whatever
   this subset gets wrong.
6. **`core_cpp_add_test(net …)` is wrapped in `if(NOT EMSCRIPTEN)`.** A test binary named after a
   module follows the module's row, and that row now says the module has a WebAssembly subset, so an
   empty `SOURCES_EMSCRIPTEN` is a fatal error rather than a skip. The alternative was a target row
   of its own, which would rename the `core-cpp.net` ctest entry. Named here because it is the one
   place in this task where a CMakeLists asks about a platform outside a source list.

---

## 9. Owed in fix round 2

**Reword the level-triggered precondition in `windows/WfmoBackend.hpp`.** The finding is verified
and stands; the framing does not. What I wrote enumerates two handle classes — "a WSAEVENT from
`WSAEventSelect` is manual-reset, and so is the wakeup channel's" — where the requirement is a
property. It is already short by one (`core::platform::Wakeup`,
`platform/windows/WindowsWakeup.cpp:14`, `CreateEvent(nullptr, TRUE, FALSE, nullptr)` — manual-reset,
so the rule holds, but it is neither a WSAEVENT nor the wakeup channel) and Task B12 makes it short
by two (`core::platform::standardInput()`, `platform/windows/WindowsTypes.cpp:19` — a console input
handle, which is not manual-reset in Win32's terms at all and satisfies the requirement for a
different reason). `HandleKind::Waitable`'s own doc already says "an event, console input, a
WSAEVENT", so the enum was broader than my sentence from the moment I wrote it.

The replacement states the property, gives the handles as examples, and carries the instruction an
enumeration cannot. The controller's tightening is folded in: "signalled by state" described a
CORRELATE, not the property — a semaphore's signal is derived from its count and
`WaitForMultipleObjects` decrements it on return, a mutex is acquired by the wait, a
synchronization timer is reset by it, so all three are "state-signalled" on any natural reading
and all three would hang. The property is **non-consumption**. And the rule is stronger than either
draft had it, because `collectSignalled()` (`windows/WfmoBackend.cpp:141-148`) probes EVERY
non-muted registration with `WaitForSingleObject(h, 0)`, not only the one the detector flagged — so
a consuming handle registered anywhere is drained by the rescan of an unrelated dispatch:

> **Waiting on a registered handle must not change it.** A wait here is a detector followed by a
> rescan: `WaitForMultipleObjects` says that *something* in a chunk fired, and
> `WaitForSingleObject(h, 0)` on **every** non-muted registration then says *which*. So each
> registered handle is waited on repeatedly, by dispatches that have nothing to do with it. A handle
> a wait CONSUMES is therefore drained by a rescan it was not the subject of: its readiness is
> dispatched to nobody and the flow parked on it hangs.
>
> Handles satisfy this for different reasons; the two that arise here are a MANUAL-RESET object,
> which stays signalled until something resets it — `WaitForMultipleObjects` consumes an AUTO-reset
> event, which is the case that fails — and a handle **whose signal a wait does not consume**:
> console input stays signalled while its buffer holds records, and reading them, not waiting on
> them, is what clears it. An auto-reset event, a semaphore, a mutex and a synchronization timer
> are all consumed by a wait and none of them may be registered here.
>
> Nothing enforces this, because nothing can: a HANDLE does not say which it is. **When you register
> a new class of handle here, check it against the property above** — not against the examples,
> which are only the handles that exist today: `WSACreateEvent` (sockets and listeners),
> `SystemPipe`'s wakeup event and `core::platform::Wakeup`, all manual-reset; Task B12 adds
> `core::platform::standardInput()`, which is the second kind.

The defect class is the one this session keeps finding — an enumeration standing in for a rule, with
no owner and no check, in a set about to grow — and I wrote a fresh instance of it in the very
comment that recorded a general finding. Held for fix round 2 rather than spent as a commit of its
own, on the controller's instruction.

---

## 10. Fix round 2

Review: spec **PASS**, quality **approved with changes**, nothing Critical. Rulings R101-R103.

### R101 (F1) — a watched direction beats `onError`

The reviewer found a divergence; the controller found that the two backends called wrong were the
two that were right, and ruled the promise itself the defect. I worked it through and agree. The
argument that settles it is not "which backend is consistent" but **what the platform already
does**: no OS lets a caller learn what went wrong from the readiness bits. You are woken, you call
`read()` or `write()`, and *that* reports the error. A reader needs the wakeup so it can read 0; a
dial needs it and then checks `SO_ERROR`. Neither asks which callback fired — so waking the watched
direction costs nothing and `onError` pre-empting it costs the bytes.

**RED, predicted before running and matched exactly.** Unit level, 2 failures, both in the rewritten
`a watched direction outranks a failure that arrived with it`:

```
IoBackend_test.cpp(84): FAILED: selectReadinessCallback(handler, Readiness::Readable | Readiness::Failed) == &onReadable
IoBackend_test.cpp(85): FAILED: selectReadinessCallback(handler, Readiness::Writable | Readiness::Failed) == &onWritable
test cases: 5 | 4 passed | 1 failed   assertions: 11 | 9 passed | 2 failed
```

Kernel level, on Linux, 2 failures, one per affected backend — the data loss itself, on a
socketpair written to and then closed so the hangup and the unread byte arrive together:

```
a hangup over buffered data wakes the reader on every backend
  backend=poll    BackendParity_test.cpp:1116: FAILED: CHECK( reader.readable >= 1 )  ->  0 >= 1
  backend=epoll   BackendParity_test.cpp:1116: FAILED: CHECK( reader.readable >= 1 )  ->  0 >= 1
test cases: 30 | 29 passed | 1 failed   assertions: 277 | 275 passed | 2 failed
```

The companion case — a hangup with **nothing** buffered — passed before and after, and deliberately
asserts only `total() >= 1`. Which callback fires there is a genuine divergence, so asserting it
would pin the divergence instead of the property. `EV_EOF` mapping is untouched, and the rules file
now says why: it is an ordinary `shutdown(WR)`, so calling it a failure would make macOS report
every normal close as an error.

### The Minors

| | Taken | What |
|---|---|---|
| F3 | yes | `_wakeup` declared before the kernel descriptor in both `EpollBackend` and `KqueueBackend`. A mem-init list that throws does not run the class destructor and an `int` has none, so the epoll/kqueue fd leaked under exactly the descriptor exhaustion that makes it matter — and made `makeDefaultBackend()`'s fallback to poll less likely to succeed. |
| F4 | yes | The `#475` case asserted `dispatched == 1`, which also holds if a kernel reported only one of the two — it could stop testing anything and stay green. A control block now `REQUIRE`s `dispatched == 2` on two throwaway pipes with no withdrawal, so the premise is asserted rather than assumed. My own lesson from round 1. |
| F5 | yes | Landed early as `7e2e3ae`: the double reported muted registrations AND directions never asked for. Three cases of its own, in the portable binary. |
| F6 | yes | `Diagnostics.hpp:33` and `BackendParity_test.cpp:502`. `detail/WaitChunking.hpp:14` was already fixed in `870d12b` — the forwarded list had gone stale. `AGENT.md:27` left alone (another lane owns it). |
| F7 | yes | `posix/` and `windows/DefaultBackend.cpp` now route through `makeBackend(preferredBackendKind())` like `linux/` and `bsd/`. Harmless today; B7 adds a second Windows backend to that exact file. |
| F8 | **argued** | `FdRegistrationFailed` stays an exception. See §4: `await_resume()` is `void`, so the only shape available to B3 was to conflate a refused registration with `OperationCancelled` — strictly worse, and the exact defect `149659a` exists to prevent. Changing the signature is a change to the loop's contract, which is B4's to make. |

### A defect of mine, found and fixed mid-round

`7e2e3ae` broke the Linux build: `ReadinessHandler::handle` is a `platform::NativeHandle`, a
`HANDLE` on Windows and an `int` elsewhere, so the new probe's `.handle = nullptr` compiled on the
one platform I had checked. Fixed in `8d05bb7` with `platform::InvalidHandle`. Third time in this
task that something reached master which my local run could not see — but the first where the other
platform *was* available and I skipped it because the fix was urgent. **A new file gets built on a
second platform before it is pushed.**

I also nearly committed B4's uncommitted work: copying `IoBackend.hpp` out of the shared tree into
my build worktree brought their `virtual setPump`, their `IHostScheduler` include and their doc
edit along with my four. Linux caught it, because `HostDrivenBackend.hpp` at HEAD has no matching
`override` yet; on Windows alone it would have gone out inside my commit. The file is now rebuilt as
`HEAD` plus my edits with an assertion that none of their three markers survive. **R87 applies to
any source file another lane has open, not only to generated tables.**

### Gates

`clang-format` (pinned 22.1.8) clean on all ten files; `mkdocs build --strict` clean.
Linux `clang-debug` **28/28 including 15 hygiene**, `gcc-release` 16/16.
Windows `cl-debug` **30/30**, `clangcl-release` **30/30**.
macOS and FreeBSD are CI's to refute: the hypothesis in the commit message is that kqueue and Wfmo
already behaved this way, so the two new parity cases should pass there unchanged.
