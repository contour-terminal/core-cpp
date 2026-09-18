#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The pinned versions of core-cpp's style tools, and how to install them.

Each tool is pinned by a `.<tool>-version` file at the repository root, in the organisation's
format: `#` comments and `key: value` lines, whose `version:` line names the PyPI release of the
tool (`clang-format`, `clang-tidy`). `.clang-format-version` spells it as the full banner
(`clang-format version 22.1.8`) because the contour-workflows format-on-edit hook compares that line
exactly; `.clang-tidy-version` spells the bare number. This script is the one reader of both:
scripts/clang-format.py loads it for the clang-format pin, and CI installs the tools through it.

    python scripts/tool-versions.py              # prints the pip requirements, one per line
    python scripts/tool-versions.py --install    # python -m pip install --user <them>
    python scripts/tool-versions.py --check      # fails unless the installed versions are the pins
"""

from __future__ import annotations

import argparse
import importlib.metadata
import re
import subprocess
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent

# The pinned tools, each with its `.<tool>-version` file at the repository root.
TOOLS = ("clang-format", "clang-tidy")


def pinned(tool: str) -> str:
    """Returns the x.y.z that the `version:` line of `.<tool>-version` pins, or exits naming the file."""
    path = REPOSITORY_ROOT / f".{tool}-version"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        sys.exit(f"tool-versions: cannot read {path}: {error}")
    versions = []
    for number, line in enumerate(text.splitlines(), start=1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, colon, value = line.partition(":")
        if not colon:
            sys.exit(f"tool-versions: {path}:{number} is not `key: value`: {line}")
        if key.strip() == "version":
            match = re.search(r"(\d+\.\d+\.\d+)$", value.strip())
            if not match:
                sys.exit(f"tool-versions: {path}:{number} names no x.y.z version: {line}")
            versions.append(match.group(1))
    if len(versions) != 1:
        sys.exit(f"tool-versions: {path} must have exactly one `version:` line, not {len(versions)}")
    return versions[0]


def requirements() -> list[str]:
    """Returns the pip requirement of every pinned tool, e.g. `clang-format==22.1.8`."""
    return [f"{tool}=={pinned(tool)}" for tool in TOOLS]


def installed(tool: str) -> str | None:
    """Returns the installed version of the PyPI distribution `tool`, or None."""
    try:
        return importlib.metadata.version(tool)
    except importlib.metadata.PackageNotFoundError:
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--install", action="store_true", help="install the pinned tools with pip --user")
    action.add_argument("--check", action="store_true", help="fail unless the pinned versions are installed")
    arguments = parser.parse_args()

    if arguments.install:
        return subprocess.call([sys.executable, "-m", "pip", "install", "--user", *requirements()])

    if arguments.check:
        mismatches = [
            f"{tool}: installed {installed(tool) or 'nothing'}, pinned {pinned(tool)}"
            for tool in TOOLS
            if installed(tool) != pinned(tool)
        ]
        for mismatch in mismatches:
            print(f"tool-versions: {mismatch}", file=sys.stderr)
        if mismatches:
            print("tool-versions: install the pins with: python scripts/tool-versions.py --install", file=sys.stderr)
            return 1
        return 0

    print("\n".join(requirements()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
