#!/usr/bin/env bash
# Apply EZ1v4.0 #ifdef EZ hunks onto this nillerusr tree.
# Safe to re-run: patch --forward skips already-applied hunks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
shopt -s nullglob
for p in scripts/patches/ez-ifdef-game_*.patch; do
  echo "applying $p"
  patch -p1 --forward --reject-file=- --fuzz=2 < "$p"
done
echo "applied ez-ifdef-phase1b. CI default remains --build-games=hl2."
