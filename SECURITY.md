# Security policy

core-cpp contains networking code: an event loop, sockets, a dialler, an HTTP server and, when
built with `CORE_CPP_WITH_TLS`, a TLS layer over OpenSSL. A defect there can be reachable from
the network in every program that links it, so it is handled privately first.

## Reporting a vulnerability

**Do not open a public issue or pull request.** Report it through GitHub's private vulnerability
reporting: [Security → Report a vulnerability](https://github.com/contour-terminal/core-cpp/security/advisories/new).

Include what you can of:

- the affected module and version (a tag or a commit);
- how to reproduce it, or a proof of concept;
- what an attacker can reach: which input, over which transport, with which privileges.

The report is acknowledged, the fix is developed in a private advisory, and a patch release is
cut. The advisory is published once the consuming projects can move to it.

## Supported versions

core-cpp is in 0.x. Fixes go to the latest release; there are no maintained older branches.

## Scope

- In scope: anything in core-cpp's own sources, including how it calls OpenSSL.
- Out of scope: defects in OpenSSL, libunicode, stb, Catch2 or Tracy themselves (report those
  upstream), and in the consuming applications, unless core-cpp's API makes the misuse the
  natural way to call it.
