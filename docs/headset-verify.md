# Headset verification checklist (Quest 3S)

Software on `master` / Phase 1b cannot prove stereo, IPD, Touch mapping, or FFR.
Run this after sideloading the CI APK. Keep `GAME_PROFILE=hl2` until `/sdcard/srceng/ez1` exists.

## 1. Install

1. Dispatch workflow `build-android-arm64` (default `--build-games=hl2`).
2. Sideload the signed debug APK.
3. Confirm content at `/sdcard/srceng/hl2` + `platform` (pre-20th-anniversary `steam_legacy`).

## 2. Boot

Set `BOOT_MODE=vr`. Expect `EngineActivity` to present within 20s; otherwise it falls back to `ValveActivity2`.

```
adb logcat -s EZQuest-VR EZQuest-VR-Input EZQuest-VR-Engine EZQuest-VR-Present EZQuest-SourceVR EZQuest-VR-FFR
```

Pass if you see:

- `EZQuest-SourceVR: RT _rt_ezquest_eye_left` (slice H1)
- `submit eye=0/1 via bound RT` (slice H2)
- `EZQuest-VR-FFR: FFR level=2 applied to 2/2 eyes`
- refresh request 90

## 3. Stereo / IPD

Stand in a hallway with vertical geometry. Each eye must have a distinct view (not a mono blit). If both eyes match, H2 bind is still flattening; dump `EZQuestVrStereoEyesReady()`.

## 4. Touch

| Control | Expect |
| --- | --- |
| Left stick | move |
| Left stick click | sprint |
| Right stick flick | 30° snap-turn |
| Right stick click | `invnext` |
| Triggers | attack |
| A / X tap | jump |
| A / X hold ~0.5s | flashlight / NV (`impulse 100`) |
| B / Y | reload |
| Left grip + trigger | use |
| Left grip + right trigger | kick (`+alt1`) |
| Left grip + right stick click | manhack (`slot5`) |

## 5. FFR / resolution knobs

`EZQUEST_VR_FFR_LEVEL` 0–4 (default 2). `EZQUEST_XR_RES_SCALE` 0.25–2.0 (default 1.0). `EZQUEST_VR_REFRESH` default 90. Tune only after stereo is confirmed.

## 6. After HL2 stereo is good

1. Sideload Entropy: Zero to `/sdcard/srceng/ez1`.
2. Merge leftover stunstick / combine hunks if needed.
3. Dispatch CI with `--build-games=ez1` and flip `com.ezquest.engine.GAME_PROFILE` to `ez1`.
