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
 * EZQuest engine seam scaffolding: stub render hook + gated LauncherMain
 * boot attempt. See ezquest_vr_engine.h.
 */

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "ezquest_vr_engine.h"

#include <android/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <GLES3/gl3.h>

#define EZTAG "EZQuest-VR-Engine"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )
#define EZERR( ... ) __android_log_print( ANDROID_LOG_ERROR, EZTAG, __VA_ARGS__ )
#define EZWAR( ... ) __android_log_print( ANDROID_LOG_WARN, EZTAG, __VA_ARGS__ )

// LauncherMainAndroid lives in main.cpp (same liblauncher.so).
extern "C" int LauncherMainAndroid( int argc, char **argv );

namespace
{

volatile int g_bootRequested = 0;
volatile int g_stubRegistered = 0;
volatile int g_engineThreadStarted = 0;
pthread_t g_engineThread;

static int EnvTruthy( const char *name )
{
        const char *v = getenv( name );
        if ( !v || !v[0] )
                return 0;
        if ( v[0] == '0' && v[1] == '\0' )
                return 0;
        if ( ( v[0] == 'n' || v[0] == 'N' ) && ( v[1] == 'o' || v[1] == 'O' ) && v[2] == '\0' )
                return 0;
        if ( ( v[0] == 'f' || v[0] == 'F' ) && ( v[1] == 'a' || v[1] == 'A' ) )
                return 0;
        return 1;
}

// ---------------------------------------------------------------------------
// Stub GLES: clear + a simple coloured quad in world space using the
// provided view/proj matrices. No external assets; proves the seam.
// ---------------------------------------------------------------------------

static GLuint g_stubProgram = 0;
static GLint g_stubUMvp = -1;
static GLint g_stubUColor = -1;
static GLuint g_stubVao = 0;
static GLuint g_stubVbo = 0;
static int g_stubReady = 0;

static GLuint CompileShader( GLenum type, const char *src )
{
        GLuint s = glCreateShader( type );
        glShaderSource( s, 1, &src, NULL );
        glCompileShader( s );
        GLint ok = 0;
        glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
        if ( !ok )
        {
                char log[512];
                glGetShaderInfoLog( s, sizeof log, NULL, log );
                EZERR( "stub shader compile failed: %s", log );
                glDeleteShader( s );
                return 0;
        }
        return s;
}

static int EnsureStubGl( void )
{
        if ( g_stubReady )
                return 1;

        const char *vs = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";
        const char *fs = R"(#version 300 es
precision mediump float;
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(uColor, 1.0);
}
)";
        GLuint v = CompileShader( GL_VERTEX_SHADER, vs );
        GLuint f = CompileShader( GL_FRAGMENT_SHADER, fs );
        if ( !v || !f )
                return 0;

        g_stubProgram = glCreateProgram();
        glAttachShader( g_stubProgram, v );
        glAttachShader( g_stubProgram, f );
        glLinkProgram( g_stubProgram );
        glDeleteShader( v );
        glDeleteShader( f );
        GLint linked = 0;
        glGetProgramiv( g_stubProgram, GL_LINK_STATUS, &linked );
        if ( !linked )
        {
                char log[512];
                glGetProgramInfoLog( g_stubProgram, sizeof log, NULL, log );
                EZERR( "stub program link failed: %s", log );
                glDeleteProgram( g_stubProgram );
                g_stubProgram = 0;
                return 0;
        }
        g_stubUMvp = glGetUniformLocation( g_stubProgram, "uMvp" );
        g_stubUColor = glGetUniformLocation( g_stubProgram, "uColor" );

        // Unit cube centred at origin (will be translated in CPU matrix).
        static const float cube[] = {
                // front
                -0.15f, -0.15f, 0.15f,  0.15f, -0.15f, 0.15f,  0.15f, 0.15f, 0.15f,
                -0.15f, -0.15f, 0.15f,  0.15f, 0.15f, 0.15f,  -0.15f, 0.15f, 0.15f,
                // back
                -0.15f, -0.15f, -0.15f, -0.15f, 0.15f, -0.15f,  0.15f, 0.15f, -0.15f,
                -0.15f, -0.15f, -0.15f,  0.15f, 0.15f, -0.15f,  0.15f, -0.15f, -0.15f,
                // left
                -0.15f, -0.15f, -0.15f, -0.15f, -0.15f, 0.15f, -0.15f, 0.15f, 0.15f,
                -0.15f, -0.15f, -0.15f, -0.15f, 0.15f, 0.15f, -0.15f, 0.15f, -0.15f,
                // right
                 0.15f, -0.15f, -0.15f,  0.15f, 0.15f, -0.15f,  0.15f, 0.15f, 0.15f,
                 0.15f, -0.15f, -0.15f,  0.15f, 0.15f, 0.15f,  0.15f, -0.15f, 0.15f,
                // top
                -0.15f, 0.15f, -0.15f, -0.15f, 0.15f, 0.15f,  0.15f, 0.15f, 0.15f,
                -0.15f, 0.15f, -0.15f,  0.15f, 0.15f, 0.15f,  0.15f, 0.15f, -0.15f,
                // bottom
                -0.15f, -0.15f, -0.15f,  0.15f, -0.15f, -0.15f,  0.15f, -0.15f, 0.15f,
                -0.15f, -0.15f, -0.15f,  0.15f, -0.15f, 0.15f, -0.15f, -0.15f, 0.15f,
        };

        glGenVertexArrays( 1, &g_stubVao );
        glGenBuffers( 1, &g_stubVbo );
        glBindVertexArray( g_stubVao );
        glBindBuffer( GL_ARRAY_BUFFER, g_stubVbo );
        glBufferData( GL_ARRAY_BUFFER, sizeof cube, cube, GL_STATIC_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( float ) * 3, NULL );
        glBindVertexArray( 0 );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        g_stubReady = 1;
        EZLOG( "stub GLES ready" );
        return 1;
}

static void MatMul4( float out[16], const float a[16], const float b[16] )
{
        float r[16];
        for ( int c = 0; c < 4; c++ )
        {
                for ( int row = 0; row < 4; row++ )
                {
                        r[c * 4 + row] =
                                a[0 * 4 + row] * b[c * 4 + 0] +
                                a[1 * 4 + row] * b[c * 4 + 1] +
                                a[2 * 4 + row] * b[c * 4 + 2] +
                                a[3 * 4 + row] * b[c * 4 + 3];
                }
        }
        memcpy( out, r, sizeof r );
}

static void MatTranslate( float out[16], float x, float y, float z )
{
        memset( out, 0, sizeof( float ) * 16 );
        out[0] = out[5] = out[10] = out[15] = 1.f;
        out[12] = x;
        out[13] = y;
        out[14] = z;
}

} // namespace

void EZQuestVrStubRenderHook( void * /*userdata*/,
                              const EzVrEyeFrame *eyes,
                              int eyeCount,
                              double predictedDisplayTime )
{
        (void)predictedDisplayTime;
        if ( !eyes || eyeCount < 1 )
                return;
        if ( !EnsureStubGl() )
                return;

        // World-space marker ~1.5 m in front of the origin on the floor plane.
        float model[16];
        MatTranslate( model, 0.f, 0.15f, -1.5f );

        for ( int i = 0; i < eyeCount; i++ )
        {
                const EzVrEyeFrame &e = eyes[i];
                if ( !e.fbo )
                        continue;

                glBindFramebuffer( GL_FRAMEBUFFER, e.fbo );
                glViewport( 0, 0, (GLsizei)e.width, (GLsizei)e.height );

                // Distinct clear colours so left/right are unmistakable.
                if ( e.eyeIndex == 0 )
                        glClearColor( 0.05f, 0.18f, 0.22f, 1.f ); // teal
                else
                        glClearColor( 0.18f, 0.06f, 0.20f, 1.f ); // purple
                glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

                glEnable( GL_DEPTH_TEST );
                glDepthFunc( GL_LEQUAL );

                float mvp[16], tmp[16];
                MatMul4( tmp, e.view, model );
                MatMul4( mvp, e.proj, tmp );

                glUseProgram( g_stubProgram );
                glUniformMatrix4fv( g_stubUMvp, 1, GL_FALSE, mvp );
                // Warm marker colour, slightly different per eye for debug.
                if ( e.eyeIndex == 0 )
                        glUniform3f( g_stubUColor, 0.95f, 0.75f, 0.25f );
                else
                        glUniform3f( g_stubUColor, 0.95f, 0.55f, 0.35f );

                glBindVertexArray( g_stubVao );
                glDrawArrays( GL_TRIANGLES, 0, 36 );
                glBindVertexArray( 0 );
        }

        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glUseProgram( 0 );
}

// ---------------------------------------------------------------------------
// Optional engine boot worker
// ---------------------------------------------------------------------------

static void *EngineBootThread( void * )
{
        EZLOG( "engine boot worker started (LauncherMainAndroid)" );
        EZWAR( "NOTE: full togles → external-FBO path is not in this public "
               "tree yet. LauncherMain will try to own its own GL surface "
               "(SDL) and will likely conflict with the XR EGL context. "
               "This path is scaffolding for the private-tree integration." );

        // Minimal argv: binary name only. Real args come from SetLauncherArgs
        // inside LauncherMainAndroid (env / java_args).
        char *argv[] = { (char *)"hl2_linux", NULL };
        int rc = LauncherMainAndroid( 1, argv );
        EZLOG( "LauncherMainAndroid returned %d", rc );
        return NULL;
}

void EZQuestVrOnFirstPresent( void )
{
        if ( g_bootRequested )
                return;
        g_bootRequested = 1;

        const int wantStub = EnvTruthy( "EZQUEST_VR_STUB_HOOK" ) ||
                             EnvTruthy( "EZQUEST_VR_TRY_ENGINE" );
        const int wantEngine = EnvTruthy( "EZQUEST_VR_TRY_ENGINE" );

        EZLOG( "first present: stub=%d try_engine=%d", wantStub, wantEngine );

        if ( wantStub )
        {
                // Register the stub only if nothing else has claimed the hook.
                // (We cannot read g_renderHook from here — it is private to
                // vr_main.cpp — so we always set the stub when requested.
                // A later EZQuestVrSetRenderHook from the real engine
                // overwrites it.)
                EZQuestVrSetRenderHook( EZQuestVrStubRenderHook, NULL );
                g_stubRegistered = 1;
                EZLOG( "registered stub render hook (teal/purple clear + cube)" );
        }

        if ( wantEngine && !g_engineThreadStarted )
        {
                g_engineThreadStarted = 1;
                if ( pthread_create( &g_engineThread, NULL, EngineBootThread, NULL ) != 0 )
                {
                        EZERR( "failed to start engine boot thread" );
                        g_engineThreadStarted = 0;
                }
                else
                {
                        pthread_detach( g_engineThread );
                        EZLOG( "engine boot thread detached" );
                }
        }
}

int EZQuestVrEngineBootRequested( void )
{
        return g_bootRequested;
}

int EZQuestVrHasExternalHook( void )
{
        // Best-effort: if the stub is the one we registered we still report 0
        // for "external". Real engine code should call EZQuestVrSetRenderHook
        // itself after init; this accessor is mainly for diagnostics.
        return 0;
}

#endif // ANDROID
