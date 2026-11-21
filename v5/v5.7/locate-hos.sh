#!/bin/bash
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
echo "handshakeos top:"
ls "$ROOT/handshakeos" | head -40
echo "--- find emu_sys.c ---"
# timeout-friendly, depth first
find "$ROOT/handshakeos" -maxdepth 4 -name 'emu_sys.c' -print 2>/dev/null
find "$ROOT/handshakeos" -maxdepth 4 -name 'hs_audio.h' -print 2>/dev/null
find "$ROOT/handshakeos" -maxdepth 4 -name 'audio.c' -print 2>/dev/null
find "$ROOT/handshakeos" -maxdepth 4 -name 'test_audio_pace.c' -print 2>/dev/null
find "$ROOT/handshakeos" -maxdepth 4 -name 'play.c' -print 2>/dev/null
echo "--- cores/snes after apply ---"
ls -l "$ROOT/handshakeos/cores/snes/snes_internal.h" "$ROOT/hs-cores/snes/snes_internal.h"
grep -n SNES_MIX_HEADROOM_SHIFT "$ROOT/hs-cores/snes/snes_internal.h" "$ROOT/handshakeos/cores/snes/snes_internal.h"
echo "--- which files exist under handshakeos ---"
find "$ROOT/handshakeos" -maxdepth 2 -type d -print
