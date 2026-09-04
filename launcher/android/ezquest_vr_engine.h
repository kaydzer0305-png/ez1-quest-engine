/*
Copyright (C) 2026 kaydzer0305-png

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/*
 * EZQuest engine seam helpers (public-tree scaffolding).
 *
 * Owns:
 *   - A development stub render hook (per-eye clear + stereo markers) that
 *     proves the EzVrEyeFrame contract without the full Source renderer.
 *   - A gated attempt to start LauncherMainAndroid from a secondary thread
 *     once the XR compositor has presented its first frame.
 *
 * Env knobs (read once at first request):
 *   EZQUEST_VR_STUB_HOOK=1   — register the stub hook instead of the
 *                              diagnostic grid (default: on when boot is
 *                              requested, otherwise off so the grid remains
 *                              the baseline).
 *   EZQUEST_VR_TRY_ENGINE=1  — after the first presented frame, spawn a
 *                              worker that calls LauncherMainAndroid().
 *                              This currently demonstrates the boot path
 *                              only; the real togles → external-FBO work
 *                              still lives in the private tree.
 *
 * See docs/vr-integration.md § "Engine render path scaffolding".
 */

#ifndef LAUNCHER_ANDROID_EZQUEST_VR_ENGINE_H
#define LAUNCHER_ANDROID_EZQUEST_VR_ENGINE_H

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "vr.h"

#if defined( __cplusplus )
extern "C" {
#endif

// Called once from the XR worker after the first compositor frame is
// presented. Reads env knobs, optionally registers the stub hook, and
// optionally starts the engine boot worker.
void EZQuestVrOnFirstPresent( void );

// True once EZQuestVrOnFirstPresent has run.
int EZQuestVrEngineBootRequested( void );

// True if a real (non-stub) render hook has been registered via
// EZQuestVrSetRenderHook from outside this module.
int EZQuestVrHasExternalHook( void );

// Development stub: distinct clear colour per eye + a small world-space
// marker so stereo parallax is obvious. Safe to register as the hook.
void EZQuestVrStubRenderHook( void *userdata,
                              const EzVrEyeFrame *eyes,
                              int eyeCount,
                              double predictedDisplayTime );

#if defined( __cplusplus )
}
#endif

#endif // ANDROID

#endif // LAUNCHER_ANDROID_EZQUEST_VR_ENGINE_H
