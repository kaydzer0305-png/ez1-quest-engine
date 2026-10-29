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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define EZTAG "EZQuest-VR-Present"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )
#define EZERR( ... ) __android_log_print( ANDROID_LOG_ERROR, EZTAG, __VA_ARGS__ )

namespace {

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
EGLDisplay g_display = EGL_NO_DISPLAY;
EGLConfig  g_config  = (EGLConfig)0;
EGLSurface g_pbuffer = EGL_NO_SURFACE;
EGLSurface g_engPbuffer = EGL_NO_SURFACE;
EGLContext g_xrCtx   = EGL_NO_CONTEXT;
EGLContext g_engCtx  = EGL_NO_CONTEXT;
uint32_t g_w = 0;
uint32_t g_h = 0;
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#endif
GLuint g_tex[2][2];
GLuint g_fbo[2][2];
GLuint g_shareMark = 0;
// Engine-side FBOs wrapping the same shared g_tex textures. The g_fbo set is
// created+attached on the XR context, but its attachments are not visible
// from the engine context (verified COMPLETE there, MISSING here, same shared
// objects — device-proven 2026-10-11), so the engine side binds these
// locally-created equivalents instead. Textures themselves share fine (magic
// marker reads back exact), so only the FBO linkage is re-established.
GLuint g_engFbo[2][2] = { { 0, 0 }, { 0, 0 } };
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

// One-shot frame capture (adb screencap reads black under an immersive
// session — it captures the 2D display pipeline, not the HMD panels — so the
// only remote view of submitted game pixels is reading them back here).
// Trigger: present-hook invocation #N (env EZQUEST_VR_SHOT_FRAME, default
// 3600 ≈ 50 s after the first engine submit, i.e. in-map; "0" disables), or
// a hot trigger: creating $APP_DATA_PATH/ez2shot.req captures the next
// present and deletes the request. Writes ez2shot_L.tga / ez2shot_R.tga next
// to it (plain TGA 32-bit so any viewer opens them, no PC-side deps).
static unsigned g_presentCount = 0;
static int g_shotDone = 0;
static int g_shotArmedLog = 0;

static int ShotTargetFrame( void )
{
        static int cached = -1;
        if ( cached < 0 )
        {
                cached = 3600;
                const char *v = getenv( "EZQUEST_VR_SHOT_FRAME" );
                if ( v && v[0] )
                        cached = atoi( v );
        }
        return cached;
}

static const char *ShotDir( void )
{
        const char *d = getenv( "APP_DATA_PATH" );
        return ( d && d[0] ) ? d : "/sdcard";
}

static void WriteEyeTga( int eye, GLuint fbo, uint32_t w, uint32_t h )
{
        if ( !fbo || !w || !h )
                return;
        glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
        glPixelStorei( GL_PACK_ALIGNMENT, 1 );
        const size_t n = (size_t)w * h;
        unsigned char *rgba = (unsigned char *)malloc( n * 4 );
        if ( !rgba )
        {
                EZERR( "vr-shot: out of memory for %ux%u", (unsigned)w, (unsigned)h );
                glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
                return;
        }
        glReadPixels( 0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
        GLenum err = glGetError();
        glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
        if ( err != GL_NO_ERROR )
        {
                EZERR( "vr-shot: glReadPixels failed 0x%x", err );
                free( rgba );
                return;
        }
        char path[1024];
        snprintf( path, sizeof path, "%s/ez2shot_%c.tga", ShotDir(), eye ? 'R' : 'L' );
        FILE *f = fopen( path, "wb" );
        if ( !f )
        {
                EZERR( "vr-shot: cannot write %s", path );
                free( rgba );
                return;
        }
        // TGA 32-bit uncompressed, top-left origin; swizzle RGBA->BGRA and
        // flip rows (GL reads bottom-first) in one pass.
        unsigned char hdr[18];
        memset( hdr, 0, sizeof hdr );
        hdr[2] = 2;
        hdr[12] = (unsigned char)( w & 0xFF );
        hdr[13] = (unsigned char)( ( w >> 8 ) & 0xFF );
        hdr[14] = (unsigned char)( h & 0xFF );
        hdr[15] = (unsigned char)( ( h >> 8 ) & 0xFF );
        hdr[16] = 32;
        hdr[17] = 0x28;
        fwrite( hdr, 1, sizeof hdr, f );
        for ( uint32_t y = 0; y < h; y++ )
        {
                const unsigned char *src = rgba + (size_t)( h - 1 - y ) * w * 4;
                for ( uint32_t x = 0; x < w; x++ )
                {
                        unsigned char px[4];
                        px[0] = src[2];
                        px[1] = src[1];
                        px[2] = src[0];
                        px[3] = src[3];
                        fwrite( px, 1, 4, f );
                        src += 4;
                }
        }
        fclose( f );
        free( rgba );
        EZLOG( "vr-shot: captured eye %d %ux%u -> %s", eye, (unsigned)w, (unsigned)h, path );
}

static void ShotTick( const EzVrEyeFrame *eyes, int eyeCount )
{
        if ( !eyes || eyeCount < 1 )
                return;
        if ( !g_shotArmedLog )
        {
                g_shotArmedLog = 1;
                EZLOG( "vr-shot: armed for present #%d (EZQUEST_VR_SHOT_FRAME; 0=off) + ez2shot.req hot trigger (repeatable)",
                       ShotTargetFrame() );
        }
        g_presentCount++;
        int want = 0;
        int latch = 0;
        const int target = ShotTargetFrame();
        if ( target > 0 && !g_shotDone && (int)g_presentCount >= target )
        {
                want = 1;
                latch = 1;
        }
        if ( !want )
        {
                char req[1024];
                snprintf( req, sizeof req, "%s/ez2shot.req", ShotDir() );
                if ( access( req, F_OK ) == 0 )
                {
                        remove( req );
                        want = 1;
                        EZLOG( "vr-shot: hot trigger request claimed" );
                }
        }
        if ( !want )
                return;
        if ( latch )
                g_shotDone = 1;
        for ( int i = 0; i < eyeCount; i++ )
                WriteEyeTga( eyes[i].eyeIndex == 1 ? 1 : 0, eyes[i].fbo, eyes[i].width, eyes[i].height );
}

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
        // Sanitize caller-visible state the blit depends on; restore after.
        // A blit silently no-ops when scissored out or color-masked off, and a
        // missed one here is a black eye with no error anywhere (found
        // 2026-10-11 via readback captures: submits flowing, swapchain black).
        GLboolean scissorWas = glIsEnabled( GL_SCISSOR_TEST );
        GLint scissorBox[4];
        glGetIntegerv( GL_SCISSOR_BOX, scissorBox );
        GLboolean colorMask[4];
        glGetBooleanv( GL_COLOR_WRITEMASK, colorMask );
        glDisable( GL_SCISSOR_TEST );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
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
        GLenum err = glGetError();
        if ( scissorWas )
        {
                glEnable( GL_SCISSOR_TEST );
                glScissor( scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3] );
        }
        glColorMask( colorMask[0], colorMask[1], colorMask[2], colorMask[3] );
        if ( err != GL_NO_ERROR )
        {
                static int logged = 0;
                if ( !logged )
                {
                        logged = 1;
                        EZERR( "present-hook blit failed 0x%x", err );
                }
        }
}

void EnsureHookLocked()
{
        if ( g_hooked )
                return;
        g_hooked = 1;
        EZQuestVrSetRenderHook( EZQuestVrEnginePresentHook, NULL );
        EZLOG( "registered engine present hook (per-eye RTs)" );
}

// Drains queued GL errors so the caller's subsequent glGetError reflects
// ITS work, not the engine's leftovers. The engine's own rendering trips
// benign errors constantly (its CheckGLError is compiled out); without this,
// diagnosing OUR blits reads stale flags.
static void DrainGlErrors( void )
{
        for ( int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++ )
                ;
}
// (Re)binds the blit READ source for one attempt. srcTex != 0 wraps the
// texture in a throwaway FBO (returned for deletion); otherwise re-queries
// the engine's current target — its bindings may change between attempts,
// so this queries live state every time instead of caching.
static GLuint BindBlitSource( unsigned srcTex )
{
        if ( srcTex )
        {
                GLuint tmpFbo = 0;
                glGenFramebuffers( 1, &tmpFbo );
                glBindFramebuffer( GL_READ_FRAMEBUFFER, tmpFbo );
                glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_TEXTURE_2D, (GLuint)srcTex, 0 );
                return tmpFbo;
        }
        // "From current FBO": the engine calls between pushing its per-eye
        // render target and popping it. But materialsystem binds render
        // targets DRAW-only on some paths, leaving READ on whatever was
        // there (0 = the black pbuffer) — blitting from the default READ
        // binding copies black forever while every log insists submits are
        // flowing (found 2026-10-11 via readback captures). Query both
        // bindings and read from whichever names the engine's target.
        GLint boundRead = 0, boundDraw = 0;
        glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &boundRead );
        if ( !boundRead )
                glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &boundDraw );
        GLuint srcFbo = boundRead ? (GLuint)boundRead : (GLuint)boundDraw;
        static int loggedSrc = 0;
        if ( !loggedSrc )
        {
                loggedSrc = 1;
                EZLOG( "submit source: readFbo=%d drawFbo=%d", boundRead, boundDraw );
        }
        if ( srcFbo )
                glBindFramebuffer( GL_READ_FRAMEBUFFER, srcFbo );
        // One-shot completeness probe: a blit with an incomplete READ or
        // DRAW side fails INVALID_FRAMEBUFFER_OPERATION with nothing copied.
        // Log both states plus what the READ color attachment actually is, so
        // a broken side is named instead of inferred.
        {
                static int loggedFbo = 0;
                if ( !loggedFbo )
                {
                        loggedFbo = 1;
                        GLenum readStatus = glCheckFramebufferStatus( GL_READ_FRAMEBUFFER );
                        GLint objType = -1, objName = -1;
                        glGetFramebufferAttachmentParameteriv( GL_READ_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objType );
                        glGetFramebufferAttachmentParameteriv( GL_READ_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &objName );
                        EZLOG( "submit fbo: readStatus=0x%x readObj=%d/%d",
                               readStatus, objType, objName );
                }
        }
        return 0;
}

void BlitSrcToEye( int eye, unsigned srcTex, int srcX0, int srcY0, int srcX1, int srcY1 )
{
        const int back = ( g_published[eye] == 0 ) ? 1 : 0;
        GLuint tmpFbo = BindBlitSource( srcTex );
        // This half runs on the ENGINE thread inside the engine's own frame,
        // so the context carries the engine's dirty state — not ours. A blit
        // with the scissor test on (flashlight shadows, VGUI) or the color
        // mask off (depth prepass) silently copies nothing; sanitize and
        // restore around it. See DrawTexToFbo.
        GLboolean scissorWas = glIsEnabled( GL_SCISSOR_TEST );
        GLint scissorBox[4];
        glGetIntegerv( GL_SCISSOR_BOX, scissorBox );
        GLboolean colorMask[4];
        glGetBooleanv( GL_COLOR_WRITEMASK, colorMask );
        glDisable( GL_SCISSOR_TEST );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        // The engine also leaves the read/draw buffer selectors wherever its
        // last pass needed them (GL_NONE after depth-only work, another
        // attachment after utility renders). Blitting with no selected image
        // is GL_INVALID_FRAMEBUFFER_OPERATION (0x506, seen on device) with
        // nothing copied — so select the color attachments explicitly.
        // (Single-target assumption: this is a forward renderer; no MRT.)
        GLint readBuf = 0, drawBuf0 = 0;
        glGetIntegerv( GL_READ_BUFFER, &readBuf );
        glGetIntegerv( GL_DRAW_BUFFER0, &drawBuf0 );
        glReadBuffer( GL_COLOR_ATTACHMENT0 );
        { const GLenum oneColor[1] = { GL_COLOR_ATTACHMENT0 }; glDrawBuffers( 1, oneColor ); }
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, g_engFbo[eye][back] );
        {
                // Numbers, not adjectives. g_engFbo is engine-local (created
                // + attached on this context), so no cross-context ambiguity
                // here by construction.
                static int loggedShare = 0;
                if ( !loggedShare )
                {
                        loggedShare = 1;
                        GLint drawAttName = -1, drawAttType = -1;
                        glGetFramebufferAttachmentParameteriv( GL_DRAW_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                &drawAttName );
                        glGetFramebufferAttachmentParameteriv( GL_DRAW_FRAMEBUFFER,
                                GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
                                &drawAttType );
                        EGLint xrCfg = -1, engCfg = -1;
                        eglQueryContext( g_display, g_xrCtx, EGL_CONFIG_ID, &xrCfg );
                        eglQueryContext( g_display, g_engCtx, EGL_CONFIG_ID, &engCfg );
                        EZLOG( "share check: isTex=%d isFbo=%d drawStatus=0x%x fbo=%u tex=%u attName=%d attType=%d xrCfg=%d engCfg=%d",
                               glIsTexture( g_tex[eye][back] ),
                               glIsFramebuffer( g_engFbo[eye][back] ),
                               glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER ),
                               g_engFbo[eye][back], g_tex[eye][back],
                               drawAttName, drawAttType, xrCfg, engCfg );
                }
        }
        DrainGlErrors();
        // Multisample sources (the engine's MSAA per-eye RTs) only resolve
        // with NEAREST; LINEAR on them is INVALID_OPERATION (0x502, seen on
        // device). Single-sample scales better with LINEAR, so probe once and
        // latch. The retry reuses the live bindings established above.
        static GLenum g_blitFilter = 0; // 0 = unprobed
        GLenum filt = g_blitFilter ? g_blitFilter : GL_LINEAR;
        glBlitFramebuffer( srcX0, srcY0, srcX1, srcY1,
                           0, 0, (GLint)g_w, (GLint)g_h,
                           GL_COLOR_BUFFER_BIT, filt );
        GLenum blitErr = glGetError();
        if ( blitErr == GL_INVALID_OPERATION && !g_blitFilter )
        {
                g_blitFilter = GL_NEAREST;
                glBlitFramebuffer( srcX0, srcY0, srcX1, srcY1,
                                   0, 0, (GLint)g_w, (GLint)g_h,
                                   GL_COLOR_BUFFER_BIT, GL_NEAREST );
                blitErr = glGetError();
                static int loggedFilter = 0;
                if ( !loggedFilter )
                {
                        loggedFilter = 1;
                        EZLOG( "submit blit: LINEAR rejected, latched NEAREST (retry err 0x%x)",
                               blitErr );
                }
        }
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        if ( scissorWas )
        {
                glEnable( GL_SCISSOR_TEST );
                glScissor( scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3] );
        }
        glColorMask( colorMask[0], colorMask[1], colorMask[2], colorMask[3] );
        // Guarded: restoring GL_NONE (0) would itself be INVALID_ENUM. The
        // engine re-selects buffers per pass anyway, which is why they were
        // dirty when we arrived.
        if ( readBuf )
                glReadBuffer( readBuf );
        if ( drawBuf0 )
        {
                const GLenum restore[1] = { (GLenum)drawBuf0 };
                glDrawBuffers( 1, restore );
        }
        if ( blitErr != GL_NO_ERROR )
        {
                static int loggedBlit = 0;
                if ( !loggedBlit )
                {
                        loggedBlit = 1;
                        EZERR( "submit blit failed 0x%x (eye %d)", blitErr, eye );
                }
        }
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
        // Verify creation on THIS context now, not three minutes later on
        // another one: names reserve fine with no context current while every
        // allocate/attach silently no-ops. A hollow target set is exactly the
        // black-submits signature, so say so here while the cause is local.
        // (glIsTexture never creates; the FBO bind below would materialize a
        // hollow name as empty, which still checks MISSING — the signal is
        // what matters.)
        {
                GLint isTex = glIsTexture( g_tex[0][0] );
                glBindFramebuffer( GL_FRAMEBUFFER, g_fbo[0][0] );
                GLenum st0 = glCheckFramebufferStatus( GL_FRAMEBUFFER );
                glBindFramebuffer( GL_FRAMEBUFFER, 0 );
                GLenum preErr = glGetError();
                EZLOG( "present targets 2 eyes %ux%u (verify: isTex=%d fbo=0x%x glErr=0x%x)",
                       (unsigned)width, (unsigned)height, isTex, st0, preErr );
        }
        // Share marker: 2x2 texture holding a magic pattern, created here on
        // the XR context. The engine side reads all 16 bytes back: an exact
        // match proves the contexts share (no engine texture holds this
        // pattern by coincidence). glIsTexture-style name checks CANNOT prove
        // this: small integer ids collide across unshared contexts.
        glGenTextures( 1, &g_shareMark );
        glBindTexture( GL_TEXTURE_2D, g_shareMark );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        {
                static const unsigned char magic[16] = {
                        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
                        0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21 };
                glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                              GL_RGBA, GL_UNSIGNED_BYTE, magic );
        }
        glBindTexture( GL_TEXTURE_2D, 0 );
        g_inited = 1;
        g_published[0] = g_published[1] = -1;
        g_haveEye[0] = g_haveEye[1] = 0;
}

extern "C" __attribute__( ( visibility( "default" ) ) )
void EZQuestVrPresentShutdown( void )
{
        if ( g_engCtx != EGL_NO_CONTEXT && g_display != EGL_NO_DISPLAY )
                eglDestroyContext( g_display, g_engCtx );
        g_engCtx = EGL_NO_CONTEXT;
        if ( g_engPbuffer != EGL_NO_SURFACE && g_display != EGL_NO_DISPLAY )
                eglDestroySurface( g_display, g_engPbuffer );
        g_engPbuffer = EGL_NO_SURFACE;
        if ( g_inited )
        {
                for ( int e = 0; e < 2; e++ )
                {
                        glDeleteFramebuffers( 2, g_fbo[e] );
                        glDeleteTextures( 2, g_tex[e] );
                        glDeleteFramebuffers( 2, g_engFbo[e] );
                        g_engFbo[e][0] = g_engFbo[e][1] = 0;
                }
        }
        if ( g_shareMark )
        {
                glDeleteTextures( 1, &g_shareMark );
                g_shareMark = 0;
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
        // The engine runs on its own thread. The XR thread keeps g_pbuffer
        // bound to the XR context, and one surface cannot be current on two
        // contexts at once (that was EGL_BAD_ACCESS 0x3002 on device) — so the
        // engine gets its own 1x1 pbuffer. It never presents to it; every
        // present goes through the submit hooks as shared textures.
        if ( g_engPbuffer == EGL_NO_SURFACE )
        {
                const EGLint pbAttribs[] = {
                        EGL_WIDTH, 1,
                        EGL_HEIGHT, 1,
                        EGL_NONE
                };
                g_engPbuffer = eglCreatePbufferSurface( g_display, g_config, pbAttribs );
                if ( g_engPbuffer == EGL_NO_SURFACE )
                {
                        EZERR( "engine pbuffer failed 0x%x", eglGetError() );
                        pthread_mutex_unlock( &g_lock );
                        return 0;
                }
        }
        EGLBoolean ok = eglMakeCurrent( g_display, g_engPbuffer, g_engPbuffer, g_engCtx );
        pthread_mutex_unlock( &g_lock );
        if ( !ok )
        {
                EZERR( "engine eglMakeCurrent failed 0x%x", eglGetError() );
                return 0;
        }
        // Bind the shared present textures to engine-local FBOs (see g_engFbo
        // note): the XR-created g_fbo linkage is not visible here, so blits
        // target these instead. Textures are shared (marker-proven); only the
        // FBO objects are per-context.
        {
                static int engFboReady = 0;
                if ( !engFboReady )
                {
                        engFboReady = 1;
                        for ( int e = 0; e < 2; e++ )
                        {
                                glGenFramebuffers( 2, g_engFbo[e] );
                                for ( int i = 0; i < 2; i++ )
                                {
                                        glBindFramebuffer( GL_FRAMEBUFFER, g_engFbo[e][i] );
                                        glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                GL_TEXTURE_2D, g_tex[e][i], 0 );
                                }
                        }
                        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
                        GLenum st = GL_NO_ERROR;
                        {
                                glBindFramebuffer( GL_FRAMEBUFFER, g_engFbo[0][0] );
                                st = glCheckFramebufferStatus( GL_FRAMEBUFFER );
                                glBindFramebuffer( GL_FRAMEBUFFER, 0 );
                        }
                        GLenum mkErr = glGetError();
                        EZLOG( "engine FBOs: local attach status=0x%x glErr=0x%x", st, mkErr );
                }
        }
        // Share validation, once: the marker texture was created on the XR
        // context at PresentInit holding a 16-byte magic pattern. Read it all
        // back through a throwaway FBO here: an exact match proves the
        // contexts share (no engine texture holds this pattern by
        // coincidence). (glGetTexImage does not exist in GLES; the FBO +
        // ReadPixels detour is the ES way to read a texture.)
        {
                static int loggedShare = 0;
                if ( !loggedShare )
                {
                        loggedShare = 1;
                        static const unsigned char magic[16] = {
                                0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
                                0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21 };
                        unsigned char px[16];
                        memset( px, 0, sizeof px );
                        GLenum fboErr = GL_NO_ERROR, readErr = GL_NO_ERROR;
                        int match = 0;
                        if ( g_shareMark )
                        {
                                GLuint tmp = 0;
                                glGenFramebuffers( 1, &tmp );
                                glBindFramebuffer( GL_READ_FRAMEBUFFER, tmp );
                                glFramebufferTexture2D( GL_READ_FRAMEBUFFER,
                                        GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_shareMark, 0 );
                                fboErr = glGetError();
                                GLenum st = glCheckFramebufferStatus( GL_READ_FRAMEBUFFER );
                                if ( fboErr == GL_NO_ERROR && st == GL_FRAMEBUFFER_COMPLETE )
                                {
                                        glReadBuffer( GL_COLOR_ATTACHMENT0 );
                                        glPixelStorei( GL_PACK_ALIGNMENT, 1 );
                                        glReadPixels( 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px );
                                        readErr = glGetError();
                                }
                                else if ( fboErr == GL_NO_ERROR )
                                {
                                        readErr = st;
                                }
                                glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
                                glDeleteFramebuffers( 1, &tmp );
                                match = ( fboErr == GL_NO_ERROR && readErr == GL_NO_ERROR &&
                                          memcmp( px, magic, sizeof px ) == 0 );
                        }
                        EZLOG( "share pixels: mark=%u match=%d fboErr=0x%x readErr=0x%x %s",
                               g_shareMark, match, fboErr, readErr,
                               match ? "MATCH (sharing on)" : "MISMATCH (no sharing)" );
                }
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
        ShotTick( eyes, eyeCount );
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
