#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Holds core-cpp's Python to the pinned ruff: formats it, and lints it.

    python scripts/python-style.py tools/migrate/rewrite.py   # the files you touched
    python scripts/python-style.py --all                      # every Python source in the tree
    python scripts/python-style.py --all --check              # what CI runs; names each unclean file
    python scripts/python-style.py --binary <path> <paths>    # uses this ruff, which must be the pin

**A bare run is an error**, not "format everything": rewriting the whole tree is what breaks the
standing rule against formatting a file another session is editing, and a default the tooling
breaks is a rule that gets broken. Name the files, or say `--all` and mean it.

Both halves run and both report before either fails, so one run tells you everything: `ruff format`
for layout, and `ruff check` for ruff's default rule set, which `ruff.toml` states -- undefined
names, unused imports, import and statement errors. Nothing stylistic; layout is the formatter's
job. The linter never rewrites, here or anywhere: a finding is for a human to fix.

It is `python-style.py` rather than `ruff.py` for two reasons. A module named `ruff.py` on sys.path
shadows the `ruff` package this very script imports to find the pinned binary, and the failure then
reads as "no ruff found" on a machine that has it -- which is how the name was chosen the first
time. And the job is wider than any one subcommand, so the file is named for what it holds rather
than for which of ruff's modes it happens to call.

Which ruff runs: --binary, else $RUFF, else the one the pinned PyPI package installed
(python scripts/tool-versions.py --install), else the first on PATH. Whichever it is must report
exactly the version .ruff-version pins, or nothing is formatted: ruff's formatter output changes
between releases, so an unpinned ruff would disagree with CI about what "formatted" means — the same
reason scripts/clang-format.py refuses every clang-format but the pin.

The files are every `*.py` git knows about, tracked or not yet added, minus what .gitignore excludes,
which today is `scripts/` and `tools/` and tomorrow is wherever the next one lands. How they are
formatted and what is linted is ruff.toml, whose line length is .clang-format's ColumnLimit.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent


def load_tool_versions():
    """Loads scripts/tool-versions.py, the one reader of the version pins."""
    spec = importlib.util.spec_from_file_location(
        "tool_versions", Path(__file__).with_name("tool-versions.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pip_installed_binary() -> str | None:
    """Returns the ruff of the `ruff` PyPI package, if that is installed."""
    try:
        from ruff.__main__ import find_ruff_bin
    except ImportError:
        return None
    try:
        binary = find_ruff_bin()
    except FileNotFoundError:
        return None
    return binary if os.path.isfile(binary) else None


def find_binary(explicit: str | None) -> str | None:
    """Returns the ruff to run, in the order the module docstring gives."""
    for candidate in (explicit, os.environ.get("RUFF")):
        if candidate:
            return candidate
    return pip_installed_binary() or shutil.which("ruff")


def version_of(binary: str) -> str | None:
    """Returns the x.y.z that `binary --version` reports, or None when it does not run."""
    try:
        output = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False).stdout
    except OSError:
        return None
    match = re.search(r"ruff (\d+\.\d+\.\d+)", output)
    return match.group(1) if match else None


def sources() -> list[str]:
    """Returns the Python sources git knows about, relative to the repository root."""
    listing = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "*.py"],
        cwd=REPOSITORY_ROOT,
        capture_output=True,
        check=True,
    ).stdout.decode("utf-8")
    return sorted(path for path in listing.split("\0") if path and (REPOSITORY_ROOT / path).is_file())


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--check", action="store_true", help="report unformatted files instead of rewriting them"
    )
    parser.add_argument("--binary", help="the ruff to run (must be the pinned version)")
    parser.add_argument("--all", action="store_true", help="check every Python source git knows about")
    parser.add_argument("paths", nargs="*", help="files to format instead of every Python source")
    arguments = parser.parse_args()

    # A bare run would rewrite every file in the tree, including ones another lane is editing, and
    # "never run a formatter over a file another session is editing" is a standing constraint. A
    # default the tooling breaks is a rule that gets broken, so name the two ways to mean it
    # (controller ruling R89).
    if arguments.all and arguments.paths:
        print("python-style.py: --all covers everything, so it takes no paths", file=sys.stderr)
        return 2
    if not arguments.all and not arguments.paths:
        print(
            "python-style.py: name the files to check, or pass --all for every Python source git knows "
            "about. A bare run rewrites the whole tree, including files another session is editing.",
            file=sys.stderr,
        )
        return 2

    pin = load_tool_versions().pinned("ruff")
    binary = find_binary(arguments.binary)
    if binary is None:
        print(
            f"python-style.py: no ruff found; install the pinned {pin} with: python scripts/tool-versions.py --install",
            file=sys.stderr,
        )
        return 2
    version = version_of(binary)
    if version != pin:
        print(
            f"python-style.py: {binary} is ruff {version or '(unknown version)'}, but .ruff-version pins {pin}. "
            "Install the pin with: python scripts/tool-versions.py --install",
            file=sys.stderr,
        )
        return 2

    files = sources() if arguments.all else arguments.paths
    if not files:
        print("python-style.py: no Python sources found", file=sys.stderr)
        return 2

    # Both halves run before either decides the exit status, so one invocation reports everything
    # that is wrong rather than the first thing.
    mode = ["--check"] if arguments.check else []
    unformatted = subprocess.call([binary, "format", *mode, "--", *files], cwd=REPOSITORY_ROOT) != 0
    # Never `--fix`: a lint finding is for a human. The formatter is the only half that writes.
    linted = subprocess.call([binary, "check", "--", *files], cwd=REPOSITORY_ROOT) != 0

    if unformatted and arguments.check:
        print(
            "python-style.py: the files above are not formatted; run: python scripts/python-style.py --all",
            file=sys.stderr,
        )
    if linted:
        print("python-style.py: the findings above are lint, not layout; fix them by hand", file=sys.stderr)
    if not unformatted and not linted:
        verb = "are formatted and lint clean" if arguments.check else "formatted, and lint clean"
        print(f"python-style.py: {len(files)} file(s) {verb} with ruff {version}")
    return 1 if (unformatted or linted) else 0


if __name__ == "__main__":
    sys.exit(main())
