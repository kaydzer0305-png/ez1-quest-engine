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
6. Slice F: tracking snapshot + Source `+forward`/`+attack` mapper.
7. Slice G: per-eye Source render targets + stereo present hook.

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
- `EZQUEST_VR_TRY_ENGINE` defaults **on**. Set `=0` to keep the diagnostic grid.
- `EZQUEST_VR_SOURCE_INPUT=0` disables Quest Touch to Source command mapping

## Engine seam

```c
void EZQuestVrSetRenderHook(EzVrRenderFn fn, void *userdata);
XrSession EZQuestVrSession(void);
int EZQuestVrHeadViews(XrView outViews[2], XrSpace *outSpace);
int EzVrInputGetState(EzVrInputState *out);
int EZQuestVrCopyTracking(EzVrTrackingSnapshot *out);
int EZQuestVrSubmitEngineEyeFromCurrentFbo(int eye, int width, int height);
int EZQuestVrStereoEyesReady(void);
```

The hook must not call `xr*` functions.

Remaining work:
1. Headset test of dual-eye RTs (`_rt_ezquest_eye_left/right`). A VR-aware
   HL2 client (`supportsvr` + `-vr`, which Android now forces) should render
   each eye into those targets and call `DoDistortionProcessing`, which
   publishes the current FBO to the XR swapchain. Without a VR view loop the
   present path still blits the mono ShowPixels buffer to both eyes, or
   splits a side-by-side backbuffer.
2. Merge Entropy: Zero 1 game code once HL2 presents in-headset.

## Build

CI unpacks Khronos `openxr_loader_for_android` 1.0.34 into `external/openxr/`.
`launcher/wscript` compiles the XR compositor, present hook, tracking snapshot,
and Source input adapter on Android. `sourcevr/` builds an OpenXR-backed
`ISourceVirtualReality` module that reads that snapshot and allocates per-eye RTs.

## Logcat tags

`EZQuest-VR`, `EZQuest-VR-Input`, `EZQuest-VR-Engine`, `EZQuest-VR-Present`, `EZQuest-SourceVR`.
