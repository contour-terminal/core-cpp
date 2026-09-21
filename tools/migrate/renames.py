# SPDX-License-Identifier: Apache-2.0
"""Loads and validates `renames.json`, the one rename table every migration tool reads.

The table is data, and every tool that reads it reads it through here, so the codemod, the semantic
pass and the drift gate cannot disagree about what a row means. A malformed table is a `TableError`
that names the row, never a silently ignored line.

Each row says what a consumer writes today (`from`), what core-cpp calls it (`to`), which consumer
profiles it applies to, and how it is applied:

| field | meaning |
|---|---|
| `kind` | `include`, `namespace`, `symbol`, `member`, `macro`, or `removed` |
| `apply` | `text` (rewrite.py), `semantic` (semantic_rename.py), `manual` (a human), `none` |
| `status` | `delivered` (the target exists in this tree) or `pending` (a Phase B task owes it) |
| `scope` | for a semantic row, the qualified name of the class whose member is renamed |
| `target` | the core-cpp header and symbol the drift gate (check-renames.py) checks |

A `pending` row must carry a `target` with a `symbol`, because that symbol is the whole of what the
gate watches for: the day the task lands it, the gate fails and says to mark the row delivered. A
pending row without one is asserted by nothing in either direction, and reads as covered.

`removed` is the one kind that runs the gate backwards. Its `from` names a symbol core-cpp **no
longer has**, and check-renames.py asserts the symbol is *absent* from the delivered headers, so a
re-introduction is refused. It has no `to` and no `target`, because there is nothing to rename to;
it carries a `note` saying what a consumer writes instead, and its `apply` can only be `none`, so no
rewrite tool is ever handed one. A removal that changes the shape of a call -- not just its name --
is a compile error at the call site, which is a better signal than a codemod that rewrites it into
something that compiles and is wrong.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

# In the order rewrite.py applies them, which is not arbitrary: a symbol row carries the source's
# full qualification (`crispy::cli::command`), so every namespace row has to run after it or the
# prefix swap would leave the symbol rows nothing to match. `removed` is last because it is never
# applied at all.
KINDS = ("include", "symbol", "member", "macro", "namespace", "removed")
APPLICATIONS = ("text", "semantic", "manual", "none")
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
        """How the row reads in a report: `namespace net -> core::net`, or `removed <symbol>`."""
        return (
            f"removed {self.source}"
            if self.kind == "removed"
            else f"{self.kind} {self.source} -> {self.target}"
        )

    def applies_to(self, profile: str) -> bool:
        return profile in self.profiles


@dataclass(frozen=True)
class Table:
    profiles: dict[str, str]
    rows: list[Row] = field(default_factory=list)

    def text_rows(self, profile: str) -> list[Row]:
        """The rows rewrite.py applies for @p profile, most specific source first."""
        if profile not in self.profiles:
            raise TableError(f"unknown profile '{profile}'; the table has {', '.join(sorted(self.profiles))}")
        rows = [row for row in self.rows if row.apply == "text" and self._rewritable(row, profile)]
        return sorted(rows, key=lambda row: (KINDS.index(row.kind), -len(row.source), row.source))

    def semantic_rows(self, profile: str) -> list[Row]:
        """The rows semantic_rename.py owns for @p profile."""
        if profile not in self.profiles:
            raise TableError(f"unknown profile '{profile}'; the table has {', '.join(sorted(self.profiles))}")
        return [row for row in self.rows if row.apply == "semantic" and self._rewritable(row, profile)]

    @staticmethod
    def _rewritable(row: Row, profile: str) -> bool:
        """Whether @p row may be handed to a rewrite tool at all, before its `apply` is consulted.

        The `kind` test is not redundant with the `apply` test above it. `_row()` forces a removed
        row's `apply` to `none`, so filtering on `apply` alone excluded one -- but only as a
        consequence of the schema, which is the schema's door seen from downstream, not a second
        door. A `Row` built in code, bypassing the loader, walked straight through. Two doors were
        claimed and one existed (controller ruling R93).
        """
        return row.kind != "removed" and row.applies_to(profile)


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
    required = ("from",) if kind == "removed" else ("from", "to")
    for name in required:
        value = raw.get(name)
        if not isinstance(value, str) or not value:
            raise TableError(f"{where}: '{name}' must be a non-empty string")
    if kind == "removed" and target:
        raise TableError(f"{where}: a removed row has no 'to': core-cpp has no such symbol to rename to")
    target = target or ""

    row_profiles = raw.get("profiles")
    if not isinstance(row_profiles, list) or not row_profiles:
        raise TableError(f"{where}: 'profiles' must be a non-empty list")
    for name in row_profiles:
        if name not in profiles:
            declared = ", ".join(sorted(profiles))
            raise TableError(f"{where}: unknown profile '{name}'; the table declares {declared}")

    apply = raw.get("apply", "none" if kind == "removed" else "text")
    if apply not in APPLICATIONS:
        raise TableError(f"{where}: apply '{apply}' is not one of {', '.join(APPLICATIONS)}")
    # A removed symbol has no replacement, so rewriting it would produce code that cannot compile.
    # The kind documents a removal and catches a re-introduction; it never edits anything (R75).
    if kind == "removed" and apply != "none":
        raise TableError(f"{where}: a removed row is never rewritten, so its 'apply' can only be 'none'")
    if kind != "removed" and apply == "none":
        raise TableError(f"{where}: 'apply' of 'none' belongs to a removed row; this one is a {kind}")

    status = raw.get("status", "delivered")
    if status not in STATUSES:
        raise TableError(f"{where}: status '{status}' is not one of {', '.join(STATUSES)}")
    if kind == "removed" and status != "delivered":
        raise TableError(
            f"{where}: a removed row is not pending: the symbol is gone now, or the row is wrong"
        )

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
    if kind == "removed" and not note:
        raise TableError(f"{where}: a removed row says in 'note' what a consumer writes instead")

    delivers = _target(raw.get("target"), where)
    if kind == "removed" and delivers is not None:
        raise TableError(
            f"{where}: a removed row names no 'target'; the gate asserts the symbol is ABSENT, "
            f"which is the opposite of what a target means"
        )
    # Being checked the day its task lands is the whole job of a pending row. Without a target the
    # gate has nothing to watch for, so the row is asserted by nothing in either direction -- a
    # comment wearing a row's clothes, and one that reads as covered (controller ruling R92).
    if status == "pending" and delivers is None:
        raise TableError(
            f"{where}: a pending row carries the 'target' the gate watches for; without one, "
            f"nothing ever tells task {task} that its symbol landed"
        )
    if status == "pending" and not delivers.symbol:
        raise TableError(
            f"{where}: a pending row's 'target' needs the 'symbol' the gate watches for, not the header alone"
        )

    return Row(
        kind=kind,
        source=source,
        target=target,
        profiles=tuple(row_profiles),
        apply=apply,
        status=status,
        task=task,
        scope=scope,
        delivers=delivers,
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
    raw_rows = raw.get("rows")
    if not isinstance(raw_rows, list) or not raw_rows:
        raise TableError(f"{path}: 'rows' is a non-empty list")

    rows = [_row(entry, index, profiles) for index, entry in enumerate(raw_rows)]

    # `scope` is part of the key because a member name is only unique inside its class. Without it,
    # `AsyncQueue::Close` could not have a row at all once `ISocket::Close` held the name in the same
    # profile -- and Read, Write, Stop and Close are exactly the members Phase B renames, several
    # classes each (controller ruling R94). For every other kind `scope` is None and changes nothing.
    seen: dict[tuple[str, str, str | None, str], int] = {}
    for index, row in enumerate(rows):
        for profile in row.profiles:
            key = (profile, row.kind, row.scope, row.source)
            if key in seen:
                named = f"'{row.scope}::{row.source}'" if row.scope else f"'{row.source}'"
                raise TableError(
                    f"rows[{index}]: profile '{profile}' renames the {row.kind} {named} twice "
                    f"(also rows[{seen[key]}])"
                )
            seen[key] = index

    return Table(profiles=dict(profiles), rows=rows)
