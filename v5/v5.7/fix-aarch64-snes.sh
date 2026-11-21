#!/bin/bash
# Relink aarch64 test_hs_snes statically (qemu-aarch64-static needs no dynamic loader)
# and rerun the suite test. Does not touch the in-flight test-snes-audio-aarch64.
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
SNES="$ROOT/hs-cores/snes"
OUT="$SNES/build/aarch64"
LOG="$ROOT/builds/v5/v5.7/snes-aarch64-rerun.log"
exec > >(tee -a "$LOG") 2>&1
echo "==== aarch64 relink $(date) ===="
file "$OUT/test_hs_snes" || true
ldd "$OUT/test_hs_snes" 2>&1 | head || true
ls /usr/aarch64-linux-gnu/lib/ld-linux-aarch64.so.1 2>&1 || true
# relink static from existing objects
coreobjs="$OUT/hs_snes.o $OUT/snes_cpu.o $OUT/snes_bus.o $OUT/snes_ppu.o $OUT/snes_apu.o"
aarch64-linux-gnu-gcc -static -o "$OUT/test_hs_snes" $coreobjs "$OUT/snes_testroms.o" "$OUT/png_write.o" "$OUT/test_hs_snes.o"
file "$OUT/test_hs_snes"
mkdir -p "$SNES/out/aarch64"
echo "==== run static test_hs_snes aarch64 ===="
if qemu-aarch64-static "$OUT/test_hs_snes" "$SNES/out/aarch64" > "$SNES/logs/test_aarch64.log" 2>&1; then
  echo "AARCH64_TEST_OK"
else
  echo "AARCH64_TEST_FAIL rc=$?"
  tail -50 "$SNES/logs/test_aarch64.log"
  exit 1
fi
grep -E 'HASH |RESULT |n32 |n48 |clips=|5-min' "$SNES/logs/test_aarch64.log"
echo "==== compare ===="
OLD_M1_V=f7a25519643fde0c
OLD_M7_V=6cec208a223494d8
m1v=$(grep '^HASH mode1' "$SNES/logs/test_aarch64.log" | awk '{print $3}' | sed 's/video=//')
m1a=$(grep '^HASH mode1' "$SNES/logs/test_aarch64.log" | awk '{print $4}' | sed 's/audio=//')
m7v=$(grep '^HASH mode7' "$SNES/logs/test_aarch64.log" | awk '{print $3}' | sed 's/video=//')
m7a=$(grep '^HASH mode7' "$SNES/logs/test_aarch64.log" | awk '{print $4}' | sed 's/audio=//')
echo "NEW aarch64 mode1 video=$m1v audio=$m1a"
echo "NEW aarch64 mode7 video=$m7v audio=$m7a"
[ "$m1v" = "$OLD_M1_V" ] && echo "VIDEO_UNCHANGED aarch64 mode1" || { echo "VIDEO_CHANGED aarch64 mode1"; exit 1; }
[ "$m7v" = "$OLD_M7_V" ] && echo "VIDEO_UNCHANGED aarch64 mode7" || { echo "VIDEO_CHANGED aarch64 mode7"; exit 1; }
if diff -q <(grep '^HASH' "$SNES/logs/test_x86_64.log") <(grep '^HASH' "$SNES/logs/test_aarch64.log") >/dev/null; then
  echo "CROSS-ARCH SNES HASHES IDENTICAL"
else
  echo "CROSS-ARCH SNES HASH MISMATCH"
  diff <(grep '^HASH' "$SNES/logs/test_x86_64.log") <(grep '^HASH' "$SNES/logs/test_aarch64.log") || true
  exit 1
fi
echo "AARCH64_SNES_OK"
