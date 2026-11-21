#!/bin/bash
set -eu
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
TMP="$ROOT/builds/v5/v5.7/src-extract"
HOS="$ROOT/handshakeos"
cp -f "$TMP/emu_sys.c"          "$HOS/src/emu_sys.c"
cp -f "$TMP/audio.c"            "$HOS/src/audio.c"
cp -f "$TMP/hs_audio.h"         "$HOS/include/hs_audio.h"
cp -f "$TMP/test_audio_pace.c"  "$HOS/tests/test_audio_pace.c"
cp -f "$TMP/test_snes_audio.c"  "$HOS/tests/test_snes_audio.c"
mkdir -p "$HOS/docs"
cp -f "$TMP/elfie-snes-audio-brief.md" "$HOS/docs/elfie-snes-audio-brief.md"
# handshakeos snes tests if present
if [ -d "$HOS/cores/snes/tests" ]; then
  cp -f "$TMP/test_hs_snes.c" "$HOS/cores/snes/tests/test_hs_snes.c"
fi
# confirm
grep -n "357368u" "$HOS/src/emu_sys.c" "$HOS/tests/test_audio_pace.c"
grep -n "hs_audio_ring_fill" "$HOS/include/hs_audio.h" "$HOS/src/audio.c"
echo "FINISH_APPLY_OK"
# dump how tests are built
echo "======= Makefile test hints ======="
grep -n -E 'test_audio_pace|test_hs_snes|TESTS|snes' "$HOS/Makefile" | head -60
echo "======= tests dir ======="
ls "$HOS/tests" | head
if [ -f "$HOS/tests/Makefile" ]; then echo "--- tests/Makefile ---"; grep -n -E 'pace|snes|audio' "$HOS/tests/Makefile" | head; fi
# existing run scripts
ls "$HOS/tools" | head
