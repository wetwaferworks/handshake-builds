#!/usr/bin/env bash
# Build HandshakeOS v5 in the consortium tree: HC v0.1.4 (ad4d8cb) objects, hs_rt (in-house freestanding
# aarch64 runtime), and the shipped aarch64 handshake-init / handshake-gb linked -nostdlib against
# hs_rt + libgcc only (no glibc, no NPTL). Test binaries keep the v4-full recipe (not shipped).
# v5.2 = v5 + REVIEW.md fixes. Writes only builds/v5/v5.2 (the v5 artifacts in builds/v5 stay untouched). Never touches builds/v1-firstboot..v4-multi.
set -euo pipefail

T=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake
ROOT=$T/handshakeos
OUT=$T/builds/v5/v5.2
HCSRC=$T/hc/build/current/hc
HC="$OUT/hc"
LOG="$OUT/logs"
RT=$ROOT/src/hs_rt
export TMPDIR="$OUT/tmp"; mkdir -p "$TMPDIR"
INC="-Iinclude -Isrc -Icores/include -Icores/common -Icores/gbc -Icores/gba -Icores/snes -Icores/genesis -Icores/psx -Icores/n64"
FP_CFLAGS="-ffp-contract=off"
GB_INC="-Ithird_party/peanut-gb -Ithird_party/minigb_apu"
CFLAGS="-std=c11 -Wall -Wextra -Werror -O3 -D_GNU_SOURCE"
TP_CFLAGS="-std=c11 -O3 -D_GNU_SOURCE -DMINIGB_APU_AUDIO_FORMAT_S16SYS"
RT_CFLAGS="-std=c11 -O2 -mcpu=cortex-a53 -mno-outline-atomics -ffreestanding -fno-builtin -fno-stack-protector -U_FORTIFY_SOURCE \
 -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-pie -Wall -Wextra -Werror"

cd "$ROOT"
mkdir -p "$OUT/host" "$OUT/aarch64" "$OUT/aarch64/hs_rt" "$OUT/tools" "$LOG"
[ -d "$OUT/roms" ] || cp -a "$T/builds/v5/roms" "$OUT/roms"
rm -f "$OUT"/host/* "$OUT"/aarch64/*.o "$OUT"/aarch64/handshake-* "$OUT"/aarch64/hs_rt/*

# ---- freeze the compiler ----
cp "$HCSRC" "$HC"; chmod 755 "$HC"
cmp "$HCSRC" "$HC"
"$HC" --version > "$LOG/hc.version"
sha256sum "$HC" > "$LOG/hc.sha256"
git -C "$T/hc" rev-parse HEAD > "$LOG/hc.git-HEAD"
grep -q '^hc 0.1.4' "$LOG/hc.version"
grep -q '^ad4d8cb' "$LOG/hc.git-HEAD"
{ gcc --version | head -1; aarch64-linux-gnu-gcc --version | head -1; aarch64-linux-gnu-ld --version | head -1; } > "$LOG/toolchain.version"

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
       cores/genesis/hs_genesis.c
       cores/psx/psx_cpu.c cores/psx/psx_gte.c cores/psx/psx_bus.c cores/psx/psx_gpu_cap.c
       cores/psx/psx_gpu_raster.c cores/psx/psx_spu.c cores/psx/psx_bios_hle.c cores/psx/hs_psx.c
       cores/n64/n64_cpu.c cores/n64/n64_bus.c cores/n64/n64_rsp.c cores/n64/n64_rdp.c
       cores/n64/n64_audio.c cores/n64/hs_n64.c)
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
    cores/psx/*|cores/n64/*|src/emu_cores.c) extra="$FP_CFLAGS" ;;
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

# ---- built-in demo ROMs: v5 ships the verified v4-full set; prove they equal the source tree ----
for f in handshake-demo.gb hs_cgb_test.gbc hs_gba_demo.gba hs_mode1_demo.sfc hs_mode7_floor.sfc hs_genesis_demo.md \
         psx/psx_3d_demo.exe n64/hs_n64_demo.z64; do cmp "roms/$f" "$OUT/roms/$f"; done
( cd "$OUT/roms" && find . -type f | sort | xargs sha256sum ) > "$LOG/builtin-roms.sha256"
echo "BUILTIN ROMS identical to handshakeos/roms"

# ---- hs_rt ----
echo "===== hs_rt compile (aarch64-linux-gnu-gcc, freestanding) ====="
RTOBJS=""
for s in start.S sys.c mem.c stdio.c; do
  o="$OUT/aarch64/hs_rt/${s%.*}.o"
  # shellcheck disable=SC2086
  aarch64-linux-gnu-gcc $RT_CFLAGS -I"$RT" -c "$RT/$s" -o "$o"
  RTOBJS="$RTOBJS $o"
  echo "PASS hs_rt $s"
done

# ---- link ----
APP_OBJS="handshake fb ui input theme gb_scale games games_multi play audio font pngenc battery scoreboard gb_emu minigb_apu emu_sys emu_cores emu_save"
CORE_OBJS() { local a="$1" s=""; for c in "${CORES[@]}"; do s="$s $OUT/$a/$(basename "${c%.c}").o"; done; echo "$s"; }
link_tests() { # arch cc static
  local a="$1" cc="$2" st="$3" o="$OUT/$1" cobjs
  cobjs=$(CORE_OBJS "$a")
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
    "$o/handshake.o" "$o/audio.o" $cobjs -pthread -lm
}
echo "===== LINK ====="
link_tests host gcc ""
link_tests aarch64 aarch64-linux-gnu-gcc "-static"
o="$OUT/host"; objs=""; for x in $APP_OBJS; do objs="$objs $o/$x.o"; done
# shellcheck disable=SC2086
gcc -O3 -o "$o/handshake-init" "$o/init.o" $objs $(CORE_OBJS host) -pthread -lm
# shellcheck disable=SC2086
gcc -O3 -o "$o/handshake-gb" "$o/gb_host.o" $objs $(CORE_OBJS host) -pthread -lm
# Shipped aarch64 binaries: hs_rt + libgcc only. No crt files, no libc, no libpthread, no libm.
o="$OUT/aarch64"; objs=""; for x in $APP_OBJS; do objs="$objs $o/$x.o"; done
SHIPLD="-O3 -static -nostdlib -nostartfiles -Wl,--gc-sections -Wl,-z,noexecstack"
# shellcheck disable=SC2086
aarch64-linux-gnu-gcc $SHIPLD -o "$o/handshake-init" "$o/init.o" $objs $(CORE_OBJS aarch64) $RTOBJS -lgcc
# shellcheck disable=SC2086
aarch64-linux-gnu-gcc $SHIPLD -o "$o/handshake-gb" "$o/gb_host.o" $objs $(CORE_OBJS aarch64) $RTOBJS -lgcc
echo "BUILD LINK OK"
file "$OUT/host/"handshake-* "$OUT/aarch64/"handshake-*
sha256sum "$OUT/aarch64/handshake-init" "$OUT/aarch64/handshake-gb" | tee "$LOG/shipped.sha256"

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
# Patterns are assembled at run time so this script never spells them.
BANW=$(printf '%s%s%s%s' e v i l)
OLDB="$(printf '%s%s' lit ho)( ?os)|ho\\.os"
sc=0
for b in "$OUT"/host/handshake-* "$OUT"/aarch64/handshake-* "$OUT"/host/*.o "$OUT"/aarch64/*.o $(find "$OUT/roms" -type f); do
  h=$( { strings -a "$b" | grep -Ei "$OLDB" ; strings -a "$b" | grep -Eiw "$BANW" ; } || true)
  if [ -n "$h" ]; then sc=$((sc+1)); echo "-- $b: $h" >> "$LOG/strings-check.txt"; fi
done
src_hits=$( { grep -rIEil "$OLDB" src include tests cores tools roms 2>/dev/null || true; grep -rIEilw "$BANW" src include tests cores tools roms 2>/dev/null || true; } | sort -u | wc -l)
echo "STRINGS_SUMMARY binary_hits=$sc source_hits=$src_hits" | tee -a "$LOG/strings-check.txt"
rt_dirty=0
for o in "$OUT"/aarch64/hs_rt/*.o; do
  h=$(aarch64-linux-gnu-nm "$o" | awk '{print $NF}' | grep -Ei "$BANNED" || true)
  if [ -n "$h" ]; then rt_dirty=$((rt_dirty+1)); echo "DIRTY hs_rt $(basename "$o"): $h" >> "$LOG/nm-locks.txt"; else echo "CLEAN  aarch64 hs_rt/$(basename "$o")" >> "$LOG/nm-locks.txt"; fi
done
echo "NM_HS_RT objects=$(ls "$OUT"/aarch64/hs_rt/*.o | wc -l) lock_dirty=$rt_dirty" | tee -a "$LOG/nm-locks.txt"
# ---- host tools (pure C11, HC --allow-runtime): initramfs writer for pack-v5.sh, PNG comparator for run-verify.sh ----
for t in hs_mkcpio:hs-mkcpio hs_pngcmp:hs-pngcmp; do
  s=${t%%:*}; b=${t##*:}
  "$HC" --allow-runtime -std=c11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -c "$ROOT/tools/$s.c" -o "$OUT/tools/$b.o"
  gcc -o "$OUT/tools/$b" "$OUT/tools/$b.o"; rm -f "$OUT/tools/$b.o"
  echo "HOST_TOOL $b ($s.c) sha256=$(sha256sum "$OUT/tools/$b" | cut -c1-16)"
done
# ---- v5.2 review regression tests (REVIEW.md HS-M1..HS-L3), linked like the shipped binaries ----
"$HC" --allow-runtime $CFLAGS $INC -c tests/test_pid1_guard.c -o "$OUT/host/test_pid1_guard.o"
gcc -O2 -o "$OUT/host/test-pid1-guard" "$OUT/host/test_pid1_guard.o"
for t in test_pid1_guard:test-pid1-guard test_hs_rt_review:test-hs-rt-review test_hs_rt_fault:test-hs-rt-fault; do
  s=${t%%:*}; b=${t##*:}
  "$HC" --target=aarch64-linux-gnu -mcpu=cortex-a53 --allow-runtime $CFLAGS $INC -c "tests/$s.c" -o "$OUT/aarch64/$s.o"
  # shellcheck disable=SC2086
  aarch64-linux-gnu-gcc $SHIPLD -o "$OUT/aarch64/$b" "$OUT/aarch64/$s.o" $RTOBJS -lgcc
done
"$OUT/host/test-pid1-guard" | tee "$LOG/regress.txt"
qemu-aarch64-static "$OUT/aarch64/test-pid1-guard" | tee -a "$LOG/regress.txt"
qemu-aarch64-static "$OUT/aarch64/test-hs-rt-review" "$OUT/tmp/hs_rt_review.tmp" | tee -a "$LOG/regress.txt"
# REVIEW HS-O1: each hs_rt fatal path in (forced) PID 1 -> fault ledger + HANDSHAKE-FAULTS.log + boot-screen re-entry
rm -f "$OUT/tmp/HANDSHAKE-FAULTS.log"
for st in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
  HS_FAULT_SEQ=$((st-1)) timeout 30 qemu-aarch64-static "$OUT/aarch64/test-hs-rt-fault" $st "$OUT/tmp/HANDSHAKE-FAULTS.log" </dev/null 2>>"$LOG/hs-rt-fault-stderr.txt" | tee -a "$LOG/regress.txt"
done
cp "$OUT/tmp/HANDSHAKE-FAULTS.log" "$LOG/hs-rt-fault-HANDSHAKE-FAULTS.log"
echo "HS_RT_FAULT_SUMMARY pass=$(grep -c "^HS_RT_FAULT stage=.* PASS" "$LOG/regress.txt") log_lines=$(grep -c "FAULT #.* -> boot screen" "$LOG/hs-rt-fault-HANDSHAKE-FAULTS.log") reboot_syscalls_in_fault_path=$(sed -n "/^static void hs_rt_fault(const char/,/^}/p" src/hs_rt/sys.c | grep -c "NR_reboot\\|NR_exit_group\\|reboot(")" | tee -a "$LOG/regress.txt"
[ "$(grep -c "^HS_RT_FAULT stage=.* PASS" "$LOG/regress.txt")" -eq 14 ]
[ "$(grep -c "^PID1_GUARD PASS" "$LOG/regress.txt")" -eq 2 ] && grep -q "^HS_RT_REVIEW PASS" "$LOG/regress.txt"
echo "REGRESS_OK"
echo BUILD_V5_2_OK
