# Strands and the resume context

!!! note "Status"
    Implemented in 0.4.0: `core::async::Strand`, `KeyedStrands`, and the current-executor context
    (`ExecutorScope`, `currentExecutor()`, `ResumeTarget`), stated by `EventLoop`,
    `ThreadPoolExecutor`, the strands and `testing::ManualExecutor`, and read by `AsyncQueue::pop`.
    The API is in [async](../modules/async.md#strands-and-the-current-executor).

## What a strand is

A strand is an executor that runs what it is given one at a time, in the order given, on another
executor. State touched only from one strand needs no lock, whatever the executor underneath:
a thread pool of four runs four strands at once and never two tasks of one strand at once.
`KeyedStrands` is one strand per key, for state partitioned by a model, a connection or a session.

morph wrote its own (`morph::exec::detail::StrandExecutor`, PR #806) over its own `std::function`
executor. The user ruled that a strand is a generic concept a consumer of core-cpp's coroutines
should not have to invent, so core-cpp has one, and consumers use it.

### A task is a resumption

In a coroutine library the unit a strand serialises is not a function call but a *resumption*:
from the moment a submitted coroutine is resumed until it next suspends. A coroutine that suspends
has left the strand; another task may run before it comes back, and it comes back only if what it
awaited resumes it on the strand. That is the whole difficulty, and the second half of this note.

### One pump per strand

`core::async::IExecutor` accepts coroutine handles and nothing else, so the only way onto the base
executor is a coroutine. Each strand therefore owns one: a *pump*, whose every turn on the base
runs up to `StrandOptions::batch` queued tasks, each resumed inline under the strand's
`ExecutorScope`, and then either queues itself on the base again (work is waiting), suspends
(idle), or ends (closed, or a `KeyedStrands` key retired).

| Phase | Meaning | Left by |
|---|---|---|
| Idle | suspended, queued nowhere | a submit, which queues the pump on the base |
| Scheduled | queued on the base | the base resuming it |
| Running | running tasks | the end of a turn |
| Exited | ended | nothing |

A submit that finds the pump anything but Idle only queues the task; so one hand-off to the base
serves every task submitted while the strand is busy, and the steady state costs no allocation.
The idle decision is made in the pump's own `await_suspend`, under the strand's lock, so a submit
racing it either sees Running and only queues, or sees Idle after the pump has suspended and may
queue it -- never a pump that is resumed while it is still running.

**The bound matters on an `EventLoop`**: without it a strand with a steady inflow would hold one
drain slot for ever, and the loop's own `dispatchBatch` would bound nothing.

### What was rejected

- **A pump per activation** (a fresh coroutine each time the strand goes busy) is simpler and
  allocates a frame per activation; morph measured exactly that kind of churn in its strand and
  removed it.
- **Submitting each task to the base directly** and serialising with a flag cannot work: the
  strand never learns when a task *suspends*, because control returns to the base, not to the
  strand. A pump is the frame that gets control back.
- **Publishing before the step that can throw.** The pump's frame, the queue's room and the base's
  `submit` can each throw, and each comes before the phase it would change is published, or is
  rolled back: a strand left "scheduled" with nothing queued anywhere accepted every later submit
  and ran none of them. `StrandAllocation_test.cpp` fails every allocation of a first submit in turn.
- **Keeping the queue when the base refuses the pump.** Other threads queue behind a scheduled pump
  and are told their work was accepted; if the base then refuses the pump, keeping their work
  leaves it with no pump to run it, and a keyed strand idle in its registry for ever. A refusal
  means the base is not running this strand, so the queue is abandoned as destruction abandons it,
  the refused submitter gets its exception and its own work back, and the strand starts afresh on
  the next submit. `~Strand` waits for a hand-off still inside the base's `submit`, or it would
  free the work that submitter is about to be given back. A frame freed by the abandonment may
  submit to the strand again from its destructor; handing that to the base that has just refused
  would throw out of the destructor, so the thread doing the freeing drops it, as a closed strand
  would.
- **Swallowing what a task throws** (morph logged it): `core::async` has no logger and depends on
  nothing, and a strand that hid an exception would make a bug quiet. What a task throws out of
  `resume()` -- which no coroutine type of this module does -- propagates to whoever resumed the
  pump, and a replacement pump takes over the queue first. The dead pump's frame is left suspended
  at its final point by the exception, and must not be freed until the `resume()` that threw has
  returned; only the thread it threw on can know when that is, so it is freed by the next death on
  that thread or at thread exit (`detail::DeadPumpReaper`). Under MSVC's `cl` this path is not
  asserted: an exception crossing coroutine frames is the unwinding interaction `Task_test.cpp`
  already skips for, and on `cl-release` it was measured corrupting the scope chain and crashing
  the harness, while `cl-debug` and `clang-cl` pass.

### Lifetime

The strand's state is shared between the owner and the pump, because the owner cannot take back a
pump it has queued on the base: `~Strand` closes the state -- queued tasks are dropped, freeing a
chain rooted in a `DetachedTask` and leaving a coroutine a `Task` owns to its owner -- and a pump
the base runs later finds it closed and ends. A task *running* on another thread is waited for,
where threads exist and unless the destructor is called from inside one of the strand's own tasks;
under single-threaded WebAssembly a running task can only be on the destroying thread's own stack.
The base executor must outlive the strand and run what the strand queued on it.

**Destroying a strand from inside one of its own tasks is supported, and deferred by design.**
morph's CI deadlocked on this twice: a completion's frame held the last reference to the object
that owned the strand, released it on the strand, and the destructor waited for its own in-flight
task. Here the destructor asks `runningHere()` first and does not wait when the answer is yes. What
the pump touches after the task returns is the strand's shared state, which the pump itself holds,
so the strand's own machinery is not freed under the running task -- **the owner's other members
are**: the task must not touch them after the release, as with any `delete this`. The task runs to
its end; the pump then finds the strand closed, takes nothing more and ends, releasing its
reference. A task that parks after the release -- on an `AsyncQueue`, say -- holds that state too,
and is dropped rather than resumed when the queue hands it back. What was queued behind the task is dropped, as for any
destruction. The same holds for `KeyedStrands`: the key whose task destroys it is not waited for,
and every other key's running task is. No ownership pattern is required of the caller -- no weak
reference, no posted release -- and `Strand_test.cpp` and `KeyedStrands_test.cpp` each have the case,
under a hand-driven executor and, for `Strand`, on a pool thread.

### Callables, a closed answer, and ambient context

morph's switch from its own `StrandExecutor` needed three things a coroutine-only strand did not
have, and each was added as the smallest thing that meets it:

- **Callables.** Every one of morph's strand sites posts a lambda. A queue entry is a parked
  coroutine *or* a posted call; the call holds the callable by value in one allocation, which is
  made before the strand's lock is taken so that `tryPost` can decide under the lock and still
  leave a refused callable untouched. What a call throws takes the way a task's throw takes.
- **A closed strand's answer.** A Task handler's end must run even after its backend is gone: it
  settles the caller's sink and leaves the gate. `post` and `submit` drop work on a closed strand,
  as destruction does; `tryPost` and `trySubmit` return false and leave it with the caller, which
  runs it itself. A policy option -- "run inline when closed" -- was rejected: it would run work on
  whatever thread the submitter is, under no scope, with the strand's serialisation gone, and hide
  that from the caller.
- **Ambient context per task.** morph installs the action's session around every resumption of a
  handler. A task-local context carried by `Task`'s promise was rejected: restoring it would cost
  every `co_await` in every consumer, fastcached's hot path included, for a need that is per
  strand. An around-task hook costs one branch per task where it is unset, and `KeyedStrands`'
  hook is given the key, because morph finds the session by the model instance the key names.
  The hook is also told each task's kind, a posted callable or a coroutine resumption
  (`RunTask::kind`, core-cpp#53): a session installed around every task on a key reaches the
  callables posted to that key too, and a hook that means it for the handler's resumptions alone
  can now say so. An enum rather than an `isResumption()` flag, by the rule that a `bool` is not an
  API word where an `enum class` names what it means. It is read from what the task already holds,
  so it grows nothing.

### Sealing: a teardown with no window

`close()` drops what is queued, and has to: it is also the destructor, which cannot wait for work
that may never end. A consumer that wants its queued work run drains first -- `waitIdle()`, or the
base pumped until `idle()` -- and closes after, but between the drain and the close more work can
arrive: a handler's finish, a coroutine that parked on the strand and is handed back. `close()`
drops it, and with it whatever it had to settle; nothing outside the strand can close that window,
because admission is decided under the strand's lock. morph's own `StrandExecutor` avoided it by
waiting for in-flight work.

`seal()` closes the *offer* door ahead of the drain: `tryPost` and `trySubmit` refuse under the same
lock that queues, so an offer racing the seal is either queued before it, and runs, or refused
after it, and handed back. **It does not close the return door.** `post` and `submit` stay admitted
until `close()`, because a plain `submit` is how every coroutine the strand already admitted comes
back -- `ResumeOn`, `resumeOn(key)`, and an `AsyncQueue`'s push, close and stop through the
`ResumeTarget` it took. A first version refused those too, and the review found what that does: a
handler parked on a queue when the seal lands is dropped when the push hands it back, its detached
chain freed without its finish, or its awaiter left waiting -- the very loss the seal exists to
prevent, moved earlier (0.4.1's review, H1).

So a drain after a seal waits for two things, and the strand can see only one. `idle()` and
`waitIdle()` answer whether anything is queued or running; a coroutine suspended off the strand is
invisible to them, and the consumer is the only one that knows it is out there. morph counts its
in-flight runs and stops them; the sequence is seal, then drain until that count and `idle()` both
say done, then close. For `KeyedStrands` the registry is the offer door -- every `try` goes through
its `offer` under its lock -- and a key's strand is only ever submitted to, so it carries no seal of
its own; a strand made after the seal, for work coming back to a key that had none, is not kept
for reuse, and no kept strand is handed out.

### Reclaimed strands are kept

A key with no work has no strand, which is what keeps a program keyed by connection from holding
a strand per connection it ever saw. Made afresh each time, though, a key going idle and busy
again cost five allocations -- the strand, its pump's frame, its queue's room, its map node, and
the task's -- where morph's own `StrandExecutor`, which recycles through a spare list, cost one;
morph's CI holds an allocation budget per call. So the registry keeps up to 32 retired strands, their
pumps waiting idle, and up to as many map nodes (`unordered_map::extract` and `insert`), and a key
that needs a strand takes a kept one. A post to an idle key then allocates the call alone, and a
coroutine's submit nothing.

A kept strand may still be referenced: a coroutine that parked on it holds it through its
`ResumeTarget`, and must come back to *that* key. So a kept strand is given to a key only when
nothing else references it, which the registry reads from its `use_count()` under its own lock:
its own reference plus the pump's two (`StrandCore::PumpReferences`). That reading is exact, not
approximate, at that point: when it holds, nothing outside the registry and the idle pump
references the strand, and no new reference can appear except through the registry, whose lock is
held. One still referenced is skipped, and taken later, once the coroutine has let it go.

`KeyedStrands` retires a key's strand when its queue runs dry, under its registry's lock and the
strand's, in that order everywhere. A retired strand is not reused; one that is still referenced
(below) hands what it is given back to the registry, which gives it to the key's current strand or
makes one. So a key never has two strands at once, which is the defect morph's `StrandExecutor`
documented fixing twice.

## The resume context

### The gap morph found

morph's review of PR #806 found that a strand-bound coroutine came back to the strand only through
morph's own awaiters, which read a thread-local "resume context" morph installed. core-cpp's did
not: `AsyncQueue::pop` handed its consumer to the executor the *queue* was constructed over, on a
push, a close and a stop alike. A handler on a strand that awaited a queue fed from another thread
therefore continued off the strand, and raced the state the strand existed to protect -- silently,
since nothing about the resumption looks wrong from inside the coroutine.

### The mechanism

An executor states, for as long as it runs a task, that it is the executor running it; an
awaitable that another thread completes reads that once, in `await_suspend`, and resumes there.

- `ExecutorScope` is a stack-shaped guard over one `thread_local` pointer: constructing one pushes,
  destroying one pops, and scopes nest -- a strand's batch runs inside the scope of the loop turn or
  pool thread that runs its pump. Two stores and no allocation, whatever the scope carries.
- `currentExecutor()` reads the innermost. `Strand::runningHere()` asks whether *any* scope in
  force is the strand's, because a task that resumes something synchronously is still inside the
  task, and the strand still serialises it.
- `ResumeTarget` is what an awaitable holds across the suspension: the executor, plus -- for a
  strand, plain or keyed -- a reference that keeps the strand's shared state alive. A raw
  `IExecutor*` would dangle: a key goes idle and is reclaimed while the coroutine waits, and an
  owner `{ AsyncQueue queue; Strand strand; }` destroys the strand first, so a push landing
  between the two members' destructors -- or from another thread at any time after -- would submit
  to freed storage. So the executor a strand's task sees as current is that shared state, not the
  `Strand` object; once closed it drops what it is given, which frees a chain nobody owns. It costs
  one reference count per park on a strand, and nothing on a loop or a pool, whose scopes carry no
  reference. `Strand::runningHere()`, not a pointer comparison, is how to ask where a task is.
- `inline` thread-locals are one per linked image: a program that loads core-cpp's headers into two
  shared libraries with hidden visibility has two scope chains, and a scope stated in one is not
  seen by an awaitable compiled into the other. core-cpp ships static libraries, where there is one.
- Outside every scope an awaitable falls back to what it did before: `AsyncQueue` uses the executor
  it was constructed over. A test double that does not state itself leaves its coroutines there too,
  which is why `testing::ManualExecutor` does.

### Where it is stated, and at what cost

| Executor | Scope |
|---|---|
| `EventLoop` | one per turn, and one around the teardown drains |
| `ThreadPoolExecutor` | one per worker thread, for its life; one around an inline resume after `stop()` |
| `Strand`, a `KeyedStrands` key | one per batch |
| `testing::ManualExecutor` | one per resumption |

**Once per turn, not once per resumption, on the loop.** Guarantee G2 puts every resumption in
step 2 of a turn, so the answer cannot change between two resumptions of one turn, and the scope's
cost -- two thread-local stores -- is paid per turn rather than per flow resumed. fastcached's
parity gate is user CPU per request, and the drain is the path it measures. The CHANGELOG gives
both measurements for 0.4.0: the drain with and without the scope
(`tests/bench/ExecutorContextBench.cpp`, *Added*), and fastcached against its own reactor
(*Changed*).

### Who reads it, and who deliberately does not

`AsyncQueue::pop` reads it, on a push, a close and the stop callback alike; it is the one awaitable
in `core::async` that posts a resumption from another thread (`ResumeOn` goes where it is told;
`Task`, `whenAll` and `whenAny` resume by symmetric transfer on whichever thread finished the work).

**`core::net` does not, and must not.** Socket operations, timers, `delay` and `sleepUntil` resume
on their `EventLoop`, in step 2 of its turn (G2): the socket's read and write slots, its park ids,
its stop callbacks and the loop's park table are the loop thread's, and a completion resumed
anywhere else would read them from another thread. A coroutine on a strand that awaits a socket
comes back on the loop and hops back with `co_await ResumeOn { strand }`; a strand *over* that
loop makes the hop a queue append on the same thread.

### Consequences

- **Behaviour change (0.4.0, Breaking):** a consumer of `AsyncQueue` that parks while an executor
  runs it now comes back to that executor, not to the queue's. For a consumer on the queue's own
  executor nothing changes.
- **An executor a consumer writes** states itself with `ExecutorScope` around its resumptions, or
  its coroutines see no current executor and every awaitable takes its fallback.
- **Holding a scope across a `co_await` is a bug** of the profiling-zone kind: the destructor would
  restore another stack's scope. The destructor asserts it.
