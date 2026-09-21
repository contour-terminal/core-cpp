# Task A12: fix round 1 (Ruling R60)

The review (`task-A12-review.md`) verdicts all 15 findings FIXED at their named file:line, Approved the task, and confirmed both judgement calls (latch `isClosed()` on EOF; `FdInterest::None` mutes everywhere) as right and consistent across all four backends and all three socket classes. Three Important items remain, plus two corrections to the report.

1. **The regression guard for finding 2 hangs instead of failing** — `src/core/net/Socket_test.cpp:233`.
   If the listener ever goes silent again — the exact defect it guards — the case parks forever, and with no ctest `TIMEOUT` set it costs 1500 seconds and then reports "Timeout" rather than naming what broke.
   - Bound the wait inside the case so it fails with a message, and set a `TIMEOUT` on the test as a backstop.
   - `.agent/rules/testing.md` already says every wait is bounded and says what it waited for; this is that rule applied to the guard itself.
2. **Finding 13's sweep is incomplete.** You removed every `REQUIRE` from a `whenAll` arm, but the shape that turns a red into a hang survives in another form: an arm that returns early without stopping its sibling. It is at `Socket_test.cpp:91`, `posix/UnixSocket_test.cpp:103`, `EventSourceParity_test.cpp:104`, and in the cross-thread joins at `Tls_test.cpp:258,333`.
   - Fix those five so a failure fails the case, and say in the report what shape you swept for the second time, so the next person can repeat it.
3. **The HTTP head still diverges from a front-end on whitespace before the colon** — `src/core/net/HttpServer.cpp:144` trims it. RFC 9112 §5.1 makes rejecting such a field a MUST, precisely because a front-end that trims and a server that rejects (or the reverse) disagree about where a header ends.
   - Reject it. Exploitability is low here — one request per connection, always closed — but this is the same class as finding 1, and the parser already rejects `Transfer-Encoding` and conflicting `Content-Length` on the same reasoning.
   - Test both spellings: whitespace before the colon, and the valid case of whitespace after it.

## Two corrections to your report

- It claims nine findings have a captured RED; the reviewer counts six behavioural REDs. Correct the number and mark which findings are characterisation rather than regression tests.
- Finding 5's "no seam exists" sits awkwardly beside the injected rename primitive the platform task added in the same window for the same reason. Either say what a seam would cost here and why it is not worth it now, or note it as a Phase B item — but do not leave "unreachable" as the whole answer.

## Then

- The presets the constraints name, the sanitizers included; `clang-format --check`, `ctest -L hygiene`, `mkdocs --strict`.
- Rebase before pushing; other agents are on this branch. Follow the shared-file staging discipline for `CHANGELOG.md` and `provenance.md`.
- Push, watch CI and Portability to green.
- Append "Fix round 1" to `task-A12-report.md` with RED/GREEN for items 1 and 3.
