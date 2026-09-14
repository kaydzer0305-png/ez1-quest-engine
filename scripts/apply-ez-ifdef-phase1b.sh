#!/usr/bin/env bash
# Apply EZ1v4.0 #ifdef EZ hunks onto this nillerusr tree.
# Safe to re-run: detect fully applied patches before applying new ones.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
shopt -s nullglob

patches=(scripts/patches/ez-ifdef-game_*.patch)
if (( ${#patches[@]} == 0 )); then
  echo "no Phase 1b patch files found" >&2
  exit 1
fi

for p in "${patches[@]}"; do
  echo "checking $p"
  if patch -p1 --batch --forward --dry-run --fuzz=2 < "$p" >/dev/null 2>&1; then
    patch -p1 --batch --forward --reject-file=- --fuzz=2 < "$p"
  elif patch -p1 --batch --reverse --dry-run --fuzz=2 < "$p" >/dev/null 2>&1; then
    echo "already applied: $p"
  else
    echo "cannot cleanly apply or reverse $p (possible partial application or drift)" >&2
    patch -p1 --batch --forward --dry-run --fuzz=2 < "$p" || true
    exit 1
  fi
done

echo "applied ez-ifdef-phase1b. CI default remains --build-games=hl2."
