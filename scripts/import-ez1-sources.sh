#!/usr/bin/env bash
# Copy EZ1v4.0 unique game units from a local entropy-zero/source-sdk-2013
# checkout into this engine tree. Does NOT rewrite shared HL2 files
# (#ifdef EZ hunks) — those still need a careful 2013-vs-nillerusr merge.
#
# Usage:
#   bash scripts/import-ez1-sources.sh /path/to/source-sdk-2013
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${1:-}"
if [[ -z "$SDK" || ! -d "$SDK/sp/src/game" ]]; then
  echo "usage: $0 /path/to/entropy-zero/source-sdk-2013" >&2
  exit 1
fi
G="$SDK/sp/src/game"
mkdir -p "$ROOT/game/server/ez1" "$ROOT/game/server/ez2" "$ROOT/game/server/mod" \
         "$ROOT/game/server/Human_Error" "$ROOT/game/client/Human_Error"
cp -v "$G"/server/ez1/* "$ROOT/game/server/ez1/"
cp -v "$G"/server/ez2/* "$ROOT/game/server/ez2/"
cp -v "$G"/server/mod/* "$ROOT/game/server/mod/"
cp -v "$G"/server/Human_Error/* "$ROOT/game/server/Human_Error/"
cp -v "$G"/client/Human_Error/* "$ROOT/game/client/Human_Error/"
cp -v "$G"/client/particles_ez.cpp "$G"/client/particles_ez.h "$ROOT/game/client/"
echo "copied unique EZ1 units. Next: apply #ifdef EZ hunks listed in docs/ez1-merge.md"
