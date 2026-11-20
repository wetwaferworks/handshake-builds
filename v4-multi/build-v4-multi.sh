#!/usr/bin/env bash
# Build HandshakeOS v4-multi userland + clean-room cores with HC v0.1.4 into
# builds/v4-multi. Never touches v1-firstboot..v3-audio or hs-cores/.
set -euo pipefail

ROOT=/workspace/handshakeos
OUT=/workspace/scratch/handshakeos/builds/v4-multi
HCSRC=/workspace/hc/build/current/hc
HC="$OUT/hc"
LOG="$OUT/logs"
INC="-Iinclude -Isrc -Icores/include -Icores/common -Icores/gbc -Icores/gba -Icores/snes -Icores/genesis"
GB_INC="-Ithird_party/peanut-gb -Ithird_party/minigb_apu"
CFLAGS="-std=c11 -Wall -Wextra -Werror -O3 -D_GNU_SOURCE"
TP_CFLAGS="-std=c11 -O3 -D_GNU_SOURCE -DMINIGB_APU_AUDIO_FORMAT_S16SYS"

cd "$ROOT"
mkdir -p "$OUT/host" "$OUT/aarch64" "$LOG" "$OUT/roms"
rm -f "$OUT"/host/* "$OUT"/aarch64/*

# ---- freeze the compiler ----
cp "$HCSRC" "$HC"; chmod 755 "$HC"
cmp "$HCSRC" "$HC"
"$HC" --version > "$LOG/hc.version"
sha256sum "$HC" > "$LOG/hc.sha256"
git -C /workspace/hc rev-parse HEAD > "$LOG/hc.git-HEAD"
grep -q '^hc 0.1.4' "$LOG/hc.version"
grep -q '^ad4d8cb' "$LOG/hc.git-HEAD"

declare -A PROFILE
for f in handshake init fb input battery gb_host games games_multi play audio emu_save; do
  PROFILE["src/${f}.c"]="--allow-runtime"
done
for t in test_handshake test_games test_audio test_multi_emu; do PROFILE["tests/${t}.c"]="--allow-runtime"; done
for f in ui theme font pngenc scoreboard gb_emu gb_scale emu_sys emu_cores; do
  PROFILE["src/${f}.c"]="--profile=component"
done
PROFILE["tests/bench_jitter.c"]="--profile=component"
PROFILE["tests/test_gb_handshake.c"]="--profile=component"
PROFILE["third_party/minigb_apu/minigb_apu.c"]="--profile=component"

CORES=(cores/common/hs_video.c
       cores/gbc/gbc_cpu.c cores/gbc/gbc_mmu.c cores/gbc/gbc_ppu.c cores/gbc/gbc_apu.c cores/gbc/gbc_core.c
       cores/gba/gba_cpu.c cores/gba/gba_bus.c cores/gba/gba_bios.c cores/gba/gba_ppu.c cores/gba/gba_apu.c cores/gba/gba_core.c
       cores/snes/hs_snes.c cores/snes/snes_cpu.c cores/snes/snes_bus.c cores/snes/snes_ppu.c cores/snes/snes_apu.c
       cores/genesis/m68k_cpu.c cores/genesis/gen_bus.c cores/genesis/gen_vdp.c cores/genesis/gen_render.c
       cores/genesis/gen_z80.c cores/genesis/gen_ym2612.c cores/genesis/gen_psg.c cores/genesis/gen_audio.c
       cores/genesis/hs_genesis.c)
for c in "${CORES[@]}"; do PROFILE["$c"]="--profile=component"; done

SRCS=(
  src/handshake.c src/fb.c src/ui.c src/input.c src/theme.c
  src/font.c src/pngenc.c src/battery.c src/scoreboard.c src/gb_emu.c
  src/gb_scale.c src/games.c src/games_multi.c src/play.c src/audio.c
  src/emu_sys.c src/emu_cores.c src/emu_save.c
  src/init.c src/gb_host.c
  third_party/minigb_apu/minigb_apu.c
  "${CORES[@]}"
  tests/test_handshake.c tests/bench_jitter.c tests/test_gb_handshake.c tests/test_games.c
  tests/test_audio.c tests/test_multi_emu.c
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
  [ "$arch" = aarch64 ] && tgt="--target=aarch64-linux-gnu -mcpu=cortex-a53"
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
  echo "===== $arch compile (hc $(cut -d' ' -f2 "$LOG/hc.version" | head -1)) ====="
  for src in "${SRCS[@]}"; do compile "$arch" "$src"; done
done

# ---- built-in demo ROM generators (host tools, verbatim verified builders) ----
mkdir -p "$OUT/tools"
CORE_OBJS() { local a="$1" s=""; for c in "${CORES[@]}"; do s="$s $OUT/$a/$(basename "${c%.c}").o"; done; echo "$s"; }
for arch in host aarch64; do
  tgt=""; cc=gcc; st=""
  [ "$arch" = aarch64 ] && { tgt="--target=aarch64-linux-gnu -mcpu=cortex-a53"; cc=aarch64-linux-gnu-gcc; st="-static"; }
  # shellcheck disable=SC2086
  "$HC" $tgt --allow-runtime -std=c11 -O2 -D_GNU_SOURCE -Icores/demo-gen -Icores/gba -c cores/demo-gen/make_gba_demo.c -o "$OUT/tools/make_gba_demo.$arch.o"
  # shellcheck disable=SC2086
  "$HC" $tgt --allow-runtime -std=c11 -O2 -D_GNU_SOURCE -Icores/demo-gen -Icores/genesis -Icores/include -c cores/demo-gen/make_md_demo.c -o "$OUT/tools/make_md_demo.$arch.o"
  # shellcheck disable=SC2046,SC2086
  $cc -O2 $st -o "$OUT/tools/make_gba_demo.$arch" "$OUT/tools/make_gba_demo.$arch.o" $(CORE_OBJS $arch)
  # shellcheck disable=SC2046,SC2086
  $cc -O2 $st -o "$OUT/tools/make_md_demo.$arch" "$OUT/tools/make_md_demo.$arch.o" $(CORE_OBJS $arch)
done
"$OUT/tools/make_gba_demo.host" roms/hs_gba_demo.gba
"$OUT/tools/make_md_demo.host" roms/hs_genesis_demo.md
qemu-aarch64-static "$OUT/tools/make_gba_demo.aarch64" "$OUT/tools/hs_gba_demo.arm64.gba"
qemu-aarch64-static "$OUT/tools/make_md_demo.aarch64" "$OUT/tools/hs_genesis_demo.arm64.md"
cmp roms/hs_gba_demo.gba "$OUT/tools/hs_gba_demo.arm64.gba"
cmp roms/hs_genesis_demo.md "$OUT/tools/hs_genesis_demo.arm64.md"
echo "DEMO ROMS generated byte-identical on x86_64 and ARM64"
for f in handshake-demo.gb hs_cgb_test.gbc hs_gba_demo.gba hs_mode1_demo.sfc hs_mode7_floor.sfc hs_genesis_demo.md; do
  cp "roms/$f" "$OUT/roms/$f"
done
cmp roms/hs_cgb_test.gbc /workspace/scratch/handshakeos/hs-cores/out/x86_64/hs_cgb_test.gbc
cmp roms/hs_mode1_demo.sfc /workspace/scratch/handshakeos/hs-cores/snes/out/x86_64/hs_mode1_demo.sfc
cmp roms/hs_mode7_floor.sfc /workspace/scratch/handshakeos/hs-cores/snes/out/x86_64/hs_mode7_floor.sfc
( cd "$OUT/roms" && sha256sum ./* ) > "$LOG/builtin-roms.sha256"

# ---- link ----
APP_OBJS="handshake fb ui input theme gb_scale games games_multi play audio font pngenc battery scoreboard gb_emu minigb_apu emu_sys emu_cores emu_save"
link() { # arch cc static
  local a="$1" cc="$2" st="$3" o="$OUT/$1" objs="" cobjs
  for x in $APP_OBJS; do objs="$objs $o/$x.o"; done
  cobjs=$(CORE_OBJS "$a")
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-test" "$o/test_handshake.o" "$o/handshake.o" -pthread
  $cc -O3 $st -o "$o/handshake-bench" "$o/bench_jitter.o" "$o/handshake.o" "$o/scoreboard.o" -pthread
  $cc -O3 $st -o "$o/handshake-gb-test" "$o/test_gb_handshake.o" "$o/handshake.o" "$o/gb_emu.o" \
    "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/gb_scale.o" "$o/minigb_apu.o" -pthread
  $cc -O3 $st -o "$o/handshake-games-test" "$o/test_games.o" "$o/games.o" "$o/handshake.o" \
    "$o/gb_emu.o" "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/minigb_apu.o" -pthread
  $cc -O3 $st -o "$o/handshake-audio-test" "$o/test_audio.o" "$o/audio.o"
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-multi-test" "$o/test_multi_emu.o" "$o/emu_sys.o" "$o/emu_cores.o" \
    "$o/emu_save.o" "$o/games.o" "$o/gb_emu.o" "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/minigb_apu.o" \
    "$o/handshake.o" "$o/audio.o" $cobjs -pthread
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-init" "$o/init.o" $objs $cobjs -pthread
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-gb" "$o/gb_host.o" $objs $cobjs -pthread
}
echo "===== LINK ====="
link host gcc ""
link aarch64 aarch64-linux-gnu-gcc "-static"
echo "BUILD LINK OK"
file "$OUT/host/"handshake-* "$OUT/aarch64/"handshake-*

echo "===== ELF audit (hc --audit-elf, per profile) =====" | tee "$LOG/elf-audit.txt"
apass=0; afail=0
for arch in host aarch64; do
  for src in "${SRCS[@]}"; do
    base="$(basename "${src%.c}")"
    prof="${PROFILE[$src]}"
    # shellcheck disable=SC2086
    if "$HC" $prof --audit-elf "$OUT/$arch/$base.o" >> "$LOG/elf-audit.txt" 2>&1; then apass=$((apass+1)); else afail=$((afail+1)); echo "AUDIT FAIL $arch $src"; fi
  done
done
echo "ELF_AUDIT pass=$apass fail=$afail" | tee -a "$LOG/elf-audit.txt"

BANNED='pthread_mutex|pthread_cond|pthread_rwlock|pthread_spin|pthread_barrier|sem_(init|wait|post|timedwait|trywait|open|destroy)|mtx_|cnd_|flock|lockf|futex|__sync_lock|__lll_lock|atomic_flag'
COMPBAN='pthread_|thrd_|clock_gettime|gettimeofday|^time$|^rand$|^srand$|random|getrandom|setjmp|longjmp'
echo "======= nm lock-symbol scan =======" > "$LOG/nm-locks.txt"
total=0; dirty=0; ctotal=0; cdirty=0
for arch in host aarch64; do
  NM=nm; [ "$arch" = aarch64 ] && NM=aarch64-linux-gnu-nm
  echo "--- $arch objects ---" >> "$LOG/nm-locks.txt"
  for src in "${SRCS[@]}"; do
    o="$OUT/$arch/$(basename "${src%.c}").o"
    total=$((total+1))
    hits=$($NM "$o" 2>/dev/null | awk '{print $NF}' | grep -Ei "$BANNED" || true)
    if [ -n "$hits" ]; then dirty=$((dirty+1)); echo "DIRTY $(basename "$o"): $hits" >> "$LOG/nm-locks.txt"
    else echo "CLEAN  $arch $(basename "$o")" >> "$LOG/nm-locks.txt"; fi
    if [ "${PROFILE[$src]}" = "--profile=component" ]; then
      ctotal=$((ctotal+1))
      ch=$($NM "$o" 2>/dev/null | awk '{print $NF}' | grep -Ei "$COMPBAN" || true)
      if [ -n "$ch" ]; then cdirty=$((cdirty+1)); echo "COMPONENT-DIRTY $(basename "$o"): $ch" >> "$LOG/nm-locks.txt"; fi
    fi
  done
done
echo "NM_SUMMARY objects=$total lock_dirty=$dirty component_objects=$ctotal component_thread_clock_rng_dirty=$cdirty" | tee -a "$LOG/nm-locks.txt"

echo "======= word / branding hygiene =======" > "$LOG/strings-check.txt"
sc=0
for b in "$OUT"/host/handshake-* "$OUT"/aarch64/handshake-* "$OUT"/host/*.o "$OUT"/aarch64/*.o "$OUT"/roms/*; do
  h=$( { strings -a "$b" | grep -Ei 'lithoos|ho\.os' ; strings -a "$b" | grep -Eiw 'evil' ; } || true)
  if [ -n "$h" ]; then sc=$((sc+1)); echo "-- $b: $h" >> "$LOG/strings-check.txt"; fi
done
src_hits=$( { grep -rIEil 'lithoos|ho\.os' src include tests cores tools roms 2>/dev/null || true; grep -rIEilw 'evil' src include tests cores tools roms 2>/dev/null || true; } | sort -u | wc -l)
echo "STRINGS_SUMMARY binary_hits=$sc source_hits=$src_hits" | tee -a "$LOG/strings-check.txt"
