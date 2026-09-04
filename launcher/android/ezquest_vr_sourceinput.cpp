/*
Copyright (C) 2026 kaydzer0305-png

Maps Quest Touch (EzVrInputState) onto Source movement/attack commands.
Does not use HID/SDL joystick paths.
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "ezquest_vr_sourceinput.h"
#include "vr_input.h"

#include <android/log.h>
#include <dlfcn.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "inputsystem/iinputsystem.h"
#include "tier1/interface.h"

#define EZTAG "EZQuest-VR-Input"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )

typedef void ( *CbufAddTextFn )( const char *text );

namespace {

CbufAddTextFn g_cbuf = NULL;
IInputSystem *g_input = NULL;
int g_resolved = 0;
int g_logged = 0;

int g_fwd = 0, g_back = 0, g_ml = 0, g_mr = 0, g_speed = 0;
int g_atk = 0, g_atk2 = 0, g_jump = 0, g_reload = 0, g_use = 0, g_menu = 0;
int g_weap = 0;
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
        if ( g_resolved )
                return;

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
                        break;
                }
        }

        CreateInterfaceFn factory = (CreateInterfaceFn)TrySym( "CreateInterface" );
        if ( !factory )
        {
                void *in = dlopen( "libinputsystem.so", RTLD_NOW | RTLD_NOLOAD );
                if ( !in )
                        in = dlopen( "libinputsystem.so", RTLD_NOW );
                if ( in )
                        factory = (CreateInterfaceFn)dlsym( in, "CreateInterface" );
        }
        if ( factory )
        {
                g_input = (IInputSystem *)factory( INPUTSYSTEM_INTERFACE_VERSION, NULL );
                if ( g_input )
                        EZLOG( "resolved IInputSystem" );
        }

        if ( g_cbuf || g_input )
                g_resolved = 1;
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

        if ( R.stickClick && !g_weap )
        {
                SendCmd( "invnext\n" );
                g_weap = 1;
        }
        else if ( !R.stickClick )
        {
                g_weap = 0;
        }

        const int fire = ( R.triggerClick || R.trigger > 0.7f || L.triggerClick );
        Hold( &g_atk, fire, "+attack\n", "-attack\n" );
        Hold( &g_atk2, ( R.gripClick || L.gripClick || R.grip > 0.7f ), "+attack2\n", "-attack2\n" );

        const int jump = R.primaryButton || L.primaryButton;
        const int reload = R.secondaryButton || L.secondaryButton;
        Hold( &g_jump, jump, "+jump\n", "-jump\n" );
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
                        /* m_yaw default 0.022 → mouse ticks = deg / 0.022
                           PostMouseYaw multiplies by 220, so yawDelta = ticks/220 */
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
                EZLOG( "source mapping live (cbuf=%d inputsys=%d)", g_cbuf ? 1 : 0, g_input ? 1 : 0 );
        }
}

#endif
