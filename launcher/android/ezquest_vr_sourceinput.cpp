/*
Copyright (C) 2026 kaydzer0305-png

Maps Quest Touch (EzVrInputState) onto Source movement/attack commands.
Does not use HID/SDL joystick paths.

Per-profile extras: SOURCEVR_GAME selects the arsenal layout. EZ1 keeps its
plan-Phase-3 extras (Bad Cop kick on +alt1, nightvision on impulse 100, the
manhack-toss weapon on slot5). EZ2 (EntropyZero2) has no manhack-toss weapon,
so that chord is lastinv (previous weapon — the panic key) there instead;
kick/nightvision are shared HL2-kit behavior and stay.
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "ezquest_vr_sourceinput.h"
#include "vr_input.h"

#include <android/log.h>
#include <dlfcn.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "inputsystem/iinputsystem.h"
#include "tier1/interface.h"

#define EZTAG "EZQuest-VR-Input"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )

typedef void ( *CbufAddTextFn )( const char *text );

namespace {

CbufAddTextFn g_cbuf = NULL;
IInputSystem *g_input = NULL;
// Separate latches: the XR thread starts resolving before the engine boot
// worker has loaded anything, and a latch shared by both lookups would
// permanently pin whichever one lost the race (2026-10-11: cbuf won,
// IInputSystem stayed NULL forever and the turn axis was dead).
int g_cbufResolved = 0;
int g_inputResolved = 0;
int g_logged = 0;
// Failure diagnostics below run every XR frame until they resolve; throttle
// to ~one line per 4s so a permanently-unresolvable symbol costs logcat spam
// instead of a diagnosis.
int g_resolveTick = 0;
static int ResolveLogDue( void ) { return ( g_resolveTick % 300 ) == 0; }

int g_fwd = 0, g_back = 0, g_ml = 0, g_mr = 0, g_speed = 0;
int g_atk = 0, g_atk2 = 0, g_jump = 0, g_reload = 0, g_use = 0, g_menu = 0;
int g_weap = 0;
int g_kick = 0;
int g_manhack = 0;
int g_nvLatched = 0;
int g_nvFrames = 0;
int g_snapLatched = 0;

static int EnvTruthy( const char *name, int deflt )
{
        const char *v = getenv( name );
        if ( !v || !v[0] )
                return deflt;
        if ( v[0] == '0' && v[1] == '\0' )
                return 0;
        return 1;
}

// True on the EZ2 profile (EntropyZero2, either casing): its arsenal has no
// manhack-toss weapon, so profile-specific chords diverge here. Cached: the
// game does not change under a running session.
static int IsEZ2( void )
{
        static int cached = -1;
        if ( cached < 0 )
        {
                const char *g = getenv( "SOURCEVR_GAME" );
                cached = ( g && strcasecmp( g, "EntropyZero2" ) == 0 ) ? 1 : 0;
        }
        return cached;
}

static void *TrySymDiag( const char *name, const char *ctx )
{
        dlerror();
        void *s = dlsym( RTLD_DEFAULT, name );
        if ( s )
                return s;
        const char *e1 = dlerror();
        void *eng = dlopen( "libengine.so", RTLD_NOW | RTLD_NOLOAD );
        if ( !eng )
                eng = dlopen( "libengine.so", RTLD_NOW );
        if ( eng )
        {
                dlerror();
                s = dlsym( eng, name );
                if ( s )
                        return s;
        }
        const char *e2 = dlerror();
        if ( ResolveLogDue() )
                EZLOG( "%s: %s unresolved (global: %s; engine-handle: %s)", ctx, name,
                       e1 && e1[0] ? e1 : "?", e2 && e2[0] ? e2 : "?" );
        return NULL;
}

static void *TrySym( const char *name )
{
        void *s = dlsym( RTLD_DEFAULT, name );
        if ( s )
                return s;
        void *eng = dlopen( "libengine.so", RTLD_NOW | RTLD_NOLOAD );
        if ( !eng )
                eng = dlopen( "libengine.so", RTLD_NOW );
        if ( eng )
        {
                s = dlsym( eng, name );
                if ( s )
                        return s;
        }
        return NULL;
}

static void ResolveEngine( void )
{
        g_resolveTick++;
        if ( !g_cbufResolved )
        {
                static const char *kCbufNames[] = {
                        "EZQuest_Cbuf_AddText",
                        "Cbuf_AddText",
                        "_Z13Cbuf_AddTextPKc",
                        NULL
                };
                for ( int i = 0; kCbufNames[i]; i++ )
                {
                        g_cbuf = (CbufAddTextFn)TrySym( kCbufNames[i] );
                        if ( g_cbuf )
                        {
                                EZLOG( "resolved %s", kCbufNames[i] );
                                g_cbufResolved = 1;
                                break;
                        }
                }
                if ( !g_cbufResolved )
                        TrySymDiag( "EZQuest_Cbuf_AddText", "cbuf" );
        }

        if ( !g_inputResolved )
        {
                // Prefer libinputsystem.so's OWN CreateInterface. A global-scope
                // lookup returns the first match in load order — usually
                // libengine.so's, which serves engine interfaces and answers
                // INPUTSYSTEM_INTERFACE_VERSION with NULL (observed on device:
                // "factory ok but ... returned NULL" every retry). Only fall
                // back to the global lookup when the input lib is not loaded.
                CreateInterfaceFn factory = NULL;
                {
                        dlerror();
                        void *in = dlopen( "libinputsystem.so", RTLD_NOW | RTLD_NOLOAD );
                        const char *eNoload = dlerror();
                        if ( !in )
                        {
                                in = dlopen( "libinputsystem.so", RTLD_NOW );
                                const char *eLoad = dlerror();
                                if ( !in && ResolveLogDue() )
                                        EZLOG( "input: dlopen libinputsystem.so failed (noload: %s; load: %s)",
                                               eNoload && eNoload[0] ? eNoload : "?",
                                               eLoad && eLoad[0] ? eLoad : "?" );
                        }
                        if ( in )
                        {
                                dlerror();
                                factory = (CreateInterfaceFn)dlsym( in, "CreateInterface" );
                                const char *eSym = dlerror();
                                if ( !factory && ResolveLogDue() )
                                        EZLOG( "input: dlsym CreateInterface failed: %s",
                                               eSym && eSym[0] ? eSym : "?" );
                        }
                }
                if ( !factory )
                        factory = (CreateInterfaceFn)TrySym( "CreateInterface" );
                if ( factory )
                {
                        g_input = (IInputSystem *)factory( INPUTSYSTEM_INTERFACE_VERSION, NULL );
                        if ( g_input )
                        {
                                EZLOG( "resolved IInputSystem" );
                                g_inputResolved = 1;
                        }
                        else if ( ResolveLogDue() )
                        {
                                EZLOG( "input: factory ok but INPUTSYSTEM_INTERFACE_VERSION returned NULL" );
                        }
                }
        }
}

static void SendCmd( const char *cmd )
{
        if ( !g_cbuf || !cmd || !cmd[0] )
                return;
        g_cbuf( cmd );
}

static void Hold( int *slot, int want, const char *plusName, const char *minusName )
{
        if ( want && !*slot )
        {
                SendCmd( plusName );
                *slot = 1;
        }
        else if ( !want && *slot )
        {
                SendCmd( minusName );
                *slot = 0;
        }
}

static void PostMouseYaw( float yawDelta )
{
        if ( !g_input || fabsf( yawDelta ) < 0.0001f )
                return;
        const int dx = (int)( yawDelta * 220.f );
        if ( dx == 0 )
                return;
        InputEvent_t ev;
        memset( &ev, 0, sizeof ev );
        ev.m_nType = IE_AnalogValueChanged;
        ev.m_nData = MOUSE_X;
        ev.m_nData2 = dx;
        g_input->PostUserEvent( ev );
}

} // namespace

void EZQuestVrSourceInputSync( void )
{
        if ( !EnvTruthy( "EZQUEST_VR_SOURCE_INPUT", 1 ) )
                return;

        ResolveEngine();
        if ( !g_cbuf && !g_input )
                return;

        EzVrInputState st;
        memset( &st, 0, sizeof st );
        if ( !EzVrInputGetState( &st ) || !st.ready )
                return;

        const EzVrController &L = st.hands[EZ_VR_HAND_LEFT];
        const EzVrController &R = st.hands[EZ_VR_HAND_RIGHT];

        const float dead = 0.25f;
        const float ly = L.active ? L.stickY : 0.f;
        const float lx = L.active ? L.stickX : 0.f;
        const float rx = R.active ? R.stickX : 0.f;

        Hold( &g_fwd,  ly >  dead, "+forward\n", "-forward\n" );
        Hold( &g_back, ly < -dead, "+back\n",    "-back\n" );
        Hold( &g_ml,   lx < -dead, "+moveleft\n", "-moveleft\n" );
        Hold( &g_mr,   lx >  dead, "+moveright\n", "-moveright\n" );
        Hold( &g_speed, L.stickClick, "+speed\n", "-speed\n" );

        const int kick = ( L.gripClick || L.grip > 0.7f ) && ( R.triggerClick || R.trigger > 0.7f );
        Hold( &g_kick, kick, "+alt1\n", "-alt1\n" );

        if ( ( L.gripClick || L.grip > 0.7f ) && R.stickClick && !g_manhack )
        {
                // EZ1: the manhack-toss weapon lives on slot5. EZ2 has no such
                // weapon; lastinv (previous weapon) is the always-valid panic
                // key instead.
                SendCmd( IsEZ2() ? "lastinv\n" : "slot5\n" );
                g_manhack = 1;
        }
        else if ( !R.stickClick )
        {
                g_manhack = 0;
        }

        if ( R.stickClick && !g_weap && !g_manhack )
        {
                SendCmd( "invnext\n" );
                g_weap = 1;
        }
        else if ( !R.stickClick )
        {
                g_weap = 0;
        }

        const int fire = !kick && ( R.triggerClick || R.trigger > 0.7f || L.triggerClick );
        Hold( &g_atk, fire, "+attack\n", "-attack\n" );
        Hold( &g_atk2, !kick && ( R.gripClick || L.gripClick || R.grip > 0.7f ), "+attack2\n", "-attack2\n" );

        const int jumpHeld = R.primaryButton || L.primaryButton;
        if ( jumpHeld )
        {
                if ( g_nvFrames < 40 )
                        g_nvFrames++;
                if ( g_nvFrames == 32 && !g_nvLatched )
                {
                        SendCmd( "impulse 100\n" );
                        g_nvLatched = 1;
                        EZLOG( "nightvision impulse 100" );
                }
        }
        else
        {
                g_nvFrames = 0;
                g_nvLatched = 0;
        }
        Hold( &g_jump, jumpHeld && !g_nvLatched, "+jump\n", "-jump\n" );

        const int reload = R.secondaryButton || L.secondaryButton;
        Hold( &g_reload, reload, "+reload\n", "-reload\n" );
        Hold( &g_use, L.gripClick && L.triggerClick, "+use\n", "-use\n" );
        Hold( &g_menu, R.menuButton || L.menuButton, "cancelselect\n", "" );

        if ( EnvTruthy( "EZQUEST_VR_SNAP_TURN", 1 ) )
        {
                const float gate = 0.68f;
                if ( fabsf( rx ) > gate && !g_snapLatched )
                {
                        float deg = 30.f;
                        const char *ds = getenv( "EZQUEST_VR_SNAP_DEGREES" );
                        if ( ds && ds[0] )
                                deg = (float)atof( ds );
                        if ( deg < 15.f ) deg = 15.f;
                        if ( deg > 90.f ) deg = 90.f;
                        const float ticks = deg / 0.022f;
                        PostMouseYaw( ( rx > 0.f ? ticks : -ticks ) / 220.f );
                        g_snapLatched = 1;
                }
                else if ( fabsf( rx ) < 0.35f )
                {
                        g_snapLatched = 0;
                }
        }
        else if ( fabsf( rx ) > 0.12f )
        {
                PostMouseYaw( rx );
        }

        if ( !g_logged )
        {
                g_logged = 1;
                EZLOG( "source mapping live (cbuf=%d inputsys=%d %s)", g_cbuf ? 1 : 0, g_input ? 1 : 0,
                       IsEZ2() ? "ez2-kick/nv/lastinv" : "ez1-kick/nv/manhack" );
        }
}

#endif
