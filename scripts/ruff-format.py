#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Formats core-cpp's Python with the pinned ruff, or checks that it is formatted.

    python scripts/ruff-format.py                    # rewrites every Python source in place
    python scripts/ruff-format.py --check            # fails, naming each file, unless all are formatted
    python scripts/ruff-format.py --binary <path>    # uses this ruff, which must be the pin

The file is `ruff-format.py`, not `ruff.py`, for the same reason its neighbour is
`clang-format.py`: a module named `ruff.py` on sys.path shadows the `ruff` package this very script
imports to find the pinned binary, and the failure reads as "no ruff found" on a machine that has it.

Which ruff runs: --binary, else $RUFF, else the one the pinned PyPI package installed
(python scripts/tool-versions.py --install), else the first on PATH. Whichever it is must report
exactly the version .ruff-version pins, or nothing is formatted: ruff's formatter output changes
between releases, so an unpinned ruff would disagree with CI about what "formatted" means — the same
reason scripts/clang-format.py refuses every clang-format but the pin.

The files are every `*.py` git knows about, tracked or not yet added, minus what .gitignore excludes,
which today is `scripts/` and `tools/` and tomorrow is wherever the next one lands. How they are
formatted is ruff.toml, whose line length is .clang-format's ColumnLimit.

This is the formatter alone. ruff's linter (`ruff check`) is not run: enabling a rule set is a
decision of its own, and one nobody has made.
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
    parser.add_argument("paths", nargs="*", help="files to format instead of every Python source")
    arguments = parser.parse_args()

    pin = load_tool_versions().pinned("ruff")
    binary = find_binary(arguments.binary)
    if binary is None:
        print(
            f"ruff-format.py: no ruff found; install the pinned {pin} with: python scripts/tool-versions.py --install",
            file=sys.stderr,
        )
        return 2
    version = version_of(binary)
    if version != pin:
        print(
            f"ruff-format.py: {binary} is ruff {version or '(unknown version)'}, but .ruff-version pins {pin}. "
            "Install the pin with: python scripts/tool-versions.py --install",
            file=sys.stderr,
        )
        return 2

    files = arguments.paths or sources()
    if not files:
        print("ruff-format.py: no Python sources found", file=sys.stderr)
        return 2

    mode = ["--check"] if arguments.check else []
    failed = subprocess.call([binary, "format", *mode, "--", *files], cwd=REPOSITORY_ROOT) != 0

    if failed and arguments.check:
        print(
            "ruff-format.py: the files above are not formatted; run: python scripts/ruff-format.py",
            file=sys.stderr,
        )
    elif not failed:
        verb = "are formatted" if arguments.check else "formatted"
        print(f"ruff-format.py: {len(files)} file(s) {verb} with ruff {version}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
