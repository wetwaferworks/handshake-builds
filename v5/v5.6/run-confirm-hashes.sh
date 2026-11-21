#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
V5=$HS_ROOT/builds/v5
echo "===== CURRENT SHA256 ====="
sha256sum \
  "$V5/handshakeos-rg35xx-2024-v5.5.img" \
  "$V5/handshakeos-rg35xx-2024-v5.5.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.6.img" \
  "$V5/handshakeos-rg35xx-2024-v5.6.img.zst"
echo "===== QEMU GREP ====="
grep -E "HandshakeOS|MULTI systems|USB_SELFTEST|QEMU_EXIT|QEMU_PID1|boot mode" "$V5/v5.6/logs/qemu-pid1-run.txt" || true
echo "===== PRIORS CHECK ====="
cat "$V5/v5.6/logs/priors-check.txt"
cmp -s "$V5/v5.6/logs/priors-before.sha256" "$V5/v5.6/logs/priors-after.sha256" && echo PRIORS_FILES_IDENTICAL
echo "===== SCAN TAIL ====="
grep -E "HIT|CLEAN|LOCK_HITS|IMAGE_" "$V5/v5.6/logs/image-scan.txt" | tail -30