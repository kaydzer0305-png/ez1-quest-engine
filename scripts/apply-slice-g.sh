#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ROOT" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])

sd_path = root / 'appframework/sdlmgr.cpp'
sd = sd_path.read_text(encoding='utf-8', errors='replace')
if 'EZQuestVrStereoEyesReady' not in sd:
    a = 'extern "C" int EZQuestVrSubmitEngineFrame( unsigned srcTex, int width, int height );'
    b = a + '\nextern "C" int EZQuestVrStereoEyesReady( void );'
    if a not in sd:
        raise SystemExit('sdlmgr extern SubmitEngineFrame not found')
    sd = sd.replace(a, b, 1)
    c = 'if ( EZQuestVrSubmitEngineFrame( params->m_srcTexName, params->m_width, params->m_height ) )'
    d = 'if ( EZQuestVrStereoEyesReady() )\n\t{\n\t\tm_flPrevGLSwapWindowTime = 0.0;\n\t\tCheckGLError( __LINE__ );\n\t\treturn;\n\t}\n\tif ( EZQuestVrSubmitEngineFrame( params->m_srcTexName, params->m_width, params->m_height ) )'
    if c not in sd:
        raise SystemExit('sdlmgr ShowPixels submit not found')
    sd = sd.replace(c, d, 1)
    sd_path.write_text(sd, encoding='utf-8')
    print('patched appframework/sdlmgr.cpp')
else:
    print('sdlmgr already patched')

sys_path = root / 'engine/sys_dll2.cpp'
sysd = sys_path.read_text(encoding='utf-8', errors='replace')
needle = 'm_bSupportsVR = modinfo->GetInt( "supportsvr" ) > 0 && CommandLine()->CheckParm( "-vr" );'
if 'm_bSupportsVR = true' not in sysd:
    if needle not in sysd:
        raise SystemExit('sys_dll2 supportsvr assignment not found')
    sysd = sysd.replace(needle, '#if defined( ANDROID )\n\t\t\tm_bSupportsVR = true;\n#else\n\t\t\t' + needle + '\n#endif', 1)
    old = 'if ( InitVR() )'
    # only the first InitVR block in OnStartup needs the Activate insert; do a unique tail
    key = 'if ( Steam3Client().SteamUtils() )'
    idx = sysd.find('if ( InitVR() )')
    if idx < 0:
        raise SystemExit('InitVR not found')
    idx2 = sysd.find(key, idx)
    if idx2 < 0:
        raise SystemExit('SteamUtils after InitVR not found')
    insert = '''#if defined( ANDROID )
\t\t\t\t\tif ( g_pSourceVR )
\t\t\t\t\t{
\t\t\t\t\t\tg_pSourceVR->SetShouldForceVRMode();
\t\t\t\t\t\tg_pSourceVR->Activate();
\t\t\t\t\t}
#endif\n\t\t\t\t\t'''
    sysd = sysd[:idx2] + insert + sysd[idx2:]
    sys_path.write_text(sysd, encoding='utf-8')
    print('patched engine/sys_dll2.cpp')
else:
    print('sys_dll2 already patched')
PY
