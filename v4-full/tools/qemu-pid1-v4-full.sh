#!/usr/bin/env bash
# Real Linux 7.3.5 (v3-audio kernel, decompressed from Image-v3-audio.gz) PID 1 boot of the
# v4-full initramfs under qemu-system-aarch64 virt, 4x Cortex-A53, 1 GiB, headless once mode.
set -u
OUT=/workspace/scratch/handshakeos/builds/v4-full
K=/tmp/v4f-Image
zcat /workspace/scratch/handshakeos/builds/v3-audio/Image-v3-audio.gz > "$K"
timeout --kill-after=10s 1500 qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 4 -m 1024 \
  -kernel "$K" -initrd "$OUT/handshakeos-initramfs-v4-full.cpio.gz" \
  -append "console=ttyAMA0 handshakeos.once panic=-1" -nographic -no-reboot -monitor none -serial stdio \
  > "$OUT/logs/qemu-pid1-v4-full.log" 2>&1
echo "QEMU_EXIT $?" >> "$OUT/logs/qemu-pid1-v4-full.log"
