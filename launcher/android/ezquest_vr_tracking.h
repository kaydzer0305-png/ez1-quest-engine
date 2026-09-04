/*
Copyright (C) 2026 kaydzer0305-png
*/

#ifndef LAUNCHER_ANDROID_EZQUEST_VR_TRACKING_H
#define LAUNCHER_ANDROID_EZQUEST_VR_TRACKING_H

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "vr.h"

#if defined( __cplusplus )
extern "C" {
#endif

struct EzVrTrackingSnapshot
{
        int valid;
        int eyeCount;
        uint32_t width;
        uint32_t height;
        XrView views[2];
        float view[2][16];
        float viewInv[2][16];
        float proj[2][16];
        float midPos[3];
        float midQuat[4];
};

void EZQuestVrPublishEyes( const EzVrEyeFrame *eyes, int eyeCount, const XrView *views );
int EZQuestVrCopyTracking( struct EzVrTrackingSnapshot *out );
int EZQuestVrTrackingValid( void );

#if defined( __cplusplus )
}
#endif

#endif
#endif
