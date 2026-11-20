#!/usr/bin/env bash
# Build HandshakeOS v3-play userland with hc 0.1.2 into builds/v3-play.
# Does not touch v1-firstboot, v2-bigtext, v2-stable-sd, or v2-hc.
set -euo pipefail

ROOT=/workspace/handshakeos
OUT=/workspace/scratch/handshakeos/builds/v3-play
HC=/workspace/hc/build/current/hc
INC="-Iinclude"
GB_INC="-Ithird_party/peanut-gb -Ithird_party/minigb_apu"
CFLAGS="-std=c11 -Wall -Wextra -Werror -O3 -D_GNU_SOURCE"
TP_CFLAGS="-std=c11 -O3 -D_GNU_SOURCE -DMINIGB_APU_AUDIO_FORMAT_S16SYS"
LOG="$OUT/logs"

cd "$ROOT"
rm -rf "$OUT/host" "$OUT/aarch64"
mkdir -p "$OUT/host" "$OUT/aarch64" "$LOG"
"$HC" --version > "$LOG/hc.version"
sha256sum "$HC" > "$LOG/hc.sha256"
git -C /workspace/hc rev-parse HEAD > "$LOG/hc.git-HEAD" 2>/dev/null || true

declare -A PROFILE
# runtime: pthread, syscalls, ioctls, clock, mount, file I/O
for f in handshake init fb input battery gb_host games play; do
  PROFILE["src/${f}.c"]="--allow-runtime"
done
PROFILE["tests/test_handshake.c"]="--allow-runtime"
PROFILE["tests/test_games.c"]="--allow-runtime"
# pure components
for f in ui theme font pngenc scoreboard gb_emu gb_scale; do
  PROFILE["src/${f}.c"]="--profile=component"
done
PROFILE["tests/bench_jitter.c"]="--profile=component"
PROFILE["tests/test_gb_handshake.c"]="--profile=component"
PROFILE["third_party/minigb_apu/minigb_apu.c"]="--profile=component"

SRCS=(
  src/handshake.c src/fb.c src/ui.c src/input.c src/theme.c
  src/font.c src/pngenc.c src/battery.c src/scoreboard.c src/gb_emu.c
  src/gb_scale.c src/games.c src/play.c
  src/init.c src/gb_host.c
  third_party/minigb_apu/minigb_apu.c
  tests/test_handshake.c tests/bench_jitter.c tests/test_gb_handshake.c tests/test_games.c
)

compile() { # arch src
  local arch="$1" src="$2" base obj prof extra tgt
  base="$(basename "${src%.c}")"
  obj="$OUT/$arch/${base}.o"
  prof="${PROFILE[$src]}"
  extra=""
  case "$src" in
    src/gb_emu.c) extra="$GB_INC -Wno-implicit-fallthrough" ;;
    src/init.c|src/gb_host.c|src/play.c|tests/test_gb_handshake.c|tests/test_games.c) extra="$GB_INC" ;;
  esac
  tgt=""
  [ "$arch" = aarch64 ] && tgt="--target=aarch64-linux-gnu"
  # shellcheck disable=SC2086
  if [ "$src" = third_party/minigb_apu/minigb_apu.c ]; then
    "$HC" $tgt $prof $TP_CFLAGS $INC $GB_INC -c "$src" -o "$obj"
  else
    "$HC" $tgt $prof $CFLAGS $INC $extra -c "$src" -o "$obj"
  fi
  echo "PASS $arch $prof $src"
}

: > "$LOG/profiles.txt"
for src in "${SRCS[@]}"; do echo "${PROFILE[$src]}  $src" >> "$LOG/profiles.txt"; done
for arch in host aarch64; do
  echo "===== $arch compile ====="
  for src in "${SRCS[@]}"; do compile "$arch" "$src"; done
done

APP_OBJS="handshake fb ui input theme gb_scale games play font pngenc battery scoreboard gb_emu minigb_apu"
link() { # arch cc static
  local a="$1" cc="$2" st="$3" o="$OUT/$1" objs=""
  for x in $APP_OBJS; do objs="$objs $o/$x.o"; done
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-test" "$o/test_handshake.o" "$o/handshake.o" -pthread
  $cc -O3 $st -o "$o/handshake-bench" "$o/bench_jitter.o" "$o/handshake.o" "$o/scoreboard.o" -pthread
  $cc -O3 $st -o "$o/handshake-gb-test" "$o/test_gb_handshake.o" "$o/handshake.o" "$o/gb_emu.o" \
    "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/gb_scale.o" "$o/minigb_apu.o" -pthread
  $cc -O3 $st -o "$o/handshake-games-test" "$o/test_games.o" "$o/games.o" "$o/handshake.o" \
    "$o/gb_emu.o" "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/minigb_apu.o" -pthread
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-init" "$o/init.o" $objs -pthread
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-gb" "$o/gb_host.o" $objs -pthread
}
echo "===== LINK ====="
link host gcc ""
link aarch64 aarch64-linux-gnu-gcc "-static"
echo "BUILD LINK OK"
file "$OUT/host/"handshake-* "$OUT/aarch64/"handshake-*

echo "===== ELF audit (hc --audit-elf, per profile) =====" | tee "$LOG/elf-audit.txt"
for arch in host aarch64; do
  for src in "${SRCS[@]}"; do
    base="$(basename "${src%.c}")"
    prof="${PROFILE[$src]}"
    "$HC" $prof --audit-elf "$OUT/$arch/$base.o" 2>&1 | tee -a "$LOG/elf-audit.txt"
  done
done

BANNED='pthread_mutex|pthread_cond|pthread_rwlock|pthread_spin|pthread_barrier|sem_(init|wait|post|timedwait|trywait|open|destroy)|mtx_|cnd_|flock|lockf|futex'
echo "======= nm lock-symbol scan =======" > "$LOG/nm-locks.txt"
total=0; dirty=0
for arch in host aarch64; do
  echo "--- $arch objects ---" >> "$LOG/nm-locks.txt"
  for o in "$OUT/$arch"/*.o; do
    total=$((total+1))
    hits=$(nm "$o" 2>/dev/null | grep -Ei "$BANNED" || true)
    if [ -n "$hits" ]; then dirty=$((dirty+1)); echo "DIRTY $(basename "$o"): $hits" >> "$LOG/nm-locks.txt"
    else echo "CLEAN  $(basename "$o")" >> "$LOG/nm-locks.txt"; fi
  done
done
echo "NM_SUMMARY objects=$total dirty=$dirty" | tee -a "$LOG/nm-locks.txt"

echo "======= LithoOS / ho.Os string check =======" > "$LOG/strings-check.txt"
sc=0
for b in "$OUT/host/handshake-init" "$OUT/aarch64/handshake-init" "$OUT/host/handshake-gb" "$OUT/aarch64/handshake-gb"; do
  echo "-- $b --" >> "$LOG/strings-check.txt"
  h=$(strings -a "$b" | grep -E 'LithoOS|ho\.Os' || true)
  if [ -n "$h" ]; then sc=$((sc+1)); echo "$h" >> "$LOG/strings-check.txt"; fi
done
echo "STRINGS_SUMMARY hits=$sc" | tee -a "$LOG/strings-check.txt"
