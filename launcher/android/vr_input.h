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
 * EZQuest slice E: OpenXR action set + Quest Touch suggested bindings.
 *
 * Lives beside the compositor loop so the engine (and the stub hook) can
 * read a snapshot of controller pose / buttons without owning the session.
 * Init failure is non-fatal — the headset still presents the diagnostic
 * scene with head tracking only.
 */

#ifndef LAUNCHER_ANDROID_VR_INPUT_H
#define LAUNCHER_ANDROID_VR_INPUT_H

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "vr.h"

#if defined( __cplusplus )
extern "C" {
#endif

enum EzVrHand_e
{
        EZ_VR_HAND_LEFT = 0,
        EZ_VR_HAND_RIGHT = 1,
        EZ_VR_HAND_COUNT = 2,
};

struct EzVrController
{
        int active;
        XrPosef gripPose;
        XrPosef aimPose;
        float trigger;
        float grip;
        float stickX;
        float stickY;
        int triggerClick;
        int gripClick;
        int stickClick;
        int primaryButton;
        int secondaryButton;
        int menuButton;
};

struct EzVrInputState
{
        int ready;
        struct EzVrController hands[EZ_VR_HAND_COUNT];
};

bool EzVrInputInit( XrInstance instance, XrSession session, XrSpace localSpace,
                    char *errBuf, size_t errLen );
void EzVrInputShutdown( void );
void EzVrInputSync( XrTime displayTime );
int EzVrInputGetState( struct EzVrInputState *out );

#if defined( __cplusplus )
}
#endif

#endif // ANDROID

#endif // LAUNCHER_ANDROID_VR_INPUT_H
