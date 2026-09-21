# Task A11 review — Phase A gate pass 3 (`core::async`)

Diff reviewed: `b505db8..origin/master` (`af392a6`), filtered set. The tree moved during the
review: a thirteenth commit, `f28462f fix(tests): the namespace gate follows the whole directory
path, not its first segment`, appeared on local `master` and is **not** in the reviewed diff and
was not on `origin/master` when I looked. It is the controller's added item, and I verified it
separately (see the verdict for 5b and Issues → Important 1).

### Finding Verdicts

| # | Verdict | Where | Would the test fail without the fix? |
|---|---|---|---|
| 1 (Critical) | **FIXED** | `src/core/async/WhenAny.hpp:65-75` (state by `shared_ptr`), `:113` (`auto const race = promise.state;`), `:206-207` (`auto const held = _state;`), `:219-222` (`make_shared`) | Yes. `WhenAny_test.cpp:333` resumes both children from inside their own `StopCallback` (`StopCallbackEvent`, `:109`), and each holds a second registration (`NoteStop` guard, `:160`) so libstdc++'s `_M_request_stop()` does not take its `if (__last_cb) return true;` exit and comes back to `_M_lock()` on the child state. Unfixed, that state is freed by the awaiter's destruction one frame earlier — the reported ASan `heap-use-after-free` in `_Stop_state_t::_M_lock()` is exactly where the walk lands. |
| 2 | **FIXED** | `src/core/async/CMakeLists.txt:15-28` (`PUBLIC_LIBS Threads::Threads` under `CORE_CPP_USE_THREADS`), `:32-38` (configure gate), `:95-102` + `src/core/async/StopTokenLinkSmoke.cpp` (consumer link smoke) | Partly. The configure gate fails by name when `PUBLIC_LIBS` is dropped (`INTERFACE_LINK_LIBRARIES` is then `…-NOTFOUND` and does not match). The link itself cannot be made to fail on any host here; see Issues → Minor 5. |
| 3 | **FIXED** | `src/core/async/WhenAny.hpp:114` (`!race->winner.has_value() && !promise.cancelled` — the latch *is* `winner`, taken in the same statement), `:276` (`if (!_state->winner.has_value() && _parentToken.stop_requested())`) | Yes, both directions. `WhenAny_test.cpp:299` requires `REQUIRE_FALSE(threwCancelled)` and `winner == 0`, which the old unconditional `if (_parentToken.stop_requested()) throw` fails. The opposite direction — cancellation before any winner is still `OperationCancelled` — is `WhenAny_test.cpp:426` (non-Windows) and, on every platform, `REQUIRE(parentCancelled)` in the new case at `:333`. |
| 4 | **FIXED** | `src/core/async/WhenAll.hpp:202`, `src/core/async/WhenAny.hpp:310` (`std::is_same_v<Tasks, Task<void>>`, no `remove_cvref_t`) | Yes. `WhenAll_test.cpp:24-27` and `WhenAny_test.cpp:29-32` `static_assert(!WhenAllTakes<Task<void>&>)` / `!…<Task<void> const>`; with `remove_cvref_t` the concept was satisfied and the assert failed. |
| 5 | **FIXED** | `tests/cmake/check-cmake-hygiene.cmake:305,319,327` (`firstNamespaceSeen` limits the *directory* half; the lowercase half now runs on every declaration) | Yes — I ran it. The selftest case `namespace core::foo { namespace Detail { } }` (`check-cmake-hygiene-selftest.cmake:64`) is reported "was not refused" against the `b505db8` scanner. |
| 5b (controller's addition) | **FIXED, but not in the reviewed diff** | `tests/cmake/check-cmake-hygiene.cmake:127` (`CORE_CPP_HYGIENE_PRIVATE_DIRECTORIES backend bsd darwin detail emscripten linux posix windows`), `:294-302` (expected namespace assembled from every segment) — commit `f28462f`, local only | Yes — I ran both halves. Against the `origin/master` scanner, `src/core/foo/testing/Fake.hpp` declaring `namespace core::foo` passes clean; the selftest fails naming exactly that case. The clean-tree row `src/core/foo/posix/Impl.cpp` declaring `core::foo` proves the private-directory exemption (without it the *clean* tree would be refused). `testing/` and `runtime/` are namespaces, `posix/ windows/ linux/ bsd/ darwin/ emscripten/ detail/ backend/` are layout — the list matches `cmake/CoreCppTargets.cmake:22-23`. Whole-tree run under the corrected rule: 401 files clean. |
| 6 | **FIXED** | `src/core/async/WhenAll.hpp:19` (`<type_traits>`) | No test (an include); the header is self-contained. |
| 7 | **FIXED** | `src/core/async/Task_test.cpp:8` (`<stdexcept>` in, `<string>` out) | No test (includes). |
| 8 | **FIXED** | `HasStopToken<Promise>` at all five sites: `Cancellation.hpp:59`, `Task.hpp:148` and `:240`, `WhenAll.hpp:165`, `WhenAny.hpp:243`; `<utility>` gone from `Awaitable.hpp` | Yes, as a compile-time test: `src/core/async/Awaitable_test.cpp` (new) asserts both concepts over `Task<void>::Awaiter`, `Task<T>::PromiseType`, `ThisCoroStopToken` and four near misses. |
| 9 | **FIXED** | `src/core/async/WhenAny.hpp:195-212` (`WhenAnyCancelBridge`), `<functional>` removed from the header | Covered by the Critical case, which is the same code path. |
| 10 | **FIXED** | `src/core/async/WhenAny.hpp:68` (`std::optional<std::size_t> winner`), `:268` (`await_resume`), `:296-300`/`:312` (docs); `detail::WhenAnyNoWinner` is gone from the whole tree | Yes. `WhenAny_test.cpp:370` requires `REQUIRE_FALSE(winner.has_value())` on a value seeded to `7`; the old sentinel form cannot compile. |
| 11 | **FIXED** | `.clang-tidy:229` (four stop-token names removed from `FunctionIgnoredRegexp`), `:234` (kept on `MethodIgnoredRegexp`), `ClassMethodCase` **and** `ClassMethodIgnoredRegexp` both removed | Config, no test. The fall-through is real: `findStyleKind` uses `SK_ClassMethod` only `if (Decl->isStatic() && NamingStyles[SK_ClassMethod])`, and a style exists only if one of its options is set — removing both options is what makes a static member fall to `Method`. No free function in `core::async` loses its exemption (`swap` is camelBack already). |
| 12 | **FIXED** | `src/core/async/Task_test.cpp:153` (`!defined(CORE_ASYNC_SYMMETRIC_TRANSFER_IS_TAIL_CALL)`), `src/core/async/CMakeLists.txt:57-79` (last `-O` off the build's own flags, `-O2`/`-O3`/`-Ofast` only, reported via `message(STATUS)`) | The RED is a standalone GCC 14.3 reproduction (`-Og`/`-O1` → rc 139). The skip is now conservative in every case I can construct: no `-O`, `-O`, `-Os`, `-Oz`, `-Og`, `-O1` and an empty `CMAKE_BUILD_TYPE` all skip. |

Deferred to B1 (`Task.hpp:157`, `WhenAll.hpp:121`, `WhenAny.hpp:60`) — untouched, as instructed.

### The Cancellation Lifetime Rule

The rule as stated: *the race state is reference-counted, and every call into it that can run
foreign code holds a reference for that call's duration.* I walked it rather than trusting the
test.

**What the rule has to protect.** Not the `WhenAnyState` struct for its own sake, but the
`StopSource childStop` **inside** it. `std::stop_source::request_stop()` on libstdc++ is
`if (auto* s = _M_state._M_get()) return s->_M_request_stop();` — a raw pointer into the
refcounted heap state; MSVC is the same shape. The refs on that heap state are `childStop`
itself plus each runner promise's `StopToken token`. Destroying the awaiter drops `childStop`
(via `_state`) *and* every runner frame (via `_runners`), so it drops the count to zero — and
`_M_request_stop()` is still walking that state. Holding `WhenAnyState` alive holds `childStop`
alive, which holds one ref, which is sufficient. So the rule is aimed at the right object.

**The walk.** Parent token stopped → the parent source's list walk invokes `_parentReg`'s
callback, `WhenAnyCancelBridge::operator()`, which first copies `_state` into the local `held`
(`WhenAny.hpp:206`) and then calls `held->childStop.request_stop()`.

- *Child A resumed from its own stop callback, unwinds.* Its body's locals (its own
  registrations, the awaitable temporary) are destroyed as it unwinds, each deregistering under
  the child state's lock — that state is alive because `held` holds it. A's `FinalAwaiter::
  await_suspend` copies `promise.state` into `race` (`:113`), sees `promise.cancelled`, latches
  nothing, decrements. `remaining` is still ≥ 1 (B has not decremented), so no transfer: the
  awaiter cannot be destroyed here.
- *Child B, the last loser.* `--race->remaining == 0` → it returns `race->continuation`. Note
  `race` is a local of `await_suspend`, on the machine stack, not in the coroutine frame — the
  frame may be destroyed under it and nothing after `request_stop()` touches `promise` or `self`.
  Symmetric transfer pops `await_suspend` before the parent resumes, so B's frame is not
  destroyed while a stack frame points into it.
- *Parent resumes, `await_resume()` throws, the awaiting frame unwinds.* The `WhenAnyAwaiter`
  temporary is destroyed. Member order matters and is right: `_parentReg` first (the callback
  currently on the stack — the standard requires `~stop_callback` not to block when it runs on
  the current thread; libstdc++ uses its `_M_destroyed` flag, our fallback's
  `deregisterCallback()` returns at once when `_requester == currentThread()`,
  `StopToken.hpp:258-268`), then `_state` (one ref dropped), then `_runners` (the child frames,
  and with them their `token` refs).
- *Back in `_M_request_stop()`.* `__destroyed` is set, so it does not touch the callback; `A`'s
  guard was still in the list when A's callback was popped, so `__last_cb` is false and it does
  `_M_lock()` — on the state `held` is keeping alive. **This is the line that was the
  use-after-free.**

**Is the reference held across *every* call that can run foreign code?** I found two such calls
and both hold one: `WhenAnyCancelBridge::operator()` (`held`) and `FinalAwaiter::await_suspend`
(`race`, taken before the `if`, and the only thing dereferenced after `request_stop()`). The two
other places that run foreign code are protected by the start-phase guard instead, which is
sound: `WhenAnyAwaiter::await_suspend` keeps `remaining` at `tasks.size() + 1` across
`_parentReg.emplace(...)` (which runs the bridge inline on an already-stopped parent) and across
every `_runners[i].handle().resume()`, so nothing in the start phase can drive `remaining` to
zero, resume the parent, or destroy the awaiter. `await_resume()` runs no foreign code.

**The second path — a child completing normally while a stop is in flight.** Bridge →
`childStop.request_stop()` → A's callback → A resumes and *completes* → A's final awaiter
latches `winner = A` and calls `race->childStop.request_stop()` **nested inside the outer walk
of the same state**. Both branches make that a no-op rather than a re-entrant walk: libstdc++'s
`_M_try_lock_and_stop()` fails on the already-set stop bit and returns `false`; our fallback's
`StopState::requestStop()` takes the lock, sees `_stopRequested` and returns `false`
(`StopToken.hpp:216-231`). Then `--race->remaining`, and if it is the last, transfer to a parent
that now sees a winner and does *not* throw. Nothing in that path dereferences a state that
`race` or `held` is not holding. So the rule covers it — but nothing tests it (Minor 1).

**Does it hold on both `StopToken` branches?** Yes, and for the same reason on each, which is the
point: the fix stops asking the stop facility to keep its own state alive across a request.
- `std::stop_token` (libstdc++, MSVC): the raw-pointer reach described above; the shared_ptr on
  `WhenAnyState` supplies the ref that `stop_source` does not.
- The fallback: `StopSourceFallback::request_stop()` already takes `auto const state = _state;`
  (`StopToken.hpp:406-413`), and `StopState::requestStop()`'s `finishCallback()` touches `this`
  after each callback — so the fallback was self-protecting *and documented as such*, not
  accidental. The report calls it "an implementation detail of ours and not a guarantee", which
  is the right characterisation for depending on it from outside. The fallback binary therefore
  passes before and after; keeping the case there as a regression guard is correct.

One thing the rule deliberately does **not** do, and should not: it does not try to keep the
`StopCallback` objects alive. They are destroyed from inside their own callbacks, which the
standard and both implementations permit. That is stated in the report and is correct.

### Strengths

- The ownership rule is the right one and is stated where it belongs — the file comment at
  `WhenAny.hpp:22-33`, the member comment at `:62-64`, and at each of the two call sites. A reader
  who adds a third call into the state has the rule in front of them.
- The Critical test is an honest reproduction, not a proxy. It resumes from inside a stop callback
  (`StopCallbackEvent`, `WhenAny_test.cpp:109`), destroys the registration from inside its own
  callback as the real awaitables do, and the extra `NoteStop` guard (`:160`) exists precisely so
  libstdc++'s `if (__last_cb) return true;` does not short-circuit the walk before the freed
  access. That detail is reasoned about in the report, and it checks out against libstdc++'s
  `_M_request_stop()`.
- Finding 3 is fixed at the right place: the latch *is* the winner optional, set in the same
  statement that decides, so there is no window between "decided" and "a winner was recorded", and
  a cancelled child cannot latch at all. Both directions have a case, and the new Critical case
  covers the throwing direction on Windows, where the older case is `#ifndef _WIN32`.
- Finding 12's fix moves the decision to whoever builds, reads the *last* `-O` exactly as the
  compiler does, and reports its answer in the configure log — "a gate that does not report reads
  as passed" applied to itself.
- The `.clang-tidy` change removes a real duplicate rather than adding a third copy, and removes
  both `ClassMethodCase` and `ClassMethodIgnoredRegexp` — leaving the regexp alone would have
  created a style with no case and silently disabled the check for static members.
- The whole-tree run under the corrected namespace rule was actually done and its one finding was
  not swept: `src/core/tui/completer/`'s eight files are an allowlist row with a written reason and
  both migration options costed, not a quiet edit. See Important 2 for what is still missing there.
- The report is candid where it cannot prove something (finding 2's RED, the fallback passing
  before and after) rather than dressing it up.

### Issues

#### Critical

None.

#### Important

1. **The controller's added item is fixed in an unpushed, un-CI'd commit and is not in the task
   report.** `f28462f` (local `master` only; `origin/master` was `af392a6` throughout my review)
   carries the whole-directory-path rule, its two self-test rows and the CHANGELOG entry. The green
   CI the report cites (Build `35526193943`, Portability `35527475488`) is on `af392a6` and
   predates it, so `ctest -L hygiene` has not run this rule anywhere but locally. I ran it myself —
   selftest 25/25 with the new scanner, RED against the old one, whole tree 401 files clean — but
   that is my run, not the gate's. `task-A11-report.md` also still describes finding 5 as "the
   directory half still applies to the first namespace only", which is now stale. Push, get CI
   green, and fold the second hole into the report.
2. **The corrected rule's one tree finding is deferred to an owner, with no ticket.**
   `src/core/tui/completer/`'s eight files declare `core::tui` where the directory and
   `docs/modules/tui.md` both say `core::tui::completer`, and the resolution is eight allowlist rows
   (`tests/cmake/check-cmake-hygiene.cmake:179-200`) pointing at "the tui owner". Every other
   deferral in this file carries an issue number (`core-cpp#23` two rules up, `core-cpp#15` in
   `Task_test.cpp`); this one carries only prose, so nothing will surface it again, and the gate is
   now permanently weakened for eight public headers while `docs/modules/tui.md` states a namespace
   the code does not have. Open an issue and reference it from the rows.

#### Minor

1. **The "child completes normally while a stop is in flight" path is untested.** I convinced
   myself it is safe (nested `request_stop()` is a no-op on both branches; `race` holds the
   reference across it), but the new case has both children unwind cancelled, and the
   winner-that-already-ran case uses `ManualEvent`, which registers no callback. A third case —
   one `StopCallbackEvent` child whose `await_resume` does *not* throw — would close the gap the
   Critical finding's own wording points at.
2. **`whenAll`'s runner promise still holds `WhenAllState*` raw** (`WhenAll.hpp:74`). Sound today,
   because `whenAll` owns no `StopSource` and never calls `request_stop()`, so nothing of its state
   is touched after foreign code runs. But the ownership rule now lives in only one of two
   near-identical runners, and B1's brief is to collapse them into one — a sentence in
   `WhenAll.hpp` saying *why* the raw pointer is still correct there would stop the collapse from
   picking the wrong half.
3. **`HasStopToken` is stricter than the probe it replaces.** The five sites used
   `requires { awaiting.promise().stopToken(); }`; the concept adds
   `-> std::convertible_to<StopToken>` (`Awaitable.hpp:40`). A promise with a mistyped
   `stopToken()` now silently takes the `if constexpr` else branch and loses cancellation
   propagation, where it used to be a compile error. Deliberate (the test asserts
   `!HasStopToken<PromiseWithWrongToken>`), but it is a silent-failure mode and worth one line in
   the concept's Doxygen.
4. **The GCC optimisation probe reads only the global flags.** `src/core/async/CMakeLists.txt:59-63`
   uses `CMAKE_CXX_FLAGS` plus `CMAKE_CXX_FLAGS_<CONFIG>`. Under a multi-config generator
   `CMAKE_BUILD_TYPE` is empty, so it sees `-O0` and skips (harmless). The one direction that bites
   is a Release tree with a per-target or per-directory `-O0`/`-Og` override: the macro is defined,
   the case runs and the binary dies. core-cpp adds no such override today.
5. **Finding 2's link smoke proves the shape, not the failure.** `core-cpp-async-link-smoke` is
   exactly right — `core::async` alone, fallback forced, deliberately not a `core_cpp_add_test()`
   so `core::testing_main → core::base → Threads` cannot hide it — but it has never been observed
   red on FreeBSD or AppleClang, only green after the fix. The configure gate is the regression
   guard that matters; that is an honest and well-documented limit, not a defect.
6. **The link smoke carries the `hygiene` label** (`CMakeLists.txt:101`), so `ctest -L hygiene`
   now builds and runs a compiled binary among the tree checks. Harmless, mildly surprising.
7. **The loser contract change is under `Changed`, not `Breaking`.** The runner no longer swallows
   `OperationCancelled` (`makeWhenAnyRunner`, `WhenAny.hpp:181-184`); a consumer task that catches
   its own cancellation and returns is now reported as the *winner* of a race it lost, and a task
   that throws `OperationCancelled` for its own reasons now yields `std::nullopt` where it used to
   yield its index. Both are stated plainly in `docs/modules/async.md:104-113` and in the CHANGELOG
   `Changed` entry, so it is recorded — but it changes what a consumer's task must do, which is the
   test the `Breaking` section applies elsewhere in the same file.
8. **`FunctionIgnoredRegexp` and `MethodIgnoredRegexp` remain two ~800-character near-duplicates**
   that must be edited in lockstep. The comment says so, and says why YAML anchors are out; nothing
   enforces it. One of three copies removed is real progress and the finding said "if you can".

### Assessment

**Task quality: Approved.** All twelve findings and the controller's added hygiene item are
genuinely fixed: the Critical ownership rule holds on both `StopToken` branches for the same
reason on each, every call into the race state that can run foreign code holds a reference (the
two that need one) or is covered by the start-phase guard (the two that do not), and the new case
reaches the bug by the same route the real awaitables take rather than by a proxy. I verified both
hygiene holes myself against the old and new scanners. What remains is process, not code:
`f28462f` is unpushed and un-CI'd, `task-A11-report.md` does not yet cover the second hole, and the
`core::tui::completer` deferral needs a ticket rather than a comment.
