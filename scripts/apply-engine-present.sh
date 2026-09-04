#!/usr/bin/env bash
# Restore emptied vr_main.cpp and apply the togles -> XR FBO hooks.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
GOOD=a06319ee8c923cea0f41abda224356086d4df235
if [ ! -s launcher/android/vr_main.cpp ]; then
  echo "restoring vr_main.cpp from $GOOD"
  git checkout "$GOOD" -- launcher/android/vr_main.cpp
fi
git apply --check scripts/vr_main_present.patch && git apply scripts/vr_main_present.patch
git apply --check scripts/sdlmgr_xr_present.patch && git apply scripts/sdlmgr_xr_present.patch
echo "applied present hooks. next: add android/ezquest_vr_present.cpp to launcher/wscript if missing"
