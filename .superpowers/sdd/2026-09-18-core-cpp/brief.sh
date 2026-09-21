#!/usr/bin/env bash
# Usage: brief.sh TASK_ID   (e.g. A1, B3, C0) -> writes task-<ID>-brief.md, prints path
set -euo pipefail
id=$1
root=$(git rev-parse --show-toplevel)
plan="$root/docs/superpowers/plans/2026-09-18-core-cpp.md"
w="$root/.superpowers/sdd/2026-09-18-core-cpp"
out="$w/task-${id}-brief.md"
phase=${id:0:1}
{
  echo "# Brief for Task ${id}"
  echo
  echo "Binding references (read these too): Global Constraints at $w/global-constraints.md; the design spec at $root/docs/superpowers/specs/2026-09-18-core-cpp-design.md (Part I sections referenced below as 'Part I §N' are in that file)."
  echo
  if [ "$phase" = "B" ] || [ "$phase" = "C" ]; then
    awk -v ph="$phase" '
      /^```/ { f = !f }
      !f && /^## Phase / { inph = ($0 ~ ("^## Phase " ph " ")) ; pre = inph; next }
      !f && /^### Task / { pre = 0 }
      pre { print }
    ' "$plan"
    echo
  fi
  awk -v id="$id" '
    /^```/ { f = !f }
    !f && /^### Task / { intask = ($0 ~ ("^### Task " id "[:( ]")) }
    !f && /^## / { intask = 0 }
    !f && /^---$/ { intask = 0 }
    intask { print }
  ' "$plan"
} > "$out"
grep -q "^### Task ${id}" "$out" || { echo "task ${id} not found" >&2; exit 3; }
echo "$out ($(wc -l < "$out") lines)"
