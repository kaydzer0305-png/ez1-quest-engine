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
 * EZQuest slice D: ANativeActivity entry + OpenXR session/swapchain/
 * compositor frame loop, hosting the built-in diagnostic scene until the
 * engine render path plugs in via EZQuestVrSetRenderHook().
 *
 * Boot flow (see vr.h for the Java contract):
 *   EngineActivity (NativeActivity, :engine process)
 *     -> ANativeActivity_onCreate            [this file]
 *     -> worker thread: xrInitializeLoaderKHR -> instance -> GLES session
 *     -> per-frame: wait/begin -> locate views -> render per-eye into
 *        swapchain FBOs -> end frame (projection layer)
 *     -> EZQuestSetEngineUp(1) + EZQuestWriteHeartbeat() from main.cpp
 *        once the compositor presents, so the Java bridge/watchdog see it.
 *
 * Everything here runs inside liblauncher.so (same module as the flat SDL
 * path); the two paths never run in the same process.
 */

#include <android/log.h>
#include <android/native_activity.h>

#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "vr.h"
#include "ezquest_vr_engine.h"
#include "vr_input.h"
#include "ezquest_vr_present.h"

// Engine bridge (launcher/android/main.cpp): heartbeat file + Java
// DeviceBridge "engine up" state. Called once the compositor presents.
extern "C" void EZQuestSetEngineUp( int up );
extern "C" void EZQuestWriteHeartbeat( void );

#define EZTAG "EZQuest-VR"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )
#define EZERR( ... ) __android_log_print( ANDROID_LOG_ERROR, EZTAG, __VA_ARGS__ )

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct EzVrApp
{
        ANativeActivity *activity;
        JavaVM *vm;
        jobject activityRef;                    // global ref of the EngineActivity

        pthread_mutex_t lock;
        pthread_cond_t cond;
        pthread_t thread;
        bool threadStarted;

        volatile bool running;                  // worker keeps going while true
        volatile bool destroyed;                // ANativeActivity_onDestroy seen

        // Status reported to Java.
        volatile int status;                    // EzVrStatus_e
        char failReason[256];

        // OpenXR objects.
        XrInstance instance;
        XrSystemId systemId;
        XrSession session;
        XrSessionState sessionState;
        XrSpace localSpace;
        XrSwapchain colorSwapchain[2];
        uint32_t swapchainLength;
        uint32_t swapchainWidth;
        uint32_t swapchainHeight;
        int64_t swapchainFormat;
        bool sessionBegun;
        bool rendering;                                 // state >= VISIBLE && < STOPPING

        // Views as last located (for EZQuestVrHeadViews).
        XrView views[2];
        bool viewsValid;

        // Function pointers resolved from the instance.
        PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetGlRequirements;
        PFN_xrResultToString pfnResultToString;
        PFN_xrSetAndroidApplicationThreadKHR pfnSetThread;

        struct EzVrEgl egl;
};

static EzVrApp g_app;

// Render hook (engine seam; NULL == built-in diagnostic scene).
static EzVrRenderFn g_renderHook;
static void *g_renderHookUserdata;

// ---------------------------------------------------------------------------
// Failure reporting
// ---------------------------------------------------------------------------

int EzVrStatus( void )
{
        return g_app.status;
}

const char *EzVrFailReason( void )
{
        return g_app.failReason;
}

static void EzSetStatus( int status )
{
        g_app.status = status;
}

static void EzSetFailedFmt( const char *fmt, ... )
{
        va_list args;
        va_start( args, fmt );
        vsnprintf( g_app.failReason, sizeof g_app.failReason, fmt, args );
        va_end( args );
        EZERR( "bootstrap failed: %s", g_app.failReason );
        EzSetStatus( EZ_XR_FAILED );
}

// Convert an XrResult to text (falls back to the numeric code).
static const char *EzXrResultStr( XrResult result )
{
        static char text[XR_MAX_RESULT_STRING_SIZE];
        if ( g_app.pfnResultToString &&
                        XR_SUCCEEDED( g_app.pfnResultToString( g_app.instance, result, text ) ) )
        {
                return text;
        }
        snprintf( text, sizeof text, "XrResult %d", (int )result );
        return text;
}

// Check a stage; on failure sets the fail reason and returns false.
#define EZ_XR( call, stage ) \
        do { \
                XrResult _r = ( call ); \
                if ( XR_FAILED( _r ) ) { \
                        EzSetFailedFmt( "%s: %s", stage, EzXrResultStr( _r ) ); \
                        return false; \
                } \
        } while ( 0 )

// ---------------------------------------------------------------------------
// JNI bindings polled by com.ezquest.engine.EngineActivity
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT jint JNICALL
Java_com_ezquest_engine_EngineActivity_nativeXrStatus( JNIEnv *, jclass )
{
        return (jint )EzVrStatus();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ezquest_engine_EngineActivity_nativeXrFailReason( JNIEnv *env, jclass )
{
        return env->NewStringUTF( g_app.failReason );
}

// ---------------------------------------------------------------------------
// OpenXR bootstrap stages
// ---------------------------------------------------------------------------

static bool EzInitLoader( JNIEnv *env )
{
        PFN_xrInitializeLoaderKHR pfnInitializeLoader = NULL;
        if ( XR_FAILED( xrGetInstanceProcAddr( XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                        reinterpret_cast<PFN_xrVoidFunction *>( &pfnInitializeLoader ) ) ) ||
                        !pfnInitializeLoader )
        {
                EzSetFailedFmt( "xrInitializeLoaderKHR unavailable (loader broken?)" );
                return false;
        }

        XrLoaderInitInfoAndroidKHR loaderInfo = {};
        loaderInfo.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
        loaderInfo.next = NULL;
        loaderInfo.applicationVM = reinterpret_cast<void *>( g_app.vm );
        loaderInfo.applicationContext = env->NewGlobalRef( g_app.activityRef );
        EZ_XR( pfnInitializeLoader(
                        reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>( &loaderInfo ) ),
                        "xrInitializeLoaderKHR" );
        EZLOG( "loader initialized" );
        return true;
}

static bool EzCreateInstance( void )
{
        // Required extensions for a Quest standalone app.
        const char *requiredExts[] = {
                XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        };
        // Optional: thread priorities so the runtime can boost our loop.
        const char *optionalExts[] = {
                XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME,
        };

        // What does the loader actually offer?
        uint32_t extCount = 0;
        EZ_XR( xrEnumerateInstanceExtensionProperties( NULL, 0, &extCount, NULL ),
                        "xrEnumerateInstanceExtensionProperties(size)" );
        XrExtensionProperties *props =
                (XrExtensionProperties * )malloc( sizeof( XrExtensionProperties ) * extCount );
        if ( !props )
        {
                EzSetFailedFmt( "out of memory enumerating instance extensions" );
                return false;
        }
        for ( uint32_t i = 0; i < extCount; i++ )
        {
                props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
                props[i].next = NULL;
        }
        EZ_XR( xrEnumerateInstanceExtensionProperties( NULL, extCount, &extCount, props ),
                        "xrEnumerateInstanceExtensionProperties" );

        const char *enabled[8];
        uint32_t enabledCount = 0;
        for ( uint32_t r = 0; r < sizeof( requiredExts ) / sizeof( requiredExts[0] ); r++ )
        {
                bool found = false;
                for ( uint32_t i = 0; i < extCount && !found; i++ )
                        found = strcmp( props[i].extensionName, requiredExts[r] ) == 0;
                if ( !found )
                {
                        EzSetFailedFmt( "required instance extension missing: %s", requiredExts[r] );
                        free( props );
                        return false;
                }
                enabled[enabledCount++] = requiredExts[r];
        }
        for ( uint32_t o = 0; o < sizeof( optionalExts ) / sizeof( optionalExts[0] ); o++ )
        {
                for ( uint32_t i = 0; i < extCount; i++ )
                {
                        if ( strcmp( props[i].extensionName, optionalExts[o] ) == 0 &&
                                        enabledCount < sizeof( enabled ) / sizeof( enabled[0] ) )
                        {
                                enabled[enabledCount++] = optionalExts[o];
                                break;
                        }
                }
        }
        free( props );

        XrInstanceCreateInfoAndroidKHR androidInfo = {};
        androidInfo.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
        androidInfo.applicationVM = reinterpret_cast<void *>( g_app.vm );
        androidInfo.applicationActivity = g_app.activityRef;

        XrInstanceCreateInfo info = {};
        info.type = XR_TYPE_INSTANCE_CREATE_INFO;
        info.next = &androidInfo;
        info.createFlags = 0;
        snprintf( info.applicationInfo.applicationName,
                        sizeof info.applicationInfo.applicationName, "Source Quest" );
        info.applicationInfo.applicationVersion = 1;
        snprintf( info.applicationInfo.engineName, sizeof info.applicationInfo.engineName,
                        "Source" );
        info.applicationInfo.engineVersion = 1;
        info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        info.enabledExtensionCount = enabledCount;
        info.enabledExtensionNames = enabled;
        info.enabledApiLayerCount = 0;
        info.enabledApiLayerNames = NULL;

        EZ_XR( xrCreateInstance( &info, &g_app.instance ), "xrCreateInstance" );
        EZLOG( "instance created with %u extensions", (unsigned )enabledCount );

        // Resolve the function pointers we need from here on.
        EZ_XR( xrGetInstanceProcAddr( g_app.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                        reinterpret_cast<PFN_xrVoidFunction *>( &g_app.pfnGetGlRequirements ) ),
                        "xrGetOpenGLESGraphicsRequirementsKHR lookup" );
        EZ_XR( xrGetInstanceProcAddr( g_app.instance, "xrResultToString",
                        reinterpret_cast<PFN_xrVoidFunction *>( &g_app.pfnResultToString ) ),
                        "xrResultToString lookup" );
        // Optional: absent when the runtime lacks the thread-settings extension.
        if ( XR_FAILED( xrGetInstanceProcAddr( g_app.instance, "xrSetAndroidApplicationThreadKHR",
                        reinterpret_cast<PFN_xrVoidFunction *>( &g_app.pfnSetThread ) ) ) )
        {
                g_app.pfnSetThread = NULL;
        }
        return true;
}

static bool EzGetSystem( void )
{
        XrSystemGetInfo sysInfo = {};
        sysInfo.type = XR_TYPE_SYSTEM_GET_INFO;
        sysInfo.next = NULL;
        sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        EZ_XR( xrGetSystem( g_app.instance, &sysInfo, &g_app.systemId ),
                        "xrGetSystem(HEAD_MOUNTED_DISPLAY): no HMD runtime" );
        EZLOG( "system id %llu", (unsigned long long )g_app.systemId );
        return true;
}

// Resolution override: EZQUEST_XR_RES_SCALE (0.25..2.0, default 1.0).
static float EzResolutionScale( void )
{
        const char *s = getenv( "EZQUEST_XR_RES_SCALE" );
        float scale = 1.0f;
        if ( s && s[0] )
        {
                scale = (float )atof( s );
        }
        if ( scale < 0.25f ) scale = 0.25f;
        if ( scale > 2.0f ) scale = 2.0f;
        return scale;
}

static bool EzCreateSession( void )
{
        // The GLES requirements also validate our context version against the
        // runtime's expectations.
        XrGraphicsRequirementsOpenGLESKHR glReqs = {};
        glReqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
        EZ_XR( g_app.pfnGetGlRequirements( g_app.instance, g_app.systemId, &glReqs ),
                        "xrGetOpenGLESGraphicsRequirementsKHR" );
        EZLOG( "GLES requirements: min %u.%u max %u.%u",
                        XR_VERSION_MAJOR( glReqs.minApiVersionSupported ),
                        XR_VERSION_MINOR( glReqs.minApiVersionSupported ),
                        XR_VERSION_MAJOR( glReqs.maxApiVersionSupported ),
                        XR_VERSION_MINOR( glReqs.maxApiVersionSupported ) );

        char err[256];
        if ( !EzVrGlesInit( &g_app.egl, err, sizeof err ) )
        {
                EzSetFailedFmt( "EGL/GLES init: %s", err );
                return false;
        }

        XrGraphicsBindingOpenGLESAndroidKHR binding = {};
        binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
        binding.next = NULL;
        binding.display = g_app.egl.display;
        binding.config = g_app.egl.config;
        binding.context = g_app.egl.context;

        XrSessionCreateInfo sessionInfo = {};
        sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
        sessionInfo.next = &binding;
        sessionInfo.createFlags = 0;
        sessionInfo.systemId = g_app.systemId;
        EZ_XR( xrCreateSession( g_app.instance, &sessionInfo, &g_app.session ),
                        "xrCreateSession" );
        g_app.sessionState = XR_SESSION_STATE_UNKNOWN;

        // Boost the XR threads (best-effort; the extension may be absent).
        if ( g_app.pfnSetThread )
        {
                pid_t tid = (pid_t )syscall( SYS_gettid );
                g_app.pfnSetThread( g_app.session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, tid );
                g_app.pfnSetThread( g_app.session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, tid );
        }
        return true;
}

static bool EzCreateSpaces( void )
{
        XrReferenceSpaceCreateInfo spaceInfo = {};
        spaceInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
        spaceInfo.next = NULL;
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        spaceInfo.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        spaceInfo.poseInReferenceSpace.position = { 0.0f, 0.0f, 0.0f };
        EZ_XR( xrCreateReferenceSpace( g_app.session, &spaceInfo, &g_app.localSpace ),
                        "xrCreateReferenceSpace(LOCAL)" );
        return true;
}

static bool EzCreateSwapchains( void )
{
        // Primary stereo is mandatory on any HMD system.
        uint32_t viewCount = 0;
        XrViewConfigurationView viewCfg[2] = {};
        for ( uint32_t i = 0; i < 2; i++ )
        {
                viewCfg[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                viewCfg[i].next = NULL;
        }
        EZ_XR( xrEnumerateViewConfigurationViews( g_app.instance, g_app.systemId,
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, viewCfg ),
                        "xrEnumerateViewConfigurationViews" );
        if ( viewCount != 2 )
        {
                EzSetFailedFmt( "expected 2 primary stereo views, got %u", (unsigned )viewCount );
                return false;
        }

        // Pick a swapchain format: prefer sRGB (GL handles linear->sRGB on
        // write), fall back to plain RGBA8.
        uint32_t formatCount = 0;
        EZ_XR( xrEnumerateSwapchainFormats( g_app.session, 0, &formatCount, NULL ),
                        "xrEnumerateSwapchainFormats(size)" );
        int64_t *formats = (int64_t * )malloc( sizeof( int64_t ) * formatCount );
        if ( !formats )
        {
                EzSetFailedFmt( "out of memory enumerating swapchain formats" );
                return false;
        }
        EZ_XR( xrEnumerateSwapchainFormats( g_app.session, formatCount, &formatCount, formats ),
                        "xrEnumerateSwapchainFormats" );
        static const int64_t GL_SRGB8_ALPHA8 = 0x8C43;
        static const int64_t GL_RGBA8 = 0x8058;
        g_app.swapchainFormat = formats[0];
        for ( uint32_t i = 0; i < formatCount; i++ )
        {
                if ( formats[i] == GL_SRGB8_ALPHA8 )
                {
                        g_app.swapchainFormat = GL_SRGB8_ALPHA8;
                        break;
                }
        }
        if ( g_app.swapchainFormat != GL_SRGB8_ALPHA8 )
        {
                for ( uint32_t i = 0; i < formatCount; i++ )
                {
                        if ( formats[i] == GL_RGBA8 )
                        {
                                g_app.swapchainFormat = GL_RGBA8;
                                break;
                        }
                }
        }
        free( formats );
        EZLOG( "swapchain format 0x%llx", (unsigned long long )g_app.swapchainFormat );

        float scale = EzResolutionScale();
        g_app.swapchainWidth = (uint32_t )( viewCfg[0].recommendedImageRectWidth * scale );
        g_app.swapchainHeight = (uint32_t )( viewCfg[0].recommendedImageRectHeight * scale );
        if ( g_app.swapchainWidth > 4096 ) g_app.swapchainWidth = 4096;
        if ( g_app.swapchainHeight > 4096 ) g_app.swapchainHeight = 4096;
        EZLOG( "swapchain %ux%u (recommended %ux%u, scale %.2f)",
                        (unsigned )g_app.swapchainWidth, (unsigned )g_app.swapchainHeight,
                        (unsigned )viewCfg[0].recommendedImageRectWidth,
                        (unsigned )viewCfg[0].recommendedImageRectHeight, scale );

        for ( uint32_t eye = 0; eye < 2; eye++ )
        {
                XrSwapchainCreateInfo scInfo = {};
                scInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
                scInfo.next = NULL;
                scInfo.createFlags = 0;
                scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
                scInfo.format = g_app.swapchainFormat;
                scInfo.sampleCount = 1;
                scInfo.width = g_app.swapchainWidth;
                scInfo.height = g_app.swapchainHeight;
                scInfo.faceCount = 1;
                scInfo.arraySize = 1;
                scInfo.mipCount = 1;
                EZ_XR( xrCreateSwapchain( g_app.session, &scInfo, &g_app.colorSwapchain[eye] ),
                                "xrCreateSwapchain(eye)" );

                uint32_t length = 0;
                EZ_XR( xrEnumerateSwapchainImages( g_app.colorSwapchain[eye], 0, &length, NULL ),
                                "xrEnumerateSwapchainImages(size)" );
                if ( eye == 0 )
                {
                        g_app.swapchainLength = length;
                }
                else if ( length != g_app.swapchainLength )
                {
                        EzSetFailedFmt( "swapchain image count mismatch (%u vs %u)",
                                        (unsigned )g_app.swapchainLength, (unsigned )length );
                        return false;
                }
        }
        return true;
}

// ---------------------------------------------------------------------------
// Event pump
// ---------------------------------------------------------------------------

static void EzHandleSessionState( XrSessionState newState )
{
        EZLOG( "session state: %d -> %d", (int )g_app.sessionState, (int )newState );
        g_app.sessionState = newState;

        switch ( newState )
        {
        case XR_SESSION_STATE_READY:
        {
                XrSessionBeginInfo beginInfo = {};
                beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
                beginInfo.next = NULL;
                beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if ( XR_FAILED( xrBeginSession( g_app.session, &beginInfo ) ) )
                {
                        EzSetFailedFmt( "xrBeginSession failed" );
                        g_app.running = false;
                        return;
                }
                g_app.sessionBegun = true;
                EZLOG( "session begun (primary stereo)" );
                break;
        }
        case XR_SESSION_STATE_VISIBLE:
        case XR_SESSION_STATE_FOCUSED:
                g_app.rendering = true;
                break;
        case XR_SESSION_STATE_SYNCHRONIZED:
                g_app.rendering = false;
                break;
        case XR_SESSION_STATE_STOPPING:
                g_app.rendering = false;
                if ( g_app.sessionBegun )
                {
                        xrEndSession( g_app.session );
                        g_app.sessionBegun = false;
                }
                break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
                g_app.rendering = false;
                g_app.running = false;
                break;
        default:
                break;
        }
}

static void EzPollEvents( void )
{
        XrEventDataBuffer event = {};
        for ( ;; )
        {
                event.type = XR_TYPE_EVENT_DATA_BUFFER;
                event.next = NULL;
                XrResult r = xrPollEvent( g_app.instance, &event );
                if ( r != XR_SUCCESS )
                        break;

                switch ( event.type )
                {
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
                {
                        const XrEventDataSessionStateChanged *changed =
                                reinterpret_cast<const XrEventDataSessionStateChanged *>( &event );
                        if ( changed->session == g_app.session )
                        {
                                EzHandleSessionState( changed->state );
                        }
                        break;
                }
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                        EZERR( "instance loss pending" );
                        g_app.running = false;
                        return;
                case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                        EZLOG( "reference space change pending" );
                        break;
                default:
                        EZLOG( "unhandled event type %d", (int )event.type );
                        break;
                }
        }
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

static bool EzRenderFrame( const XrFrameState &frameState )
{
        // Locate both views in the LOCAL reference space.
        XrViewLocateInfo locateInfo = {};
        locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
        locateInfo.next = NULL;
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = g_app.localSpace;

        for ( uint32_t i = 0; i < 2; i++ )
        {
                g_app.views[i].type = XR_TYPE_VIEW;
                g_app.views[i].next = NULL;
        }
        uint32_t locatedCount = 0;
        XrViewState viewState = {};
        viewState.type = XR_TYPE_VIEW_STATE;
        viewState.next = NULL;
        EZ_XR( xrLocateViews( g_app.session, &locateInfo, &viewState, 2, &locatedCount, g_app.views ),
                        "xrLocateViews" );
        g_app.viewsValid = ( locatedCount == 2 );

        // Build per-eye targets.
        EzVrEyeFrame eyes[2];
        for ( uint32_t eye = 0; eye < 2; eye++ )
        {
                EzVrEyeFrame &target = eyes[eye];
                memset( &target, 0, sizeof target );

                uint32_t imageIndex = 0;
                EZ_XR( xrAcquireSwapchainImage( g_app.colorSwapchain[eye], NULL, &imageIndex ),
                                "xrAcquireSwapchainImage" );
                XrSwapchainImageWaitInfo waitInfo = {};
                waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
                waitInfo.next = NULL;
                waitInfo.timeout = 1000000000; // 1s; compositor should never stall us
                EZ_XR( xrWaitSwapchainImage( g_app.colorSwapchain[eye], &waitInfo ),
                                "xrWaitSwapchainImage" );

                target.width = g_app.swapchainWidth;
                target.height = g_app.swapchainHeight;
                target.eyeIndex = (int )eye;
                EzVrMatFromPose( target.view, target.viewInv, g_app.views[eye].pose );
                EzVrMatProjFromFov( target.proj, g_app.views[eye].fov, 0.05f, 500.0f );

                // The scene hands back the FBO/texture wrapped for this eye/image.
                target.fbo = EzVrSceneFboForEye( eye, imageIndex );
                target.colorImage = EzVrSceneImageForEye( eye, imageIndex );
        }

        // Render through the seam (engine) or the built-in scene.
        if ( g_renderHook )
        {
                g_renderHook( g_renderHookUserdata, eyes, 2, frameState.predictedDisplayTime );
        }
        else
        {
                EzVrSceneDraw( eyes, 2, frameState.predictedDisplayTime );
        }

        for ( uint32_t eye = 0; eye < 2; eye++ )
        {
                EZ_XR( xrReleaseSwapchainImage( g_app.colorSwapchain[eye], NULL ),
                                "xrReleaseSwapchainImage" );
        }

        // Submit the projection layer.
        XrCompositionLayerProjectionView layerViews[2];
        for ( uint32_t eye = 0; eye < 2; eye++ )
        {
                layerViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                layerViews[eye].next = NULL;
                layerViews[eye].pose = g_app.views[eye].pose;
                layerViews[eye].fov = g_app.views[eye].fov;
                layerViews[eye].subImage.swapchain = g_app.colorSwapchain[eye];
                layerViews[eye].subImage.imageRect.offset.x = 0;
                layerViews[eye].subImage.imageRect.offset.y = 0;
                layerViews[eye].subImage.imageRect.extent.width = (int32_t )g_app.swapchainWidth;
                layerViews[eye].subImage.imageRect.extent.height = (int32_t )g_app.swapchainHeight;
                layerViews[eye].subImage.imageArrayIndex = 0;
        }

        XrCompositionLayerProjection projection = {};
        projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        projection.next = NULL;
        projection.layerFlags = 0;
        projection.space = g_app.localSpace;
        projection.viewCount = 2;
        projection.views = layerViews;

        const XrCompositionLayerBaseHeader *layers[1] = {
                reinterpret_cast<const XrCompositionLayerBaseHeader *>( &projection )
        };

        XrFrameEndInfo endInfo = {};
        endInfo.type = XR_TYPE_FRAME_END_INFO;
        endInfo.next = NULL;
        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = 1;
        endInfo.layers = layers;
        EZ_XR( xrEndFrame( g_app.session, &endInfo ), "xrEndFrame" );
        return true;
}

static void EzTeardown( void )
{
        EZQuestVrPresentShutdown();
        EzVrInputShutdown();
        EzVrSceneShutdown();

        for ( uint32_t eye = 0; eye < 2; eye++ )
        {
                if ( g_app.colorSwapchain[eye] != XR_NULL_HANDLE )
                {
                        xrDestroySwapchain( g_app.colorSwapchain[eye] );
                        g_app.colorSwapchain[eye] = XR_NULL_HANDLE;
                }
        }
        if ( g_app.localSpace != XR_NULL_HANDLE )
        {
                xrDestroySpace( g_app.localSpace );
                g_app.localSpace = XR_NULL_HANDLE;
        }
        if ( g_app.session != XR_NULL_HANDLE )
        {
                if ( g_app.sessionBegun )
                {
                        // Destroying a running session is legal but noisy; end it first.
                        xrEndSession( g_app.session );
                        g_app.sessionBegun = false;
                }
                xrDestroySession( g_app.session );
                g_app.session = XR_NULL_HANDLE;
        }
        EzVrGlesShutdown( &g_app.egl );
        if ( g_app.instance != XR_NULL_HANDLE )
        {
                xrDestroyInstance( g_app.instance );
                g_app.instance = XR_NULL_HANDLE;
        }
        EZLOG( "xr resources released" );
}

static void EzVrRun( JNIEnv *env )
{
        if ( !EzInitLoader( env ) || !EzCreateInstance() || !EzGetSystem() ||
                        !EzCreateSession() || !EzCreateSpaces() || !EzCreateSwapchains() )
        {
                // EzSetFailedFmt already recorded the reason + status.
                EzTeardown();
                return;
        }

        {
                char ierr[256];
                if ( !EzVrInputInit( g_app.instance, g_app.session, g_app.localSpace,
                                ierr, sizeof ierr ) )
                {
                        EZLOG( "input init skipped: %s", ierr );
                }
        }

        EZQuestVrPresentInit( g_app.swapchainWidth, g_app.swapchainHeight );

        // Wrap swapchain images in FBOs and build the diagnostic scene.
        GLuint *images = (GLuint * )malloc( sizeof( GLuint ) * 2 * g_app.swapchainLength );
        if ( !images )
        {
                EzSetFailedFmt( "out of memory for swapchain images" );
                EzTeardown();
                return;
        }
        uint32_t perEye = g_app.swapchainLength;
        for ( uint32_t eye = 0; eye < 2; eye++ )
        {
                XrSwapchainImageOpenGLESKHR *scImages =
                        (XrSwapchainImageOpenGLESKHR * )malloc( sizeof( XrSwapchainImageOpenGLESKHR ) * perEye );
                if ( !scImages )
                {
                        free( images );
                        EzSetFailedFmt( "out of memory for swapchain image handles" );
                        EzTeardown();
                        return;
                }
                for ( uint32_t i = 0; i < perEye; i++ )
                {
                        scImages[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
                        scImages[i].next = NULL;
                }
                uint32_t got = 0;
                if ( XR_FAILED( xrEnumerateSwapchainImages( g_app.colorSwapchain[eye], perEye, &got,
                                reinterpret_cast<XrSwapchainImageBaseHeader *>( scImages ) ) ) || got != perEye )
                {
                        free( scImages );
                        free( images );
                        EzSetFailedFmt( "xrEnumerateSwapchainImages failed (eye %u)", (unsigned )eye );
                        EzTeardown();
                        return;
                }
                for ( uint32_t i = 0; i < perEye; i++ )
                {
                        images[eye * perEye + i] = (GLuint )scImages[i].image;
                }
                free( scImages );
        }

        char err[256];
        if ( !EzVrSceneInit( images, perEye, 2, g_app.swapchainWidth, g_app.swapchainHeight,
                        err, sizeof err ) )
        {
                free( images );
                EzSetFailedFmt( "scene init: %s", err );
                EzTeardown();
                return;
        }
        free( images );

        EZLOG( "entering frame loop" );
        uint64_t frameIndex = 0;
        while ( g_app.running )
        {
                EzPollEvents();
                if ( !g_app.running )
                        break;

                if ( !g_app.sessionBegun || !g_app.rendering )
                {
                        // Idle until the runtime focuses us; keep the thread responsive.
                        struct timespec ts = { 0, 5 * 1000 * 1000 };
                        nanosleep( &ts, NULL );
                        continue;
                }

                XrFrameState frameState = {};
                frameState.type = XR_TYPE_FRAME_STATE;
                frameState.next = NULL;
                XrFrameWaitInfo waitInfo = {};
                waitInfo.type = XR_TYPE_FRAME_WAIT_INFO;
                waitInfo.next = NULL;
                if ( XR_FAILED( xrWaitFrame( g_app.session, &waitInfo, &frameState ) ) )
                {
                        EzSetFailedFmt( "xrWaitFrame failed" );
                        break;
                }
                XrFrameBeginInfo beginInfo = {};
                beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
                beginInfo.next = NULL;
                if ( XR_FAILED( xrBeginFrame( g_app.session, &beginInfo ) ) )
                {
                        EzSetFailedFmt( "xrBeginFrame failed" );
                        break;
                }

                EzVrInputSync( frameState.predictedDisplayTime );

                if ( frameState.shouldRender )
                {
                        if ( !EzRenderFrame( frameState ) )
                        {
                                // Fail reason recorded; bail out of the loop.
                                break;
                        }
                        frameIndex++;
                        if ( frameIndex == 1 )
                        {
                                EZLOG( "first frame presented; reporting engine up" );
                                EZQuestSetEngineUp( 1 );
                                EzSetStatus( EZ_XR_RUNNING );
                                EZQuestVrOnFirstPresent();
                        }
                        if ( ( frameIndex & 7 ) == 0 )
                        {
                                EZQuestWriteHeartbeat();
                        }
                }
                else
                {
                        // End the frame empty so the runtime keeps pacing us.
                        XrFrameEndInfo endInfo = {};
                        endInfo.type = XR_TYPE_FRAME_END_INFO;
                        endInfo.displayTime = frameState.predictedDisplayTime;
                        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
                        endInfo.layerCount = 0;
                        endInfo.layers = NULL;
                        if ( XR_FAILED( xrEndFrame( g_app.session, &endInfo ) ) )
                        {
                                EzSetFailedFmt( "xrEndFrame(empty) failed" );
                                break;
                        }
                }
        }

        EZLOG( "frame loop done after %llu frames", (unsigned long long )frameIndex );
        if ( EzVrStatus() == EZ_XR_RUNNING )
        {
                EZQuestSetEngineUp( 0 );
        }
        if ( EzVrStatus() != EZ_XR_FAILED )
        {
                EzSetStatus( EZ_XR_STARTING ); // stopped cleanly before presenting
        }
        EzTeardown();
}

// ---------------------------------------------------------------------------
// Worker thread + ANativeActivity lifecycle
// ---------------------------------------------------------------------------

static void *EzVrThreadMain( void * )
{
        JNIEnv *env = NULL;
        bool attached = g_app.vm->AttachCurrentThread( &env, NULL ) == JNI_OK;

        EzVrRun( env );

        if ( attached && env )
        {
                g_app.vm->DetachCurrentThread();
        }

        pthread_mutex_lock( &g_app.lock );
        g_app.threadStarted = false;
        pthread_cond_broadcast( &g_app.cond );
        pthread_mutex_unlock( &g_app.lock );

        // Ask Java to finish us (no-op if the activity is already going away).
        if ( !g_app.destroyed && g_app.activity )
        {
                ANativeActivity_finish( g_app.activity );
        }
        return NULL;
}

static void EzOnActivityStart( ANativeActivity * ) { }
static void EzOnActivityResume( ANativeActivity * ) { }
static void EzOnActivityPause( ANativeActivity * ) { }
static void EzOnActivityStop( ANativeActivity * ) { }

static void EzOnActivityDestroy( ANativeActivity *activity )
{
        EzVrApp *app = (EzVrApp * )activity->instance;
        if ( !app )
        {
                return;
        }
        EZLOG( "onDestroy" );
        app->destroyed = true;
        app->running = false;

        // Unstick a blocking xrWaitFrame by requesting the session to exit.
        if ( app->session != XR_NULL_HANDLE && app->sessionBegun )
        {
                xrRequestExitSession( app->session );
        }

        if ( app->threadStarted )
        {
                pthread_mutex_lock( &app->lock );
                while ( app->threadStarted )
                {
                        pthread_cond_wait( &app->cond, &app->lock );
                }
                pthread_mutex_unlock( &app->lock );
        }
        if ( app->activityRef )
        {
                JNIEnv *env = NULL;
                if ( app->vm && app->vm->GetEnv( (void ** )&env, JNI_VERSION_1 ) == JNI_OK && env )
                {
                        env->DeleteGlobalRef( app->activityRef );
                }
                app->activityRef = NULL;
        }
        pthread_mutex_destroy( &app->lock );
        pthread_cond_destroy( &app->cond );
        EZLOG( "onDestroy complete" );
}

extern "C" __attribute__( ( visibility( "default" ) ) ) void
ANativeActivity_onCreate( ANativeActivity *activity, void *, size_t )
{
        memset( &g_app, 0, sizeof g_app );

        g_app.activity = activity;
        g_app.vm = activity->vm;
        g_app.instance = XR_NULL_HANDLE;
        g_app.systemId = XR_NULL_SYSTEM_ID;
        g_app.session = XR_NULL_HANDLE;
        g_app.localSpace = XR_NULL_HANDLE;
        g_app.sessionState = XR_SESSION_STATE_UNKNOWN;
        for ( uint32_t eye = 0; eye < 2; eye++ )
        {
                g_app.colorSwapchain[eye] = XR_NULL_HANDLE;
        }
        g_app.status = EZ_XR_STARTING;
        g_app.running = true;

        pthread_mutex_init( &g_app.lock, NULL );
        pthread_cond_init( &g_app.cond, NULL );

        // The JNIEnv is valid on this (activity) thread right now.
        g_app.activityRef = activity->env->NewGlobalRef( activity->clazz );

        activity->instance = &g_app;
        activity->callbacks->onStart = EzOnActivityStart;
        activity->callbacks->onResume = EzOnActivityResume;
        activity->callbacks->onPause = EzOnActivityPause;
        activity->callbacks->onStop = EzOnActivityStop;
        activity->callbacks->onDestroy = EzOnActivityDestroy;

        EZLOG( "ANativeActivity_onCreate: starting XR worker (EZQuest slice D)" );
        if ( pthread_create( &g_app.thread, NULL, EzVrThreadMain, NULL ) == 0 )
        {
                g_app.threadStarted = true;
        }
        else
        {
                EzSetFailedFmt( "could not start XR worker thread" );
                g_app.running = false;
        }
}

// ---------------------------------------------------------------------------
// Engine seam accessors
// ---------------------------------------------------------------------------

void EZQuestVrSetRenderHook( EzVrRenderFn fn, void *userdata )
{
        g_renderHook = fn;
        g_renderHookUserdata = userdata;
}

XrSession EZQuestVrSession( void )
{
        return g_app.session;
}

int EZQuestVrHeadViews( XrView outViews[2], XrSpace *outSpace )
{
        if ( !g_app.viewsValid )
        {
                return 0;
        }
        outViews[0] = g_app.views[0];
        outViews[1] = g_app.views[1];
        if ( outSpace )
        {
                *outSpace = g_app.localSpace;
        }
        return 1;
}
