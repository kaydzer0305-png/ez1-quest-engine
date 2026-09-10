#!/usr/bin/env bash
# Headset verify helper for Quest 3S (issue #22).
# Gate before flipping GAME_PROFILE=ez1.
#
# Usage:
#   bash scripts/headset-verify.sh            # live logcat
#   bash scripts/headset-verify.sh --check    # adb + /sdcard/srceng layout only
#   bash scripts/headset-verify.sh --once 25  # capture 25s then print needle hits
set -euo pipefail

TAGS=(
  EZQuest:I
  EZQuest-VR:I
  EZQuest-VR-Input:I
  EZQuest-VR-Engine:I
  EZQuest-VR-Present:I
  EZQuest-SourceVR:I
  EZQuest-VR-FFR:I
  EZQuest-Content:I
)

need_adb() {
  command -v adb >/dev/null || { echo "adb not on PATH" >&2; exit 1; }
  adb get-state >/dev/null 2>&1 || {
    echo "no adb device. enable USB debugging / wireless adb on the Quest 3S." >&2
    exit 1
  }
}

print_needles() {
  cat <<'EOF'
== issue #22 needles ==
  EZQuest-SourceVR: RT _rt_ezquest_eye_left
  EZQuest-SourceVR: RT _rt_ezquest_eye_right
  submit eye=0 via bound RT
  submit eye=1 via bound RT
  EZQuest-VR-FFR: FFR level=2 applied to 2/2 eyes
In-headset: cover one eye — scene must shift (true IPD).
Input: left stick walk, triggers fire, A/X jump, right-stick flick snap-turns, click invnext.
EOF
}

check_content() {
  echo "== required HL2 content =="
  local missing=0
  for p in \
    /sdcard/srceng/hl2/gameinfo.txt \
    /sdcard/srceng/hl2/hl2_misc_dir.vpk \
    /sdcard/srceng/hl2/hl2_pak_dir.vpk \
    /sdcard/srceng/platform
  do
    if adb shell "[ -e $p ]" >/dev/null 2>&1; then
      echo "  OK       $p"
    else
      echo "  MISSING  $p"
      missing=1
    fi
  done
  echo "== optional EZ1 content (do not flip GAME_PROFILE until stereo passes) =="
  for p in /sdcard/srceng/ez1/gameinfo.txt /sdcard/srceng/episodic /sdcard/srceng/ep2; do
    if adb shell "[ -e $p ]" >/dev/null 2>&1; then
      echo "  present  $p"
    else
      echo "  absent   $p"
    fi
  done
  return "$missing"
}

score_capture() {
  local log="$1"
  echo "== needle hits in capture =="
  grep -E "_rt_ezquest_eye_left" "$log" | head -1 || echo "  MISS  RT left"
  grep -E "_rt_ezquest_eye_right" "$log" | head -1 || echo "  MISS  RT right"
  grep -E "submit eye=0" "$log" | head -1 || echo "  MISS  submit eye=0"
  grep -E "submit eye=1" "$log" | head -1 || echo "  MISS  submit eye=1"
  grep -E "FFR level=" "$log" | head -1 || echo "  MISS  FFR applied"
}

MODE="${1:-live}"
need_adb
print_needles
check_content || true

if [[ "$MODE" == "--check" ]]; then
  exit 0
fi

if [[ "$MODE" == "--once" ]]; then
  SECS="${2:-20}"
  TMP="$(mktemp)"
  echo "== capturing ${SECS}s of logcat (boot BOOT_MODE=vr first) =="
  timeout "${SECS}" adb logcat -s "${TAGS[@]}" | tee "$TMP" || true
  score_capture "$TMP"
  rm -f "$TMP"
  exit 0
fi

exec adb logcat -s "${TAGS[@]}"
