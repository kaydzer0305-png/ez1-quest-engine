/*
Copyright (C) 2026 kaydzer0305-png
*/

#ifndef LAUNCHER_ANDROID_EZQUEST_VR_SOURCEINPUT_H
#define LAUNCHER_ANDROID_EZQUEST_VR_SOURCEINPUT_H

#if defined( ANDROID ) || defined( __ANDROID__ )

#if defined( __cplusplus )
extern "C" {
#endif

// Map the latest EzVrInputState onto Source +forward/+attack style
// commands. Safe to call every XR frame; no-ops until engine symbols resolve.
void EZQuestVrSourceInputSync( void );

#if defined( __cplusplus )
}
#endif

#endif
#endif
