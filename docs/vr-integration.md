# VR integration: OpenXR bootstrap + engine seam

This document describes how the immersive VR boot path is wired into this
mirror, what the Java/native contract is, and where the engine render path
plugs in.

## What is in the public tree

`launcher/android/vr_main.cpp` + `vr_scene.cpp` + `vr_input.cpp` +
`ezquest_vr_engine.cpp` (compiled into `liblauncher.so`) implement the
native half of the `EngineActivity` boot path:

1. `ANativeActivity_onCreate` starts a worker that boots OpenXR.
2. Loader init -> instance -> GLES session -> LOCAL space -> stereo swapchains.
3. Frame loop presents a diagnostic scene or a registered render hook.
4. Java status contract + heartbeat on first present.
5. Slice E input: `ezquest` action set with Quest Touch bindings.
   Snapshot via `EzVrInputGetState()`. Init failure is non-fatal.

## Boot flow (BOOT_MODE safe flip)

```
LauncherActivity
    BOOT_MODE=flat -> ValveActivity2 (verified 2D HL2)
    BOOT_MODE=vr   -> EngineActivity
                      present OK -> stay in headset
                      fail/timeout 20s -> ValveActivity2
```

## Environment knobs

- `EZQUEST_XR_RES_SCALE` (0.25-2.0, default 1.0)
- `EZQUEST_VR_STUB_HOOK=1` teal/purple stub hook after first present
- `EZQUEST_VR_TRY_ENGINE=1` spawn `LauncherMainAndroid()` (scaffolding only)

## Engine seam

```c
void EZQuestVrSetRenderHook(EzVrRenderFn fn, void *userdata);
XrSession EZQuestVrSession(void);
int EZQuestVrHeadViews(XrView outViews[2], XrSpace *outSpace);
int EzVrInputGetState(EzVrInputState *out);
```

The hook must not call `xr*` functions.

Remaining work:
1. Start LauncherMain without SDL owning a window; reuse XR EGL context.
2. Point togles at `EzVrEyeFrame.fbo` with per-eye view/proj.
3. Feed `EzVrInputState` into Source input.
4. Merge Entropy: Zero 1 game code once HL2 presents in-headset.

## Build

CI unpacks Khronos `openxr_loader_for_android` 1.0.34 into `external/openxr/`.
`launcher/wscript` compiles `vr_main.cpp`, `vr_scene.cpp`, `vr_input.cpp`,
and `ezquest_vr_engine.cpp` on Android only.

## Logcat tags

`EZQuest-VR`, `EZQuest-VR-Input`, `EZQuest-VR-Engine`, `EZQuest-Engine`.
