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
- ✅ Headset-verify kit: `docs/headset-verify.md` + `scripts/headset-verify.sh --check|--once`.
- 🔬 Remaining on-device: confirm stereo / input / FFR on Quest 3S (issue #22), then finish `#ifdef EZ` hunks (issue #23) and flip `GAME_PROFILE=ez1`.

## Roadmap

1. Headset-verify HL2 **in-headset stereo** (H1/H2 logcat + IPD check) — run `bash scripts/headset-verify.sh --once 25` on the Quest 3S. Issue #22.
2. Unique EZ1v4.0 units via `bash scripts/import-ez1-sources.sh` / workflow `import-ez1-units` (this branch). `#ifdef EZ` hunks stay on `feat/ez-ifdef-phase1b` (PR #21).
3. Flip `com.ezquest.engine.GAME_PROFILE` to `ez1` once `/sdcard/srceng/ez1` exists **and** #22 passes.
4. Tune FFR level / `EZQUEST_XR_RES_SCALE` on-device.

## How we build

- GitHub Actions: dispatch `build-android-arm64` → download `SourceQuest-apk` → sideload.
- Keep `build_games=hl2` until #22 passes. `ez1` is wired but must not be the CI default yet.
- Engine: `./waf configure -T release --android=aarch64,4.9,24 --togles --disable-warns`
- OpenXR loader: CI unpacks Khronos `openxr_loader_for_android` 1.0.34 into `external/openxr/`.
- Device content (not redistributed): pre-20th-anniversary HL2 (`steam_legacy`) as `hl2/` + `platform/`.

See `docs/vr-integration.md` for the Java/native contract and engine seam.
See `docs/ez1-merge.md` for the Entropy: Zero 1 game-code merge.
See `docs/headset-verify.md` for the Quest 3S pass/fail gate.

## Legal notes

- Engine source derives from the leaked Source 2017/2018 tree via nillerusr — **not for commercial use**, personal builds only.
- Game content is never committed. Own HL2 and Entropy: Zero on Steam and supply the files yourself.

## Credits

- [nillerusr](https://github.com/nillerusr) — Android/ARM Source engine port.
- [FWGS / Xash3D-FWGS](https://github.com/FWGS/xash3d-fwgs) — xcompile Android toolchain layer.
- [The Breadmen](https://store.steampowered.com/app/714070/) — Entropy: Zero.
- Valve — Source SDK 2013 and the games.
- Team Beef / Lambda generation community — prior art for standalone VR ports.
