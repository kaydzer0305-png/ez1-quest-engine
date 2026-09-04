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

/* Mono / side-by-side backbuffer. If width looks like SBS (≈ 2× eye width)
 * the buffer is split into left/right present targets. */
int EZQuestVrSubmitEngineFrame( unsigned srcTex, int width, int height );

/* Publish one Source eye. srcTex = 0 means blit the currently bound READ FBO. */
int EZQuestVrSubmitEngineEye( int eye, unsigned srcTex, int width, int height );
int EZQuestVrSubmitEngineEyeFromCurrentFbo( int eye, int width, int height );

void EZQuestVrEnginePresentHook( void *userdata, const EzVrEyeFrame *eyes,
                                 int eyeCount, double predictedDisplayTime );
int EZQuestVrHasEngineFrame( void );

/* 1 if both eyes were published this generation — ShowPixels should not
 * overwrite them with a mono blit. */
int EZQuestVrStereoEyesReady( void );

#if defined( __cplusplus )
}
#endif

#endif
#endif
