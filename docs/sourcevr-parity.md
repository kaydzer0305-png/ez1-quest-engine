# SourceVR parity (0.1.25 → EZQuest)

Ports the launcher-side SourceVR behaviors EZ1 needs, reverse-engineered
from the decompiled `SourceVR-0.1.25.apk`
(`package=com.sourcevrport.hl2vr`, `GAME_PROFILE=multi`):

| SourceVR 0.1.25 (smali reference) | EZQuest port (this tree) |
| --- | --- |
| `ProfileLaunchArguments` — 1024B / 128-token validation, managed-arg blocklist, `+vr_eye_resolution` / `+vr_compositor_sharpening` allowlists | `android/java/com/ezquest/engine/ProfileLaunchArguments.java` (same limits, same sets) |
| `DeveloperProfileLaunch` — `RUN_DEV_PROFILE` + `debug.*.dev_payload=1` + extra allowlist | `android/java/com/ezquest/engine/DeveloperProfileLaunch.java` (`debug.ezquest.dev_payload`, ids `hl2`/`ez1`, override forwarding) |
| `ModManager` + `userdata/CustomContentName` — `custom/<mod>` vs `custom/.sourcevr-disabled-<mod>` renames, `UNMOUNTABLE_CONTENT_DIRECTORIES={materials,maps,models,resource,scripts,sound}`, `_dir.vpk` + numbered VPKs | `android/java/com/ezquest/engine/ModManager.java` (same marker names, same content dirs, legacy in-mod `.sourcevr-disabled` file honored read-only) |
| `MainActivity` native bootstrap reads managed `-game` itself; Java never lets users override it | `launcher/android/main.cpp:SetLauncherArgs()` — injects `-game <VALVE_GAME_PATH>/<SOURCEVR_GAME>` when `java_args` lacks `-game` (the VR `NativeActivity` path), appends validated `SOURCEVR_USER_LAUNCH_ARGS_PATH` tokens with argv bounds checks |
| `EngineEnv` publishes `EXTRAS_VPK_PATH` / `SOURCEVR_USER_LAUNCH_ARGS_PATH` | `EngineEnv.apply()` — extras from enabled `custom/` mods, launch-args published only when valid (fail-closed), summary logs mod count |
| Manifest `GAME_PROFILE` + per-flavor packages | `GAME_PROFILE` stays `hl2` in the manifest; `RUN_DEV_PROFILE` intent-filter on `LauncherActivity` for adb-gated `ez1` boots |

## Why this shape

- SourceVR ships its engine as content-addressed blobs (`assets/sourcevr/v1/blobs`)
  with 6 baked profiles (`hl2/ep1/ep2/lostcoast/portal/portal2`). EZQuest
  builds its own `libserver/libclient` from source (`--build-games=ez1`),
  so a Java-only profile entry in the SourceVR APK could never boot EZ1
  game code. The portable direction is the reverse: teach EZQuest the
  SourceVR launcher contracts.
- CI and the manifest intentionally stay on `hl2` until `/sdcard/srceng/ez1`
  exists (see `docs/ez1-merge.md`). The dev override lets testers boot `ez1`
  content on an `hl2`-manifest build without flipping the manifest early.

## Usage

### Boot ez1 without a manifest flip

```bash
adb shell setprop debug.ezquest.dev_payload 1
adb shell am start -a com.ezquest.engine.action.RUN_DEV_PROFILE \
  --es com.ezquest.engine.extra.DEV_GAME_PROFILE ez1 \
  -n com.ezquest.engine/.LauncherActivity
# or action-only (manifest now declares the filter):
adb shell am start -a com.ezquest.engine.action.RUN_DEV_PROFILE \
  --es com.ezquest.engine.extra.DEV_GAME_PROFILE ez1
```

The launcher gates content for `ez1`, forwards the override to
`EngineActivity` (and to the flat fallback / importer when those trigger),
and native injects `-game /sdcard/srceng/ez1` on the VR path. Logcat:

```
EZQuest-DevProfile: dev override authorized: ez1
EZQuest-Env: profile=ez1 game=/sdcard/srceng/ez1 ... mods=.. launchArgs=..
SetLauncherArgs: injected -game /sdcard/srceng/ez1 (VR fallback, SOURCEVR_GAME=ez1)
```

Remove the gate with `adb shell setprop debug.ezquest.dev_payload 0`.

### Per-profile launch args

```
<filesRoot>/launch-args/hl2.txt
<filesRoot>/launch-args/ez1.txt
```

(`filesRoot` = `getExternalFilesDir`, falling back to `getFilesDir`.)
Example valid contents:

```
+vr_eye_resolution 2064x2208 +vr_compositor_sharpening quality
```

Invalid files (managed flags like `-game`, quotes, `@files`, bad
`+vr_*` values, >1024B, >128 tokens) are rejected fail-closed:
`EngineEnv` unsets `SOURCEVR_USER_LAUNCH_ARGS_PATH` and logs
`launch-args invalid (<ERROR>)`. Native appends the file's tokens only
when the path env is set.

### custom/ mods

```
sdcard/srceng/ez1/custom/my_mod/materials/...
sdcard/srceng/ez1/custom/.sourcevr-disabled-old_mod/...
```

- Enabled = `custom/<name>/`; disabled = `custom/.sourcevr-disabled-<name>/`
  (rename, same as SourceVR — mods interoperate both ways).
- `ModManager.enabledModDirs()` feeds `EXTRAS_VPK_PATH` (comma-joined).
- Names: 1–64 chars, `[A-Za-z0-9_.-]`, no `..`, no separators, no leading `.`.
- `looksMountable()` reports whether a mod holds `materials/maps/models/
  resource/scripts/sound` or VPKs (diagnostics only for now; no UI yet).

## What was NOT ported (deliberate)

- Steam depot download (`SteamSession/SteamDownloadService`), Workshop,
  and OTA `UpdateManager`: EZQuest sideloads owned Steam content via the
  existing importer (`Download/EZQuest-<GAME>-Import/`) for legal reasons.
- `LauncherFranchise` tile UI and `ModManagerActivity`: the launcher is
  still a status panel, not a game grid. The data contracts above land
  first so a future UI has validated backends to call.
- Bhaptics bridge and Portal menu-map staging: VR-input work stays in
  `launcher/android/vr_input.cpp` + `ezquest_vr_sourceinput.cpp`.

## Verification

- `javac` (Android `android.jar`) compiles the touched Java files;
  see branch `feat/sourcevr-parity`.
- On-device: stage `hl2/platform` (+ `ez1/ep2/episodic` for EZ1), push a
  valid + invalid `launch-args` file in turn, toggle a `custom/` mod, and
  confirm the `EZQuest-Env` summary + `SetLauncherArgs` lines in logcat.
- Reference decompile preserved at
  `C:\Users\neilr\Downloads\SourceVR-0.1.25-apktool\` (Apktool 3.0.3).
