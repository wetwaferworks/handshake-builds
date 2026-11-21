#!/bin/bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.9
LOG=$OUT/logs
PACK=$OUT/scripts/pack-v5.9.sh
IMG=$V5/handshakeos-rg35xx-2024-v5.9.img
mkdir -p "$LOG"

# Refuse to touch v5.8 / v5.7 images.
test -f "$V5/handshakeos-rg35xx-2024-v5.8.img"
test -f "$PACK"
test -x "$OUT/aarch64/handshake-init"

echo "=== pack 1 ==="
# setsid not needed if we stay attached; this script is the pack run.
bash "$PACK" 2>&1 | tee "$LOG/pack-v5.9-1.txt"
test -f "$IMG"
test -f "$IMG.zst"
sha256sum "$IMG" "$IMG.zst" | tee "$LOG/pack-v5.9-hash-1.txt"
cp -a "$IMG" "$OUT/tmp/v5.9-pack1.img"
cp -a "$IMG.zst" "$OUT/tmp/v5.9-pack1.img.zst"
# Keep first hashes only; second pack overwrites $IMG.

echo "=== pack 2 ==="
bash "$PACK" 2>&1 | tee "$LOG/pack-v5.9-2.txt"
sha256sum "$IMG" "$IMG.zst" | tee "$LOG/pack-v5.9-hash-2.txt"

if cmp -s "$LOG/pack-v5.9-hash-1.txt" "$LOG/pack-v5.9-hash-2.txt"; then
  echo "PACK_TWICE identical"
else
  echo "PACK_TWICE MISMATCH" >&2
  cat "$LOG/pack-v5.9-hash-1.txt" "$LOG/pack-v5.9-hash-2.txt" >&2
  exit 1
fi

# Confirm v5.8 image hash unchanged
sha256sum "$V5/handshakeos-rg35xx-2024-v5.8.img" | tee "$LOG/v5.8-img-after-v5.9-pack.txt"

{
  echo "# HandshakeOS v5.9 SHA-256"
  sha256sum "$IMG" "$IMG.zst"
  if [ -f "$V5/handshakeos-initramfs-v5.9.uimg" ]; then
    sha256sum "$V5/handshakeos-initramfs-v5.9.uimg"
  fi
  if [ -f "$V5/handshakeos-initramfs-v5.9.cpio.gz" ]; then
    sha256sum "$V5/handshakeos-initramfs-v5.9.cpio.gz"
  fi
  sha256sum "$V5/v5.8/kernel/Image.gz"
} | tee "$V5/SHA256SUMS-v5.9"

echo "WROTE $V5/SHA256SUMS-v5.9"
ls -l "$IMG" "$IMG.zst"
