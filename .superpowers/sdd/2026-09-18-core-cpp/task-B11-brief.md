# Brief for Task B11

Binding references (read these too): Global Constraints at D:/core-cpp/.superpowers/sdd/2026-09-18-core-cpp/global-constraints.md; the design spec at D:/core-cpp/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file).


**Every B task follows this pattern:**
1. Port the named tests from `D:\fastcached\src\FastCache\{Async,Net}` (renamed per the Part I §2 rename map, into core names) and/or adapt the contour tests.
2. Build and confirm the new cases FAIL.
3. Implement.
4. Confirm PASS on Windows (`clangcl-debug`, `cl-debug`) and WSL (`clang-debug`, `gcc-debug`, `clang-tsan`, `clang-asan-ubsan`).
5. Push, and require CI `ci-ok` green.
6. Commit.

**Sources:** every fastcached path named in Phase B is read as `git -C D:\fastcached -c core.autocrlf=false -c core.eol=lf show origin/master:<path>`, never from the `D:\fastcached` working tree.

Implementation must preserve the lifetime rules in `.agent/rules/wire-and-protocol.md` of fastcached `origin/master` (§Sockets, §Dialing and the reactor, §Socket and coroutine lifetime) and `D:\fastcached\AGENT.md` (grep: lifetime, ParkedWork, teardown, IOCP). Each rule carried over is written to `.agent/rules/async-and-net.md` in the same task that implements it.


### Task B11: TLS merge (`core::net_tls`)
- [ ] Tests first: contour `Tls_test` (client + server, hostname/IP verification, `constantTimeEquals`), fastcached `TlsContext_test`, `TlsSocket_test`, sharing one cert fixture. Plus: `makeSelfSignedServerContext(SelfSignedOptions{.commonName = "core-test"})` puts that CN in the certificate.
- [ ] Implement `ITlsContext`, `makeTlsServerContextFromFiles`, `makeSelfSignedServerContext`, `makeTlsClientContext`, `wrapTls`, `certificateFingerprint`. No OpenSSL type in headers. Windows TLS runs only in the `cl-release-tls` CI leg (vcpkg).
- [ ] Commit `net: one TLS layer: fastcached's record pump with contour's client verification`.


---

# Checked at the pin before you were dispatched — three things the brief above does not say

Method: `git ls-tree -r --name-only 0708dd54 | grep -iE "tls|cert|ssl|pem|crypto"` over the whole
tree rather than checking the named files. **Run it yourself and say what it still gets wrong** —
this is the seventh source-list correction in this project.

## 1. A stated requirement with no gate, and fastcached has the gate

The design spec says, of TLS: **"No OpenSSL type appears in any header."** That is a requirement with
nothing enforcing it. **core-cpp's `tests/cmake/` has nine checks and none of them is about this**,
and `grep -rln openssl tests/ cmake/ scripts/` returns nothing.

fastcached has exactly this gate, with a self-test, at the pin:

```
scripts/check-crypto-seam.cmake
scripts/check-crypto-seam-selftest.cmake
```

**Port it in the same task that creates the surface it guards.** A seam that ships ungated is one
refactor from being a seam in prose only, and retro-fitting the check onto an existing violation is
a different and worse job than landing them together. Register it with the `hygiene` label; it reads
the source tree, so it is **`tree-level`** — which means **adding it also means adding a `style`
step for it, or it runs nowhere in CI.**

This is the second instance found of the class B4 named while writing the clang-tidy procedure:
**a principle stated without a procedure is a sentence people agree with and do not execute.** The
first was *"a gate that does not report reads as passed"*, which had ten lines of correct reasoning
and no instruction. B13 is auditing `.agent/rules/` for more; this one is the spec's, not the
rulebook's, which is worth knowing — **the class is not confined to the rulebook.**

## 2. core-cpp already generates its certificates and must keep doing so

fastcached ships fixtures on disk — `testdata/tls/{server.crt,server.key,README.md}`. **Do not
import them.** contour's `Tls_test.cpp`, already in the tree, calls
`core::net::generateSelfSignedCertificate("contour-dev")` and `makeSelfSignedServerContext()` at
runtime, and core-cpp has **no `.crt`, `.key` or `.pem` anywhere.**

Generating is better and the reason is not convenience: **a committed certificate expires**, and it
expires into a test failure that looks like a TLS defect years after anyone remembers the fixture
exists. Keep generation; the plan's *"sharing one cert fixture"* means one helper, not one file.

`SelfSignedOptions{.commonName = ...}` removing the hard-coded `"fastcache-node"` is in the plan —
note that contour's side already takes a name (`"contour-dev"`, `"the-real-daemon"`), so the merge is
towards what core-cpp has, not away from it.

## 3. Two files the list does not mention, one in scope and one not

- **`src/FastCache/Core/Errors/CryptoError.hpp`** — a crypto-specific error type. B2 merged
  `NetError` into a single vocabulary whose union has **no crypto codes**. Decide explicitly whether
  TLS failures widen `NetError` or need their own type, and **say which in your report**; do not let
  it be settled by whichever you reach for first.
- **`src/FastCache/Cluster/RosterCertificate.*`** — fastcached's cluster roster. **Out of scope**, by
  the rule that no consumer-specific concept enters core-cpp. Named here so you recognise it as
  excluded rather than discovering it and wondering.
- `scripts/tls-smoke.{ps1,sh}` exist at the pin; decide whether the `cl-release-tls` leg wants an
  equivalent, and say so either way.
