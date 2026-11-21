#!/usr/bin/env bash
# QEMU virt PID1 proofs for v5.6. Uses the v5.2 kernel (boots on virt) + v5.6 initramfs.
# USB proof: test-only ramdisk overlay adds dummy_hcd.ko (never in the shipped image).
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.6
LOG=$OUT/logs
mkdir -p "$LOG" "$OUT/tmp" "$OUT/qemu-test"
K=$OUT/tmp/Image-v52
zcat "$V5/v5.2/staging/Image.gz" > "$K" 2>/dev/null || zcat "$T/builds/v3-audio/Image.gz" > "$K"
CPIO=$V5/handshakeos-initramfs-v5.6.cpio.gz

run() { # label extra-append initrd
  local label=$1 append=$2 initrd=$3
  timeout --kill-after=10s 180 qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 4 -m 1024 \
    -kernel "$K" -initrd "$initrd" \
    -append "console=ttyAMA0 $append panic=-1" -nographic -no-reboot -monitor none -serial stdio \
    > "$LOG/qemu-pid1-$label.log" 2>&1
  echo "QEMU_EXIT $?" >> "$LOG/qemu-pid1-$label.log"
  echo "===== $label ====="
  grep -E "HandshakeOS|boot mode=|USB_SELFTEST|MULTI systems_ok|PID1|FAULT|QEMU_EXIT" "$LOG/qemu-pid1-$label.log" | tail -40
}

run once "handshakeos.once" "$CPIO"

# USB proof ramdisk: unpack, add dummy_hcd.ko, repack deterministically-enough for the test
RD=$OUT/tmp/qemu-usb-rd
rm -rf "$RD"; mkdir -p "$RD/modules"
( cd "$RD" && zcat "$CPIO" | cpio -id --quiet )
cp "$OUT/modules/dummy_hcd.ko" "$RD/modules/dummy_hcd.ko"
( cd "$RD" && find . | cpio -o -H newc --quiet | gzip -9n > "$OUT/qemu-test/initramfs-usbselftest.cpio.gz" )
run usbselftest "handshakeos.once handshakeos.usbselftest" "$OUT/qemu-test/initramfs-usbselftest.cpio.gz"

# host snapshots of splash + boot menu (no system qemu)
if [ -x "$OUT/host/handshake-init" ]; then
  "$OUT/host/handshake-init" --snapshot-splash="$OUT/previews/splash-v5.6.png" || true
  "$OUT/host/handshake-init" --snapshot-bootmenu="$OUT/previews/bootmenu-v5.6.png" || true
  ls -l "$OUT/previews/splash-v5.6.png" "$OUT/previews/bootmenu-v5.6.png" 2>/dev/null || true
fi
echo QEMU_PID1_DONE
