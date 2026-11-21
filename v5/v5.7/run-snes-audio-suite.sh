#!/bin/bash
# v5.7: build and run the SNES suite + 5-min audio + ALSA pace, both arches.
# Logs to builds/v5/v5.7/snes-audio-suite.log. No git. Never power off.
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
SNES="$ROOT/hs-cores/snes"
HOS="$ROOT/handshakeos"
OUT="$ROOT/builds/v5/v5.7"
LOG="$OUT/snes-audio-suite.log"
mkdir -p "$OUT" "$SNES/out/x86_64" "$SNES/out/aarch64" "$SNES/logs"
exec > >(tee -a "$LOG") 2>&1
echo "==== SNES AUDIO SUITE $(date) ===="
OLD_M1_V=f7a25519643fde0c
OLD_M1_A=53a467054495db9a
OLD_M7_V=6cec208a223494d8
OLD_M7_A=818670cd17aa583d
echo "OLD HASH mode1 video=$OLD_M1_V audio=$OLD_M1_A"
echo "OLD HASH mode7 video=$OLD_M7_V audio=$OLD_M7_A"

# preserve pre-v5.7 logs
for a in x86_64 aarch64; do
  if [ -f "$SNES/logs/test_$a.log" ] && [ ! -f "$SNES/logs/test_$a.log.pre-v57" ]; then
    cp -f "$SNES/logs/test_$a.log" "$SNES/logs/test_$a.log.pre-v57"
  fi
done

cd "$SNES"
export HC=${HC:-"$ROOT/hc/build/current/hc"}
echo "HC=$HC"
if [ ! -x "$HC" ]; then
  echo "WARN: HC missing at $HC, build.sh will try its default"
fi

echo "==== build.sh both arches ===="
if ! bash ./build.sh; then
  echo "BUILD_FAIL"
  exit 1
fi
echo "BUILD_OK"

rc=0
for a in x86_64 aarch64; do
  echo "==== test_hs_snes $a ===="
  mkdir -p "$SNES/out/$a"
  if [ "$a" = x86_64 ]; then
    if ! ./build/x86_64/test_hs_snes "$SNES/out/x86_64" > "$SNES/logs/test_x86_64.log" 2>&1; then
      echo "TEST_FAIL $a"
      tail -40 "$SNES/logs/test_x86_64.log"
      rc=1
    fi
  else
    if ! qemu-aarch64-static ./build/aarch64/test_hs_snes "$SNES/out/aarch64" > "$SNES/logs/test_aarch64.log" 2>&1; then
      echo "TEST_FAIL $a"
      tail -40 "$SNES/logs/test_aarch64.log"
      rc=1
    fi
  fi
  echo "--- $a hashes ---"
  grep -E 'HASH |RESULT |n32 |n48 |clips=|5-min' "$SNES/logs/test_$a.log" || true
done

echo "==== video/audio hash compare ===="
for a in x86_64 aarch64; do
  m1v=$(grep '^HASH mode1' "$SNES/logs/test_$a.log" | awk '{print $3}' | sed 's/video=//')
  m1a=$(grep '^HASH mode1' "$SNES/logs/test_$a.log" | awk '{print $4}' | sed 's/audio=//')
  m7v=$(grep '^HASH mode7' "$SNES/logs/test_$a.log" | awk '{print $3}' | sed 's/video=//')
  m7a=$(grep '^HASH mode7' "$SNES/logs/test_$a.log" | awk '{print $4}' | sed 's/audio=//')
  echo "NEW $a mode1 video=$m1v audio=$m1a"
  echo "NEW $a mode7 video=$m7v audio=$m7a"
  if [ "$m1v" = "$OLD_M1_V" ]; then echo "VIDEO_UNCHANGED $a mode1"; else echo "VIDEO_CHANGED $a mode1 old=$OLD_M1_V new=$m1v"; rc=1; fi
  if [ "$m7v" = "$OLD_M7_V" ]; then echo "VIDEO_UNCHANGED $a mode7"; else echo "VIDEO_CHANGED $a mode7 old=$OLD_M7_V new=$m7v"; rc=1; fi
  if [ "$m1a" = "$OLD_M1_A" ]; then echo "AUDIO_SAME $a mode1 (unexpected if mix changed)"; else echo "AUDIO_CHANGED $a mode1 old=$OLD_M1_A new=$m1a"; fi
  if [ "$m7a" = "$OLD_M7_A" ]; then echo "AUDIO_SAME $a mode7 (unexpected if mix changed)"; else echo "AUDIO_CHANGED $a mode7 old=$OLD_M7_A new=$m7a"; fi
done
# cross-arch
if diff -q <(grep '^HASH' "$SNES/logs/test_x86_64.log") <(grep '^HASH' "$SNES/logs/test_aarch64.log") >/dev/null; then
  echo "CROSS-ARCH SNES HASHES IDENTICAL"
else
  echo "CROSS-ARCH SNES HASH MISMATCH"
  diff <(grep '^HASH' "$SNES/logs/test_x86_64.log") <(grep '^HASH' "$SNES/logs/test_aarch64.log") || true
  rc=1
fi

echo "==== test_audio_pace + test_snes_audio ===="
CORE="$HOS/cores/snes/hs_snes.c $HOS/cores/snes/snes_cpu.c $HOS/cores/snes/snes_bus.c $HOS/cores/snes/snes_ppu.c $HOS/cores/snes/snes_apu.c"
TR="$SNES/tests/snes_testroms.c"
INC="-I$HOS/include -I$HOS/cores/snes -I$SNES/tests"
CF="-std=c11 -Wall -Wextra -Werror -O2 -D_GNU_SOURCE"

# host
gcc $CF $INC -o "$OUT/test-audio-pace-host" "$HOS/tests/test_audio_pace.c" "$HOS/src/audio.c"
gcc $CF $INC -o "$OUT/test-snes-audio-host" "$HOS/tests/test_snes_audio.c" "$HOS/src/audio.c" $CORE $TR
# aarch64
aarch64-linux-gnu-gcc $CF $INC -static -o "$OUT/test-audio-pace-aarch64" "$HOS/tests/test_audio_pace.c" "$HOS/src/audio.c"
aarch64-linux-gnu-gcc $CF $INC -static -o "$OUT/test-snes-audio-aarch64" "$HOS/tests/test_snes_audio.c" "$HOS/src/audio.c" $CORE $TR

(cd "$OUT" && ./test-audio-pace-host) || { echo "PACE_HOST_FAIL"; rc=1; }
(cd "$OUT" && ./test-snes-audio-host) || { echo "SNES_AUDIO_HOST_FAIL"; rc=1; }
(cd "$OUT" && qemu-aarch64-static ./test-audio-pace-aarch64) || { echo "PACE_AARCH64_FAIL"; rc=1; }
(cd "$OUT" && qemu-aarch64-static ./test-snes-audio-aarch64) || { echo "SNES_AUDIO_AARCH64_FAIL"; rc=1; }

if [ "$rc" -eq 0 ]; then
  echo "SNES_AUDIO_SUITE_OK"
else
  echo "SNES_AUDIO_SUITE_FAIL rc=$rc"
fi
exit $rc
