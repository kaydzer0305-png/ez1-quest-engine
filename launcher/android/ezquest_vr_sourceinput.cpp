/*
Copyright (C) 2026 kaydzer0305-png

Maps Quest Touch (EzVrInputState) onto Source movement/attack commands.
Does not use HID/SDL joystick paths.

SOURCEVR_GAME selects the layout.

EZ1 keeps its plan-Phase-3 extras (Bad Cop kick on +alt1, nightvision on a
long-press of the face button via impulse 100, the manhack-toss weapon on
slot5). That path is unchanged.

EZ2 follows vr_bindings_ez2.cfg section 2 (the VR action table), not the EZ1
chords. OpenXR actions are created in vr_input.cpp; this file is what turns
them into +attack / +use / blink commands. The Touch profile has float trigger
and squeeze values and no click paths, so presses are analog thresholds.
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "ezquest_vr_sourceinput.h"
#include "vr_input.h"

#include <android/log.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "inputsystem/iinputsystem.h"
#include "icvar.h"
#include "tier1/convar.h"
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

static void ApplyTurn( float rx, int snap, float degrees )
{
        if ( snap )
        {
                const float gate = 0.68f;
                if ( fabsf( rx ) > gate && !g_snapLatched )
                {
                        if ( degrees < 15.f ) degrees = 15.f;
                        if ( degrees > 90.f ) degrees = 90.f;
                        const float ticks = degrees / 0.022f;
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
}

// Live menu values. Retried until vstdlib's ICVar is up; a miss returns the
// catalog default so the first frames before the game DLL registers still
// match vr_defaults.cfg (smooth off, snap on, 30 degrees).
static ICvar *GetCvar( void )
{
        static ICvar *cvar = NULL;
        if ( cvar )
                return cvar;
        const char *libs[] = { "libvstdlib.so", "libengine.so", NULL };
        for ( int i = 0; libs[i]; i++ )
        {
                void *h = dlopen( libs[i], RTLD_NOW | RTLD_NOLOAD );
                if ( !h )
                        h = dlopen( libs[i], RTLD_NOW );
                if ( !h )
                        continue;
                CreateInterfaceFn factory = (CreateInterfaceFn)dlsym( h, "CreateInterface" );
                if ( !factory )
                        continue;
                cvar = (ICvar *)factory( CVAR_INTERFACE_VERSION, NULL );
                if ( cvar )
                {
                        EZLOG( "resolved ICvar from %s", libs[i] );
                        return cvar;
                }
        }
        return NULL;
}

static int CvarInt( const char *name, int deflt )
{
        ICvar *cvar = GetCvar();
        if ( !cvar || !name )
                return deflt;
        ConVar *cv = cvar->FindVar( name );
        if ( !cv )
                return deflt;
        return cv->GetInt();
}

} // namespace

// EZ1 chord layout. Kept as it was: kick, nightvision long-press, manhack on
// slot5. Not used when SOURCEVR_GAME is EntropyZero2.
static void SyncEZ1( const EzVrController &L, const EzVrController &R )
{
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
                SendCmd( "slot5\n" );
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

        float deg = 30.f;
        const char *ds = getenv( "EZQUEST_VR_SNAP_DEGREES" );
        if ( ds && ds[0] )
                deg = (float)atof( ds );
        ApplyTurn( rx, EnvTruthy( "EZQUEST_VR_SNAP_TURN", 1 ), deg );
}

// vr_bindings_ez2.cfg section 2. Strings in kEz2Groups are the keyboard
// fallback chains, one arsenal group per right-A press, in that file's order.
static int g2_fwd, g2_back, g2_ml, g2_mr, g2_speed;
static int g2_atk, g2_atk2, g2_use, g2_reload, g2_scores, g2_menu;
static int g2_a, g2_b, g2_x, g2_flash;
static int g2_slot;
static int g2_blinkArm = 1;

static const char *const kEz2Groups[] = {
        "use weapon_crowbar; use weapon_stunstick\n",
        "use weapon_displacer_pistol; use weapon_pulsepistol; use weapon_357\n",
        "use weapon_smg1; use weapon_smg2; use weapon_oicw; use weapon_ar2_proto\n",
        "use weapon_shotgun; use weapon_crossbow; use weapon_physcannon\n",
        "use weapon_frag; use weapon_hopwire\n",
        "use weapon_slam\n",
};

static void EdgeCmd( int *latch, int down, const char *cmd )
{
        if ( down && !*latch )
        {
                SendCmd( cmd );
                *latch = 1;
        }
        else if ( !down )
        {
                *latch = 0;
        }
}

static void SyncEZ2( const EzVrController &L, const EzVrController &R )
{
        // Catalog defaults: grip threshold 0.6, sprint floor 0.85. Trigger and
        // squeeze are floats on this profile (see vr_input.cpp).
        const float press = 0.60f;
        const float dead = 0.25f;
        const float sprintFloor = 0.85f;
        const int rTrig = R.trigger > press;
        const int rGrip = R.grip > press;
        const int lTrig = L.trigger > press;
        const int lGrip = L.grip > press;
        const float ly = L.active ? L.stickY : 0.f;
        const float lx = L.active ? L.stickX : 0.f;
        const float rx = R.active ? R.stickX : 0.f;

        Hold( &g2_atk, rTrig, "+attack\n", "-attack\n" );
        Hold( &g2_atk2, rGrip, "+attack2\n", "-attack2\n" );
        Hold( &g2_use, lTrig, "+use\n", "-use\n" );
        Hold( &g2_reload, lGrip, "+reload\n", "-reload\n" );
        Hold( &g2_scores, L.secondaryButton, "+showscores\n", "-showscores\n" );
        Hold( &g2_menu, L.menuButton || R.menuButton, "cancelselect\n", "" );

        if ( R.primaryButton && !g2_a )
        {
                SendCmd( kEz2Groups[g2_slot] );
                EZLOG( "arsenal group %d", g2_slot + 1 );
                g2_slot = ( g2_slot + 1 ) % (int)( sizeof kEz2Groups / sizeof kEz2Groups[0] );
                g2_a = 1;
        }
        else if ( !R.primaryButton )
        {
                g2_a = 0;
        }

        EdgeCmd( &g2_b, R.secondaryButton, "impulse 101\n" );
        EdgeCmd( &g2_x, L.primaryButton, "lastinv\n" );
        EdgeCmd( &g2_flash, R.stickClick, "impulse 100\n" );

        // 0 smooth, 1 teleport (~2.4 m), 2 dash (~4.8 m blink + shove).
        // Read live so a menu change of ez2vr_locomotion_mode takes effect.
        int loco = CvarInt( "ez2vr_locomotion_mode", 0 );
        if ( loco < 0 || loco > 2 )
                loco = 0;

        const int moving = ( ly > dead || ly < -dead || lx > dead || lx < -dead );
        const float mag = sqrtf( lx * lx + ly * ly );
        if ( loco == 0 )
        {
                Hold( &g2_fwd,  ly >  dead, "+forward\n", "-forward\n" );
                Hold( &g2_back, ly < -dead, "+back\n", "-back\n" );
                Hold( &g2_ml,   lx < -dead, "+moveleft\n", "-moveleft\n" );
                Hold( &g2_mr,   lx >  dead, "+moveright\n", "-moveright\n" );
                Hold( &g2_speed, L.stickClick || ( moving && mag >= sprintFloor ),
                        "+speed\n", "-speed\n" );
                // Don't fire a blink on the frame the menu switches into
                // teleport while the stick is already deflected.
                g2_blinkArm = ( mag < 0.35f ) ? 1 : 0;
        }
        else
        {
                Hold( &g2_fwd, 0, "+forward\n", "-forward\n" );
                Hold( &g2_back, 0, "+back\n", "-back\n" );
                Hold( &g2_ml, 0, "+moveleft\n", "-moveleft\n" );
                Hold( &g2_mr, 0, "+moveright\n", "-moveright\n" );
                Hold( &g2_speed, 0, "+speed\n", "-speed\n" );

                const float gate = 0.65f;
                if ( mag > gate && g2_blinkArm )
                {
                        // Stick direction, not a 4-way snap. ly>0 is +forward
                        // and lx>0 is +moveright, same signs as smooth mode.
                        // ez2vr_blink normalizes against flattened eye yaw.
                        const float dist = ( loco == 2 ) ? 192.f : 96.f;
                        const int dash = ( loco == 2 ) ? 1 : 0;
                        char buf[96];
                        snprintf( buf, sizeof buf, "ez2vr_blink %.3f %.3f %.0f %d\n",
                                ly, lx, dist, dash );
                        SendCmd( buf );
                        g2_blinkArm = 0;
                }
                else if ( mag < 0.35f )
                {
                        g2_blinkArm = 1;
                }
        }

        const int snap = CvarInt( "ez2vr_turn_mode", 0 ) == 0;
        ApplyTurn( rx, snap, (float)CvarInt( "ez2vr_turn_snap_angle", 30 ) );
}

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

        if ( IsEZ2() )
                SyncEZ2( L, R );
        else
                SyncEZ1( L, R );

        if ( !g_logged )
        {
                g_logged = 1;
                EZLOG( "source mapping live (cbuf=%d inputsys=%d %s)", g_cbuf ? 1 : 0, g_input ? 1 : 0,
                       IsEZ2() ? "ez2-action-table" : "ez1-kick/nv/manhack" );
        }
}

#endif
