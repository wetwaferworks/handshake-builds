#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
V5=$HS_ROOT/builds/v5
OUT=$V5/v5.7
export TMPDIR=$OUT/tmp; mkdir -p "$TMPDIR"
outs() { sha256sum "$V5/handshakeos-rg35xx-2024-v5.7.img" "$V5/handshakeos-rg35xx-2024-v5.7.img.zst" \
  "$V5/handshakeos-initramfs-v5.7.uimg" "$V5/handshakeos-initramfs-v5.7.cpio.gz" "$V5/roms-manifest-v5.7.txt" \
  "$V5"/fel-v5.7/*; }
bash "$V5/pack-v5.7.sh" > "$OUT/logs/pack-run1.txt" 2>&1
outs > "$OUT/logs/pack-run1.sha256"
bash "$V5/pack-v5.7.sh" > "$OUT/logs/pack-run2.txt" 2>&1
outs > "$OUT/logs/pack-run2.sha256"
cat "$OUT/logs/pack-run2.txt"
if cmp "$OUT/logs/pack-run1.sha256" "$OUT/logs/pack-run2.sha256"; then
  echo "PACK_REPRO run1==run2 IDENTICAL ($(wc -l < "$OUT/logs/pack-run1.sha256") files)"
else
  echo "PACK_REPRO DIFFER"; diff -u "$OUT/logs/pack-run1.sha256" "$OUT/logs/pack-run2.sha256" || true
  exit 1
fi
ls -l "$V5/handshakeos-rg35xx-2024-v5.7.img" "$V5/handshakeos-rg35xx-2024-v5.7.img.zst"
export PATH="$PATH:/usr/sbin:/sbin"
dd if="$V5/handshakeos-rg35xx-2024-v5.7.img" of="$TMPDIR/p1.fat" bs=512 skip=8192 status=none
minfo -i "$TMPDIR/p1.fat" :: | grep -Ei 'sectors per cluster|free|big size' || true
mdir -i "$TMPDIR/p1.fat" ::/ | tail -n 8
fsck.fat -n "$TMPDIR/p1.fat"
rm -f "$TMPDIR/p1.fat"
echo PACK_TWICE_OK
