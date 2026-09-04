/*
Copyright (C) 2026 kaydzer0305-png
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "ezquest_vr_present.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <android/log.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#define EZTAG "EZQuest-VR-Present"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )
#define EZERR( ... ) __android_log_print( ANDROID_LOG_ERROR, EZTAG, __VA_ARGS__ )

namespace {

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
EGLDisplay g_display = EGL_NO_DISPLAY;
EGLConfig  g_config  = (EGLConfig)0;
EGLSurface g_pbuffer = EGL_NO_SURFACE;
EGLContext g_xrCtx   = EGL_NO_CONTEXT;
EGLContext g_engCtx  = EGL_NO_CONTEXT;
uint32_t g_w = 0;
uint32_t g_h = 0;
GLuint g_tex[2][2];
GLuint g_fbo[2][2];
int g_inited = 0;
int g_published[2] = { -1, -1 };
int g_hooked = 0;
int g_haveEye[2] = { 0, 0 };
unsigned g_gen = 0;
unsigned g_eyeGen[2] = { 0, 0 };
GLuint g_blitProg = 0;
GLuint g_blitVao = 0;
GLuint g_blitVbo = 0;
GLint  g_blitUTex = -1;

GLuint Compile( GLenum type, const char *src )
{
        GLuint s = glCreateShader( type );
        glShaderSource( s, 1, &src, NULL );
        glCompileShader( s );
        GLint ok = 0;
        glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
        if ( !ok )
        {
                char log[256];
                glGetShaderInfoLog( s, sizeof log, NULL, log );
                EZERR( "shader: %s", log );
                glDeleteShader( s );
                return 0;
        }
        return s;
}

int EnsureBlitProg()
{
        if ( g_blitProg )
                return 1;
        const char *vs =
                "#version 300 es\n"
                "layout(location=0) in vec2 aPos;\n"
                "out vec2 vUv;\n"
                "void main(){ vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
        const char *fs =
                "#version 300 es\n"
                "precision mediump float;\n"
                "in vec2 vUv;\n"
                "uniform sampler2D uTex;\n"
                "out vec4 fragColor;\n"
                "void main(){ fragColor = texture(uTex, vec2(vUv.x, 1.0 - vUv.y)); }\n";
        GLuint v = Compile( GL_VERTEX_SHADER, vs );
        GLuint f = Compile( GL_FRAGMENT_SHADER, fs );
        if ( !v || !f )
                return 0;
        g_blitProg = glCreateProgram();
        glAttachShader( g_blitProg, v );
        glAttachShader( g_blitProg, f );
        glLinkProgram( g_blitProg );
        glDeleteShader( v );
        glDeleteShader( f );
        GLint linked = 0;
        glGetProgramiv( g_blitProg, GL_LINK_STATUS, &linked );
        if ( !linked )
        {
                EZERR( "blit program link failed" );
                return 0;
        }
        g_blitUTex = glGetUniformLocation( g_blitProg, "uTex" );
        static const float quad[] = { -1,-1,  1,-1,  -1,1,  1,1 };
        glGenVertexArrays( 1, &g_blitVao );
        glGenBuffers( 1, &g_blitVbo );
        glBindVertexArray( g_blitVao );
        glBindBuffer( GL_ARRAY_BUFFER, g_blitVbo );
        glBufferData( GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 8, NULL );
        glBindVertexArray( 0 );
        return 1;
}

void DrawTexToFbo( GLuint tex, GLuint fbo, uint32_t w, uint32_t h )
{
        if ( !EnsureBlitProg() || !tex || !fbo )
                return;
        glBindFramebuffer( GL_FRAMEBUFFER, fbo );
        glViewport( 0, 0, (GLsizei)w, (GLsizei)h );
        glDisable( GL_DEPTH_TEST );
        glUseProgram( g_blitProg );
        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, tex );
        glUniform1i( g_blitUTex, 0 );
        glBindVertexArray( g_blitVao );
        glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
        glBindVertexArray( 0 );
        glBindTexture( GL_TEXTURE_2D, 0 );
        glUseProgram( 0 );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void EnsureHookLocked()
{
        if ( g_hooked )
                return;
        g_hooked = 1;
        EZQuestVrSetRenderHook( EZQuestVrEnginePresentHook, NULL );
        EZLOG( "registered engine present hook (per-eye RTs)" );
}

void BlitSrcToEye( int eye, unsigned srcTex, int srcX0, int srcY0, int srcX1, int srcY1 )
{
        const int back = ( g_published[eye] == 0 ) ? 1 : 0;
        GLuint tmpFbo = 0;
        if ( srcTex )
        {
                glGenFramebuffers( 1, &tmpFbo );
                glBindFramebuffer( GL_READ_FRAMEBUFFER, tmpFbo );
                glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_TEXTURE_2D, (GLuint)srcTex, 0 );
        }
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, g_fbo[eye][back] );
        glBlitFramebuffer( srcX0, srcY0, srcX1, srcY1,
                           0, 0, (GLint)g_w, (GLint)g_h,
                           GL_COLOR_BUFFER_BIT, GL_LINEAR );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        if ( tmpFbo )
                glDeleteFramebuffers( 1, &tmpFbo );
        g_published[eye] = back;
        g_haveEye[eye] = 1;
        g_eyeGen[eye] = g_gen;
}

} // namespace

extern "C" __attribute__( ( visibility( "default" ) ) )
void EZQuestVrPresentInit( uint32_t width, uint32_t height )
{
        g_display = eglGetCurrentDisplay();
        g_xrCtx = eglGetCurrentContext();
        g_pbuffer = eglGetCurrentSurface( EGL_DRAW );
        g_w = width;
        g_h = height;
        EGLint num = 0;
        EGLint attrs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                EGL_NONE
        };
        eglChooseConfig( g_display, attrs, &g_config, 1, &num );
        for ( int e = 0; e < 2; e++ )
        {
                glGenTextures( 2, g_tex[e] );
                glGenFramebuffers( 2, g_fbo[e] );
                for ( int i = 0; i < 2; i++ )
                {
                        glBindTexture( GL_TEXTURE_2D, g_tex[e][i] );
                        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
                        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
                        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
                        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
                        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height,
                                      0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
                        glBindFramebuffer( GL_FRAMEBUFFER, g_fbo[e][i] );
                        glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                GL_TEXTURE_2D, g_tex[e][i], 0 );
                }
        }
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        glBindTexture( GL_TEXTURE_2D, 0 );
        g_inited = 1;
        g_published[0] = g_published[1] = -1;
        g_haveEye[0] = g_haveEye[1] = 0;
        EZLOG( "present targets 2 eyes %ux%u", (unsigned)width, (unsigned)height );
}

extern "C" __attribute__( ( visibility( "default" ) ) )
void EZQuestVrPresentShutdown( void )
{
        if ( g_engCtx != EGL_NO_CONTEXT && g_display != EGL_NO_DISPLAY )
                eglDestroyContext( g_display, g_engCtx );
        g_engCtx = EGL_NO_CONTEXT;
        if ( g_inited )
        {
                for ( int e = 0; e < 2; e++ )
                {
                        glDeleteFramebuffers( 2, g_fbo[e] );
                        glDeleteTextures( 2, g_tex[e] );
                }
        }
        if ( g_blitProg ) glDeleteProgram( g_blitProg );
        if ( g_blitVbo ) glDeleteBuffers( 1, &g_blitVbo );
        if ( g_blitVao ) glDeleteVertexArrays( 1, &g_blitVao );
        g_inited = g_hooked = 0;
        g_haveEye[0] = g_haveEye[1] = 0;
        g_published[0] = g_published[1] = -1;
}

extern "C" __attribute__( ( visibility( "default" ) ) )
int EZQuestVrBindEngineContext( void )
{
        if ( !g_inited || g_display == EGL_NO_DISPLAY || g_xrCtx == EGL_NO_CONTEXT )
                return 0;
        pthread_mutex_lock( &g_lock );
        if ( g_engCtx == EGL_NO_CONTEXT )
        {
                const EGLint ctxAttribs[] = {
                        EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
                        EGL_CONTEXT_MINOR_VERSION_KHR, 0,
                        EGL_NONE
                };
                g_engCtx = eglCreateContext( g_display, g_config, g_xrCtx, ctxAttribs );
                if ( g_engCtx == EGL_NO_CONTEXT )
                {
                        EZERR( "share context failed 0x%x", eglGetError() );
                        pthread_mutex_unlock( &g_lock );
                        return 0;
                }
                EZLOG( "created engine share-group context" );
        }
        EGLBoolean ok = eglMakeCurrent( g_display, g_pbuffer, g_pbuffer, g_engCtx );
        pthread_mutex_unlock( &g_lock );
        if ( !ok )
        {
                EZERR( "engine eglMakeCurrent failed 0x%x", eglGetError() );
                return 0;
        }
        return 1;
}

extern "C" __attribute__( ( visibility( "default" ) ) )
int EZQuestVrSubmitEngineEye( int eye, unsigned srcTex, int width, int height )
{
        if ( !g_inited )
                return 0;
        if ( eye < 0 || eye > 1 )
                return 0;
        const int w = width > 0 ? width : (int)g_w;
        const int h = height > 0 ? height : (int)g_h;
        pthread_mutex_lock( &g_lock );
        g_gen++;
        BlitSrcToEye( eye, srcTex, 0, 0, w, h );
        EnsureHookLocked();
        pthread_mutex_unlock( &g_lock );
        return 1;
}

extern "C" __attribute__( ( visibility( "default" ) ) )
int EZQuestVrSubmitEngineEyeFromCurrentFbo( int eye, int width, int height )
{
        return EZQuestVrSubmitEngineEye( eye, 0, width, height );
}

extern "C" __attribute__( ( visibility( "default" ) ) )
int EZQuestVrSubmitEngineFrame( unsigned srcTex, int width, int height )
{
        if ( !g_inited )
                return 0;
        const int w = width > 0 ? width : (int)g_w;
        const int h = height > 0 ? height : (int)g_h;
        pthread_mutex_lock( &g_lock );
        g_gen++;
        const int looksSbs = ( w >= (int)g_w * 3 / 2 ) || ( w >= h * 3 / 2 );
        if ( looksSbs )
        {
                const int mid = w / 2;
                BlitSrcToEye( 0, srcTex, 0, 0, mid, h );
                BlitSrcToEye( 1, srcTex, mid, 0, w, h );
                EZLOG( "sbs split %dx%d -> two eyes", w, h );
        }
        else
        {
                BlitSrcToEye( 0, srcTex, 0, 0, w, h );
                BlitSrcToEye( 1, srcTex, 0, 0, w, h );
        }
        EnsureHookLocked();
        pthread_mutex_unlock( &g_lock );
        return 1;
}

extern "C" __attribute__( ( visibility( "default" ) ) )
void EZQuestVrEnginePresentHook( void * /*userdata*/, const EzVrEyeFrame *eyes,
                                 int eyeCount, double /*predictedDisplayTime*/ )
{
        if ( !eyes || eyeCount < 1 )
                return;
        for ( int i = 0; i < eyeCount; i++ )
        {
                const int eye = ( eyes[i].eyeIndex == 1 || i == 1 ) ? 1 : 0;
                int srcEye = eye;
                if ( !g_haveEye[srcEye] || g_published[srcEye] < 0 )
                        srcEye = g_haveEye[0] ? 0 : 1;
                if ( !g_haveEye[srcEye] || g_published[srcEye] < 0 )
                        continue;
                DrawTexToFbo( g_tex[srcEye][g_published[srcEye]],
                              eyes[i].fbo, eyes[i].width, eyes[i].height );
        }
}

extern "C" __attribute__( ( visibility( "default" ) ) )
int EZQuestVrHasEngineFrame( void )
{
        return g_haveEye[0] || g_haveEye[1];
}

extern "C" __attribute__( ( visibility( "default" ) ) )
int EZQuestVrStereoEyesReady( void )
{
        if ( !g_haveEye[0] || !g_haveEye[1] )
                return 0;
        const unsigned a = g_eyeGen[0], b = g_eyeGen[1];
        const unsigned d = a > b ? a - b : b - a;
        return d <= 1 && a != 0 && b != 0;
}

#endif
