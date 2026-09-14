# Headset verification checklist (Quest 3S)

This is the gate before flipping `GAME_PROFILE` to `ez1`. Run it after sideloading the CI `SourceQuest.apk`. Tracked as issue #22.

## Content layout

```
/sdcard/srceng/hl2/gameinfo.txt
/sdcard/srceng/hl2/hl2_misc_dir.vpk
/sdcard/srceng/hl2/hl2_pak_dir.vpk
/sdcard/srceng/platform/
```

Optional EZ1 content (do not flip the profile until stereo is confirmed on HL2):

```
/sdcard/srceng/ez1/gameinfo.txt
/sdcard/srceng/episodic/
/sdcard/srceng/ep2/
```

Use pre-20th-anniversary HL2 (`steam_legacy`). Own the games; nothing from Steam is committed here.

## How to run

1. Dispatch `build-android-arm64` with `build_games=hl2`.
2. Sideload `SourceQuest.apk` and set `BOOT_MODE=vr`.
3. With `adb` connected to the Quest 3S:

```bash
bash scripts/headset-verify.sh --check
bash scripts/headset-verify.sh --once 25
bash scripts/headset-verify.sh
```

## Pass criteria

1. **Stereo RTs**
   - `EZQuest-SourceVR: RT _rt_ezquest_eye_left`
   - `EZQuest-SourceVR: RT _rt_ezquest_eye_right`
   - `submit eye=0/1 via bound RT` (slice H2)
   - In-headset true IPD: covering one eye must visibly shift the scene.
2. **Input**
   - Left stick walks; triggers fire; A/X jumps; grips use secondary action.
   - Right-stick flick snap-turns about 30°.
   - Right-stick click cycles weapons.
3. **FFR / refresh**
   - `EZQuest-VR-FFR: FFR level=2 applied to 2/2 eyes`
   - The session requests 90 Hz.
4. **Comfort**
   - No mono/post-process flattening after several minutes in-map.
   - If performance is poor, try `EZQUEST_XR_RES_SCALE=0.75` before changing FFR.

Paste the `--once` summary into issue #22. Do not set `GAME_PROFILE=ez1` until this passes.
