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
 * EZQuest slice D: native OpenXR bootstrap for EngineActivity.
 *
 * vr.h     - shared declarations between vr_main.cpp (ANativeActivity glue
 *            + OpenXR lifecycle + frame loop) and vr_scene.cpp (EGL/GLES
 *            context + the built-in diagnostic scene).
 *
 * Boot contract with the Java side (com.ezquest.engine.EngineActivity):
 *   - NativeActivity loads liblauncher.so (meta-data android.app.lib_name)
 *     and calls ANativeActivity_onCreate, implemented in vr_main.cpp.
 *   - Java polls Java_com_ezquest_engine_EngineActivity_nativeXrStatus():
 *     0 = starting, 1 = presenting (compositor live), 2 = failed.
 *   - On 2 the Java side falls back to the flat SDL engine
 *     (com.valvesoftware.ValveActivity2); the reason is retrievable via
 *     Java_com_ezquest_engine_EngineActivity_nativeXrFailReason().
 *
 * Engine seam (private OpenXR tree): the frame loop owns the XR session and
 * the per-eye swapchain FBOs. The engine plugs in via
 * EZQuestVrSetRenderHook(): once registered, the hook receives per-eye
 * view/projection matrices plus a bound FBO for each eye, every presented
 * frame, instead of the built-in diagnostic scene. The XrSession handle is
 * exposed through EZQuestVrSession() for later action-space work.
 */

#ifndef LAUNCHER_ANDROID_VR_H
#define LAUNCHER_ANDROID_VR_H

#if defined( ANDROID ) || defined( __ANDROID__ )

#include <stdint.h>
#include <stddef.h>

// OpenXR platform selection: must happen before the OpenXR headers.
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1

// OpenXR does not pull in platform headers itself; the GLES/EGL types
// (EGLDisplay, EGLContext, GLuint) must already be declared here.
#include <jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#if defined( __cplusplus )
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Status reported to Java (EngineActivity.nativeXrStatus()).
// ---------------------------------------------------------------------------
enum EzVrStatus_e
{
        EZ_XR_STARTING = 0,             // bootstrap in progress
        EZ_XR_RUNNING = 1,              // session focused and frames presenting
        EZ_XR_FAILED = 2,               // terminal failure; Java falls back to flat
};

// One eye's render target for a frame (seam consumed by the engine hook).
struct EzVrEyeFrame
{
        GLuint colorImage;              // GL texture backing the swapchain image
        GLuint fbo;                             // FBO wrapping colorImage (+ depth RB); bind it to render
        uint32_t width;
        uint32_t height;
        int eyeIndex;                   // 0 = left, 1 = right
        float view[16];                 // world -> eye, column-major
        float viewInv[16];              // eye -> world, column-major
        float proj[16];                 // eye projection, column-major, GL depth [-1,1]
};

// Engine render hook: draw the scene into every eye target. NULL restores
// the built-in diagnostic scene. Called from the VR thread only.
typedef void ( *EzVrRenderFn )( void *userdata, const EzVrEyeFrame *eyes, int eyeCount, double predictedDisplayTime );
void EZQuestVrSetRenderHook( EzVrRenderFn fn, void *userdata );

// Live session handle (XR_NULL_HANDLE until the session exists). The engine
// uses this for action spaces / input later; do not destroy it here.
XrSession EZQuestVrSession( void );

// Head views as last located (views[0] = left, views[1] = right), plus the
// reference space they are relative to. Returns 0 until the first locate.
int EZQuestVrHeadViews( XrView outViews[2], XrSpace *outSpace );

// Status accessors shared with the JNI bindings (vr_main.cpp).
int EzVrStatus( void );
const char *EzVrFailReason( void );

// ---------------------------------------------------------------------------
// GLES/EGL + built-in scene (vr_scene.cpp).
// ---------------------------------------------------------------------------

// Initialized EGL trio handed to xrCreateSession's graphics binding.
struct EzVrEgl
{
        EGLDisplay display;
        EGLConfig config;
        EGLContext context;
        EGLSurface pbuffer;             // 1x1 pbuffer the context stays current on
};

// Create display/config/context (GLES3), kept current on a 1x1 pbuffer.
// Returns false and fills errBuf on failure.
bool EzVrGlesInit( struct EzVrEgl *eglOut, char *errBuf, size_t errLen );
void EzVrGlesShutdown( struct EzVrEgl *egl );

// Wrap every swapchain color image in an FBO + depth renderbuffer and build
// the diagnostic scene. images has imagesPerEye * eyeCount entries.
bool EzVrSceneInit( const GLuint *images, int imagesPerEye, int eyeCount,
                                        uint32_t width, uint32_t height, char *errBuf, size_t errLen );

// Accessors for the wrapped targets (eye = 0/1, imageIndex from
// xrAcquireSwapchainImage). Return 0 when out of range.
GLuint EzVrSceneFboForEye( uint32_t eye, uint32_t imageIndex );
GLuint EzVrSceneImageForEye( uint32_t eye, uint32_t imageIndex );

// Built-in render hook implementation: head-tracked grid ground + horizon.
void EzVrSceneDraw( const EzVrEyeFrame *eyes, int eyeCount, double predictedDisplayTime );

void EzVrSceneShutdown( void );

// Matrix helpers shared by the seam consumers (column-major float[16]).
void EzVrMatProjFromFov( float out[16], const XrFovf &fov, float nearZ, float farZ );
void EzVrMatFromPose( float outView[16], float outViewInv[16], const XrPosef &pose );

#if defined( __cplusplus )
} // extern "C"
#endif

#endif // ANDROID

#endif // LAUNCHER_ANDROID_VR_H
