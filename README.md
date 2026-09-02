# Entropy: Zero 1 → Meta Quest 3S (standalone)

A personal, non-commercial project to play **Entropy: Zero 1** (the Half-Life 2 mod) as a
standalone OpenXR app on the Meta Quest 3S — no PC streaming involved.

## What this repository is

This is the **engine baseline + build farm** for the project: a private mirror of
[nillerusr/source-engine](https://github.com/nillerusr/source-engine) (Source, ported to
Android/ARM) with our own build fixes and an APK packaging pipeline running on GitHub Actions.

Current status:

- ✅ Engine compiles and links for **Android arm64** (NDK r20 + clang, libc++).
  Build fixes live in `scripts/waifulib/xcompile.py` and `wscript`:
  libc++ instead of the removed GNU STL for NDK ≥ r18, `libandroid_support`
  headers excluded (they shadow bionic and break `<cmath>`) but its archive
  still linked for `iconv`.
- ✅ **Signed debug APK** produced by CI (`.github/workflows/build-android-arm64.yml`,
  manual dispatch): waf-built `.so` files + prebuilt SDL2 + `libc++_shared.so`
  + SDL2 Java activity layer, zipaligned and apksigner-signed.
- ✅ Flat (2D) HL2 boot baseline verified on-device.
- 🔬 Immersive VR rendering (OpenXR session → swapchain → compositor) is being
  finished in a separate private tree, not in this mirror yet.

## Roadmap

1. Base HL2 running **in-headset in VR** (OpenXR frame loop complete).
2. Merge **Entropy: Zero 1** game code (SDK-2013-based) into the engine build.
3. Motion-controller input mapping, comfort options, performance pass
   (fixed foveation, resolution scaling).

## How we build

- GitHub Actions (this repo, private): dispatch
  `build-android-arm64` → download the `SourceQuest-apk` artifact → sideload.
- Engine: `./waf configure -T release --android=aarch64,4.9,24 --togles --disable-warns`
- Device content (supplied by the user, not redistributed): pre-20th-anniversary
  Half-Life 2 files (Steam `steam_legacy` branch) in the classic
  `hl2/` + `platform/` layout on device storage.

## Legal notes

- The engine source here derives from the leaked Source 2017/2018 tree
  (via nillerusr's port) — **not for commercial purposes**, and not something
  we distribute. Builds are personal-use only.
- Game content (Valve's, and the Entropy: Zero team's) is never committed
  here. You must own Half-Life 2 (and Entropy: Zero) on Steam and supply the
  files yourself.

## Credits

- [nillerusr](https://github.com/nillerusr) — the Android/ARM Source engine port
  and the years of engine fixes this project stands on.
- [FWGS / Xash3D-FWGS](https://github.com/FWGS/xash3d-fwgs) — the xcompile
  Android toolchain layer.
- [The Breadmen](https://store.steampowered.com/app/714070/) — Entropy: Zero.
- Valve — Source SDK 2013 and the games.
- Team Beef / Lambda generation community — prior art for standalone VR ports.
