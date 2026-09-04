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

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "vr_input.h"

#include <android/log.h>
#include <string.h>
#include <stdio.h>

#define EZTAG "EZQuest-VR-Input"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )
#define EZERR( ... ) __android_log_print( ANDROID_LOG_ERROR, EZTAG, __VA_ARGS__ )

struct EzVrInput
{
        XrInstance instance;
        XrSession session;
        XrSpace localSpace;
        XrActionSet actionSet;
        XrAction gripPose;
        XrAction aimPose;
        XrAction trigger;
        XrAction grip;
        XrAction stick;
        XrAction triggerClick;
        XrAction gripClick;
        XrAction stickClick;
        XrAction primaryBtn;
        XrAction secondaryBtn;
        XrAction menuBtn;
        XrPath handPath[EZ_VR_HAND_COUNT];
        XrSpace gripSpace[EZ_VR_HAND_COUNT];
        XrSpace aimSpace[EZ_VR_HAND_COUNT];
        EzVrInputState state;
};

static EzVrInput g_in;

static XrPath MakePath( const char *s )
{
        XrPath path = XR_NULL_PATH;
        if ( XR_FAILED( xrStringToPath( g_in.instance, s, &path ) ) )
                return XR_NULL_PATH;
        return path;
}

static bool CreateAction( XrActionType type, const char *name, const char *localized, XrAction *out )
{
        XrActionCreateInfo info = {};
        info.type = XR_TYPE_ACTION_CREATE_INFO;
        info.actionType = type;
        snprintf( info.actionName, sizeof info.actionName, "%s", name );
        snprintf( info.localizedActionName, sizeof info.localizedActionName, "%s", localized );
        info.countSubactionPaths = EZ_VR_HAND_COUNT;
        info.subactionPaths = g_in.handPath;
        XrResult r = xrCreateAction( g_in.actionSet, &info, out );
        if ( XR_FAILED( r ) )
        {
                EZERR( "xrCreateAction(%s) failed: %d", name, (int)r );
                *out = XR_NULL_HANDLE;
                return false;
        }
        return true;
}

bool EzVrInputInit( XrInstance instance, XrSession session, XrSpace localSpace, char *errBuf, size_t errLen )
{
        memset( &g_in, 0, sizeof g_in );
        g_in.instance = instance;
        g_in.session = session;
        g_in.localSpace = localSpace;

        g_in.handPath[EZ_VR_HAND_LEFT] = MakePath( "/user/hand/left" );
        g_in.handPath[EZ_VR_HAND_RIGHT] = MakePath( "/user/hand/right" );
        if ( g_in.handPath[0] == XR_NULL_PATH || g_in.handPath[1] == XR_NULL_PATH )
        {
                snprintf( errBuf, errLen, "xrStringToPath(hand) failed" );
                return false;
        }

        XrActionSetCreateInfo setInfo = {};
        setInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
        snprintf( setInfo.actionSetName, sizeof setInfo.actionSetName, "ezquest" );
        snprintf( setInfo.localizedActionSetName, sizeof setInfo.localizedActionSetName, "EZQuest" );
        setInfo.priority = 0;
        if ( XR_FAILED( xrCreateActionSet( instance, &setInfo, &g_in.actionSet ) ) )
        {
                snprintf( errBuf, errLen, "xrCreateActionSet failed" );
                return false;
        }

        if ( !CreateAction( XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", &g_in.gripPose ) ||
             !CreateAction( XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose", &g_in.aimPose ) ||
             !CreateAction( XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", &g_in.trigger ) ||
             !CreateAction( XR_ACTION_TYPE_FLOAT_INPUT, "grip", "Grip", &g_in.grip ) ||
             !CreateAction( XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Thumbstick", &g_in.stick ) ||
             !CreateAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_click", "Trigger Click", &g_in.triggerClick ) ||
             !CreateAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "grip_click", "Grip Click", &g_in.gripClick ) ||
             !CreateAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Stick Click", &g_in.stickClick ) ||
             !CreateAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "primary", "Primary Button", &g_in.primaryBtn ) ||
             !CreateAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary", "Secondary Button", &g_in.secondaryBtn ) ||
             !CreateAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu Button", &g_in.menuBtn ) )
        {
                snprintf( errBuf, errLen, "creating actions failed" );
                EzVrInputShutdown();
                return false;
        }

        struct Bind { XrAction action; const char *path; };
        const Bind binds[] = {
                { g_in.gripPose, "/user/hand/left/input/grip/pose" },
                { g_in.gripPose, "/user/hand/right/input/grip/pose" },
                { g_in.aimPose, "/user/hand/left/input/aim/pose" },
                { g_in.aimPose, "/user/hand/right/input/aim/pose" },
                { g_in.trigger, "/user/hand/left/input/trigger/value" },
                { g_in.trigger, "/user/hand/right/input/trigger/value" },
                { g_in.grip, "/user/hand/left/input/squeeze/value" },
                { g_in.grip, "/user/hand/right/input/squeeze/value" },
                { g_in.stick, "/user/hand/left/input/thumbstick" },
                { g_in.stick, "/user/hand/right/input/thumbstick" },
                { g_in.stickClick, "/user/hand/left/input/thumbstick/click" },
                { g_in.stickClick, "/user/hand/right/input/thumbstick/click" },
                { g_in.primaryBtn, "/user/hand/left/input/x/click" },
                { g_in.primaryBtn, "/user/hand/right/input/a/click" },
                { g_in.secondaryBtn, "/user/hand/left/input/y/click" },
                { g_in.secondaryBtn, "/user/hand/right/input/b/click" },
                { g_in.menuBtn, "/user/hand/left/input/menu/click" },
        };

        XrActionSuggestedBinding suggested[32];
        uint32_t nSuggested = 0;
        for ( uint32_t i = 0; i < sizeof( binds ) / sizeof( binds[0] ); i++ )
        {
                XrPath path = MakePath( binds[i].path );
                if ( path == XR_NULL_PATH ) continue;
                suggested[nSuggested].action = binds[i].action;
                suggested[nSuggested].binding = path;
                nSuggested++;
        }

        XrInteractionProfileSuggestedBinding profile = {};
        profile.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        profile.interactionProfile = MakePath( "/interaction_profiles/oculus/touch_controller" );
        profile.countSuggestedBindings = nSuggested;
        profile.suggestedBindings = suggested;
        XrResult bindResult = xrSuggestInteractionProfileBindings( instance, &profile );
        if ( XR_FAILED( bindResult ) )
        {
                EZERR( "xrSuggestInteractionProfileBindings: %d", (int)bindResult );
                snprintf( errBuf, errLen, "suggest bindings failed (%d)", (int)bindResult );
                EzVrInputShutdown();
                return false;
        }

        for ( int h = 0; h < EZ_VR_HAND_COUNT; h++ )
        {
                XrActionSpaceCreateInfo spaceInfo = {};
                spaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
                spaceInfo.poseInActionSpace.orientation.w = 1.f;
                spaceInfo.subactionPath = g_in.handPath[h];
                spaceInfo.action = g_in.gripPose;
                if ( XR_FAILED( xrCreateActionSpace( session, &spaceInfo, &g_in.gripSpace[h] ) ) )
                        g_in.gripSpace[h] = XR_NULL_HANDLE;
                spaceInfo.action = g_in.aimPose;
                if ( XR_FAILED( xrCreateActionSpace( session, &spaceInfo, &g_in.aimSpace[h] ) ) )
                        g_in.aimSpace[h] = XR_NULL_HANDLE;
        }

        XrSessionActionSetsAttachInfo attach = {};
        attach.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        attach.countActionSets = 1;
        attach.actionSets = &g_in.actionSet;
        if ( XR_FAILED( xrAttachSessionActionSets( session, &attach ) ) )
        {
                snprintf( errBuf, errLen, "xrAttachSessionActionSets failed" );
                EzVrInputShutdown();
                return false;
        }

        g_in.state.ready = 1;
        EZLOG( "action set attached (%u suggested bindings)", (unsigned)nSuggested );
        return true;
}

void EzVrInputShutdown( void )
{
        for ( int h = 0; h < EZ_VR_HAND_COUNT; h++ )
        {
                if ( g_in.gripSpace[h] != XR_NULL_HANDLE )
                        xrDestroySpace( g_in.gripSpace[h] );
                if ( g_in.aimSpace[h] != XR_NULL_HANDLE )
                        xrDestroySpace( g_in.aimSpace[h] );
                g_in.gripSpace[h] = XR_NULL_HANDLE;
                g_in.aimSpace[h] = XR_NULL_HANDLE;
        }
        if ( g_in.actionSet != XR_NULL_HANDLE )
        {
                xrDestroyActionSet( g_in.actionSet );
                g_in.actionSet = XR_NULL_HANDLE;
        }
        memset( &g_in, 0, sizeof g_in );
}

static float ReadFloat( XrAction action, XrPath sub )
{
        XrActionStateGetInfo info = {};
        info.type = XR_TYPE_ACTION_STATE_GET_INFO;
        info.action = action;
        info.subactionPath = sub;
        XrActionStateFloat state = {};
        state.type = XR_TYPE_ACTION_STATE_FLOAT;
        if ( XR_FAILED( xrGetActionStateFloat( g_in.session, &info, &state ) ) || !state.isActive )
                return 0.f;
        return state.currentState;
}

static int ReadBool( XrAction action, XrPath sub )
{
        XrActionStateGetInfo info = {};
        info.type = XR_TYPE_ACTION_STATE_GET_INFO;
        info.action = action;
        info.subactionPath = sub;
        XrActionStateBoolean state = {};
        state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
        if ( XR_FAILED( xrGetActionStateBoolean( g_in.session, &info, &state ) ) || !state.isActive )
                return 0;
        return state.currentState ? 1 : 0;
}

static void ReadVec2( XrAction action, XrPath sub, float *x, float *y )
{
        *x = 0.f; *y = 0.f;
        XrActionStateGetInfo info = {};
        info.type = XR_TYPE_ACTION_STATE_GET_INFO;
        info.action = action;
        info.subactionPath = sub;
        XrActionStateVector2f state = {};
        state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
        if ( XR_FAILED( xrGetActionStateVector2f( g_in.session, &info, &state ) ) || !state.isActive )
                return;
        *x = state.currentState.x;
        *y = state.currentState.y;
}

void EzVrInputSync( XrTime displayTime )
{
        if ( !g_in.state.ready || g_in.session == XR_NULL_HANDLE )
                return;

        XrActiveActionSet active = {};
        active.actionSet = g_in.actionSet;
        active.subactionPath = XR_NULL_PATH;
        XrActionsSyncInfo sync = {};
        sync.type = XR_TYPE_ACTIONS_SYNC_INFO;
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        if ( XR_FAILED( xrSyncActions( g_in.session, &sync ) ) )
                return;

        for ( int h = 0; h < EZ_VR_HAND_COUNT; h++ )
        {
                EzVrController &c = g_in.state.hands[h];
                memset( &c, 0, sizeof c );
                c.gripPose.orientation.w = 1.f;
                c.aimPose.orientation.w = 1.f;
                XrPath sub = g_in.handPath[h];
                c.trigger = ReadFloat( g_in.trigger, sub );
                c.grip = ReadFloat( g_in.grip, sub );
                ReadVec2( g_in.stick, sub, &c.stickX, &c.stickY );
                c.triggerClick = ReadBool( g_in.triggerClick, sub ) || ( c.trigger > 0.85f );
                c.gripClick = ReadBool( g_in.gripClick, sub ) || ( c.grip > 0.85f );
                c.stickClick = ReadBool( g_in.stickClick, sub );
                c.primaryButton = ReadBool( g_in.primaryBtn, sub );
                c.secondaryButton = ReadBool( g_in.secondaryBtn, sub );
                c.menuButton = ReadBool( g_in.menuBtn, sub );

                if ( g_in.gripSpace[h] != XR_NULL_HANDLE )
                {
                        XrSpaceLocation loc = {};
                        loc.type = XR_TYPE_SPACE_LOCATION;
                        if ( XR_SUCCEEDED( xrLocateSpace( g_in.gripSpace[h], g_in.localSpace, displayTime, &loc ) ) &&
                             ( loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) &&
                             ( loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) )
                        {
                                c.gripPose = loc.pose;
                                c.active = 1;
                        }
                }
                if ( g_in.aimSpace[h] != XR_NULL_HANDLE )
                {
                        XrSpaceLocation loc = {};
                        loc.type = XR_TYPE_SPACE_LOCATION;
                        if ( XR_SUCCEEDED( xrLocateSpace( g_in.aimSpace[h], g_in.localSpace, displayTime, &loc ) ) &&
                             ( loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) &&
                             ( loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) )
                        {
                                c.aimPose = loc.pose;
                                c.active = 1;
                        }
                }
        }
}

int EzVrInputGetState( struct EzVrInputState *out )
{
        if ( !out ) return 0;
        *out = g_in.state;
        return g_in.state.ready;
}

#endif // ANDROID
