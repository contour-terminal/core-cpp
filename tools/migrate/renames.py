# SPDX-License-Identifier: Apache-2.0
"""Loads and validates `renames.json`, the one rename table every migration tool reads.

The table is data, and every tool that reads it reads it through here, so the codemod, the semantic
pass and the drift gate cannot disagree about what a row means. A malformed table is a `TableError`
that names the row, never a silently ignored line.

Each row says what a consumer writes today (`from`), what core-cpp calls it (`to`), which consumer
profiles it applies to, and how it is applied:

| field | meaning |
|---|---|
| `kind` | `include`, `namespace`, `symbol`, `member` or `macro`: what shape the source has |
| `apply` | `text` (rewrite.py), `semantic` (semantic_rename.py) or `manual` (a human) |
| `status` | `delivered` (the target exists in this tree) or `pending` (a Phase B task owes it) |
| `scope` | for a semantic row, the qualified name of the class whose member is renamed |
| `target` | the core-cpp header and symbol the drift gate (check-renames.py) checks |
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

# In the order rewrite.py applies them, which is not arbitrary: a symbol row carries the source's
# full qualification (`crispy::cli::command`), so every namespace row has to run after it or the
# prefix swap would leave the symbol rows nothing to match.
KINDS = ("include", "symbol", "member", "macro", "namespace")
APPLICATIONS = ("text", "semantic", "manual")
STATUSES = ("delivered", "pending")


class TableError(Exception):
    """The table is not a table this tree can act on."""


@dataclass(frozen=True)
class Target:
    """The core-cpp side of a row: what check-renames.py asserts the delivered tree has."""

    header: str
    symbol: str | None = None
    public: bool = True


@dataclass(frozen=True)
class Row:
    kind: str
    source: str
    target: str
    profiles: tuple[str, ...]
    apply: str = "text"
    status: str = "delivered"
    task: str | None = None
    scope: str | None = None
    delivers: Target | None = None
    note: str | None = None

    @property
    def label(self) -> str:
        """How the row reads in a report: `namespace net -> core::net`."""
        return f"{self.kind} {self.source} -> {self.target}"

    def appliesTo(self, profile: str) -> bool:
        return profile in self.profiles


@dataclass(frozen=True)
class Table:
    profiles: dict[str, str]
    rows: list[Row] = field(default_factory=list)

    def textRows(self, profile: str) -> list[Row]:
        """The rows rewrite.py applies for @p profile, most specific source first."""
        if profile not in self.profiles:
            raise TableError(f"unknown profile '{profile}'; the table has {', '.join(sorted(self.profiles))}")
        rows = [row for row in self.rows if row.apply == "text" and row.appliesTo(profile)]
        return sorted(rows, key=lambda row: (KINDS.index(row.kind), -len(row.source), row.source))

    def semanticRows(self, profile: str) -> list[Row]:
        """The rows semantic_rename.py owns for @p profile."""
        if profile not in self.profiles:
            raise TableError(f"unknown profile '{profile}'; the table has {', '.join(sorted(self.profiles))}")
        return [row for row in self.rows if row.apply == "semantic" and row.appliesTo(profile)]


def _target(raw: object, where: str) -> Target | None:
    if raw is None:
        return None
    if not isinstance(raw, dict):
        raise TableError(f"{where}: 'target' must be an object")
    unknown = set(raw) - {"header", "symbol", "public"}
    if unknown:
        raise TableError(f"{where}: 'target' has unknown fields {sorted(unknown)}")
    header = raw.get("header")
    if not isinstance(header, str) or not header:
        raise TableError(f"{where}: 'target' needs a 'header'")
    symbol = raw.get("symbol")
    if symbol is not None and not isinstance(symbol, str):
        raise TableError(f"{where}: 'target.symbol' must be a string")
    public = raw.get("public", True)
    if not isinstance(public, bool):
        raise TableError(f"{where}: 'target.public' must be true or false")
    return Target(header=header, symbol=symbol, public=public)


def _row(raw: object, index: int, profiles: dict[str, str]) -> Row:
    where = f"rows[{index}]"
    if not isinstance(raw, dict):
        raise TableError(f"{where} is not an object")
    known = {"kind", "from", "to", "profiles", "apply", "status", "task", "scope", "target", "note"}
    unknown = set(raw) - known
    if unknown:
        raise TableError(f"{where}: unknown fields {sorted(unknown)}")

    kind = raw.get("kind")
    if kind not in KINDS:
        raise TableError(f"{where}: kind '{kind}' is not one of {', '.join(KINDS)}")
    source, target = raw.get("from"), raw.get("to")
    for name, value in (("from", source), ("to", target)):
        if not isinstance(value, str) or not value:
            raise TableError(f"{where}: '{name}' must be a non-empty string")

    rowProfiles = raw.get("profiles")
    if not isinstance(rowProfiles, list) or not rowProfiles:
        raise TableError(f"{where}: 'profiles' must be a non-empty list")
    for name in rowProfiles:
        if name not in profiles:
            declared = ", ".join(sorted(profiles))
            raise TableError(f"{where}: unknown profile '{name}'; the table declares {declared}")

    apply = raw.get("apply", "text")
    if apply not in APPLICATIONS:
        raise TableError(f"{where}: apply '{apply}' is not one of {', '.join(APPLICATIONS)}")
    status = raw.get("status", "delivered")
    if status not in STATUSES:
        raise TableError(f"{where}: status '{status}' is not one of {', '.join(STATUSES)}")

    task = raw.get("task")
    if status == "pending" and not task:
        raise TableError(f"{where}: a pending row names the task that owes it, in 'task'")
    if status == "delivered" and task:
        raise TableError(f"{where}: a delivered row carries no 'task'")

    scope = raw.get("scope")
    if apply == "semantic" and not scope:
        raise TableError(f"{where}: a semantic row needs the 'scope' whose member it renames")

    note = raw.get("note")
    if apply == "manual" and not note:
        raise TableError(f"{where}: a manual row says in 'note' what a human has to do")

    return Row(
        kind=kind,
        source=source,
        target=target,
        profiles=tuple(rowProfiles),
        apply=apply,
        status=status,
        task=task,
        scope=scope,
        delivers=_target(raw.get("target"), where),
        note=note,
    )


def load(path: Path) -> Table:
    """Reads and validates the table at @p path, or raises TableError naming what is wrong."""
    try:
        raw = json.loads(Path(path).read_text(encoding="utf-8"))
    except OSError as error:
        raise TableError(f"cannot read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise TableError(f"{path} is not JSON: {error}") from error

    if not isinstance(raw, dict):
        raise TableError(f"{path}: the table is an object with 'version', 'profiles' and 'rows'")
    if raw.get("version") != 1:
        raise TableError(f"{path}: version {raw.get('version')!r} is not 1")
    profiles = raw.get("profiles")
    if not isinstance(profiles, dict) or not profiles:
        raise TableError(f"{path}: 'profiles' maps every consumer profile to what it covers")
    rawRows = raw.get("rows")
    if not isinstance(rawRows, list) or not rawRows:
        raise TableError(f"{path}: 'rows' is a non-empty list")

    rows = [_row(entry, index, profiles) for index, entry in enumerate(rawRows)]

    seen: dict[tuple[str, str, str], int] = {}
    for index, row in enumerate(rows):
        for profile in row.profiles:
            key = (profile, row.kind, row.source)
            if key in seen:
                raise TableError(
                    f"rows[{index}]: profile '{profile}' renames the {row.kind} '{row.source}' twice "
                    f"(also rows[{seen[key]}])"
                )
            seen[key] = index

    return Table(profiles=dict(profiles), rows=rows)
