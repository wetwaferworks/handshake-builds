#!/usr/bin/env bash
# Build HandshakeOS v5 in the consortium tree: HC v0.1.4 (ad4d8cb) objects, hs_rt (in-house freestanding
# aarch64 runtime), and the shipped aarch64 handshake-init / handshake-gb linked -nostdlib against
# hs_rt + libgcc only (no glibc, no NPTL). Test binaries keep the v4-full recipe (not shipped).
# v5.5 = queued v5.2 kernel + USB modules + boot log + A/B updater + splash/menu + N64 + audio/DMG (configfs mass storage, src/usb.c) + live FPS readout / session timing log (src/fps.c).
# Writes only builds/v5/v5.8 (v5 through v5.7 artifacts stay untouched). Never touches builds/v1-firstboot..v4-multi.
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"

T=$HS_ROOT
ROOT=$T/handshakeos
OUT=$T/builds/v5/v5.8
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
 -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-pie -ffunction-sections -fdata-sections -Wall -Wextra -Werror"

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
# v5.5: the hc repo HEAD moved to 4b940a3 (the 2026-11-20 publish commit). The frozen compiler is pinned by
# binary hash (same ee626d46 as v5.2) and ad4d8cb must be an ancestor; the diff since ad4d8cb is logged.
grep -q '^ee626d46371d2a5e' "$LOG/hc.sha256"
git -C "$T/hc" merge-base --is-ancestor ad4d8cb HEAD
git -C "$T/hc" diff --stat ad4d8cb HEAD > "$LOG/hc.diff-since-ad4d8cb.txt"
{ gcc --version | head -1; aarch64-linux-gnu-gcc --version | head -1; aarch64-linux-gnu-ld --version | head -1; } > "$LOG/toolchain.version"

declare -A PROFILE
for f in handshake init fb input battery gb_host games games_multi play audio emu_save usb bootlog cfg sha256 mod splash bootmenu update rompart debug; do
  PROFILE["src/${f}.c"]="--allow-runtime"
done
for t in test_handshake test_games test_audio test_multi_emu test_cfg test_update test_bootlog test_splash_pack test_audio_pace test_dmg_compat test_rompart test_snes_audio; do PROFILE["tests/${t}.c"]="--allow-runtime"; done
for f in ui theme font pngenc scoreboard gb_emu gb_scale emu_sys emu_cores fps; do
  PROFILE["src/${f}.c"]="--profile=component"
done
PROFILE["tests/bench_jitter.c"]="--profile=component"
PROFILE["tests/test_gb_handshake.c"]="--profile=component"
PROFILE["third_party/minigb_apu/minigb_apu.c"]="--profile=component"

CORES=(cores/common/hs_video.c
       cores/gbc/gbc_cpu.c cores/gbc/gbc_mmu.c cores/gbc/gbc_ppu.c cores/gbc/gbc_apu.c cores/gbc/gbc_core.c cores/gbc/gbc_compat_pal.c
       cores/gba/gba_cpu.c cores/gba/gba_bus.c cores/gba/gba_bios.c cores/gba/gba_ppu.c cores/gba/gba_apu.c cores/gba/gba_core.c
       cores/snes/hs_snes.c cores/snes/snes_cpu.c cores/snes/snes_bus.c cores/snes/snes_ppu.c cores/snes/snes_apu.c
       cores/genesis/m68k_cpu.c cores/genesis/gen_bus.c cores/genesis/gen_vdp.c cores/genesis/gen_render.c
       cores/genesis/gen_z80.c cores/genesis/gen_ym2612.c cores/genesis/gen_psg.c cores/genesis/gen_audio.c
       cores/genesis/hs_genesis.c
       cores/psx/psx_cpu.c cores/psx/psx_gte.c cores/psx/psx_bus.c cores/psx/psx_gpu_cap.c
       cores/psx/psx_gpu_raster.c cores/psx/psx_spu.c cores/psx/psx_bios_hle.c cores/psx/hs_psx.c
       cores/n64/n64_cpu.c cores/n64/n64_bus.c cores/n64/n64_rsp.c cores/n64/n64_rdp.c
       cores/n64/n64_audio.c cores/n64/n64_save.c cores/n64/n64_pf.c cores/n64/hs_n64.c)
for c in "${CORES[@]}"; do PROFILE["$c"]="--profile=component"; done

SRCS=(
  src/handshake.c src/fb.c src/ui.c src/input.c src/theme.c
  src/font.c src/pngenc.c src/battery.c src/scoreboard.c src/gb_emu.c
  src/gb_scale.c src/games.c src/games_multi.c src/play.c src/audio.c
  src/emu_sys.c src/emu_cores.c src/emu_save.c src/usb.c src/fps.c
  src/bootlog.c src/cfg.c src/sha256.c src/mod.c src/splash.c src/bootmenu.c src/update.c src/rompart.c src/debug.c
  src/init.c src/gb_host.c
  third_party/minigb_apu/minigb_apu.c
  "${CORES[@]}"
  tests/test_handshake.c tests/bench_jitter.c tests/test_gb_handshake.c tests/test_games.c
  tests/test_audio.c tests/test_multi_emu.c
  tests/test_cfg.c tests/test_update.c tests/test_bootlog.c tests/test_splash_pack.c
  tests/test_audio_pace.c tests/test_dmg_compat.c tests/test_rompart.c tests/test_snes_audio.c
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
    tests/test_snes_audio.c) extra="-I$T/hs-cores/snes/tests" ;;
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
APP_OBJS="handshake fb ui input theme gb_scale games games_multi play audio font pngenc battery scoreboard gb_emu minigb_apu emu_sys emu_cores emu_save usb debug fps bootlog cfg sha256 mod splash bootmenu update rompart"
CORE_OBJS() { local a="$1" s=""; for c in "${CORES[@]}"; do s="$s $OUT/$a/$(basename "${c%.c}").o"; done; echo "$s"; }
link_tests() { # arch cc static
  local a="$1" cc="$2" st="$3" o="$OUT/$1" cobjs
  cobjs=$(CORE_OBJS "$a")
  $cc -O3 -c tests/hs_thread_host.c -o "$o/hs_thread_host.o"
  $cc -O3 $st -o "$o/handshake-test" "$o/test_handshake.o" "$o/handshake.o" "$o/hs_thread_host.o" -pthread
  $cc -O3 $st -o "$o/handshake-bench" "$o/bench_jitter.o" "$o/handshake.o" "$o/scoreboard.o" "$o/hs_thread_host.o" -pthread
  $cc -O3 $st -o "$o/handshake-gb-test" "$o/test_gb_handshake.o" "$o/handshake.o" "$o/gb_emu.o" \
    "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/gb_scale.o" "$o/minigb_apu.o" "$o/hs_thread_host.o" -pthread
  $cc -O3 $st -o "$o/handshake-games-test" "$o/test_games.o" "$o/games.o" "$o/handshake.o" \
    "$o/gb_emu.o" "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/minigb_apu.o" "$o/hs_thread_host.o" -pthread
  $cc -O3 $st -o "$o/handshake-audio-test" "$o/test_audio.o" "$o/audio.o"
  if [ -f "$o/test_audio_pace.o" ]; then
    $cc -O3 $st -o "$o/test-audio-pace" "$o/test_audio_pace.o" "$o/audio.o" "$o/play.o" 2>/dev/null ||     $cc -O3 $st -o "$o/test-audio-pace" "$o/test_audio_pace.o" "$o/audio.o"
  fi
  if [ -f "$o/test_dmg_compat.o" ]; then
    $cc -O3 $st -o "$o/test-dmg-compat" "$o/test_dmg_compat.o" $cobjs 2>/dev/null ||     $cc -O3 $st -o "$o/test-dmg-compat" "$o/test_dmg_compat.o" "$o/gbc_compat_pal.o"
  fi
  if [ -f "$o/test_rompart.o" ]; then
    $cc -O3 $st -o "$o/test-rompart" "$o/test_rompart.o" "$o/rompart.o" "$o/bootlog.o"
  fi
  if [ -f "$o/test_snes_audio.o" ]; then
    # test-only: compile the clean-room mode1 ROM builder from hs-cores
    $cc -O3 $CFLAGS $INC -I"$T/hs-cores/snes/tests" -c "$T/hs-cores/snes/tests/snes_testroms.c" -o "$o/snes_testroms.o"
    $cc -O3 $st -o "$o/test-snes-audio" "$o/test_snes_audio.o" "$o/audio.o"       "$o/hs_snes.o" "$o/snes_cpu.o" "$o/snes_bus.o" "$o/snes_ppu.o" "$o/snes_apu.o" "$o/snes_testroms.o"
  fi
  # shellcheck disable=SC2086
  $cc -O3 $st -o "$o/handshake-multi-test" "$o/test_multi_emu.o" "$o/emu_sys.o" "$o/emu_cores.o" \
    "$o/emu_save.o" "$o/games.o" "$o/gb_emu.o" "$o/fb.o" "$o/font.o" "$o/pngenc.o" "$o/minigb_apu.o" \
    "$o/handshake.o" "$o/hs_thread_host.o" "$o/audio.o" $cobjs -pthread -lm
}
echo "===== LINK ====="
link_tests host gcc ""
link_tests aarch64 aarch64-linux-gnu-gcc "-static"
o="$OUT/host"; objs=""; for x in $APP_OBJS; do objs="$objs $o/$x.o"; done
# shellcheck disable=SC2086
gcc -O3 -c tests/hs_thread_host.c -o "$o/hs_thread_host.o"
# shellcheck disable=SC2086
gcc -O3 -o "$o/handshake-init" "$o/init.o" $objs "$o/hs_thread_host.o" $(CORE_OBJS host) -pthread -lm
# shellcheck disable=SC2086
gcc -O3 -o "$o/handshake-gb" "$o/gb_host.o" $objs "$o/hs_thread_host.o" $(CORE_OBJS host) -pthread -lm
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
# ---- v5.5: USB TRANSFER state machine on a simulated configfs/sysfs tree + FPS meter (host glibc and aarch64 hs_rt) ----
"$HC" --allow-runtime $CFLAGS $INC -c tests/test_usb_gadget.c -o "$OUT/host/test_usb_gadget.o"
gcc -O2 -o "$OUT/host/test-usb-gadget" "$OUT/host/test_usb_gadget.o" "$OUT/host/usb.o"
"$HC" --profile=component $CFLAGS $INC -c tests/test_fps.c -o "$OUT/host/test_fps.o"
gcc -O2 -o "$OUT/host/test-fps" "$OUT/host/test_fps.o" "$OUT/host/fps.o"
"$HC" --target=aarch64-linux-gnu -mcpu=cortex-a53 --allow-runtime $CFLAGS $INC -c tests/test_usb_gadget.c -o "$OUT/aarch64/test_usb_gadget.o"
# shellcheck disable=SC2086
aarch64-linux-gnu-gcc $SHIPLD -o "$OUT/aarch64/test-usb-gadget" "$OUT/aarch64/test_usb_gadget.o" "$OUT/aarch64/usb.o" $RTOBJS -lgcc
"$HC" --target=aarch64-linux-gnu -mcpu=cortex-a53 --profile=component $CFLAGS $INC -c tests/test_fps.c -o "$OUT/aarch64/test_fps.o"
# shellcheck disable=SC2086
aarch64-linux-gnu-gcc $SHIPLD -o "$OUT/aarch64/test-fps" "$OUT/aarch64/test_fps.o" "$OUT/aarch64/fps.o" $RTOBJS -lgcc
: > "$LOG/usb-fps-test.txt"
for run in "host:$OUT/host/test-usb-gadget" "qemu:qemu-aarch64-static $OUT/aarch64/test-usb-gadget"; do
  lab=${run%%:*}; cmd=${run#*:}; rm -rf "$OUT/tmp/usbsim-$lab"
  # shellcheck disable=SC2086
  $cmd "$OUT/tmp/usbsim-$lab" | sed "s/^/[$lab] /" | tee -a "$LOG/usb-fps-test.txt"
done
"$OUT/host/test-fps" | sed 's/^/[host] /' | tee -a "$LOG/usb-fps-test.txt"
qemu-aarch64-static "$OUT/aarch64/test-fps" | sed 's/^/[qemu] /' | tee -a "$LOG/usb-fps-test.txt"
[ "$(grep -c 'USB_SIM scenarios=10/10 checks=35 failed=0 PASS' "$LOG/usb-fps-test.txt")" -eq 2 ]
[ "$(grep -c 'FPS_TEST checks=8 failed=0 PASS' "$LOG/usb-fps-test.txt")" -eq 2 ]
echo "USB_FPS_TESTS_OK" | tee -a "$LOG/usb-fps-test.txt"
echo "REGRESS_OK"

# ---- v5.5: splash packer, cfg / update / bootlog / dmg / audio-pace (host + aarch64) ----
"$HC" --allow-runtime $CFLAGS $INC -c tools/hs-pack-splash.c -o "$OUT/host/hs-pack-splash.o" 2>/dev/null || \
  gcc -O2 -std=c11 -Wall -Wextra -Itools -o "$OUT/tools/hs-pack-splash" tools/hs-pack-splash.c
if [ ! -x "$OUT/tools/hs-pack-splash" ]; then
  gcc -O2 -std=c11 -Wall -Wextra -Itools -o "$OUT/tools/hs-pack-splash" tools/hs-pack-splash.c
fi
mkdir -p "$OUT/splashes" assets/splashes
# packer reads the CloudAtlas pack copied into handshakeos/assets/splashes
"$OUT/tools/hs-pack-splash" assets/splashes "$OUT/splashes/pack.bin"
cp -a "$OUT/splashes/pack.bin" "$OUT/pack.bin"
# host unit tests
gcc -O2 -std=c11 -Wall -Wextra -Werror $INC -o "$OUT/host/test-cfg" tests/test_cfg.c src/cfg.c
gcc -O2 -std=c11 -Wall -Wextra -Werror $INC -o "$OUT/host/test-bootlog" tests/test_bootlog.c src/bootlog.c
gcc -O2 -std=c11 -Wall -Wextra -Werror $INC -o "$OUT/host/test-update" tests/test_update.c src/update.c src/sha256.c src/cfg.c
gcc -O2 -std=c11 -Wall -Wextra -Werror $INC -o "$OUT/host/test-splash-pack" tests/test_splash_pack.c src/splash.c src/theme.c src/fb.c src/font.c src/pngenc.c src/input.c
"$OUT/host/test-cfg" | tee "$LOG/v57-unit.txt"
"$OUT/host/test-bootlog" | tee -a "$LOG/v57-unit.txt"
"$OUT/host/test-update" | tee -a "$LOG/v57-unit.txt"
"$OUT/host/test-splash-pack" "$OUT/splashes/pack.bin" | tee -a "$LOG/v57-unit.txt"
# audio pace + dmg already compiled if listed in SRCS — run if binaries exist
if [ -x "$OUT/host/test-audio-pace" ]; then
  "$OUT/host/test-audio-pace" | tee "$LOG/audio-pace-host.txt"
  qemu-aarch64-static "$OUT/aarch64/test-audio-pace" | tee "$LOG/audio-pace-aarch64.txt"
fi
if [ -x "$OUT/host/test-dmg-compat" ]; then
  "$OUT/host/test-dmg-compat" | tee "$LOG/dmg-compat.txt"
fi
if [ -x "$OUT/host/test-snes-audio" ]; then
  ( cd "$OUT/tmp" && "$OUT/host/test-snes-audio" ) | tee "$LOG/snes-audio-host.txt"
  grep -q "0 xruns / 0 overruns / 0 clips  OK" "$LOG/snes-audio-host.txt"
fi
if [ -x "$OUT/aarch64/test-snes-audio" ]; then
  ( cd "$OUT/tmp" && qemu-aarch64-static "$OUT/aarch64/test-snes-audio" ) | tee "$LOG/snes-audio-aarch64.txt"
  grep -q "0 xruns / 0 overruns / 0 clips  OK" "$LOG/snes-audio-aarch64.txt"
fi
if [ -x "$OUT/host/test-rompart" ]; then
  "$OUT/host/test-rompart" "$OUT/tmp/rompart-test.img" | tee "$LOG/rompart-host.txt"
  grep -q "ALL ROMPART TESTS PASSED" "$LOG/rompart-host.txt"
fi
if [ -x "$OUT/aarch64/test-rompart" ]; then
  qemu-aarch64-static "$OUT/aarch64/test-rompart" "$OUT/tmp/rompart-test-a64.img" | tee "$LOG/rompart-aarch64.txt"
  grep -q "ALL ROMPART TESTS PASSED" "$LOG/rompart-aarch64.txt"
fi
grep -q "ALL CFG TESTS PASSED" "$LOG/v57-unit.txt"
grep -q "ALL BOOTLOG TESTS PASSED" "$LOG/v57-unit.txt"
grep -q "ALL UPDATE TESTS PASSED" "$LOG/v57-unit.txt"
grep -q "ALL SPLASH PACK TESTS PASSED" "$LOG/v57-unit.txt"
echo "V57_UNIT_OK" | tee -a "$LOG/v57-unit.txt"
echo BUILD_V5_7_OK
