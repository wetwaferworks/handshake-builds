#!/usr/bin/env bash
# Determinism & liveness gate for hc-compiled HandshakeOS v3-play.
set -u
OUT=/workspace/scratch/handshakeos/builds/v3-play
LOG="$OUT/logs"
ROOT=/workspace/handshakeos
GOLDEN=5f44cefa72e8ac19
GB_LEDGER=5a45548614037e20
GB_FRAME=33e3294d476b16a6
GB_AUDIO=8c6078dd78fa5589

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
  if run_one "host-$i" 60 "$OUT/host/handshake-test" | tee -a "$LOG/host-handshake-test.txt"; then
    hpass=$((hpass+1))
  fi
done
echo "HOST_HANDSHAKE $hpass/20" | tee -a "$LOG/host-handshake-test.txt"

echo "===== ARM64 qemu handshake-test 15x =====" | tee "$LOG/qemu-handshake-test.txt"
apass=0
for i in $(seq -w 1 15); do
  if run_one "qemu-$i" 180 qemu-aarch64-static "$OUT/aarch64/handshake-test" | tee -a "$LOG/qemu-handshake-test.txt"; then
    apass=$((apass+1))
  fi
done
echo "QEMU_HANDSHAKE $apass/15" | tee -a "$LOG/qemu-handshake-test.txt"

echo "===== ARM64 qemu handshake-gb-test =====" | tee "$LOG/qemu-gb-test.txt"
start=$(date +%s.%N)
gbout=$(mktemp)
timeout --signal=TERM --kill-after=10s 600 qemu-aarch64-static "$OUT/aarch64/handshake-gb-test" "$OUT/gb-640x480-qemu-scaler.png" >"$gbout" 2>&1
grc=$?
end=$(date +%s.%N)
elapsed=$(python3 -c "print(f'{float('$end')-float('$start'):.3f}')")
cat "$gbout" | tee -a "$LOG/qemu-gb-test.txt"
gbok=0
if [ "$grc" -eq 0 ] \
   && grep -q "golden ledger=$GB_LEDGER frame=$GB_FRAME audio=$GB_AUDIO" "$gbout" \
   && grep -q "xruns=0 overruns=0" "$gbout" \
   && grep -q "SCALER 640x480 EDGE-TO-EDGE PASS" "$gbout" \
   && grep -q "ALL GB TESTS PASSED" "$gbout"; then
  gbok=1
  echo "QEMU_GB PASS  rc=$grc  t=${elapsed}s" | tee -a "$LOG/qemu-gb-test.txt"
else
  echo "QEMU_GB FAIL  rc=$grc  t=${elapsed}s" | tee -a "$LOG/qemu-gb-test.txt"
fi
rm -f "$gbout"

echo "===== HOST handshake-gb-test =====" | tee "$LOG/host-gb-test.txt"
hgb=0
if "$OUT/host/handshake-gb-test" "$OUT/gb-640x480-host-scaler.png" 2>&1 | tee -a "$LOG/host-gb-test.txt" | grep -q "SCALER 640x480 EDGE-TO-EDGE PASS" \
   && grep -q "golden ledger=$GB_LEDGER frame=$GB_FRAME audio=$GB_AUDIO" "$LOG/host-gb-test.txt" \
   && grep -q "ALL GB TESTS PASSED" "$LOG/host-gb-test.txt"; then hgb=1; fi
echo "HOST_GB $hgb" | tee -a "$LOG/host-gb-test.txt"

echo "===== games smoke (host + qemu) =====" | tee "$LOG/games-test.txt"
gm=0
"$OUT/host/handshake-games-test" 2>&1 | tee -a "$LOG/games-test.txt"
r1=${PIPESTATUS[0]}
timeout 600 qemu-aarch64-static "$OUT/aarch64/handshake-games-test" 2>&1 | tee -a "$LOG/games-test.txt"
r2=${PIPESTATUS[0]}
if [ "$r1" -eq 0 ] && [ "$r2" -eq 0 ]; then gm=1; fi
echo "GAMES host_rc=$r1 qemu_rc=$r2 ok=$gm" | tee -a "$LOG/games-test.txt"

echo "===== SUMMARY ====="
echo "HOST $hpass/20  QEMU $apass/15  QEMU_GB $gbok  HOST_GB $hgb  GAMES $gm"
if [ "$hpass" -eq 20 ] && [ "$apass" -eq 15 ] && [ "$gbok" -eq 1 ] && [ "$hgb" -eq 1 ] && [ "$gm" -eq 1 ]; then
  echo GATE_PASS
  exit 0
fi
echo GATE_FAIL
exit 1
