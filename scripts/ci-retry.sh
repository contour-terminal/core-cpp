#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Runs a command, and runs it again -- up to three attempts, 15s then 30s apart -- if it fails.
#
#     scripts/ci-retry.sh sudo apt-get install -y -q ninja-build
#
# For the CI steps that install a toolchain from somebody else's server (Canonical's archive,
# apt.llvm.org, Homebrew). Those fail for reasons no change can cause -- a 503 from the archive red
# `ci-ok` three times in a few hours (core-cpp#42) -- and a gate that reds for a non-reason trains its
# readers to re-run instead of read. It is NOT a verdict: every step that uses it still asserts what
# it installed afterwards (`command -v`, a version check), so a compiler that is genuinely missing
# still fails the job, and it fails there rather than here.
#
# Every retry says so on the job's log as a warning, with the attempt and the exit status, so a
# flaky mirror is visible rather than absorbed.

set -u

if [ $# -eq 0 ]; then
    echo "usage: $0 <command> [arguments...]" >&2
    exit 2
fi

attempts=3
delay="${CI_RETRY_DELAY:-15}" # overridable only so the behaviour can be exercised quickly
attempt=1
while true; do
    "$@"
    status=$?
    if [ "$status" -eq 0 ]; then
        exit 0
    fi
    if [ "$attempt" -ge "$attempts" ]; then
        echo "::error::'$*' failed $attempts times; the last exit status was $status" >&2
        exit "$status"
    fi
    echo "::warning::'$*' failed (attempt $attempt of $attempts, exit $status); retrying in ${delay}s" >&2
    sleep "$delay"
    attempt=$((attempt + 1))
    delay=$((delay * 2))
done
