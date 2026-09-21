# Task B2: `core::net_types` — one error vocabulary for both lineages

`core::net` carries contour's `NetErrorCode` today; fastcached's `Net` has its own, overlapping
but not equal. Every Phase B task after this one returns these codes, and both lineages' callers
have to keep compiling against the merged set. This task settles the vocabulary before B3 rewrites
the backends on top of it.

Read the plan's Task B2 (`docs/superpowers/plans/2026-09-18-core-cpp.md`) and the design spec's §2
item 5 and §1 module table (`docs/superpowers/specs/2026-09-18-core-cpp-design.md`).

## The merged enum, from the spec

```
Ok, Eof, Cancelled, Timeout, WouldBlock, BadHandle, ConnReset, ConnRefused,
AddressInUse, AddressNotAvail, AddressError, HostUnreach, PermissionDenied,
Unsupported, MessageTooLarge, SystemError
```

It is the union of the two, with two renames the spec's rename map names:

| Today | Merged | Lineage |
|---|---|---|
| `NetErrorCode::BadFileHandle` | `BadHandle` | fastcached |
| `NetErrorCode::Other` | `SystemError` | contour |

New to core-cpp, from fastcached: `AddressNotAvail`, `HostUnreach`, `PermissionDenied`.
New to fastcached, from contour: `AddressError`, `Unsupported`, `MessageTooLarge`.

`core::net_types` is `KIND INTERFACE`, header-only, and **links nothing** — it is what
`fastcache-cc` will link alone in Task C4, so anything it drags in becomes that binary's
dependency. Check what `NetError.hpp` and `IoResult.hpp` include today and keep the set minimal;
`<format>` in particular is not free.

## Sources

- core-cpp today: `src/core/net/NetError.hpp`, `src/core/net/IoResult.hpp` and their tests.
- fastcached at **`0708dd54dc7ee72622c8c0783c2bd4a06f0e9b21`**, read as a blob:
  `git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show 0708dd54:src/FastCache/Net/NetError.hpp`

Take fastcached's `isDeadlineExpiry` **with its comment**. That comment is a bug report written
out in full (LASTRADA-Software/fastcached#824): a deadline armed with `SO_RCVTIMEO` expires as
`EAGAIN`/`WouldBlock` on POSIX and `WSAETIMEDOUT`/`Timeout` on Winsock, both operands are
load-bearing at every caller, and narrowing it to `Timeout` alone makes two accept loops stop
accepting a quarter-second after they start, with one Debug line as the only symptom. Carry the
reasoning, rewritten against core-cpp's callers and citing the issue as a full URL. Do not carry
fastcached's inventory of its own call sites.

## Decide and record

1. **`NetError::toString()`'s rendering.** contour's produces
   `end of stream (accept) [errno 104]`; fastcached's produces
   `NetError(code=6 system=104 context=accept)` — a numeric code a reader has to look up. Pick
   one, say why, and record the change for whichever lineage loses under `Breaking` with a
   migration. A log line's shape is API for anyone grepping their logs.
2. **What `toString(NetErrorCode)` says for each new code**, in contour's style (lowercase,
   human, no punctuation): the existing function is a `constexpr switch` with no `default`, so
   adding a code makes every compiler point at it. Keep that property — it is why the switch has
   no `default` — and say so in a comment if it is not already said.
3. **Whether `Ok` stays in the enum.** Both lineages carry it as "not normally stored in an error
   result", and both comment that it is a sentinel. It is an invalid state in a
   `std::expected<T, NetError>`'s error channel. Keep it or drop it, but decide deliberately and
   record the reason; `.agent/rules/design-principles.md` has an opinion about a constructed
   object being usable.

## Tests, first

The plan names the set; each must fail before your change:

- `toString` answers for **every** code — write it so a new code without an answer fails the
  test, not only the ones that exist today (iterate the enum, do not list twelve strings).
- `isDeadlineExpiry` over both operands and over every code that is neither, with a case whose
  name says what #824 broke.
- `makeNetError` builds what its arguments say, including the empty-context and zero-`systemCode`
  paths that `toString()` branches on.
- fastcached's `NetErrorCode` round-trips: a value from each lineage survives the rename
  (`BadFileHandle` → `BadHandle`, `Other` → `SystemError`) with the same meaning at the call
  sites that used it. Find those call sites with `git grep` across `src/core/net/` — there are
  many for `Other`.

Write the case, run it, capture the RED verbatim, then implement, then capture the GREEN.

## The ripple

Renaming `Other` → `SystemError` and `BadFileHandle` → `BadHandle` touches every net source that
names them. That is mechanical, but it is also the whole point of this task, so:

- Sweep `src/core/net/**` and fix every site. `ctest -L hygiene` and the layering check must stay
  green.
- `tools/migrate/renames.json` — a concurrent task (C0) is creating it. If it exists when you
  land, add these two rows in the same commit and run `check-renames.py`; if it does not, say so
  in the report and I will route the rows to C0.
- Both renames are `Breaking` in `CHANGELOG.md`, with the migration spelled out (a consumer
  sed line is the migration, and both contour and fastcached have callers).
- `.agent/reference/provenance.md` gains the fastcached row for `NetError.hpp` with the pin.
- `docs/modules/net.md`: the error table.

## Then

- `python scripts/clang-format.py --check` on what you touched, the `clang-tidy` preset clean,
  `ctest -L hygiene`, `mkdocs build --strict` (you are changing a public header's comments).
- Local presets: WSL `clang-debug`, `gcc-release`; Windows `cl-debug` and `clangcl-release`.
  Build `clangcl-release` before every push, not only `cl-debug`.
- Push, watch `Build` to green.

## Concurrency

Other agents share this checkout. Yours is `src/core/net/NetError.hpp`, `IoResult.hpp`, their
tests, and the call sites of the two renamed codes. I will tell you in the dispatch message who
else is live and where.

- **Never `git pull --rebase`** — it refuses with another session's unstaged work, and stashing
  would take their edits. `git fetch origin`, then push; it is a fast-forward.
- For `CHANGELOG.md`, `.agent/reference/provenance.md` and `docs/`, read every hunk with
  `git diff -- <file>` and stage only your own with `git apply --cached` from a trimmed patch
  (`git add -p` is interactive and unavailable). Confirm with `git diff --cached -- <file>` and
  check `git show --stat` before pushing.
- Never run a formatter over a file another session is editing.

## Report

Write to `D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/task-B2-report.md`: RED/GREEN per test,
the three decisions and why, how many call sites the two renames touched, and the CI run ID.
Return only status, the commit range, a one-line test summary, and concerns.
