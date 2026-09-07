# Entropy: Zero 1 → Meta Quest 3S (standalone)

A personal, non-commercial project to play **Entropy: Zero 1** (the Half-Life 2 mod) as a
standalone OpenXR app on the Meta Quest 3S — no PC streaming involved.

## What this repository is

This is the **engine baseline + build farm** for the project: a private mirror of
[nillerusr/source-engine](https://github.com/nillerusr/source-engine) (Source, ported to
Android/ARM) with our own build fixes and an APK packaging pipeline running on GitHub Actions.

Current status:

- ✅ Engine compiles and links for **Android arm64** (NDK r20 + clang, libc++).
- ✅ **Signed debug APK** produced by CI (`.github/workflows/build-android-arm64.yml`).
- ✅ Flat (2D) HL2 boot baseline verified on-device.
- ✅ Immersive VR bootstrap (slice D): OpenXR session → GLES swapchain → compositor loop.
- ✅ Public-tree engine seam: `EZQuestVrSetRenderHook()` + stub hook / gated `LauncherMain`.
- ✅ Slice E input scaffolding: Quest Touch action set (`launcher/android/vr_input.cpp`).
- ✅ `vr_main.cpp` restored; ShowPixels publishes into XR swapchain FBOs.
- ✅ Slice F: Quest Touch → Source `+forward`/`+attack` + tracking snapshot.
- ✅ Slice G: per-eye Source RTs (`sourcevr` `CreateRenderTargets`) + stereo present hook.
- ✅ Slice H1: allocate `_rt_ezquest_eye_*` on `Activate()` if the matsys window was missed.
- ✅ Slice H2: re-bind the eye RT before XR submit so PostProcess cannot flatten stereo.
- ✅ Slice H3: snap-turn + `invnext` on right stick click.
- ✅ EZ1 build profile (`--build-games=ez1`) + Bad Cop Touch extras (kick / NV / manhack).
- ✅ EZ1 extra VPC lists + import script for EZ1v4.0 unique units.
- ✅ Quest FFR + 90 Hz request (`EZQUEST_VR_FFR`, `EZQUEST_VR_REFRESH`).
- ✅ Phase 1b `#ifdef EZ` allegiance / NV / bleed / metro glow hunks (CI still `hl2`).
- 🔬 Remaining: headset-verify stereo/input/FFR, import EZ1 unique units, stunstick+combine leftover hunks, then `GAME_PROFILE=ez1`.

## Roadmap

1. Headset-verify HL2 **in-headset stereo** (H1/H2 logcat + IPD check).
2. Copy EZ1v4.0 unique units (`bash scripts/import-ez1-sources.sh`) and merge `#ifdef EZ` hunks.
3. Flip `com.ezquest.engine.GAME_PROFILE` to `ez1` once `/sdcard/srceng/ez1` exists.
4. Tune FFR level / `EZQUEST_XR_RES_SCALE` on-device.

## How we build

- GitHub Actions: dispatch `build-android-arm64` → download `SourceQuest-apk` → sideload.
- Engine: `./waf configure -T release --android=aarch64,4.9,24 --togles --disable-warns`
- OpenXR loader: CI unpacks Khronos `openxr_loader_for_android` 1.0.34 into `external/openxr/`.
- Device content (not redistributed): pre-20th-anniversary HL2 (`steam_legacy`) as `hl2/` + `platform/`.

See `docs/vr-integration.md` for the Java/native contract and engine seam.
See `docs/ez1-merge.md` for the Entropy: Zero 1 game-code merge.

## Legal notes

- Engine source derives from the leaked Source 2017/2018 tree via nillerusr — **not for commercial use**, personal builds only.
- Game content is never committed. Own HL2 and Entropy: Zero on Steam and supply the files yourself.

## Credits

- [nillerusr](https://github.com/nillerusr) — Android/ARM Source engine port.
- [FWGS / Xash3D-FWGS](https://github.com/FWGS/xash3d-fwgs) — xcompile Android toolchain layer.
- [The Breadmen](https://store.steampowered.com/app/714070/) — Entropy: Zero.
- Valve — Source SDK 2013 and the games.
- Team Beef / Lambda generation community — prior art for standalone VR ports.
