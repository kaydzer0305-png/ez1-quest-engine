#!/usr/bin/env bash
# Filter Quest logcat for EZQuest VR bring-up tags.
# Usage: bash scripts/headset-verify.sh
set -euo pipefail
exec adb logcat -s \
  EZQuest:I \
  EZQuest-VR:I \
  EZQuest-VR-Input:I \
  EZQuest-VR-Engine:I \
  EZQuest-VR-Present:I \
  EZQuest-SourceVR:I \
  EZQuest-VR-FFR:I \
  EZQuest-Content:I
