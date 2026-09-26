# Review: parity performance (232a595..3cdd22b)

Reviewer: read-only review of 23c67ec (starvation test) and 3cdd22b (parity performance), on
origin/release/v0.2.0. Nothing built or run; every finding below was checked against the source at
3cdd22b.

**Verdict: APPROVE with two should-fixes.** No blocker. The ParkMap, the inbound skip, the atomic
stop, the stop-callback skip and the epoll buffer are correct; the one real hazard is the new
`Park::watch` pointer on a path the socket contract forbids, and one test claims more than it checks.

## Findings

### S1 (should-fix): a displaced watch slot leaves `Park::watch` dangling -- a lost wake becomes a use-after-free

`src/core/net/EventLoop.cpp:971-981`, `:1251-1262`, `:1206-1208`, `:1392`; the invariant stated at
`src/core/net/detail/ParkTable.hpp:301-305`.

`dropWatch` clears `watch` only in the parks the watch's two slots name. A park whose slot was
overwritten keeps a pointer to the watch and is named by nothing. Overwriting is a contract
violation (one read and one write per socket), but the guards are `assert` here and
`contract::claimReadSlot`, which is Debug-only (`ISocket.hpp:72`). So in Release:

1. Read park A on fd 5 (UntilClosed): `W.reader = A`, `A.watch = &W`.
2. A second read on fd 5 (the violation): `W.reader = B`, `B.watch = &W`.
3. `notifyHandleClosing(5)`: A is not in `_byHandle` any more (watched parks are no longer indexed)
   and not in a slot, so A is not collected, not queued and not marked; `dropWatch` clears
   `B.watch` only and frees W.
4. `~EventLoop` -> `unparkEverything` -> `releaseWatchSlot(A)` (`:1392`) dereferences the freed W. A
   stop-token cancel or deadline on A reaches it the same way, through `unregisterPark`.

Before 3cdd22b the same violation left A stranded, as the comment at `:966-969` says ("the
registration would stay sound"): `releaseWatchSlot` looked the watch up by handle and compared ids,
and close still found A through `parksOn`. Now it is a use-after-free, and that comment and the
"never dangles" line in `ParkTable.hpp` are false. The fix is cheap and on a cold path: before
overwriting a slot, clear the displaced park's pointer
(`if (auto* old = _parks.find(watch->reader)) old->watch = nullptr;`, and the same for `writer`), so
the rule "`watch` is non-null only while a slot names the park" holds whatever the caller does. A
public `ParkEntry` with UntilClosed and an interest that has neither Read nor Write gets a `watch`
and no slot, the same shape; the same fix, or refusing that entry, covers it.

### S2 (should-fix): the recycle test's name claims a check it does not make

`src/core/net/detail/ParkTable_test.cpp:124`. The case is named "...and keeps no park that still
holds work", but nothing in it hands `recycle` a park whose `parked` is non-empty. The
`park->parked` guard in `ParkTable::recycle` (`ParkTable.hpp:542`) can be deleted and the case stays
green. That guard is what stops a recycled park from silently dropping (and so freeing) a chain.
Add the half the name promises: acquire, give `parked` a `ParkedWork` with a claim, `recycle`, and
check that the next `acquire` is a different object and the claim is still armed. Or rename the case.

### N1 (nit): `recycle` is `noexcept` and allocates

`ParkTable.hpp:562`: `_spare.push_back` can throw `bad_alloc` until the vector has grown to 64, and in
a `noexcept` function that means `std::terminate`. `_spare.reserve(MaxSpareParks)` on the first
`acquire`, or at construction, removes it.

### N2 (nit): the provenance row for `ParkTable.hpp` does not mention `ParkMap`

`.agent/reference/provenance.md:207` still describes the file as Task B4's `ParkId`, `ParkEntry` and
table. `ParkId` moved out in B13, and the file now also holds the open-addressing map and the park
recycling. The new test row is fine.

## Checked and clean

**ParkMap (focus 1).**
- Backward-shift deletion (`ParkTable.hpp:420-432`): an entry at `next` moves into `hole` exactly when
  `hole` lies cyclically in `[home, next)`. The `<=` cannot be an equality because `hole != next`.
  This is the standard condition, and it scans to the first empty slot.
- Load factor: `insert` grows when `(size+1)*2 > capacity`, so at least half the slots are always
  empty. Every probe loop terminates, including `find` of an absent key and `find(0)`.
- `home` shifts by `_shift == 64` only while `_entries` is empty. Every path that calls `home` is
  guarded: `find` and `erase` return on `_size == 0`, and `insert` grows first. After `clear()` the
  capacity and `_shift` are kept.
- Key 0 against `ParkId::invalid()`: `erase` refuses `!id`. `find(0)` probes to an empty slot and
  returns null, because the comparison is only made against occupied slots. `add` issues
  `++_nextId`, never 0. The zero-id test distinguishes: `home(1)` is slot 9 of 16, so the old
  `erase(0)` matched empty slot 0, decremented `_size`, and `CHECK(map.size() == 1)` failed.
- Resize re-inserts through `insert` with `_size` reset. It cannot recurse into another grow.
- Stale ids: the key is the never-reused 64-bit id, not a slot or an address. A recycled `Park`
  object is filed under a fresh id, so a stale `ParkId` (in a watch slot, in `_ready`, in
  `_closedParks`, in a cancel) resolves to null exactly as before. A recycled park's handler address
  is reused, but only after `unregisterPark` detached it. That is the same contract that freeing it
  relied on. `recycle` resets every field of `Park`: checked against the struct, with `parked`
  guarded rather than reset.
- Teardown: `takeAll` moves the parks out in `forEach` (no insert or erase inside), then `clear()`.
  A chain freed afterwards that re-enters `unregisterPark` or `registerPark` finds an empty map or
  inserts into it, and the fixpoint collects it.
- `readinessCount`: `_readiness` goes up in `add` and down in `dropHandleIndex`, both only for
  `handle != InvalidHandle`. `dropHandleIndex` is reached only from `take`, once per park. Same
  count as `_byHandle.size()` was.

**Inbound skip and atomic stop (focus 2).** Every write to `_inbound` (`submit :639`,
`schedule :661`, `post :742`, `requestCancel :1091`) sets the flag under the mutex, after the push.
The turn clears the flag under the same mutex before it swaps. So a push after the swap always
sets it again, and the flag is only a hint in front of a mutex-protected queue. A post that races
the lock-free `false` load has already called or will call `wake()` after its push. The wake
channel is sticky, so the wait this turn enters returns and the next turn takes the post. That is
the same exposure as a post landing just after the swap before this change. `cancelQueued` erasing
while the flag is set only costs one extra lock. `stop()` does a release store and then `wake()`;
`run()` does an acquire load each turn, and the flag is never reset. A stop between the check and
the wait is caught by the wake.

**ResultAwaitable (focus 3).** `stop_possible()` is false only with no stop state, or with every
source gone and no request made (`StopToken.hpp:21`, `:195-205`, and std). No source can be created
from a token, so once false it stays false. A token already stopped reports true, and was returned
early at `:187` anyway. `await_resume` resets `_cancelReg` unconditionally, which is a no-op when
it is disengaged.

**Epoll buffer (focus 4).** Only `span{entries, ready}` is read, and only when `ready > 0`. Entries
beyond `ready` are never touched. The array is filled completely before `_batch.dispatch()`, so a
re-entrant `wait` from a callback could not corrupt the walk. `_events` is constructed before
`_wakeup` and `epoll_create1`, which matches the comment.

**Other 3cdd22b changes.** `Parked` moves, `take` and `abandon` are field-by-field equivalents of
the old exchange. `AbandonClaim`'s move constructor nulls the source, so `take` leaves the entry
empty. The callback branch of `drainReadyQueue` pops before calling, and a callback entry has no
`parked` to lose. `_hostDriven` is initialised in declaration order. `dropWatch` and
`notifyHandleClosing` collect the watched parks from the slots before `dropWatch` clears them.

**The starvation test (focus 5).** The test is bounded: at most 256 rounds, each with at most 8
settle turns, and every scripted wait is zero. It distinguishes the fixed behaviour from the bug.
If the writer is never woken, round 0's drain takes the pre-park bytes (not counted, because
`wakes == 0`), and every later drain finds 0, so `stalledWakes` becomes about 255 and
`CHECK(stalledWakes == 0)` fails. If the writer is woken, the pair has just been emptied, so the
writer moves bytes on every wake. Removing the leftover `pushTimeout` is correct: a `spawn` wake
consumes no scripted step. The `ParkMap` random run (200k steps, fixed SplitMix64 seed, zero ids,
absent ids and ids already taken) checks every step against `std::unordered_map`.

**CHANGELOG and provenance (focus 6).** The entry is under `[Unreleased]` / Changed and describes
the behaviour and the measurement factually. The ParkMap zero-key bug was introduced and fixed
inside the same commit, so it correctly earns no Fixed entry. The test file has a provenance row,
which follows the convention for test files.
