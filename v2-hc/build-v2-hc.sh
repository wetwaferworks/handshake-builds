#!/usr/bin/env bash
# Build HandshakeOS v2 userland with HC (frozen copy) into builds/v2-hc.
# Does not touch v1-firstboot, v2-bigtext, or v2-stable-sd.
set -euo pipefail

ROOT=/workspace/handshakeos
OUT=/workspace/scratch/handshakeos/builds/v2-hc
HC="$OUT/hc"
INC="-Iinclude"
GB_INC="-Ithird_party/peanut-gb -Ithird_party/minigb_apu"
# Match Makefile CFLAGS / fast v2-bigtext: -std=c11 -Wall -Wextra -Werror -O3
CFLAGS="-std=c11 -Wall -Wextra -Werror -O3 -D_GNU_SOURCE"
TP_CFLAGS="-std=c11 -O3 -D_GNU_SOURCE -DMINIGB_APU_AUDIO_FORMAT_S16SYS"
LOG="$OUT/logs"

cd "$ROOT"
mkdir -p "$OUT/host" "$OUT/aarch64" "$LOG"

# ---- profile map (as specified) ----
# --allow-runtime: OS/runtime TUs that legitimately need pthread, syscalls, ioctls, clock
# --profile=component: pure component TUs
declare -A PROFILE
# runtime
for f in handshake init fb input battery gb_host; do
  PROFILE["src/${f}.c"]="--allow-runtime"
done
PROFILE["tests/test_handshake.c"]="--allow-runtime"
# component
for f in ui theme font pngenc scoreboard gb_emu; do
  PROFILE["src/${f}.c"]="--profile=component"
done
PROFILE["tests/bench_jitter.c"]="--profile=component"
PROFILE["tests/test_gb_handshake.c"]="--profile=component"
PROFILE["third_party/minigb_apu/minigb_apu.c"]="--profile=component"

compile() { # arch src
  local arch="$1" src="$2"
  local base dest obj prof extra tgt
  dest="$OUT/$arch"
  base="$(basename "${src%.c}")"
  obj="$dest/${base}.o"
  prof="${PROFILE[$src]:-}"
  extra=""
  case "$src" in
    src/gb_emu.c)
      # third-party peanut_gb.h has intentional switch fallthrough without attributes.
      # first-party gb_emu.c remains under -Werror.
      extra="$GB_INC -Wno-implicit-fallthrough"
      ;;
    src/init.c|src/gb_host.c|tests/test_gb_handshake.c)
      extra="$GB_INC"
      ;;
    third_party/minigb_apu/minigb_apu.c)
      extra="$GB_INC $TP_CFLAGS"
      ;;
  esac
  tgt=""
  if [ "$arch" = aarch64 ]; then
    tgt="--target=aarch64-linux-gnu"
  fi
  echo "  [$arch] $prof $src -> $obj"
  # shellcheck disable=SC2086
  if [ "$src" = "third_party/minigb_apu/minigb_apu.c" ]; then
    "$HC" $tgt $prof $TP_CFLAGS $INC $GB_INC -c "$src" -o "$obj"
  else
    "$HC" $tgt $prof $CFLAGS $INC $extra -c "$src" -o "$obj"
  fi
}

echo "===== HOST compile ====="
HOST_SRCS=(
  src/handshake.c src/fb.c src/ui.c src/input.c src/theme.c
  src/font.c src/pngenc.c src/battery.c src/scoreboard.c src/gb_emu.c
  src/init.c src/gb_host.c
  third_party/minigb_apu/minigb_apu.c
  tests/test_handshake.c tests/bench_jitter.c tests/test_gb_handshake.c
)
: > "$LOG/profiles.txt"
for src in "${HOST_SRCS[@]}"; do
  compile host "$src"
  echo "${PROFILE[$src]}  $src" | tee -a "$LOG/profiles.txt"
done

echo "===== AARCH64 compile ====="
for src in "${HOST_SRCS[@]}"; do
  compile aarch64 "$src"
done

echo "===== LINK host ====="
gcc -O3 -o "$OUT/host/handshake-test" \
  "$OUT/host/test_handshake.o" "$OUT/host/handshake.o" -pthread
gcc -O3 -o "$OUT/host/handshake-bench" \
  "$OUT/host/bench_jitter.o" "$OUT/host/handshake.o" "$OUT/host/scoreboard.o" -pthread
gcc -O3 -o "$OUT/host/handshake-gb-test" \
  "$OUT/host/test_gb_handshake.o" "$OUT/host/handshake.o" "$OUT/host/gb_emu.o" \
  "$OUT/host/fb.o" "$OUT/host/font.o" "$OUT/host/pngenc.o" "$OUT/host/minigb_apu.o" -pthread
gcc -O3 $GB_INC -o "$OUT/host/handshake-init" \
  "$OUT/host/init.o" \
  "$OUT/host/handshake.o" "$OUT/host/fb.o" "$OUT/host/ui.o" "$OUT/host/input.o" \
  "$OUT/host/theme.o" "$OUT/host/font.o" "$OUT/host/pngenc.o" "$OUT/host/battery.o" \
  "$OUT/host/scoreboard.o" "$OUT/host/gb_emu.o" "$OUT/host/minigb_apu.o" -pthread
gcc -O3 $GB_INC -o "$OUT/host/handshake-gb" \
  "$OUT/host/gb_host.o" \
  "$OUT/host/handshake.o" "$OUT/host/fb.o" "$OUT/host/ui.o" "$OUT/host/input.o" \
  "$OUT/host/theme.o" "$OUT/host/font.o" "$OUT/host/pngenc.o" "$OUT/host/battery.o" \
  "$OUT/host/scoreboard.o" "$OUT/host/gb_emu.o" "$OUT/host/minigb_apu.o" -pthread

echo "===== LINK aarch64 static ====="
X=aarch64-linux-gnu-gcc
$X -O3 -static -o "$OUT/aarch64/handshake-test" \
  "$OUT/aarch64/test_handshake.o" "$OUT/aarch64/handshake.o" -pthread
$X -O3 -static -o "$OUT/aarch64/handshake-bench" \
  "$OUT/aarch64/bench_jitter.o" "$OUT/aarch64/handshake.o" "$OUT/aarch64/scoreboard.o" -pthread
$X -O3 -static -o "$OUT/aarch64/handshake-gb-test" \
  "$OUT/aarch64/test_gb_handshake.o" "$OUT/aarch64/handshake.o" "$OUT/aarch64/gb_emu.o" \
  "$OUT/aarch64/fb.o" "$OUT/aarch64/font.o" "$OUT/aarch64/pngenc.o" "$OUT/aarch64/minigb_apu.o" -pthread
$X -O3 -static -o "$OUT/aarch64/handshake-init" \
  "$OUT/aarch64/init.o" \
  "$OUT/aarch64/handshake.o" "$OUT/aarch64/fb.o" "$OUT/aarch64/ui.o" "$OUT/aarch64/input.o" \
  "$OUT/aarch64/theme.o" "$OUT/aarch64/font.o" "$OUT/aarch64/pngenc.o" "$OUT/aarch64/battery.o" \
  "$OUT/aarch64/scoreboard.o" "$OUT/aarch64/gb_emu.o" "$OUT/aarch64/minigb_apu.o" -pthread
$X -O3 -static -o "$OUT/aarch64/handshake-gb" \
  "$OUT/aarch64/gb_host.o" \
  "$OUT/aarch64/handshake.o" "$OUT/aarch64/fb.o" "$OUT/aarch64/ui.o" "$OUT/aarch64/input.o" \
  "$OUT/aarch64/theme.o" "$OUT/aarch64/font.o" "$OUT/aarch64/pngenc.o" "$OUT/aarch64/battery.o" \
  "$OUT/aarch64/scoreboard.o" "$OUT/aarch64/gb_emu.o" "$OUT/aarch64/minigb_apu.o" -pthread

echo "BUILD LINK OK"
file "$OUT/host/"handshake-* "$OUT/aarch64/"handshake-*
