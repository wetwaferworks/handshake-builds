#!/bin/bash
# Apply v5.7 SNES audio patches into both trees. No git. No power-off.
set -eu
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
TAR="$ROOT/builds/v5/v5.7/snes-audio-v57.tar"
STAMP="$ROOT/builds/v5/v5.7"
TMP="$STAMP/src-extract"
mkdir -p "$TMP"
tar -xf "$TAR" -C "$TMP"
echo "EXTRACTED $(ls -1 "$TMP" | tr '\n' ' ')"

# locate trees
HSC="$ROOT/hs-cores/snes"
HOS_SNES=""
for c in "$ROOT/handshakeos/cores/snes" "$ROOT/handshakeos/src/cores/snes"; do
  if [ -f "$c/snes_apu.c" ]; then HOS_SNES=$c; break; fi
done
EMU_SYS=$(find "$ROOT/handshakeos" -name emu_sys.c -not -path '*/.*' | head -1)
AUDIO_C=$(find "$ROOT/handshakeos" -name audio.c -path '*audio*' -o -name audio.c | head -1)
# be more precise
AUDIO_C=$(find "$ROOT/handshakeos" -name audio.c -not -path '*/.*' | head -1)
AUDIO_H=$(find "$ROOT/handshakeos" -name hs_audio.h -not -path '*/.*' | head -1)
PACE=$(find "$ROOT/handshakeos" -name test_audio_pace.c -not -path '*/.*' | head -1)
HOS_TEST_DIR=$(dirname "$PACE")

echo "HSC=$HSC"
echo "HOS_SNES=$HOS_SNES"
echo "EMU_SYS=$EMU_SYS"
echo "AUDIO_C=$AUDIO_C"
echo "AUDIO_H=$AUDIO_H"
echo "PACE=$PACE"
echo "HOS_TEST_DIR=$HOS_TEST_DIR"

# backup current (for old-hash hunt we copy hashes first)
# apply
cp -f "$TMP/snes_internal.h" "$HSC/snes_internal.h"
cp -f "$TMP/hs_snes.h"       "$HSC/hs_snes.h"
cp -f "$TMP/snes_apu.c"      "$HSC/snes_apu.c"
cp -f "$TMP/hs_snes.c"       "$HSC/hs_snes.c"
cp -f "$TMP/test_hs_snes.c"  "$HSC/tests/test_hs_snes.c"
mkdir -p "$HSC/docs"
cp -f "$TMP/elfie-snes-audio-brief.md" "$HSC/docs/elfie-snes-audio-brief.md"
cp -f "$TMP/elfie-snes-audio-brief.md" "$ROOT/hs-cores/docs/elfie-snes-audio-brief.md" 2>/dev/null || mkdir -p "$ROOT/hs-cores/docs" && cp -f "$TMP/elfie-snes-audio-brief.md" "$ROOT/hs-cores/docs/elfie-snes-audio-brief.md"

if [ -n "$HOS_SNES" ]; then
  cp -f "$TMP/snes_internal.h" "$HOS_SNES/snes_internal.h"
  cp -f "$TMP/hs_snes.h"       "$HOS_SNES/hs_snes.h"
  cp -f "$TMP/snes_apu.c"      "$HOS_SNES/snes_apu.c"
  cp -f "$TMP/hs_snes.c"       "$HOS_SNES/hs_snes.c"
  if [ -d "$HOS_SNES/tests" ]; then
    cp -f "$TMP/test_hs_snes.c" "$HOS_SNES/tests/test_hs_snes.c"
  fi
  mkdir -p "$ROOT/handshakeos/docs"
  cp -f "$TMP/elfie-snes-audio-brief.md" "$ROOT/handshakeos/docs/elfie-snes-audio-brief.md"
fi
cp -f "$TMP/emu_sys.c" "$EMU_SYS"
cp -f "$TMP/audio.c" "$AUDIO_C"
cp -f "$TMP/hs_audio.h" "$AUDIO_H"
cp -f "$TMP/test_audio_pace.c" "$PACE"
cp -f "$TMP/test_snes_audio.c" "$HOS_TEST_DIR/test_snes_audio.c"

echo "APPLIED"
# confirm markers
grep -n "SNES_MIX_HEADROOM_SHIFT" "$HSC/snes_internal.h" "$HOS_SNES/snes_internal.h"
grep -n "357368u" "$EMU_SYS" "$PACE"
echo "DONE"
