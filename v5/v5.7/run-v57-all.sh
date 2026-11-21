#!/usr/bin/env bash
# Build, pack twice, scan, and QEMU-prove HandshakeOS v5.7.
# Does not overwrite v5..v5.6. No git writes. Never powers off on faults.
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.7
LOG=$OUT/logs
mkdir -p "$LOG" "$OUT/tmp" "$OUT/modules" "$OUT/previews"
export TMPDIR=$OUT/tmp
export PATH="$PATH:/usr/sbin:/sbin"
# Make sure WSL can find mkimage, sfdisk, etc.

echo "===== PRIORS BEFORE ====="
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
  "$V5/handshakeos-rg35xx-2024-v5.6.img" \
  "$V5/handshakeos-rg35xx-2024-v5.6.img.zst" \
  > "$LOG/priors-before.sha256"
# v5.6 pin (must stay d7d4afc1 / 46faf51a)
grep -q '^d7d4afc13bf063d3543c05b0a068c9819e5e8b937d70d5aa43fdc0f6534c483a ' "$LOG/priors-before.sha256"
grep -q '^46faf51a447669fa46d3cbc550971f3a47fe0aa95cfae942887efb1562479fe9 ' "$LOG/priors-before.sha256"
# v5.5 pin
grep -q '^aee856069633715e1712d943fa8ca74ce7b01483f9f85af5e55f00c681abbfed ' "$LOG/priors-before.sha256"
cat "$LOG/priors-before.sha256"

# USB modules from the already-built v5.6 tree (same v5.2 vermagic kernel)
if [ ! -f "$OUT/modules/libcomposite.ko" ]; then
  cp "$V5/v5.6/modules/libcomposite.ko" "$V5/v5.6/modules/usb_f_mass_storage.ko" "$OUT/modules/"
fi
if [ -f "$V5/v5.6/modules/dummy_hcd.ko" ] && [ ! -f "$OUT/modules/dummy_hcd.ko" ]; then
  cp "$V5/v5.6/modules/dummy_hcd.ko" "$OUT/modules/"
fi

echo "===== BUILD v5.7 ====="
bash "$V5/build-v5.7.sh" 2>&1 | tee "$LOG/build.txt"
grep -q '^BUILD_V5_7_OK' "$LOG/build.txt"

echo "===== PACK TWICE ====="
bash "$V5/pack-twice-v5.7.sh" 2>&1 | tee "$LOG/pack-twice.txt"
grep -q '^PACK_TWICE_OK' "$LOG/pack-twice.txt"
grep -q 'PACK_REPRO run1==run2 IDENTICAL' "$LOG/pack-twice.txt"

echo "===== ZERO-MUTEX SCAN ====="
bash "$V5/scan-v5.7.sh" 2>&1 | tee "$LOG/scan-run.txt"
grep -q '^IMAGE_ZERO_MUTEX_CLEAN' "$LOG/scan-run.txt"
grep -q '^LOCK_HITS 0' "$LOG/image-scan.txt"

echo "===== QEMU PID1 ====="
bash "$V5/run-qemu-pid1-v5.7.sh" 2>&1 | tee "$LOG/qemu-pid1-run.txt"
grep -q 'MULTI systems_ok=7' "$LOG/qemu-pid1-run.txt"
grep -q 'USB_SELFTEST dummy_hcd OK' "$LOG/qemu-pid1-run.txt"
grep -q 'QEMU_PID1_DONE' "$LOG/qemu-pid1-run.txt"

echo "===== QEMU HSROMS 8G + USB LUN ====="
bash "$V5/run-qemu-hsroms-v5.7.sh" 2>&1 | tee "$LOG/qemu-hsroms-run.txt"
grep -q '^HSROMS_QEMU_OK' "$LOG/qemu-hsroms-run.txt"
grep -q '^USB_LUN_P2_OK' "$LOG/qemu-hsroms-run.txt"

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
  "$V5/handshakeos-rg35xx-2024-v5.6.img" \
  "$V5/handshakeos-rg35xx-2024-v5.6.img.zst" \
  > "$LOG/priors-after.sha256"
if cmp -s "$LOG/priors-before.sha256" "$LOG/priors-after.sha256"; then
  echo PRIORS_UNTOUCHED | tee "$LOG/priors-check.txt"
else
  echo PRIORS_DIFFER | tee "$LOG/priors-check.txt"
  diff -u "$LOG/priors-before.sha256" "$LOG/priors-after.sha256" || true
  exit 1
fi
grep -q '^d7d4afc13bf063d3543c05b0a068c9819e5e8b937d70d5aa43fdc0f6534c483a ' "$LOG/priors-after.sha256"
grep -q '^46faf51a447669fa46d3cbc550971f3a47fe0aa95cfae942887efb1562479fe9 ' "$LOG/priors-after.sha256"

echo "===== v5.7 HASHES ====="
sha256sum \
  "$V5/handshakeos-rg35xx-2024-v5.7.img" \
  "$V5/handshakeos-rg35xx-2024-v5.7.img.zst" \
  "$V5/handshakeos-initramfs-v5.7.uimg" \
  "$V5/handshakeos-initramfs-v5.7.cpio.gz" \
  | tee "$LOG/image-v5.7.sha256"
ls -l "$V5/handshakeos-rg35xx-2024-v5.7.img" "$V5/handshakeos-rg35xx-2024-v5.7.img.zst"
echo V57_ALL_OK
