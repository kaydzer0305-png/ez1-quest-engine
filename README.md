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
- ⚠️ `launcher/android/vr_main.cpp` was wiped to an empty blob on master. Restore it with
  `scripts/restore-vr-main.sh` (checks out slice D and applies `scripts/vr_main_slice_e.patch`).
- 🔬 Remaining: togles → XR FBO render path, Source input mapping, EZ1 game-code merge.

## Roadmap

1. Base HL2 running **in-headset in VR** — compositor done; engine render path is next.
2. Merge **Entropy: Zero 1** game code (SDK-2013-based) into the engine build.
3. Comfort options + performance (fixed foveation; `EZQUEST_XR_RES_SCALE` already exists).

## How we build

- GitHub Actions: dispatch `build-android-arm64` → download `SourceQuest-apk` → sideload.
- Engine: `./waf configure -T release --android=aarch64,4.9,24 --togles --disable-warns`
- OpenXR loader: CI unpacks Khronos `openxr_loader_for_android` 1.0.34 into `external/openxr/`.
- Device content (not redistributed): pre-20th-anniversary HL2 (`steam_legacy`) as `hl2/` + `platform/`.

See `docs/vr-integration.md` for the Java/native contract and engine seam.

## Legal notes

- Engine source derives from the leaked Source 2017/2018 tree via nillerusr — **not for commercial use**, personal builds only.
- Game content is never committed. Own HL2 and Entropy: Zero on Steam and supply the files yourself.

## Credits

- [nillerusr](https://github.com/nillerusr) — Android/ARM Source engine port.
- [FWGS / Xash3D-FWGS](https://github.com/FWGS/xash3d-fwgs) — xcompile Android toolchain layer.
- [The Breadmen](https://store.steampowered.com/app/714070/) — Entropy: Zero.
- Valve — Source SDK 2013 and the games.
- Team Beef / Lambda generation community — prior art for standalone VR ports.
