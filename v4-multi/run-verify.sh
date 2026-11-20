#!/usr/bin/env bash
# Determinism, liveness and multi-system gate for HC v0.1.4 HandshakeOS v4-multi.
set -u
OUT=/workspace/scratch/handshakeos/builds/v4-multi
LOG="$OUT/logs"
PRE="$OUT/previews"
ROOT=/workspace/handshakeos
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
  start=$(date +%s.%N)
  timeout --signal=TERM --kill-after=5s "$to" "$@" >"$out" 2>&1
  rc=$?
  end=$(date +%s.%N)
  elapsed=$(python3 -c "print(f'{float('$end')-float('$start'):.3f}')")
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
"$OUT/host/handshake-multi-test" --roms="$OUT/roms" --out="$PRE" --frames=90 > "$LOG/multi-emu.host.txt" 2>&1; m1=$?
mkdir -p "$LOG/qemu-previews"
timeout 1800 qemu-aarch64-static "$OUT/aarch64/handshake-multi-test" --roms="$OUT/roms" --out="$LOG/qemu-previews" --frames=90 > "$LOG/multi-emu.qemu.txt" 2>&1; m2=$?
cat "$LOG/multi-emu.host.txt" "$LOG/multi-emu.qemu.txt" >> "$LOG/multi-emu-test.txt"
grep -E '^(MULTI|REGISTRY|SAV)' "$LOG/multi-emu.host.txt" > "$LOG/multi-hashes.host.txt"
grep -E '^(MULTI|REGISTRY|SAV)' "$LOG/multi-emu.qemu.txt" > "$LOG/multi-hashes.qemu.txt"
mx=0; cmp -s "$LOG/multi-hashes.host.txt" "$LOG/multi-hashes.qemu.txt" && mx=1
pngx=0; for p in "$LOG"/qemu-previews/*.png; do cmp -s "$p" "$PRE/$(basename "$p")" && pngx=$((pngx+1)); done
npng=$(ls "$LOG"/qemu-previews/*.png | wc -l)
mt=0; [ "$m1" -eq 0 ] && [ "$m2" -eq 0 ] && grep -q "ALL MULTI-EMU TESTS PASSED" "$LOG/multi-emu.host.txt" \
  && grep -q "ALL MULTI-EMU TESTS PASSED" "$LOG/multi-emu.qemu.txt" && [ "$mx" -eq 1 ] && [ "$pngx" -eq "$npng" ] && mt=1
echo "MULTI host_rc=$m1 qemu_rc=$m2 cross_arch_lines_identical=$mx ($(wc -l < "$LOG/multi-hashes.host.txt") lines) png_identical=$pngx/$npng ok=$mt" | tee -a "$LOG/multi-emu-test.txt"

echo "===== handshake-init --multi-selftest (host + qemu) =====" | tee "$LOG/multi-selftest.txt"
"$OUT/host/handshake-init" --multi-selftest="$OUT/roms" > "$LOG/multi-selftest.host.txt" 2>&1; s1=$?
timeout 1200 qemu-aarch64-static "$OUT/aarch64/handshake-init" --multi-selftest="$OUT/roms" > "$LOG/multi-selftest.qemu.txt" 2>&1; s2=$?
cat "$LOG/multi-selftest.host.txt" "$LOG/multi-selftest.qemu.txt" >> "$LOG/multi-selftest.txt"
sx=0; cmp -s "$LOG/multi-selftest.host.txt" "$LOG/multi-selftest.qemu.txt" && sx=1
st=0; [ "$s1" -eq 0 ] && [ "$s2" -eq 0 ] && [ "$sx" -eq 1 ] && st=1
echo "SELFTEST host_rc=$s1 qemu_rc=$s2 identical=$sx ok=$st" | tee -a "$LOG/multi-selftest.txt"

echo "===== picker preview =====" | tee "$LOG/picker-preview.txt"
GR=$(mktemp -d); mkdir -p "$GR/ROMS"
"$OUT/host/handshake-init" --snapshot-games="$PRE/games-picker-v4-multi.png" --games-root="$GR" >> "$LOG/picker-preview.txt" 2>&1; pp=$?
rm -rf "$GR"
echo "PICKER rc=$pp" | tee -a "$LOG/picker-preview.txt"

echo "===== SUMMARY ====="
echo "HOST $hpass/20  QEMU $apass/15  HOST_GB $hgb  QEMU_GB $qgb  GAMES $gm  AUDIO $au  MULTI $mt  SELFTEST $st  PICKER $pp"
if [ "$hpass" -eq 20 ] && [ "$apass" -eq 15 ] && [ "$hgb" -eq 1 ] && [ "$qgb" -eq 1 ] && [ "$gm" -eq 1 ] \
   && [ "$au" -eq 1 ] && [ "$mt" -eq 1 ] && [ "$st" -eq 1 ] && [ "$pp" -eq 0 ]; then
  echo GATE_PASS; exit 0
fi
echo GATE_FAIL; exit 1
