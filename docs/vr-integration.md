# VR integration (slice D): OpenXR bootstrap

This document describes how the immersive VR boot path is wired into this
mirror, what the Java/native contract is, and where the engine render path
(the work currently living in the private OpenXR tree) plugs in.

## What slice D delivers

`launcher/android/vr_main.cpp` + `vr_scene.cpp` (compiled into
`liblauncher.so` together with the flat SDL glue in `android/main.cpp`)
implement the native half of the `EngineActivity` boot path:

1. `ANativeActivity_onCreate` — the entry `android.app.lib_name=launcher`
   resolves to when `EngineActivity` (a `NativeActivity`) starts. It spawns
   a worker thread and registers lifecycle callbacks.
2. OpenXR bootstrap on the worker thread:
   `xrInitializeLoaderKHR` (Android loader init with VM + activity) →
   `xrCreateInstance` (`XR_KHR_android_create_instance`,
   `XR_KHR_opengl_es_enable`, plus `XR_KHR_android_thread_settings` when
   offered) → `xrGetSystem(HEAD_MOUNTED_DISPLAY)` →
   `xrCreateSession` bound to an EGL display/config/context trio →
   `xrCreateReferenceSpace(LOCAL)` → two primary-stereo color swapchains.
3. A frame loop: `xrWaitFrame` → `xrBeginFrame` → `xrLocateViews` → per-eye
   `xrAcquireSwapchainImage` / render into an FBO wrapping the swapchain
   image / `xrReleaseSwapchainImage` → `xrEndFrame` with an
   `XrCompositionLayerProjection` of both eyes.
4. The built-in diagnostic scene (head-tracked grid floor + horizon
   gradient, animated so a static frame is never mistaken for a 2D image).
   It exists purely to prove the compositor path end-to-end and is replaced
   wholesale once the engine registers its render hook.
5. Status reporting for Java: `nativeXrStatus()` (0 starting, 1 presenting,
   2 failed), `nativeXrFailReason()`, plus the existing bridge calls
   `EZQuestSetEngineUp(1)` and `EZQuestWriteHeartbeat()` once the first
   frame is presented.

## Boot flow (BOOT_MODE safe flip)

```
LauncherActivity (2D, MAIN/LAUNCHER)
    |  content gates (ContentRouter: importer, RecoveryActivity)
    |
    |  BOOT_MODE meta-data (com.ezquest.engine.BOOT_MODE, default "flat")
    |
    +-- "flat" --> ValveActivity2 (SDLActivity, verified HL2 baseline)
    |
    +-- "vr"   --> EngineActivity (NativeActivity, :engine process)
                      |  profile/content gates (unchanged)
                      |  NativeActivity loads liblauncher.so
                      |  vr_main.cpp worker boots OpenXR
                      |
                      +-- presents  -> stay in headset
                      +-- fail/timeout (20 s) -> ValveActivity2 (flat fallback)
```

The manifest ships `BOOT_MODE=vr`. Set it to `flat` to restore the pure 2D
boot without touching any code. The fallback is deliberately one-way and
automatic: a broken XR bootstrap can never brick the headset boot — the
worst case is a few seconds of black before the flat engine takes over.

## Java/native contract

| Symbol | Side | Meaning |
| --- | --- | --- |
| `Java_com_ezquest_engine_EngineActivity_nativeXrStatus` | native, polled by Java | `EzVrStatus_e`: 0 starting / 1 running / 2 failed |
| `Java_com_ezquest_engine_EngineActivity_nativeXrFailReason` | native, polled by Java | human-readable failure text for logs and RecoveryActivity |
| `Java_com_ezquest_engine_DeviceBridge_nativeEngineUp` | native, polled by Java | nonzero once `EZQuestSetEngineUp(1)` was called |
| `Java_com_ezquest_engine_DeviceBridge_nativePushBattery/Thermal` | Java → native | device state the engine can read (see `main.cpp` bridge block) |
| `EZQuestWriteHeartbeat` | native internal | heartbeat file the Java `AnrWatchdog` reads |

The activity polls every 500 ms for up to 20 s. XR init on Quest normally
presents within a couple of seconds; the timeout only guards degenerate
cases (runtime wedged, permissions issues) and then routes to flat.

## Environment contract

The VR path runs in the `:engine` process, so it consumes exactly the env
vars `EngineEnv.apply()` published before the native library loads
(`APP_DATA_PATH`, `NATIVE_LIB_DIR`, `SOURCEVR_*`, `VALVE_GAME_PATH`, ...).
`vr_main.cpp` does not read them yet (the diagnostic scene needs no
content), but the engine render path plugged in below inherits them for
free — same contract the flat `ValveActivity2` path already exercises.

Extra knob:

- `EZQUEST_XR_RES_SCALE` (float, 0.25–2.0, default 1.0) — multiplier on the
  runtime-recommended swapchain dimensions. This is the lever for the
  roadmap's "resolution scaling" perf pass.

## The seam for the engine render path

The frame loop owns the XR session, the swapchains, and per-eye FBOs. The
engine plugs in without touching any of that lifecycle code:

```c
// launcher/android/vr.h
typedef void (*EzVrRenderFn)(void *userdata, const EzVrEyeFrame *eyes,
                             int eyeCount, double predictedDisplayTime);
void EZQuestVrSetRenderHook(EzVrRenderFn fn, void *userdata);

XrSession EZQuestVrSession(void);                      // live session
int EZQuestVrHeadViews(XrView outViews[2], XrSpace*);  // last-located head views
```

Every presented frame, the loop fills an `EzVrEyeFrame` per eye —
world→eye and eye→world matrices, GL projection (column-major), the bound
FBO, and the swapchain image dimensions — and calls the hook on the VR
thread. The default hook draws the diagnostic grid scene; the engine
registers its own hook (drawing the Source frame via togles into those FBOs)
and the loop needs no further changes. The hook must not call any `xr*`
functions; acquisition/release/projection-layer submission stays in the
loop.

The engine boot itself (replacing the diagnostic scene with real gameplay)
lands in the private OpenXR tree: it must start `LauncherMain` off the XR
worker thread and register the hook once the togles renderer can target
external FBOs. Until then `EngineActivity` + the diagnostic scene are the
complete, shippable result of this slice.

## Build integration

- CI (`.github/workflows/build-android-arm64.yml`) downloads the Khronos
  `openxr_loader_for_android` AAR (pinned 1.0.34) from Maven Central and
  unpacks headers into `external/openxr/include/openxr/` and
  `libopenxr_loader.so` into `external/openxr/lib/aarch64/` (untracked,
  build-time only).
- `launcher/wscript` registers the `OPENXR` uselib from that directory
  (`-lopenxr_loader -lEGL -lGLESv3 -landroid`) and compiles
  `android/vr_main.cpp` + `android/vr_scene.cpp` into `liblauncher.so`
  on Android targets only. Configure fails loudly if the AAR was not
  fetched.
- `libopenxr_loader.so` is packaged into the APK's `lib/arm64-v8a/` next to
  `liblauncher.so`; the dynamic linker resolves the `DT_NEEDED` from the
  extracted native lib dir.

## Debugging

- Logcat tags: `EZQuest-VR` (native bootstrap + frame loop), `EZQuest-Engine`
  (activity gates, XR polling, fallback), `EZQuest-ANR`, `EZQuest-Bridge`.
- `EZQUEST_XR_RES_SCALE` is an environment variable; the simplest way to
  set a device-local override today is a line in the per-profile launch
  env (`EngineEnv` supports `SOURCEVR_USER_LAUNCH_ARGS_PATH` for args; for
  env-only knobs, adjust the default in `EzResolutionScale()`).
- Diagnostics export from RecoveryActivity includes the session log written
  by the diagnostics stack (`diag/EventLog`).

## Known limitations (by design, this slice)

- No input: only head pose is consumed; controllers land with the
  action-space slice in the engine tree (the manifest already declares
  hand-tracking as optional).
- No reprojection tricks, foveation, or comfort options yet — the perf pass
  is roadmap item 3.
- The diagnostic scene ignores `EZQuestVrSession()`/input and does not
  pause rendering on `onPause` beyond what the runtime's session state
  machine already implies (VISIBLE→SYNCHRONIZED transitions).
