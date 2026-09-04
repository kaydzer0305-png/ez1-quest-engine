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
 * EZQuest slice D: EGL/GLES context + built-in diagnostic scene.
 *
 * The OpenXR compositor owns the display; we never touch the ANativeWindow.
 * Instead the session's graphics binding carries an EGLDisplay/Config/
 * Context created here (current on a 1x1 pbuffer), and rendering goes into
 * the per-image FBOs wrapping the OpenXR GLES swapchain textures.
 *
 * The built-in scene is a head-tracked infinite grid floor with a horizon
 * gradient: stereo parallax on the grid makes in-headset VR immediately
 * verifiable without the engine. It is replaced wholesale once the engine
 * registers EZQuestVrSetRenderHook().
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>

#include <android/log.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vr.h"

#define EZTAG "EZQuest-VR"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )

// ---------------------------------------------------------------------------
// Small state
// ---------------------------------------------------------------------------

namespace
{

struct EzScene
{
        GLuint program;
        GLint uProj;
        GLint uView;
        GLint uTime;
        GLint uExtent;
        GLint uHeight;
        GLuint vao;
        GLuint vbo;

        int imagesPerEye;
        int eyeCount;
        uint32_t width;
        uint32_t height;
        GLuint *fbos;                   // imagesPerEye * eyeCount
        GLuint *depths;                 // parallel depth renderbuffers
        GLuint *images;                 // parallel color textures (swapchain images)
};

EzScene g_scene;

void EzSetErr( char *errBuf, size_t errLen, const char *fmt, ... )
{
        if ( !errBuf || !errLen )
        {
                return;
        }
        va_list args;
        va_start( args, fmt );
        vsnprintf( errBuf, errLen, fmt, args );
        va_end( args );
}

// ---------------------------------------------------------------------------
// Shaders: infinite grid floor + horizon gradient
// ---------------------------------------------------------------------------

const char *kVertSrc = R"(#version 300 es
precision highp float;

layout(location = 0) in vec2 aPos;      // plane quad, [-1, 1] x [-1, 1]

uniform mat4 uProj;
uniform mat4 uView;
uniform float uExtent;                  // half-size of the ground quad, meters
uniform float uHeight;                  // ground height, meters

out vec3 vWorld;
out float vDist;

void main() {
    vec3 world = vec3(aPos.x * uExtent, uHeight, aPos.y * uExtent);
    vWorld = world;
    vec4 eye = uView * vec4(world, 1.0);
    vDist = length(eye.xyz);
    gl_Position = uProj * eye;
}
)";

const char *kFragSrc = R"(#version 300 es
precision highp float;

in vec3 vWorld;
in float vDist;

uniform float uTime;

out vec4 fragColor;

void main() {
    // Horizon gradient: fog color fades in with distance.
    vec3 fogColor = vec3(0.045, 0.075, 0.11);
    float fog = clamp(vDist / 45.0, 0.0, 1.0);
    fog = fog * fog;

    // Anti-aliased grid on the ground plane (2 m cells, 1 m sub-grid).
    vec2 cell = abs(fract(vWorld.xz * 0.5 - 0.5) - 0.5) / fwidth(vWorld.xz * 0.5);
    float lineMajor = 1.0 - min(min(cell.x, cell.y), 1.0);
    vec2 sub = abs(fract(vWorld.xz - 0.5) - 0.5) / fwidth(vWorld.xz);
    float lineMinor = 1.0 - min(min(sub.x, sub.y), 1.0);

    // Slow luminous pulse so static frames are never mistaken for a 2D image.
    float pulse = 0.75 + 0.25 * sin(uTime * 1.4);

    vec3 lineColor = mix(vec3(0.20, 0.75, 0.55), vec3(0.55, 0.95, 0.80), pulse);
    vec3 baseColor = vec3(0.06, 0.10, 0.13);

    vec3 color = baseColor;
    color = mix(color, lineColor * 0.45, lineMinor * 0.6);
    color = mix(color, lineColor, lineMajor);

    // Warm origin marker so yaw is readable.
    float marker = 1.0 - smoothstep(0.7, 1.1, length(vWorld.xz));
    color = mix(color, vec3(0.95, 0.65, 0.25), marker);

    fragColor = vec4(mix(color, fogColor, fog), 1.0);
}
)";

GLuint EzCompileShader( GLenum type, const char *src, char *errBuf, size_t errLen )
{
        GLuint shader = glCreateShader( type );
        glShaderSource( shader, 1, &src, NULL );
        glCompileShader( shader );
        GLint ok = GL_FALSE;
        glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
        if ( ok != GL_TRUE )
        {
                char log[512] = {};
                glGetShaderInfoLog( shader, sizeof log, NULL, log );
                EzSetErr( errBuf, errLen, "shader compile failed: %s", log );
                glDeleteShader( shader );
                return 0;
        }
        return shader;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// EGL
// ---------------------------------------------------------------------------

bool EzVrGlesInit( struct EzVrEgl *eglOut, char *errBuf, size_t errLen )
{
        memset( eglOut, 0, sizeof( *eglOut ) );

        eglOut->display = eglGetDisplay( EGL_DEFAULT_DISPLAY );
        if ( eglOut->display == EGL_NO_DISPLAY )
        {
                EzSetErr( errBuf, errLen, "eglGetDisplay failed" );
                return false;
        }

        EGLint major = 0, minor = 0;
        if ( !eglInitialize( eglOut->display, &major, &minor ) )
        {
                EzSetErr( errBuf, errLen, "eglInitialize failed (0x%x)", eglGetError() );
                return false;
        }
        EZLOG( "EGL %d.%d", (int )major, (int )minor );

        // Off-screen config: the compositor consumes our FBO renders, the
        // window surface is never used.
        const EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 0,
                EGL_STENCIL_SIZE, 0,
                EGL_NONE,
        };
        EGLConfig configs[8] = {};
        EGLint configCount = 0;
        if ( !eglChooseConfig( eglOut->display, configAttribs, configs, 8, &configCount ) ||
                        configCount < 1 )
        {
                EzSetErr( errBuf, errLen, "eglChooseConfig failed (0x%x)", eglGetError() );
                return false;
        }
        eglOut->config = configs[0];

        if ( !eglBindAPI( EGL_OPENGL_ES_API ) )
        {
                EzSetErr( errBuf, errLen, "eglBindAPI failed (0x%x)", eglGetError() );
                return false;
        }

        // Prefer 3.2, fall back 3.1 -> 3.0 (xrGetOpenGLESGraphicsRequirementsKHR
        // already validated the runtime's minimum).
        static const EGLint minorVersions[] = { 2, 1, 0 };
        EGLContext context = EGL_NO_CONTEXT;
        for ( size_t i = 0; i < sizeof( minorVersions ) / sizeof( minorVersions[0] ); i++ )
        {
                const EGLint ctxAttribs[] = {
                        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
                        EGL_CONTEXT_MINOR_VERSION_KHR, minorVersions[i],
                        EGL_NONE,
                };
                context = eglCreateContext( eglOut->display, eglOut->config, EGL_NO_CONTEXT, ctxAttribs );
                if ( context != EGL_NO_CONTEXT )
                {
                        EZLOG( "GLES 3.%d context", (int )minorVersions[i] );
                        break;
                }
        }
        if ( context == EGL_NO_CONTEXT )
        {
                EzSetErr( errBuf, errLen, "eglCreateContext failed (0x%x)", eglGetError() );
                return false;
        }
        eglOut->context = context;

        const EGLint pbufferAttribs[] = {
                EGL_WIDTH, 1,
                EGL_HEIGHT, 1,
                EGL_NONE,
        };
        eglOut->pbuffer = eglCreatePbufferSurface( eglOut->display, eglOut->config, pbufferAttribs );
        if ( eglOut->pbuffer == EGL_NO_SURFACE )
        {
                EzSetErr( errBuf, errLen, "eglCreatePbufferSurface failed (0x%x)", eglGetError() );
                return false;
        }
        if ( !eglMakeCurrent( eglOut->display, eglOut->pbuffer, eglOut->pbuffer, eglOut->context ) )
        {
                EzSetErr( errBuf, errLen, "eglMakeCurrent failed (0x%x)", eglGetError() );
                return false;
        }
        return true;
}

void EzVrGlesShutdown( struct EzVrEgl *egl )
{
        if ( !egl )
        {
                return;
        }
        if ( egl->display != EGL_NO_DISPLAY )
        {
                eglMakeCurrent( egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
                if ( egl->context != EGL_NO_CONTEXT )
                {
                        eglDestroyContext( egl->display, egl->context );
                        egl->context = EGL_NO_CONTEXT;
                }
                if ( egl->pbuffer != EGL_NO_SURFACE )
                {
                        eglDestroySurface( egl->display, egl->pbuffer );
                        egl->pbuffer = EGL_NO_SURFACE;
                }
                eglTerminate( egl->display );
                egl->display = EGL_NO_DISPLAY;
        }
}

// ---------------------------------------------------------------------------
// Scene build / teardown
// ---------------------------------------------------------------------------

bool EzVrSceneInit( const GLuint *images, int imagesPerEye, int eyeCount,
                                        uint32_t width, uint32_t height, char *errBuf, size_t errLen )
{
        memset( &g_scene, 0, sizeof g_scene );
        g_scene.imagesPerEye = imagesPerEye;
        g_scene.eyeCount = eyeCount;
        g_scene.width = width;
        g_scene.height = height;

        GLuint vert = EzCompileShader( GL_VERTEX_SHADER, kVertSrc, errBuf, errLen );
        if ( !vert )
        {
                return false;
        }
        GLuint frag = EzCompileShader( GL_FRAGMENT_SHADER, kFragSrc, errBuf, errLen );
        if ( !frag )
        {
                glDeleteShader( vert );
                return false;
        }
        g_scene.program = glCreateProgram();
        glAttachShader( g_scene.program, vert );
        glAttachShader( g_scene.program, frag );
        glLinkProgram( g_scene.program );
        glDeleteShader( vert );
        glDeleteShader( frag );

        GLint linked = GL_FALSE;
        glGetProgramiv( g_scene.program, GL_LINK_STATUS, &linked );
        if ( linked != GL_TRUE )
        {
                char log[512] = {};
                glGetProgramInfoLog( g_scene.program, sizeof log, NULL, log );
                EzSetErr( errBuf, errLen, "program link failed: %s", log );
                return false;
        }

        g_scene.uProj = glGetUniformLocation( g_scene.program, "uProj" );
        g_scene.uView = glGetUniformLocation( g_scene.program, "uView" );
        g_scene.uTime = glGetUniformLocation( g_scene.program, "uTime" );
        g_scene.uExtent = glGetUniformLocation( g_scene.program, "uExtent" );
        g_scene.uHeight = glGetUniformLocation( g_scene.program, "uHeight" );

        // Full-screen ground quad; the vertex shader extrudes it onto y=uHeight.
        static const float quad[4][2] = {
                { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f },
        };
        glGenVertexArrays( 1, &g_scene.vao );
        glGenBuffers( 1, &g_scene.vbo );
        glBindVertexArray( g_scene.vao );
        glBindBuffer( GL_ARRAY_BUFFER, g_scene.vbo );
        glBufferData( GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( float ) * 2, NULL );
        glBindVertexArray( 0 );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        // Wrap each swapchain image: FBO + depth renderbuffer.
        const int total = imagesPerEye * eyeCount;
        g_scene.images = (GLuint * )malloc( sizeof( GLuint ) * total );
        g_scene.fbos = (GLuint * )malloc( sizeof( GLuint ) * total );
        g_scene.depths = (GLuint * )malloc( sizeof( GLuint ) * total );
        if ( !g_scene.images || !g_scene.fbos || !g_scene.depths )
        {
                EzSetErr( errBuf, errLen, "out of memory building scene targets" );
                return false;
        }
        memcpy( g_scene.images, images, sizeof( GLuint ) * total );
        glGenFramebuffers( total, g_scene.fbos );
        glGenRenderbuffers( total, g_scene.depths );

        for ( int i = 0; i < total; i++ )
        {
                glBindRenderbuffer( GL_RENDERBUFFER, g_scene.depths[i] );
                glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                (GLsizei )width, (GLsizei )height );
                glBindFramebuffer( GL_FRAMEBUFFER, g_scene.fbos[i] );
                glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                images[i], 0 );
                glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                g_scene.depths[i] );
                GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
                if ( status != GL_FRAMEBUFFER_COMPLETE )
                {
                        EzSetErr( errBuf, errLen, "FBO %d incomplete (0x%x)", i, status );
                        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
                        return false;
                }
        }
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );

        glEnable( GL_DEPTH_TEST );
        glDepthFunc( GL_LEQUAL );
        EZLOG( "scene ready: %d targets, %ux%u", total, (unsigned )width, (unsigned )height );
        return true;
}

void EzVrSceneShutdown( void )
{
        if ( g_scene.fbos && g_scene.depths )
        {
                const int total = g_scene.imagesPerEye * g_scene.eyeCount;
                if ( total > 0 )
                {
                        glDeleteFramebuffers( total, g_scene.fbos );
                        glDeleteRenderbuffers( total, g_scene.depths );
                }
        }
        free( g_scene.fbos );
        free( g_scene.depths );
        free( g_scene.images );
        if ( g_scene.vbo )
        {
                glDeleteBuffers( 1, &g_scene.vbo );
        }
        if ( g_scene.vao )
        {
                glDeleteVertexArrays( 1, &g_scene.vao );
        }
        if ( g_scene.program )
        {
                glDeleteProgram( g_scene.program );
        }
        memset( &g_scene, 0, sizeof g_scene );
}

GLuint EzVrSceneFboForEye( uint32_t eye, uint32_t imageIndex )
{
        if ( (int )eye >= g_scene.eyeCount || (int )imageIndex >= g_scene.imagesPerEye )
        {
                return 0;
        }
        return g_scene.fbos[imageIndex * g_scene.eyeCount + eye];
}

GLuint EzVrSceneImageForEye( uint32_t eye, uint32_t imageIndex )
{
        if ( (int )eye >= g_scene.eyeCount || (int )imageIndex >= g_scene.imagesPerEye )
        {
                return 0;
        }
        return g_scene.images[imageIndex * g_scene.eyeCount + eye];
}

void EzVrSceneDraw( const EzVrEyeFrame *eyes, int eyeCount, double predictedDisplayTime )
{
        glUseProgram( g_scene.program );
        glUniform1f( g_scene.uTime, (float )predictedDisplayTime );

        glBindVertexArray( g_scene.vao );
        glUniform1f( g_scene.uExtent, 60.0f );
        glUniform1f( g_scene.uHeight, 0.0f );
        glUniformMatrix4fv( g_scene.uProj, 1, GL_FALSE, eyes[0].proj );
        glUniformMatrix4fv( g_scene.uView, 1, GL_FALSE, eyes[0].view );

        for ( int i = 0; i < eyeCount && i < 2; i++ )
        {
                glBindFramebuffer( GL_FRAMEBUFFER, eyes[i].fbo );
                glViewport( 0, 0, (GLsizei )eyes[i].width, (GLsizei )eyes[i].height );
                glClearColor( 0.045f, 0.075f, 0.11f, 1.0f );
                glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
                glUniformMatrix4fv( g_scene.uProj, 1, GL_FALSE, eyes[i].proj );
                glUniformMatrix4fv( g_scene.uView, 1, GL_FALSE, eyes[i].view );
                glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
        }
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glBindVertexArray( 0 );
        glUseProgram( 0 );
}

// ---------------------------------------------------------------------------
// Matrix helpers (column-major float[16], GL conventions)
// ---------------------------------------------------------------------------

void EzVrMatProjFromFov( float out[16], const XrFovf &fov, float nearZ, float farZ )
{
        // Asymmetric frustum matching the classic GL depth range [-1, 1]
        // (glFrustum convention), fed by OpenXR tangential half-angles.
        const float n = nearZ;
        const float f = farZ;
        const float l = n * tanf( fov.angleLeft );              // angleLeft < 0
        const float r = n * tanf( fov.angleRight );
        const float b = n * tanf( fov.angleDown );              // angleDown < 0
        const float t = n * tanf( fov.angleUp );

        memset( out, 0, sizeof( float ) * 16 );
        out[0 + 0 * 4] = 2.0f * n / ( r - l );                  // col 0
        out[0 + 1 * 4] = 2.0f * n / ( t - b );                  // col 1
        out[0 + 2 * 4] = ( r + l ) / ( r - l );                 // col 2
        out[1 + 2 * 4] = ( t + b ) / ( t - b );
        out[2 + 2 * 4] = -( f + n ) / ( f - n );
        out[3 + 2 * 4] = -2.0f * f * n / ( f - n );
        out[2 + 3 * 4] = -1.0f;                                                 // col 3
}

void EzVrMatFromPose( float outView[16], float outViewInv[16], const XrPosef &pose )
{
        // Quaternion (x, y, z, w) -> rotation; XrPosef maps view space into the
        // reference space, so viewInv = [R | p] and view = [R^T | -R^T p].
        const float x = pose.orientation.x;
        const float y = pose.orientation.y;
        const float z = pose.orientation.z;
        const float w = pose.orientation.w;

        const float r00 = 1.0f - 2.0f * ( y * y + z * z );
        const float r01 = 2.0f * ( x * y - z * w );
        const float r02 = 2.0f * ( x * z + y * w );
        const float r10 = 2.0f * ( x * y + z * w );
        const float r11 = 1.0f - 2.0f * ( x * x + z * z );
        const float r12 = 2.0f * ( y * z - x * w );
        const float r20 = 2.0f * ( x * z - y * w );
        const float r21 = 2.0f * ( y * z + x * w );
        const float r22 = 1.0f - 2.0f * ( x * x + y * y );

        const float px = pose.position.x;
        const float py = pose.position.y;
        const float pz = pose.position.z;

        // viewInv (eye -> world), column-major.
        memset( outViewInv, 0, sizeof( float ) * 16 );
        outViewInv[0 + 0 * 4] = r00;    outViewInv[1 + 0 * 4] = r10;    outViewInv[2 + 0 * 4] = r20;
        outViewInv[0 + 1 * 4] = r01;    outViewInv[1 + 1 * 4] = r11;    outViewInv[2 + 1 * 4] = r21;
        outViewInv[0 + 2 * 4] = r02;    outViewInv[1 + 2 * 4] = r12;    outViewInv[2 + 2 * 4] = r22;
        outViewInv[0 + 3 * 4] = px;             outViewInv[1 + 3 * 4] = py;             outViewInv[2 + 3 * 4] = pz;
        outViewInv[3 + 3 * 4] = 1.0f;

        // view (world -> eye) = inverse: rotation transposed, position negated.
        const float tx = -( r00 * px + r10 * py + r20 * pz );
        const float ty = -( r01 * px + r11 * py + r21 * pz );
        const float tz = -( r02 * px + r12 * py + r22 * pz );

        memset( outView, 0, sizeof( float ) * 16 );
        outView[0 + 0 * 4] = r00;       outView[1 + 0 * 4] = r01;       outView[2 + 0 * 4] = r02;
        outView[0 + 1 * 4] = r10;       outView[1 + 1 * 4] = r11;       outView[2 + 1 * 4] = r12;
        outView[0 + 2 * 4] = r20;       outView[1 + 2 * 4] = r21;       outView[2 + 2 * 4] = r22;
        outView[0 + 3 * 4] = tx;        outView[1 + 3 * 4] = ty;        outView[2 + 3 * 4] = tz;
        outView[3 + 3 * 4] = 1.0f;
}
