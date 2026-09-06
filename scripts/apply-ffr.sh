#!/bin/sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ROOT" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])
path = root / 'launcher/android/vr_main.cpp'
text = path.read_text(encoding='utf-8', errors='replace')
changed = False

if 'ezquest_vr_ffr.h' not in text:
    needle = '#include "ezquest_vr_sourceinput.h"'
    if needle not in text:
        raise SystemExit('vr_main include block not found')
    text = text.replace(needle, needle + '\n#include "ezquest_vr_ffr.h"', 1)
    changed = True

if 'enabled[16]' not in text:
    if 'const char *enabled[8];' not in text:
        raise SystemExit('enabled[] array not found')
    text = text.replace('const char *enabled[8];', 'const char *enabled[16];', 1)
    changed = True

if 'EZQuestVrAppendOptionalFbExts' not in text:
    needle = '        free( props );'
    if needle not in text:
        raise SystemExit('free(props) not found')
    text = text.replace(
        needle,
        '        EZQuestVrAppendOptionalFbExts( props, extCount, enabled, &enabledCount, 16 );\n' + needle,
        1)
    changed = True

if 'EZQuestVrApplyFfrAndRefresh' not in text:
    needle = '''        }
        return true;
}

// ---------------------------------------------------------------------------
// Event pump
// ---------------------------------------------------------------------------
'''
    if needle not in text:
        raise SystemExit('EzCreateSwapchains tail not found')
    text = text.replace(
        needle,
        '''        }
        EZQuestVrApplyFfrAndRefresh( g_app.instance, g_app.session,
                        g_app.colorSwapchain[0], g_app.colorSwapchain[1] );
        return true;
}

// ---------------------------------------------------------------------------
// Event pump
// ---------------------------------------------------------------------------
''',
        1)
    changed = True

if changed:
    path.write_text(text, encoding='utf-8')
    print('patched launcher/android/vr_main.cpp')
else:
    print('vr_main already patched')
PY
