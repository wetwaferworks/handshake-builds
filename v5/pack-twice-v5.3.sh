#!/usr/bin/env bash
# v5.3: install the kernel bundle (built on the box from the v3-audio tree, see kernel/build-kernel-v5.3.sh),
# then run pack-v5.3.sh twice and require byte-identical outputs (run1 == run2).
set -euo pipefail
V5=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v5
OUT=$V5/v5.3
export TMPDIR=$OUT/tmp; mkdir -p "$TMPDIR"
tar xzf "$OUT/scripts/kernel-ship.tgz" --no-same-owner -C "$OUT"
sha256sum "$OUT"/kernel/* | tee "$OUT/logs/kernel-bundle.sha256"
outs() { sha256sum "$V5/handshakeos-rg35xx-2024-v5.3.img" "$V5/handshakeos-rg35xx-2024-v5.3.img.zst" \
  "$V5/handshakeos-initramfs-v5.3.uimg" "$V5/handshakeos-initramfs-v5.3.cpio.gz" "$V5/roms-manifest-v5.3.txt" \
  "$V5"/fel-v5.3/*; }
bash "$V5/pack-v5.3.sh" > "$OUT/logs/pack-run1.txt" 2>&1
outs > "$OUT/logs/pack-run1.sha256"
bash "$V5/pack-v5.3.sh" > "$OUT/logs/pack-run2.txt" 2>&1
outs > "$OUT/logs/pack-run2.sha256"
cat "$OUT/logs/pack-run2.txt"
if cmp "$OUT/logs/pack-run1.sha256" "$OUT/logs/pack-run2.sha256"; then echo "PACK_REPRO run1==run2 IDENTICAL ($(wc -l < "$OUT/logs/pack-run1.sha256") files)"; else echo "PACK_REPRO DIFFER"; exit 1; fi
ls -l "$V5/handshakeos-rg35xx-2024-v5.3.img" "$V5/handshakeos-rg35xx-2024-v5.3.img.zst"
# free space on the FAT partition for ROMS
export PATH="$PATH:/usr/sbin:/sbin"
dd if="$V5/handshakeos-rg35xx-2024-v5.3.img" of="$TMPDIR/p1.fat" bs=512 skip=8192 status=none
minfo -i "$TMPDIR/p1.fat" :: | grep -Ei 'sectors per cluster|free|big size' || true
mdir -i "$TMPDIR/p1.fat" ::/ | tail -n 3
fsck.fat -n "$TMPDIR/p1.fat"
rm -f "$TMPDIR/p1.fat"
echo PACK_TWICE_OK
