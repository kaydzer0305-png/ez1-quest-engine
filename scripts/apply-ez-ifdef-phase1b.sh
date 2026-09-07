#!/usr/bin/env bash
# Apply EZ1v4.0 #ifdef EZ hunks onto this nillerusr tree.
# Safe to re-run: patch --forward --reject-file=- skips already-applied hunks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH="$ROOT/scripts/patches/ez-ifdef-phase1b.patch"
cd "$ROOT"
patch -p1 --forward --reject-file=- --fuzz=2 < "$PATCH"
echo "applied ez-ifdef-phase1b. CI default remains --build-games=hl2."
