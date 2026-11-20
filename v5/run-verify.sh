#!/usr/bin/env bash
# Determinism, liveness and multi-system gate for HC v0.1.4 HandshakeOS v5.
set -u
OUT=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v5
LOG="$OUT/logs"
PRE="$OUT/previews"
ROOT=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/handshakeos
export TMPDIR="$OUT/tmp"; mkdir -p "$TMPDIR"
GOLDEN=5f44cefa72e8ac19
GB_LEDGER=5a45548614037e20
GB_FRAME=33e3294d476b16a6
GB_AUDIO=8c6078dd78fa5589
mkdir -p "$PRE"
cd "$ROOT"
run_one() { # label timeout cmd...
  local label="$1" to="$2"; shift 2
  local out rc elapsed
  out=$(mktemp)
  local start end
  start=$(date +%s%N)
  timeout --signal=TERM --kill-after=5s "$to" "$@" >"$out" 2>&1
  rc=$?
  end=$(date +%s%N)
  local ms=$(( (end - start + 500000) / 1000000 ))
  elapsed=$(printf '%d.%03d' $((ms / 1000)) $((ms % 1000)))
  local text
  text=$(cat "$out")
  rm -f "$out"

  local hash match frozen fail
  hash=$(printf '%s\n' "$text" | sed -n 's/.*hash=\([0-9a-f]\{16\}\).*/\1/p' | head -1)
  # all four fairness lines must MATCH and equal GOLDEN
  local fm
  fm=$(printf '%s\n' "$text" | grep -c "hash=$GOLDEN  MATCH" || true)
  frozen=$(printf '%s\n' "$text" | grep -c "run_phase failed" || true)
  # also count unexpected watchdog freeze prints
  if [ "$rc" -eq 0 ] && [ "$fm" -eq 4 ] && printf '%s\n' "$text" | grep -q "ALL TESTS PASSED" && [ "$frozen" -eq 0 ]; then
    printf 'PASS  %s  rc=%s  t=%ss  hash=%s  fairness=%s/4 MATCH  frozen_fail=%s\n' \
      "$label" "$rc" "$elapsed" "$hash" "$fm" "$frozen"
    return 0
  fi
  printf 'FAIL  %s  rc=%s  t=%ss  hash=%s  fairness=%s/4  frozen_fail=%s\n' \
    "$label" "$rc" "$elapsed" "${hash:-none}" "$fm" "$frozen"
  printf '%s\n' "$text" | tail -40
  return 1
}

echo "===== HOST handshake-test 20x =====" | tee "$LOG/host-handshake-test.txt"
hpass=0
for i in $(seq -w 1 20); do
  if run_one "host-$i" 60 "$OUT/host/handshake-test" | tee -a "$LOG/host-handshake-test.txt"; then hpass=$((hpass+1)); fi
done
echo "HOST_HANDSHAKE $hpass/20" | tee -a "$LOG/host-handshake-test.txt"

echo "===== ARM64 qemu handshake-test 15x =====" | tee "$LOG/qemu-handshake-test.txt"
apass=0
for i in $(seq -w 1 15); do
  if run_one "qemu-$i" 180 qemu-aarch64-static "$OUT/aarch64/handshake-test" | tee -a "$LOG/qemu-handshake-test.txt"; then apass=$((apass+1)); fi
done
echo "QEMU_HANDSHAKE $apass/15" | tee -a "$LOG/qemu-handshake-test.txt"

gb_ok() { # file rc
  [ "$2" -eq 0 ] && grep -q "golden ledger=$GB_LEDGER frame=$GB_FRAME audio=$GB_AUDIO" "$1" \
    && grep -q "xruns=0 overruns=0" "$1" && grep -q "SCALER 640x480 EDGE-TO-EDGE PASS" "$1" \
    && grep -q "ALL GB TESTS PASSED" "$1"
}
echo "===== HOST handshake-gb-test =====" | tee "$LOG/host-gb-test.txt"
"$OUT/host/handshake-gb-test" "$OUT/logs/gb-640x480-host-scaler.png" > "$LOG/host-gb-test.raw" 2>&1; r=$?
cat "$LOG/host-gb-test.raw" >> "$LOG/host-gb-test.txt"
hgb=0; gb_ok "$LOG/host-gb-test.raw" $r && hgb=1
echo "HOST_GB $hgb rc=$r" | tee -a "$LOG/host-gb-test.txt"
echo "===== ARM64 qemu handshake-gb-test =====" | tee "$LOG/qemu-gb-test.txt"
timeout --kill-after=10s 900 qemu-aarch64-static "$OUT/aarch64/handshake-gb-test" "$OUT/logs/gb-640x480-qemu-scaler.png" > "$LOG/qemu-gb-test.raw" 2>&1; r=$?
cat "$LOG/qemu-gb-test.raw" >> "$LOG/qemu-gb-test.txt"
qgb=0; gb_ok "$LOG/qemu-gb-test.raw" $r && qgb=1
echo "QEMU_GB $qgb rc=$r" | tee -a "$LOG/qemu-gb-test.txt"

echo "===== games smoke (host + qemu) =====" | tee "$LOG/games-test.txt"
"$OUT/host/handshake-games-test" >> "$LOG/games-test.txt" 2>&1; r1=$?
timeout 600 qemu-aarch64-static "$OUT/aarch64/handshake-games-test" >> "$LOG/games-test.txt" 2>&1; r2=$?
gm=0; [ "$r1" -eq 0 ] && [ "$r2" -eq 0 ] && gm=1
echo "GAMES host_rc=$r1 qemu_rc=$r2 ok=$gm" | tee -a "$LOG/games-test.txt"

echo "===== audio unit (host + qemu) =====" | tee "$LOG/audio-test.txt"
"$OUT/host/handshake-audio-test" >> "$LOG/audio-test.txt" 2>&1; a1=$?
timeout 300 qemu-aarch64-static "$OUT/aarch64/handshake-audio-test" >> "$LOG/audio-test.txt" 2>&1; a2=$?
au=0; [ "$a1" -eq 0 ] && [ "$a2" -eq 0 ] && au=1
echo "AUDIO host_rc=$a1 qemu_rc=$a2 ok=$au" | tee -a "$LOG/audio-test.txt"

echo "===== test_multi_emu (host + ARM64 qemu) =====" | tee "$LOG/multi-emu-test.txt"
rm -rf "$PRE/x86_64" "$PRE/aarch64" "$LOG/qemu-previews"; mkdir -p "$PRE/x86_64" "$PRE/aarch64"
"$OUT/host/handshake-multi-test" --roms="$OUT/roms" --out="$PRE/x86_64" --frames=90 > "$LOG/multi-emu.host.txt" 2>&1; m1=$?
timeout 3600 qemu-aarch64-static "$OUT/aarch64/handshake-multi-test" --roms="$OUT/roms" --out="$PRE/aarch64" --frames=90 > "$LOG/multi-emu.qemu.txt" 2>&1; m2=$?
cat "$LOG/multi-emu.host.txt" "$LOG/multi-emu.qemu.txt" >> "$LOG/multi-emu-test.txt"
grep -E '^(MULTI|REGISTRY|SAV|MEM)' "$LOG/multi-emu.host.txt" > "$LOG/multi-hashes.host.txt"
grep -E '^(MULTI|REGISTRY|SAV|MEM)' "$LOG/multi-emu.qemu.txt" > "$LOG/multi-hashes.qemu.txt"
mx=0; cmp -s "$LOG/multi-hashes.host.txt" "$LOG/multi-hashes.qemu.txt" && mx=1
pngx=0; for p in "$PRE"/aarch64/*.png; do cmp -s "$p" "$PRE/x86_64/$(basename "$p")" && pngx=$((pngx+1)); done
npng=$(ls "$PRE"/aarch64/*.png | wc -l)
nh=$(ls "$PRE"/x86_64/*.png | wc -l)
[ "$nh" -eq "$npng" ] || pngx=-1
mt=0; [ "$m1" -eq 0 ] && [ "$m2" -eq 0 ] && grep -q "ALL MULTI-EMU TESTS PASSED" "$LOG/multi-emu.host.txt" \
  && grep -q "ALL MULTI-EMU TESTS PASSED" "$LOG/multi-emu.qemu.txt" && [ "$mx" -eq 1 ] && [ "$pngx" -eq "$npng" ] \
  && [ "$npng" -eq 18 ] && mt=1
echo "MULTI host_rc=$m1 qemu_rc=$m2 cross_arch_lines_identical=$mx ($(wc -l < "$LOG/multi-hashes.host.txt") lines) png_identical=$pngx/$npng ok=$mt" | tee -a "$LOG/multi-emu-test.txt"

echo "===== handshake-init --multi-selftest (host + qemu) =====" | tee "$LOG/multi-selftest.txt"
"$OUT/host/handshake-init" --multi-selftest="$OUT/roms" > "$LOG/multi-selftest.host.txt" 2>&1; s1=$?
timeout 1200 qemu-aarch64-static "$OUT/aarch64/handshake-init" --multi-selftest="$OUT/roms" > "$LOG/multi-selftest.qemu.txt" 2>&1; s2=$?
cat "$LOG/multi-selftest.host.txt" "$LOG/multi-selftest.qemu.txt" >> "$LOG/multi-selftest.txt"
sx=0; cmp -s "$LOG/multi-selftest.host.txt" "$LOG/multi-selftest.qemu.txt" && sx=1
st=0; [ "$s1" -eq 0 ] && [ "$s2" -eq 0 ] && [ "$sx" -eq 1 ] && grep -q "^MULTI systems_ok=7 demos_ok=8$" "$LOG/multi-selftest.host.txt" && st=1
echo "SELFTEST host_rc=$s1 qemu_rc=$s2 identical=$sx ok=$st" | tee -a "$LOG/multi-selftest.txt"

echo "===== picker preview =====" | tee "$LOG/picker-preview.txt"
GR=$(mktemp -d); mkdir -p "$GR/ROMS"
"$OUT/host/handshake-init" --snapshot-games="$PRE/x86_64/games-picker-v5.png" --games-root="$GR" >> "$LOG/picker-preview.txt" 2>&1; pp=$?
timeout 600 qemu-aarch64-static "$OUT/aarch64/handshake-init" --snapshot-games="$PRE/aarch64/games-picker-v5.png" --games-root="$GR" >> "$LOG/picker-preview.txt" 2>&1; pq=$?
cp "$PRE/x86_64/games-picker-v5.png" "$PRE/games-picker-v5.png"
cmp "$PRE/x86_64/games-picker-v5.png" "$PRE/aarch64/games-picker-v5.png" >> "$LOG/picker-preview.txt" 2>&1 || pp=99
[ "$pq" -eq 0 ] || pp=98
rm -rf "$GR"
echo "PICKER rc=$pp (host+qemu byte-identical when 0)" | tee -a "$LOG/picker-preview.txt"

echo "===== regression + core-suite equivalence =====" | tee "$LOG/equivalence.txt"
# 1) the five 2D systems: MULTI lines byte-identical to the v4-multi gate
V4M=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v4-multi/logs
grep -E '^MULTI(-NEAREST)? sys=(GB|GBC|GBA|SNES|SNES-MODE7|GENESIS) ' "$V4M/multi-hashes.host.txt" > "$LOG/eq-v4multi-2d.ref"
grep -E '^MULTI(-NEAREST)? sys=(GB|GBC|GBA|SNES|SNES-MODE7|GENESIS) ' "$LOG/multi-hashes.host.txt" > "$LOG/eq-v4multi-2d.now"
r2d=0; cmp -s "$LOG/eq-v4multi-2d.ref" "$LOG/eq-v4multi-2d.now" && r2d=1
grep -E '^MULTI (GB|GBC|GBA|SNES|GEN) ' "$V4M/multi-selftest.host.txt" > "$LOG/eq-v4multi-selftest.ref"
grep -E '^MULTI (GB|GBC|GBA|SNES|GEN) ' "$LOG/multi-selftest.host.txt" > "$LOG/eq-v4multi-selftest.now"
rst=0; cmp -s "$LOG/eq-v4multi-selftest.ref" "$LOG/eq-v4multi-selftest.now" && rst=1
echo "V4MULTI_2D_LINES identical=$r2d ($(wc -l < "$LOG/eq-v4multi-2d.now") lines)  V4MULTI_SELFTEST_2D identical=$rst ($(wc -l < "$LOG/eq-v4multi-selftest.now") lines)" | tee -a "$LOG/equivalence.txt"
# 2) PS1 / N64 AA previews: pixel-identical to the verified hs-psx / hs-n64 suite PNGs
#    (tools/hs-pngcmp: pure C11 PNG decoder + RGB compare, built by build-v5.sh with HC)
HCR=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/hs-cores
n=0; ok=0
for sysn in psx n64; do
  for m in 1x fxaa 2x 4x 8x; do
    for arch in x86_64 aarch64; do
      if "$OUT/tools/hs-pngcmp" --size=640x480 "$PRE/$arch/demo-$sysn-$m.png" "$HCR/$sysn/out/x86_64/${sysn}_3d_${m}_640x480.png" >/dev/null 2>&1; then
        r=PIXELS_IDENTICAL; ok=$((ok+1))
      else
        r=DIFF
      fi
      n=$((n+1))
      printf 'CORE-SUITE %s %-4s %-7s %s\n' "$sysn" "$m" "$arch" "$r" >> "$LOG/equivalence.txt"
    done
  done
done
echo "CORE_SUITE_PNG identical=$ok/$n" >> "$LOG/equivalence.txt"
grep -q "CORE_SUITE_PNG identical=20/20" "$LOG/equivalence.txt" && csq=1 || csq=0
eq=0; [ "$r2d" -eq 1 ] && [ "$rst" -eq 1 ] && [ "$csq" -eq 1 ] && eq=1
echo "EQUIVALENCE ok=$eq" | tee -a "$LOG/equivalence.txt"

echo "===== SUMMARY ====="
echo "HOST $hpass/20  QEMU $apass/15  HOST_GB $hgb  QEMU_GB $qgb  GAMES $gm  AUDIO $au  MULTI $mt  SELFTEST $st  PICKER $pp  EQUIV $eq"
if [ "$hpass" -eq 20 ] && [ "$apass" -eq 15 ] && [ "$hgb" -eq 1 ] && [ "$qgb" -eq 1 ] && [ "$gm" -eq 1 ] \
   && [ "$au" -eq 1 ] && [ "$mt" -eq 1 ] && [ "$st" -eq 1 ] && [ "$pp" -eq 0 ] && [ "$eq" -eq 1 ]; then
  echo GATE_PASS; exit 0
fi
echo GATE_FAIL; exit 1
