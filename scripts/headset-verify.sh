#!/usr/bin/env bash
# Quest 3S verification helper. Gate before GAME_PROFILE=ez1.
set -euo pipefail

TAGS=(EZQuest:I EZQuest-VR:I EZQuest-VR-Input:I EZQuest-VR-Engine:I EZQuest-VR-Present:I EZQuest-SourceVR:I EZQuest-VR-FFR:I EZQuest-Content:I)

need_adb() {
  command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
  adb get-state >/dev/null 2>&1 || { echo "no adb device; enable Quest USB debugging or wireless adb" >&2; exit 1; }
}

check_content() {
  echo "== required HL2 content =="
  local missing=0
  for p in /sdcard/srceng/hl2/gameinfo.txt /sdcard/srceng/hl2/hl2_misc_dir.vpk /sdcard/srceng/hl2/hl2_pak_dir.vpk /sdcard/srceng/platform; do
    if adb shell "[ -e '$p' ]" >/dev/null 2>&1; then
      echo "  OK       $p"
    else
      echo "  MISSING  $p"
      missing=1
    fi
  done
  return "$missing"
}

score_capture() {
  local log="$1" misses=0
  for needle in "_rt_ezquest_eye_left" "_rt_ezquest_eye_right" "submit eye=0" "submit eye=1" "FFR level="; do
    if grep -F -m1 "$needle" "$log"; then :; else echo "  MISS  $needle"; misses=$((misses + 1)); fi
  done
  echo "== result: $((5 - misses))/5 log needles found =="
  return "$misses"
}

MODE="${1:-live}"
need_adb

if [[ "$MODE" == "--check" ]]; then
  check_content
  exit $?
fi

check_content || true

if [[ "$MODE" == "--once" ]]; then
  SECS="${2:-20}"
  [[ "$SECS" =~ ^[1-9][0-9]*$ ]] || { echo "seconds must be a positive integer" >&2; exit 2; }
  TMP="$(mktemp)"
  trap 'rm -f "$TMP"' EXIT
  echo "== capturing ${SECS}s of logcat =="
  timeout "${SECS}s" adb logcat -s "${TAGS[@]}" | tee "$TMP" || true
  score_capture "$TMP"
  exit $?
fi

exec adb logcat -s "${TAGS[@]}"
