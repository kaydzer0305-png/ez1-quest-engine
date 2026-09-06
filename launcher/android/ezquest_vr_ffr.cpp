/*
Copyright (C) 2026 kaydzer0305-png

Quest performance knobs (roadmap item 3):
  - XR_FB_display_refresh_rate -> 90 Hz when available
  - XR_FB_foveation + configuration + swapchain_update_state -> fixed foveation

Disable with EZQUEST_VR_FFR=0. Level via EZQUEST_VR_FFR_LEVEL=
  0 none, 1 low, 2 medium (default), 3 high, 4 high-top.
Refresh via EZQUEST_VR_REFRESH (default 90).
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "ezquest_vr_ffr.h"

#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define EZTAG "EZQuest-VR-FFR"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )
#define EZERR( ... ) __android_log_print( ANDROID_LOG_ERROR, EZTAG, __VA_ARGS__ )

#ifndef XR_FB_FOVEATION_EXTENSION_NAME
#define XR_FB_FOVEATION_EXTENSION_NAME "XR_FB_foveation"
#endif
#ifndef XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME
#define XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME "XR_FB_foveation_configuration"
#endif
#ifndef XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME
#define XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME "XR_FB_swapchain_update_state"
#endif
#ifndef XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME
#define XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME "XR_FB_display_refresh_rate"
#endif

#ifndef XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB
#define XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB ((XrStructureType)1000114000)
#endif
#ifndef XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB
#define XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB ((XrStructureType)1000114003)
#endif
#ifndef XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB
#define XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB ((XrStructureType)1000114001)
#endif
#ifndef XR_FOVEATION_LEVEL_NONE_FB
#define XR_FOVEATION_LEVEL_NONE_FB 0
#define XR_FOVEATION_LEVEL_LOW_FB 1
#define XR_FOVEATION_LEVEL_MEDIUM_FB 2
#define XR_FOVEATION_LEVEL_HIGH_FB 3
#define XR_FOVEATION_LEVEL_HIGH_TOP_FB 4
#endif

typedef struct XrFoveationProfileCreateInfoFB_local {
        XrStructureType type;
        const void *next;
} XrFoveationProfileCreateInfoFB_local;

typedef struct XrFoveationLevelProfileCreateInfoFB_local {
        XrStructureType type;
        const void *next;
        int32_t level;
        float verticalOffset;
        uint32_t dynamic;
} XrFoveationLevelProfileCreateInfoFB_local;

typedef struct XrSwapchainStateFoveationFB_local {
        XrStructureType type;
        const void *next;
        uint64_t flags;
        uint64_t profile;
} XrSwapchainStateFoveationFB_local;

typedef XrResult (XRAPI_PTR *PFN_xrCreateFoveationProfileFB_local)(
        XrSession session, const XrFoveationProfileCreateInfoFB_local *info, uint64_t *profile );
typedef XrResult (XRAPI_PTR *PFN_xrUpdateSwapchainFB_local)(
        XrSwapchain swapchain, const void *state );
typedef XrResult (XRAPI_PTR *PFN_xrRequestDisplayRefreshRateFB_local)(
        XrSession session, float refresh );

static int EnvTruthy( const char *name, int deflt )
{
        const char *v = getenv( name );
        if ( !v || !v[0] )
                return deflt;
        if ( v[0] == '0' && v[1] == '\0' )
                return 0;
        return 1;
}

static int HasExt( const XrExtensionProperties *props, uint32_t count, const char *name )
{
        for ( uint32_t i = 0; i < count; i++ )
        {
                if ( strcmp( props[i].extensionName, name ) == 0 )
                        return 1;
        }
        return 0;
}

void EZQuestVrAppendOptionalFbExts(
        const XrExtensionProperties *props, uint32_t propCount,
        const char **names, uint32_t *inoutCount, uint32_t capacity )
{
        static const char *kWant[] = {
                XR_FB_FOVEATION_EXTENSION_NAME,
                XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
                XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,
                XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
        };
        for ( uint32_t w = 0; w < sizeof( kWant ) / sizeof( kWant[0] ); w++ )
        {
                if ( !HasExt( props, propCount, kWant[w] ) )
                        continue;
                if ( *inoutCount >= capacity )
                        break;
                int already = 0;
                for ( uint32_t i = 0; i < *inoutCount; i++ )
                {
                        if ( strcmp( names[i], kWant[w] ) == 0 )
                        {
                                already = 1;
                                break;
                        }
                }
                if ( already )
                        continue;
                names[(*inoutCount)++] = kWant[w];
                EZLOG( "enabling %s", kWant[w] );
        }
}

int EZQuestVrApplyFfrAndRefresh( XrInstance instance, XrSession session,
        XrSwapchain leftEye, XrSwapchain rightEye )
{
        if ( !instance || !session )
                return 0;

        float wantHz = 90.f;
        const char *rs = getenv( "EZQUEST_VR_REFRESH" );
        if ( rs && rs[0] )
                wantHz = (float)atof( rs );
        if ( wantHz >= 70.f )
        {
                PFN_xrRequestDisplayRefreshRateFB_local pfnRefresh = NULL;
                if ( XR_SUCCEEDED( xrGetInstanceProcAddr( instance, "xrRequestDisplayRefreshRateFB",
                                (PFN_xrVoidFunction *)&pfnRefresh ) ) && pfnRefresh )
                {
                        XrResult rr = pfnRefresh( session, wantHz );
                        if ( XR_SUCCEEDED( rr ) )
                                EZLOG( "requested %.0f Hz", wantHz );
                        else
                                EZLOG( "refresh request %.0f Hz failed (%d)", wantHz, (int)rr );
                }
        }

        if ( !EnvTruthy( "EZQUEST_VR_FFR", 1 ) )
        {
                EZLOG( "FFR disabled (EZQUEST_VR_FFR=0)" );
                return 0;
        }

        int level = XR_FOVEATION_LEVEL_MEDIUM_FB;
        const char *ls = getenv( "EZQUEST_VR_FFR_LEVEL" );
        if ( ls && ls[0] )
        {
                level = atoi( ls );
                if ( level < 0 ) level = 0;
                if ( level > 4 ) level = 4;
        }

        PFN_xrCreateFoveationProfileFB_local pfnCreate = NULL;
        PFN_xrUpdateSwapchainFB_local pfnUpdate = NULL;
        if ( XR_FAILED( xrGetInstanceProcAddr( instance, "xrCreateFoveationProfileFB",
                        (PFN_xrVoidFunction *)&pfnCreate ) ) || !pfnCreate )
        {
                EZLOG( "xrCreateFoveationProfileFB missing — FFR skipped" );
                return 0;
        }
        if ( XR_FAILED( xrGetInstanceProcAddr( instance, "xrUpdateSwapchainFB",
                        (PFN_xrVoidFunction *)&pfnUpdate ) ) || !pfnUpdate )
        {
                EZLOG( "xrUpdateSwapchainFB missing — FFR skipped" );
                return 0;
        }

        XrFoveationLevelProfileCreateInfoFB_local levelInfo = {};
        levelInfo.type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB;
        levelInfo.level = level;
        levelInfo.verticalOffset = 0.f;
        levelInfo.dynamic = 0;

        XrFoveationProfileCreateInfoFB_local profileInfo = {};
        profileInfo.type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB;
        profileInfo.next = &levelInfo;

        uint64_t profile = 0;
        XrResult cr = pfnCreate( session, &profileInfo, &profile );
        if ( XR_FAILED( cr ) || !profile )
        {
                EZERR( "xrCreateFoveationProfileFB failed (%d)", (int)cr );
                return 0;
        }

        XrSwapchain eyes[2] = { leftEye, rightEye };
        int applied = 0;
        for ( int i = 0; i < 2; i++ )
        {
                if ( !eyes[i] )
                        continue;
                XrSwapchainStateFoveationFB_local state = {};
                state.type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB;
                state.profile = profile;
                XrResult ur = pfnUpdate( eyes[i], &state );
                if ( XR_SUCCEEDED( ur ) )
                        applied++;
                else
                        EZERR( "xrUpdateSwapchainFB eye %d failed (%d)", i, (int)ur );
        }
        EZLOG( "FFR level=%d applied to %d/2 eyes", level, applied );
        return applied == 2 ? 1 : 0;
}

#endif
