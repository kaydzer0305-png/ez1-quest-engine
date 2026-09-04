#!/usr/bin/env bash
# Restore launcher/android/vr_main.cpp after the empty-blob wipe and apply
# the slice E input / first-present hook. Run from the repo root.
set -euo pipefail
ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"
# Last known good compositor loop (slice D).
GOOD=a06319ee8c923cea0f41abda224356086d4df235
git checkout "$GOOD" -- launcher/android/vr_main.cpp
git apply scripts/vr_main_slice_e.patch
echo "restored and patched launcher/android/vr_main.cpp ($(wc -c < launcher/android/vr_main.cpp) bytes)"
