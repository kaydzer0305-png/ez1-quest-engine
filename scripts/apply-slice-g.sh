#!/bin/sh
# Apply slice G glue that is awkward to push as full 60k+ files.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ROOT" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])

sd = (root / "appframework/sdlmgr.cpp").read_text(encoding="utf-8", errors="replace")
old = """#if defined( ANDROID )
extern \"C\" int EZQuestVrBindEngineContext( void );
extern \"C\" int EZQuestVrSubmitEngineFrame( unsigned srcTex, int width, int height );
#endif"""
new = """#if defined( ANDROID )
extern \"C\" int EZQuestVrBindEngineContext( void );
extern \"C\" int EZQuestVrSubmitEngineFrame( unsigned srcTex, int width, int height );
extern \"C\" int EZQuestVrStereoEyesReady( void );
#endif"""
if old not in sd:
    raise SystemExit("sdlmgr extern block not found")
sd = sd.replace(old, new, 1)
old = """#if defined( ANDROID )
\tif ( EZQuestVrSubmitEngineFrame( params->m_srcTexName, params->m_width, params->m_height ) )
\t{
\t\tm_flPrevGLSwapWindowTime = 0.0;
\t\tCheckGLError( __LINE__ );
\t\treturn;
\t}
#endif"""
new = """#if defined( ANDROID )
\tif ( EZQuestVrStereoEyesReady() )
\t{
\t\tm_flPrevGLSwapWindowTime = 0.0;
\t\tCheckGLError( __LINE__ );
\t\treturn;
\t}
\tif ( EZQuestVrSubmitEngineFrame( params->m_srcTexName, params->m_width, params->m_height ) )
\t{
\t\tm_flPrevGLSwapWindowTime = 0.0;
\t\tCheckGLError( __LINE__ );
\t\treturn;
\t}
#endif"""
if old not in sd:
    raise SystemExit("sdlmgr ShowPixels block not found")
sd = sd.replace(old, new, 1)
(root / "appframework/sdlmgr.cpp").write_text(sd, encoding="utf-8")
print("patched appframework/sdlmgr.cpp")

sysd = (root / "engine/sys_dll2.cpp").read_text(encoding="utf-8", errors="replace")
old = """\t\t\tm_bSupportsVR = modinfo->GetInt( \"supportsvr\" ) > 0 && CommandLine()->CheckParm( \"-vr\" );
\t\t\tif ( m_bSupportsVR )
\t\t\t{
\t\t\t\t// This also has to happen before CreateGameWindow to know where to put
\t\t\t\t// the window and how big to make it
\t\t\t\tif ( InitVR() )
\t\t\t\t{
\t\t\t\t\tif ( Steam3Client().SteamUtils() )
\t\t\t\t\t{
\t\t\t\t\t\tif ( Steam3Client().SteamUtils()->IsSteamRunningInVR() && g_pSourceVR->IsHmdConnected() )
\t\t\t\t\t\t{
\t\t\t\t\t\t\tint nForceVRAdapterIndex = g_pSourceVR->GetVRModeAdapter();
\t\t\t\t\t\t\tmaterials->SetAdapter( nForceVRAdapterIndex, 0 );

\t\t\t\t\t\t\tg_pSourceVR->SetShouldForceVRMode();
\t\t\t\t\t\t}
\t\t\t\t\t}
\t\t\t\t}
\t\t\t}"""
new = """#if defined( ANDROID )
\t\t\tm_bSupportsVR = true;
#else
\t\t\tm_bSupportsVR = modinfo->GetInt( \"supportsvr\" ) > 0 && CommandLine()->CheckParm( \"-vr\" );
#endif
\t\t\tif ( m_bSupportsVR )
\t\t\t{
\t\t\t\t// This also has to happen before CreateGameWindow to know where to put
\t\t\t\t// the window and how big to make it
\t\t\t\tif ( InitVR() )
\t\t\t\t{
#if defined( ANDROID )
\t\t\t\t\tif ( g_pSourceVR )
\t\t\t\t\t{
\t\t\t\t\t\tg_pSourceVR->SetShouldForceVRMode();
\t\t\t\t\t\tg_pSourceVR->Activate();
\t\t\t\t\t}
#endif
\t\t\t\t\tif ( Steam3Client().SteamUtils() )
\t\t\t\t\t{
\t\t\t\t\t\tif ( Steam3Client().SteamUtils()->IsSteamRunningInVR() && g_pSourceVR->IsHmdConnected() )
\t\t\t\t\t\t{
\t\t\t\t\t\t\tint nForceVRAdapterIndex = g_pSourceVR->GetVRModeAdapter();
\t\t\t\t\t\t\tmaterials->SetAdapter( nForceVRAdapterIndex, 0 );

\t\t\t\t\t\t\tg_pSourceVR->SetShouldForceVRMode();
\t\t\t\t\t\t}
\t\t\t\t\t}
\t\t\t\t}
\t\t\t}"""
if old not in sysd:
    raise SystemExit("sys_dll2 VR block not found")
sysd = sysd.replace(old, new, 1)
(root / "engine/sys_dll2.cpp").write_text(sysd, encoding="utf-8")
print("patched engine/sys_dll2.cpp")
PY
