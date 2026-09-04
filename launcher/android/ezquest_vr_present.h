/*
Copyright (C) 2026 kaydzer0305-png
*/

#ifndef LAUNCHER_ANDROID_EZQUEST_VR_PRESENT_H
#define LAUNCHER_ANDROID_EZQUEST_VR_PRESENT_H

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "vr.h"

#if defined( __cplusplus )
extern "C" {
#endif

void EZQuestVrPresentInit( uint32_t width, uint32_t height );
void EZQuestVrPresentShutdown( void );
int EZQuestVrBindEngineContext( void );
int EZQuestVrSubmitEngineFrame( unsigned srcTex, int width, int height );
void EZQuestVrEnginePresentHook( void *userdata, const EzVrEyeFrame *eyes,
                                 int eyeCount, double predictedDisplayTime );
int EZQuestVrHasEngineFrame( void );

#if defined( __cplusplus )
}
#endif

#endif
#endif
