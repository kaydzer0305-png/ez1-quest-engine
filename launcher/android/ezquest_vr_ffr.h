/*
Copyright (C) 2026 kaydzer0305-png

Optional Meta Quest fixed foveated rendering + 90 Hz refresh.
Missing extensions are non-fatal; the compositor keeps running at default.
*/
#ifndef LAUNCHER_ANDROID_EZQUEST_VR_FFR_H
#define LAUNCHER_ANDROID_EZQUEST_VR_FFR_H

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "vr.h"

#ifdef __cplusplus
extern "C" {
#endif

void EZQuestVrAppendOptionalFbExts(
        const XrExtensionProperties *props, uint32_t propCount,
        const char **names, uint32_t *inoutCount, uint32_t capacity );

int EZQuestVrApplyFfrAndRefresh( XrInstance instance, XrSession session,
        XrSwapchain leftEye, XrSwapchain rightEye );

#ifdef __cplusplus
}
#endif

#endif
#endif
