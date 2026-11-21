#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.6
DEST=/tmp/v56-checkpoint
rm -rf "$DEST"
mkdir -p "$DEST"/{logs,scripts,src/hs_rt,bin,modules,initramfs,n64}

cp -a "$OUT/logs/." "$DEST/logs/"
# scripts
cp -a "$V5/build-v5.6.sh" "$V5/pack-v5.6.sh" "$V5/pack-twice-v5.6.sh" "$V5/run-qemu-pid1-v5.6.sh" "$OUT/run-v56-all.sh" "$DEST/scripts/"
cp -a "$T/hs-cores/n64/build.sh" "$DEST/n64/build.sh"
# hs_rt sources (strtol + %f + prior lseek/realloc)
cp -a "$T/handshakeos/src/hs_rt/." "$DEST/src/hs_rt/"
# shipped-style aarch64 bins
cp -a "$OUT/aarch64/handshake-init" "$OUT/aarch64/handshake-gb" "$DEST/bin/"
cp -a "$T/hs-cores/n64/build/aarch64/bench_n64" "$T/hs-cores/n64/build/aarch64/hs_pf_probe" "$DEST/bin/"
cp -a "$OUT/modules/." "$DEST/modules/"
cp -a "$OUT/splashes/pack.bin" "$DEST/pack.bin" 2>/dev/null || true
cp -a "$V5/handshakeos-initramfs-v5.6.cpio.gz" "$V5/handshakeos-initramfs-v5.6.uimg" "$V5/roms-manifest-v5.6.txt" "$DEST/initramfs/"
# do NOT include img/zst (may still be writing)
cd "$DEST"
tar -czf "$OUT/checkpoint-partial.tar.gz" .
ls -l "$OUT/checkpoint-partial.tar.gz"
sha256sum "$OUT/checkpoint-partial.tar.gz"
echo CHECKPOINT_TAR_OK