/*
Copyright (C) 2026 kaydzer0305-png
Android ISourceVirtualReality backend. Reads EZQuestVrCopyTracking().
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "sourcevr/isourcevirtualreality.h"
#include "tier3/tier3.h"
#include "tier1/interface.h"
#include "mathlib/vmatrix.h"

#include <dlfcn.h>
#include <string.h>
#include <math.h>
#include <android/log.h>

#define EZTAG "EZQuest-SourceVR"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )

struct XrQuaternionf { float x, y, z, w; };
struct XrVector3f { float x, y, z; };
struct XrPosef { XrQuaternionf orientation; XrVector3f position; };
struct XrFovf { float angleLeft, angleRight, angleUp, angleDown; };
struct XrView { int type; const void *next; XrPosef pose; XrFovf fov; };

struct EzVrTrackingSnapshot {
        int valid;
        int eyeCount;
        unsigned width;
        unsigned height;
        XrView views[2];
        float view[2][16];
        float viewInv[2][16];
        float proj[2][16];
        float midPos[3];
        float midQuat[4];
};

typedef int (*CopyTrackingFn)( EzVrTrackingSnapshot *out );
static const float IN_PER_M = 39.3700787f;

static void PoseToVMatrix( VMatrix &out, const float q[4], const float p[3] )
{
        const float x=q[0], y=q[1], z=q[2], w=q[3];
        const float xx=x*x, yy=y*y, zz=z*z, xy=x*y, xz=x*z, yz=y*z, wx=w*x, wy=w*y, wz=w*z;
        float r[3][3];
        r[0][0]=1.f-2.f*(yy+zz); r[0][1]=2.f*(xy-wz); r[0][2]=2.f*(xz+wy);
        r[1][0]=2.f*(xy+wz); r[1][1]=1.f-2.f*(xx+zz); r[1][2]=2.f*(yz-wx);
        r[2][0]=2.f*(xz-wy); r[2][1]=2.f*(yz+wx); r[2][2]=1.f-2.f*(xx+yy);
        out.Init(
                r[0][0], -r[2][0], r[1][0], p[0]*IN_PER_M,
                r[0][2], -r[2][2], r[1][2], -p[2]*IN_PER_M,
                r[0][1], -r[2][1], r[1][1], p[1]*IN_PER_M,
                0, 0, 0, 1 );
}

static void FovToProj( VMatrix *out, const XrFovf &fov, float zn, float zf )
{
        const float tL=tanf(fov.angleLeft), tR=tanf(fov.angleRight);
        const float tU=tanf(fov.angleUp), tD=tanf(fov.angleDown);
        const float idx=(tR-tL)!=0.f?1.f/(tR-tL):1.f;
        const float idy=(tU-tD)!=0.f?1.f/(tU-tD):1.f;
        const float A=zf/(zn-zf), B=zn*A;
        out->Init( 2.f*idx, 0, (tR+tL)*idx, 0,
                   0, 2.f*idy, (tU+tD)*idy, 0,
                   0, 0, A, B,
                   0, 0, -1.f, 0 );
}

class CEzQuestSourceVR : public CTier3AppSystem< ISourceVirtualReality >
{
        typedef CTier3AppSystem< ISourceVirtualReality > BaseClass;
public:
        CEzQuestSourceVR() : m_active(false), m_force(false), m_copy(NULL) { memset(&m_snap,0,sizeof m_snap); }
        virtual bool Connect( CreateInterfaceFn factory ) { BaseClass::Connect(factory); Resolve(); return true; }
        virtual void Disconnect() { BaseClass::Disconnect(); }
        virtual void *QueryInterface( const char *name )
        {
                if ( name && !strcmp( name, SOURCE_VIRTUAL_REALITY_INTERFACE_VERSION ) ) return this;
                return BaseClass::QueryInterface( name );
        }
        virtual InitReturnVal_t Init() { Resolve(); EZLOG("init copy=%p", (void*)m_copy); return INIT_OK; }
        virtual void Shutdown() {}
        virtual bool ShouldRunInVR() { return m_active || Ok(); }
        virtual bool IsHmdConnected() { return Ok() || m_force; }
        virtual void GetViewportBounds( VREye, int *x, int *y, int *w, int *h )
        {
                Refresh();
                if (x) *x=0; if (y) *y=0;
                if (w) *w = m_snap.width ? (int)m_snap.width : 1440;
                if (h) *h = m_snap.height ? (int)m_snap.height : 1440;
        }
        virtual bool DoDistortionProcessing( VREye ) { return false; }
        virtual bool CompositeHud( VREye, float[4], bool, bool, bool ) { return false; }
        virtual VMatrix GetMideyePose()
        {
                Refresh(); VMatrix m; m.Identity();
                if ( m_snap.valid ) PoseToVMatrix( m, m_snap.midQuat, m_snap.midPos );
                return m;
        }
        virtual bool SampleTrackingState( float, float ) { return Refresh(); }
        virtual bool GetEyeProjectionMatrix( VMatrix *out, VREye eye, float zn, float zf, float )
        {
                if (!out) return false;
                Refresh();
                if ( m_snap.valid ) FovToProj( out, m_snap.views[eye==VREye_Right?1:0].fov, zn, zf );
                else out->Identity();
                return m_snap.valid != 0;
        }
        virtual bool WillDriftInYaw() { return false; }
        virtual bool GetDisplayBounds( VRRect_t *r )
        {
                if (!r) return false; Refresh();
                r->nX=0; r->nY=0;
                r->nWidth = m_snap.width ? (int)m_snap.width*2 : 2880;
                r->nHeight = m_snap.height ? (int)m_snap.height : 1440;
                return true;
        }
        virtual VMatrix GetMidEyeFromEye( VREye eye )
        {
                Refresh(); VMatrix m; m.Identity();
                if (!m_snap.valid || m_snap.eyeCount<2) return m;
                const int i = eye==VREye_Right?1:0;
                m[0][3] = (m_snap.views[i].pose.position.x - m_snap.midPos[0]) * IN_PER_M;
                m[1][3] = (-(m_snap.views[i].pose.position.z - m_snap.midPos[2])) * IN_PER_M;
                m[2][3] = (m_snap.views[i].pose.position.y - m_snap.midPos[1]) * IN_PER_M;
                return m;
        }
        virtual int GetVRModeAdapter() { return 0; }
        virtual void CreateRenderTargets( IMaterialSystem * ) {}
        virtual void ShutdownRenderTargets() {}
        virtual ITexture *GetRenderTarget( VREye, EWhichRenderTarget ) { return NULL; }
        virtual void GetRenderTargetFrameBufferDimensions( int &w, int &h )
        {
                Refresh();
                w = m_snap.width ? (int)m_snap.width : 1440;
                h = m_snap.height ? (int)m_snap.height : 1440;
        }
        virtual bool Activate() { m_active = m_force = true; EZLOG("Activate"); return true; }
        virtual void Deactivate() { m_active = false; }
        virtual bool ShouldForceVRMode() { return m_force || Ok(); }
        virtual void SetShouldForceVRMode() { m_force = true; }
private:
        void Resolve()
        {
                if (m_copy) return;
                m_copy = (CopyTrackingFn)dlsym( RTLD_DEFAULT, "EZQuestVrCopyTracking" );
                if (!m_copy) {
                        void *h = dlopen( "liblauncher.so", RTLD_NOW|RTLD_NOLOAD );
                        if (!h) h = dlopen( "liblauncher.so", RTLD_NOW );
                        if (h) m_copy = (CopyTrackingFn)dlsym( h, "EZQuestVrCopyTracking" );
                }
        }
        bool Refresh() { Resolve(); return m_copy ? m_copy(&m_snap)!=0 : false; }
        bool Ok() { Refresh(); return m_snap.valid!=0; }
        bool m_active, m_force;
        CopyTrackingFn m_copy;
        EzVrTrackingSnapshot m_snap;
};

static CEzQuestSourceVR g_SourceVR;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CEzQuestSourceVR, ISourceVirtualReality,
        SOURCE_VIRTUAL_REALITY_INTERFACE_VERSION, g_SourceVR );

#endif
