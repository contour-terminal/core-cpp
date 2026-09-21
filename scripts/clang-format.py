#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Formats core-cpp's C++ sources with the pinned clang-format, or checks that they are formatted.

    python scripts/clang-format.py src/core/net/EventLoop.cpp   # the files you touched
    python scripts/clang-format.py --all                        # every C++ source in the tree
    python scripts/clang-format.py --all --check                # what CI runs; names each unformatted file
    python scripts/clang-format.py --binary <path> <paths>      # uses this clang-format, which must be the pin

**A bare run is an error**, not "format everything": rewriting the whole tree is what breaks the
standing rule against formatting a file another session is editing, and a default the tooling
breaks is a rule that gets broken. Name the files, or say `--all` and mean it.

Which clang-format runs: --binary, else $CLANG_FORMAT, else the one the pinned PyPI package installed
(python scripts/tool-versions.py --install), else the first on PATH. Whichever it is must report
exactly the version .clang-format-version pins, or nothing is formatted: two releases of clang-format
format the same file differently, and CI would disagree with the machine that formatted it.

The files are every C++ source git knows about, tracked or not yet added, minus what .gitignore
excludes. clang-format itself skips what .clang-format-ignore names.
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
EXTENSIONS = ("cpp", "hpp", "h", "ipp", "inl")

# Files per clang-format invocation: Windows limits a command line to 32767 characters.
BATCH_SIZE = 100


def load_tool_versions():
    """Loads scripts/tool-versions.py, the one reader of the version pins."""
    spec = importlib.util.spec_from_file_location(
        "tool_versions", Path(__file__).with_name("tool-versions.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pip_installed_binary() -> str | None:
    """Returns the clang-format of the `clang-format` PyPI package, if that is installed."""
    try:
        import clang_format  # the PyPI package's module
    except ImportError:
        return None
    get_executable = getattr(clang_format, "get_executable", None)
    if get_executable is None:
        return None
    binary = get_executable("clang-format")
    return binary if os.path.isfile(binary) else None


def find_binary(explicit: str | None) -> str | None:
    """Returns the clang-format to run, in the order the module docstring gives."""
    for candidate in (explicit, os.environ.get("CLANG_FORMAT")):
        if candidate:
            return candidate
    return pip_installed_binary() or shutil.which("clang-format")


def version_of(binary: str) -> str | None:
    """Returns the x.y.z that `binary --version` reports, or None when it does not run."""
    try:
        output = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False).stdout
    except OSError:
        return None
    match = re.search(r"clang-format version (\d+\.\d+\.\d+)", output)
    return match.group(1) if match else None


def sources() -> list[str]:
    """Returns the C++ sources git knows about, relative to the repository root."""
    patterns = [f"*.{extension}" for extension in EXTENSIONS]
    listing = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *patterns],
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
    parser.add_argument("--binary", help="the clang-format to run (must be the pinned version)")
    parser.add_argument("--all", action="store_true", help="format every C++ source git knows about")
    parser.add_argument("paths", nargs="*", help="files to format instead of every C++ source")
    arguments = parser.parse_args()

    # A bare run would rewrite every file in the tree, including ones another lane is editing, and
    # "never run a formatter over a file another session is editing" is a standing constraint. A
    # default the tooling breaks is a rule that gets broken, so name the two ways to mean it
    # (controller ruling R89).
    if arguments.all and arguments.paths:
        print("clang-format.py: --all covers everything, so it takes no paths", file=sys.stderr)
        return 2
    if not arguments.all and not arguments.paths:
        print(
            "clang-format.py: name the files to format, or pass --all for every C++ source git knows "
            "about. A bare run rewrites the whole tree, including files another session is editing.",
            file=sys.stderr,
        )
        return 2

    # EXTENSIONS used to filter only what --all DISCOVERED, never what a caller NAMED, so an
    # explicitly named path went to clang-format whatever it was. clang-format parses its input as
    # C++ regardless of the name, so `clang-format.py x.cmake` rewrote a CMake file as C++ and
    # reported "1 file(s) formatted" -- and `--check` was worse than blind, it was inverted: it
    # failed a pristine .cmake and passed the mangled one, so it drove a caller toward the damage
    # and then certified it.
    #
    # Refused rather than skipped, and by name. A silent skip would leave a caller believing a file
    # they named was formatted when nothing touched it, which is the same class of defect one layer
    # up. This sits with the argument checks above rather than after the version check below,
    # because a path that is not a C++ source is wrong whichever clang-format is installed -- and
    # because it lets the self-test reach this refusal on a machine that has no clang-format at all.
    if not arguments.all:
        unsupported = [path for path in arguments.paths if Path(path).suffix[1:].lower() not in EXTENSIONS]
        if unsupported:
            print(
                f"clang-format.py: refusing {len(unsupported)} path(s) that are not C++ sources:",
                file=sys.stderr,
            )
            for path in unsupported:
                print(f"  {path}", file=sys.stderr)
            print(
                "clang-format.py: clang-format parses whatever it is given as C++ and rewrites it, "
                "so formatting one of these destroys it and reports success. C++ sources are: "
                + ", ".join(f".{extension}" for extension in EXTENSIONS),
                file=sys.stderr,
            )
            return 2

    pin = load_tool_versions().pinned("clang-format")
    binary = find_binary(arguments.binary)
    if binary is None:
        print(
            f"clang-format.py: no clang-format found; install the pinned {pin} with: "
            "python scripts/tool-versions.py --install",
            file=sys.stderr,
        )
        return 2
    version = version_of(binary)
    if version != pin:
        print(
            f"clang-format.py: {binary} is clang-format {version or '(unknown version)'}, but "
            f".clang-format-version pins {pin}. Install the pin with: python scripts/tool-versions.py --install",
            file=sys.stderr,
        )
        return 2

    files = sources() if arguments.all else arguments.paths
    mode = ["--dry-run", "--Werror"] if arguments.check else ["-i"]
    failed = False
    for start in range(0, len(files), BATCH_SIZE):
        batch = files[start : start + BATCH_SIZE]
        if subprocess.call([binary, *mode, "--style=file", *batch], cwd=REPOSITORY_ROOT) != 0:
            failed = True

    if failed and arguments.check:
        print(
            "clang-format.py: the files above are not formatted; run: python scripts/clang-format.py --all",
            file=sys.stderr,
        )
    elif not failed:
        verb = "are formatted" if arguments.check else "formatted"
        print(f"clang-format.py: {len(files)} file(s) {verb} with clang-format {version}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
