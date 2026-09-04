/*
Copyright (C) 2026 kaydzer0305-png
Android ISourceVirtualReality backend. Reads EZQuestVrCopyTracking()
and allocates per-eye Source render targets for the client stereo loop.
*/

#if defined( ANDROID ) || defined( __ANDROID__ )

#include "sourcevr/isourcevirtualreality.h"
#include "tier3/tier3.h"
#include "tier1/interface.h"
#include "mathlib/vmatrix.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"

#include <dlfcn.h>
#include <string.h>
#include <math.h>
#include <android/log.h>

#define EZTAG "EZQuest-SourceVR"
#define EZLOG( ... ) __android_log_print( ANDROID_LOG_INFO, EZTAG, __VA_ARGS__ )
#define EZERR( ... ) __android_log_print( ANDROID_LOG_ERROR, EZTAG, __VA_ARGS__ )

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
typedef int (*SubmitEyeFn)( int eye, int width, int height );
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
        CEzQuestSourceVR()
                : m_active(false), m_force(false), m_copy(NULL), m_submitEye(NULL)
        {
                memset(&m_snap,0,sizeof m_snap);
                m_rtColor[0] = m_rtColor[1] = NULL;
                m_rtDepth[0] = m_rtDepth[1] = NULL;
                m_rtW = m_rtH = 0;
        }
        virtual bool Connect( CreateInterfaceFn factory ) { BaseClass::Connect(factory); Resolve(); return true; }
        virtual void Disconnect() { BaseClass::Disconnect(); }
        virtual void *QueryInterface( const char *name )
        {
                if ( name && !strcmp( name, SOURCE_VIRTUAL_REALITY_INTERFACE_VERSION ) ) return this;
                return BaseClass::QueryInterface( name );
        }
        virtual InitReturnVal_t Init()
        {
                Resolve();
                m_active = m_force = true;
                EZLOG("init copy=%p submit=%p (VR forced on)", (void*)m_copy, (void*)m_submitEye);
                return INIT_OK;
        }
        virtual void Shutdown() { ShutdownRenderTargets(); }
        virtual bool ShouldRunInVR() { return true; }
        virtual bool IsHmdConnected() { return true; }
        virtual void GetViewportBounds( VREye eye, int *x, int *y, int *w, int *h )
        {
                Refresh();
                const int ew = EyeW();
                const int eh = EyeH();
                if ( m_rtColor[0] )
                {
                        if (x) *x=0; if (y) *y=0;
                        if (w) *w=ew; if (h) *h=eh;
                        return;
                }
                if (x) *x = (eye==VREye_Right) ? ew : 0;
                if (y) *y = 0;
                if (w) *w = ew;
                if (h) *h = eh;
        }
        virtual bool DoDistortionProcessing( VREye eye )
        {
                Resolve();
                Refresh();
                const int i = eye==VREye_Right ? 1 : 0;
                if ( m_submitEye )
                {
                        m_submitEye( i, EyeW(), EyeH() );
                        return true;
                }
                return false;
        }
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
                r->nWidth = EyeW() * 2;
                r->nHeight = EyeH();
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
        virtual void CreateRenderTargets( IMaterialSystem *pMaterialSystem )
        {
                if ( !pMaterialSystem )
                        return;
                Refresh();
                const int w = EyeW();
                const int h = EyeH();
                if ( m_rtColor[0] && m_rtW == w && m_rtH == h )
                        return;
                ShutdownRenderTargets();
                static const char *kColor[2] = { "_rt_ezquest_eye_left", "_rt_ezquest_eye_right" };
                static const char *kDepth[2] = { "_rt_ezquest_eye_left_z", "_rt_ezquest_eye_right_z" };
                for ( int i = 0; i < 2; i++ )
                {
                        ITexture *color = pMaterialSystem->CreateNamedRenderTargetTextureEx2(
                                kColor[i], w, h, RT_SIZE_LITERAL,
                                pMaterialSystem->GetBackBufferFormat(),
                                MATERIAL_RT_DEPTH_SEPARATE,
                                TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP,
                                0 );
                        ITexture *depth = pMaterialSystem->CreateNamedRenderTargetTextureEx2(
                                kDepth[i], w, h, RT_SIZE_LITERAL,
                                IMAGE_FORMAT_NV_DST24,
                                MATERIAL_RT_DEPTH_NONE,
                                TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP,
                                0 );
                        m_rtColor[i] = color;
                        m_rtDepth[i] = depth;
                        if ( color )
                                color->IncrementReferenceCount();
                        if ( depth )
                                depth->IncrementReferenceCount();
                        EZLOG( "RT %s color=%p depth=%p %dx%d", kColor[i], (void*)color, (void*)depth, w, h );
                }
                m_rtW = w;
                m_rtH = h;
        }
        virtual void ShutdownRenderTargets()
        {
                for ( int i = 0; i < 2; i++ )
                {
                        if ( m_rtColor[i] ) { m_rtColor[i]->DecrementReferenceCount(); m_rtColor[i] = NULL; }
                        if ( m_rtDepth[i] ) { m_rtDepth[i]->DecrementReferenceCount(); m_rtDepth[i] = NULL; }
                }
                m_rtW = m_rtH = 0;
        }
        virtual ITexture *GetRenderTarget( VREye eye, EWhichRenderTarget which )
        {
                const int i = eye==VREye_Right ? 1 : 0;
                if ( which == RT_Depth )
                        return m_rtDepth[i];
                return m_rtColor[i];
        }
        virtual void GetRenderTargetFrameBufferDimensions( int &w, int &h )
        {
                Refresh();
                w = EyeW();
                h = EyeH();
        }
        virtual bool Activate() { m_active = m_force = true; EZLOG("Activate"); return true; }
        virtual void Deactivate() { m_active = false; }
        virtual bool ShouldForceVRMode() { return true; }
        virtual void SetShouldForceVRMode() { m_force = true; }
private:
        int EyeW() const { return m_snap.width ? (int)m_snap.width : 1440; }
        int EyeH() const { return m_snap.height ? (int)m_snap.height : 1440; }
        void Resolve()
        {
                if (!m_copy)
                {
                        m_copy = (CopyTrackingFn)dlsym( RTLD_DEFAULT, "EZQuestVrCopyTracking" );
                        if (!m_copy) {
                                void *h = dlopen( "liblauncher.so", RTLD_NOW|RTLD_NOLOAD );
                                if (!h) h = dlopen( "liblauncher.so", RTLD_NOW );
                                if (h) m_copy = (CopyTrackingFn)dlsym( h, "EZQuestVrCopyTracking" );
                        }
                }
                if (!m_submitEye)
                {
                        m_submitEye = (SubmitEyeFn)dlsym( RTLD_DEFAULT, "EZQuestVrSubmitEngineEyeFromCurrentFbo" );
                        if (!m_submitEye) {
                                void *h = dlopen( "liblauncher.so", RTLD_NOW|RTLD_NOLOAD );
                                if (!h) h = dlopen( "liblauncher.so", RTLD_NOW );
                                if (h) m_submitEye = (SubmitEyeFn)dlsym( h, "EZQuestVrSubmitEngineEyeFromCurrentFbo" );
                        }
                }
        }
        bool Refresh() { Resolve(); return m_copy ? m_copy(&m_snap)!=0 : false; }
        bool m_active, m_force;
        CopyTrackingFn m_copy;
        SubmitEyeFn m_submitEye;
        EzVrTrackingSnapshot m_snap;
        ITexture *m_rtColor[2];
        ITexture *m_rtDepth[2];
        int m_rtW, m_rtH;
};

static CEzQuestSourceVR g_SourceVR;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CEzQuestSourceVR, ISourceVirtualReality,
        SOURCE_VIRTUAL_REALITY_INTERFACE_VERSION, g_SourceVR );

#endif
