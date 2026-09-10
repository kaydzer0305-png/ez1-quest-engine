# Headset verification checklist (Quest 3S)

This is the gate before flipping `GAME_PROFILE` to `ez1` or merging
the full `#ifdef EZ` set. Run after sideloading the CI `SourceQuest.apk`.

## Content layout

```
/sdcard/srceng/hl2/gameinfo.txt
/sdcard/srceng/hl2/hl2_misc_dir.vpk
/sdcard/srceng/hl2/hl2_pak_dir.vpk
/sdcard/srceng/platform/
```

EZ1 content (do **not** flip the profile until stereo is confirmed on HL2):

```
/sdcard/srceng/ez1/gameinfo.txt
/sdcard/srceng/episodic/
/sdcard/srceng/ep2/
```

Use pre-20th-anniversary HL2 (`steam_legacy`). Own the games; nothing
from Steam is committed here.

## Logcat

```bash
adb logcat -s EZQuest-VR:I EZQuest-VR-Input:I EZQuest-VR-Engine:I \
  EZQuest-VR-Present:I EZQuest-SourceVR:I EZQuest-VR-FFR:I EZQuest:I
```

Or: `bash scripts/headset-verify.sh`

## Pass criteria

1. **Stereo RTs**
   - `EZQuest-SourceVR: RT _rt_ezquest_eye_left`
   - `EZQuest-SourceVR: RT _rt_ezquest_eye_right`
   - `submit eye=0/1 via bound RT … (slice H2)`
   - In-headset: true IPD (hold one eye closed — scene should shift).
2. **Input**
   - Left stick walks, triggers fire, A/X jump, grips secondary.
   - Right-stick flick snap-turns ~30°.
   - Right-stick click cycles weapons.
3. **FFR / refresh**
   - `EZQuest-VR-FFR: FFR level=2 applied to 2/2 eyes`
   - Session requested 90 Hz (`EZQUEST_VR_REFRESH`).
4. **Comfort**
   - No mono/post-process flatten after a few minutes in-map.
   - If motion-sick, drop `EZQUEST_XR_RES_SCALE` to `0.75` before
     touching FFR level.

## After HL2 stereo passes

1. Port Priority 1 hunks in `docs/ez1-ifdef-inventory.md`.
2. Dispatch CI with `build_games=ez1` on a branch that already has
   unique units **and** those hunks.
3. Stage `/sdcard/srceng/ez1`, then set
   `com.ezquest.engine.GAME_PROFILE` to `ez1` in `AndroidManifest.xml`.
