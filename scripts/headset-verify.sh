#!/usr/bin/env bash
# Quest 3S verification helper. Gate before GAME_PROFILE=ez1.
set -euo pipefail

TAGS=(EZQuest:I EZQuest-VR:I EZQuest-VR-Input:I EZQuest-VR-Engine:I EZQuest-VR-Present:I EZQuest-SourceVR:I EZQuest-VR-FFR:I EZQuest-Content:I)

need_adb() {
  command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
  adb get-state >/dev/null 2>&1 || { echo "no adb device; enable Quest USB debugging or wireless adb" >&2; exit 1; }
}

check_content() {
  echo "== required HL2 content (BOOT_MODE=vr, GAME_PROFILE=hl2) =="
  local missing=0
  for p in /sdcard/srceng/hl2/gameinfo.txt /sdcard/srceng/hl2/hl2_misc_dir.vpk /sdcard/srceng/hl2/hl2_pak_dir.vpk /sdcard/srceng/platform; do
    if adb shell "[ -e '$p' ]" >/dev/null 2>&1; then
      echo "  OK       $p"
    else
      echo "  MISSING  $p"
      missing=1
    fi
  done
  echo "== optional EZ1 content (do NOT flip GAME_PROFILE until HL2 stereo passes) =="
  for p in /sdcard/srceng/ez1/gameinfo.txt /sdcard/srceng/episodic /sdcard/srceng/ep2; do
    if adb shell "[ -e '$p' ]" >/dev/null 2>&1; then
      echo "  OK       $p"
    else
      echo "  (absent) $p"
    fi
  done
  return "$missing"
}

dump_config() {
  echo "== device knobs (empty = engine default) =="
  adb shell "getprop | grep -i -E 'ezquest|boot_mode|game_profile' || echo '  (no EZQuest system properties set)'"
  echo "-- EngineActivity extras / env understood by vr_main.cpp:"
  echo "  EZQUEST_XR_RES_SCALE (default 1.0), EZQUEST_VR_FFR=1 (level 2), EZQUEST_VR_REFRESH=90,"
  echo "  EZQUEST_VR_SNAP_TURN=1 (EZQUEST_VR_SNAP_DEGREES=30), EZQUEST_VR_SOURCE_INPUT=1"
}

score_capture() {
  local log="$1" misses=0 total=0
  check_needle() {
    total=$((total + 1))
    if grep -F -m1 "$1" "$log" >/dev/null; then
      echo "  PASS  $1"
    else
      echo "  MISS  $1"
      misses=$((misses + 1))
    fi
  }
  echo "== stereo RTs (slice G/H1) =="
  check_needle "_rt_ezquest_eye_left"
  check_needle "_rt_ezquest_eye_right"
  echo "== stereo submit (slice H2) =="
  check_needle "submit eye=0"
  check_needle "submit eye=1"
  echo "== FFR / refresh =="
  check_needle "FFR level="
  check_needle "90"
  echo "== result: $((total - misses))/$total log needles found =="
  echo "In-headset still required: true IPD (cover one eye), snap-turn, fire, no mono flattening."
  return "$misses"
}

MODE="${1:-live}"
need_adb

if [[ "$MODE" == "--check" ]]; then
  check_content
  exit $?
fi

if [[ "$MODE" == "--config" ]]; then
  dump_config
  exit $?
fi

check_content || true

if [[ "$MODE" == "--once" ]]; then
  SECS="${2:-25}"
  [[ "$SECS" =~ ^[1-9][0-9]*$ ]] || { echo "seconds must be a positive integer" >&2; exit 2; }
  TMP="$(mktemp)"
  trap 'rm -f "$TMP"' EXIT
  dump_config || true
  echo "== capturing ${SECS}s of logcat (BOOT_MODE=vr, in-map) =="
  timeout "${SECS}s" adb logcat -s "${TAGS[@]}" | tee "$TMP" || true
  score_capture "$TMP"
  exit $?
fi

exec adb logcat -s "${TAGS[@]}"
