#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Host ASan+UBSan build of the HandshakeOS app/core objects used by the multi-system test and the
# pure-C fuzzers (tests/fuzz/*.c). Sanitizer builds are review tooling only: never shipped, never
# audited by HC (the sanitizer runtime is not HandshakeOS code). Usage: build-asan.sh OUTDIR
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=${1:?OUTDIR}; mkdir -p "$OUT"
REC="-fno-sanitize-recover=undefined"; [ "${HS_SAN_RECOVER:-0}" = 1 ] && REC="-fsanitize-recover=undefined"
SAN="-fsanitize=address,undefined $REC -fno-omit-frame-pointer -g -O1"
INC="-Iinclude -Isrc -Icores/include -Icores/common -Icores/gbc -Icores/gba -Icores/snes -Icores/genesis -Icores/psx -Icores/n64 -Ithird_party/peanut-gb -Ithird_party/minigb_apu"
CF="-std=c11 -D_GNU_SOURCE -ffp-contract=off -DMINIGB_APU_AUDIO_FORMAT_S16SYS -Wno-implicit-fallthrough"
SRCS="src/emu_sys.c src/emu_cores.c src/emu_save.c src/games.c src/gb_emu.c src/fb.c src/font.c src/pngenc.c src/handshake.c src/audio.c third_party/minigb_apu/minigb_apu.c"
SRCS="$SRCS $(ls cores/common/*.c cores/gbc/*.c cores/gba/*.c cores/snes/*.c cores/genesis/*.c cores/psx/*.c cores/n64/*.c | grep -v test_ | tr "\n" " ")"
objs=""
for s in $SRCS; do
  o="$OUT/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
  gcc $CF $SAN $INC -c "$s" -o "$o"; objs="$objs $o"
done
for t in tests/test_multi_emu.c tests/fuzz/fuzz_emu.c; do
  b=$(basename "${t%.c}")
  gcc $CF $SAN $INC -c "$t" -o "$OUT/$b.o"
  # shellcheck disable=SC2086
  gcc $SAN -o "$OUT/$b" "$OUT/$b.o" $objs -pthread -lm
done
echo "ASAN_BUILD_OK $OUT"
