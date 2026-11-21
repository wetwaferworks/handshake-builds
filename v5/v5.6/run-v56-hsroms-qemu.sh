#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.6
LOG=$OUT/logs
# Do not pack again. Image already at d7d4afc1 / 46faf51a, scan clean, PID1 done.
echo "===== QEMU HSROMS 8G ====="
bash "$V5/run-qemu-hsroms-v5.6.sh" 2>&1 | tee "$LOG/qemu-hsroms-run.txt"
grep -q 'HSROMS_QEMU_OK' "$LOG/qemu-hsroms-run.txt"
echo "===== PRIORS AFTER ====="
sha256sum \
  "$V5/handshakeos-rg35xx-2024-v5.img" \
  "$V5/handshakeos-rg35xx-2024-v5.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.1.img" \
  "$V5/handshakeos-rg35xx-2024-v5.1.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.2.img" \
  "$V5/handshakeos-rg35xx-2024-v5.2.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.3.img" \
  "$V5/handshakeos-rg35xx-2024-v5.3.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.5.img" \
  "$V5/handshakeos-rg35xx-2024-v5.5.img.zst" \
  > "$LOG/priors-after.sha256"
if cmp -s "$LOG/priors-before.sha256" "$LOG/priors-after.sha256"; then
  echo PRIORS_UNTOUCHED | tee "$LOG/priors-check.txt"
else
  echo PRIORS_DIFFER | tee "$LOG/priors-check.txt"
  diff -u "$LOG/priors-before.sha256" "$LOG/priors-after.sha256" || true
  exit 1
fi
grep -q '^aee856069633715e1712d943fa8ca74ce7b01483f9f85af5e55f00c681abbfed ' "$LOG/priors-after.sha256"
grep -q '^cc27bc550a69dc414377e497a3fc2d17b50143077bc1808e20a4329948327267 ' "$LOG/priors-after.sha256"
echo "===== v5.6 SHA256 ====="
sha256sum "$V5/handshakeos-rg35xx-2024-v5.6.img" "$V5/handshakeos-rg35xx-2024-v5.6.img.zst" | tee "$LOG/image-v5.6.sha256"
echo V56_ALL_DONE
