#!/usr/bin/env bash
# v5.3 QEMU system proofs (qemu-system-aarch64 virt, 4x Cortex-A53, 1 GiB, headless once mode):
#  A) shipped v5.3 kernel (kernel/Image-v5.3-usb.gz) + v5.3 initramfs as PID 1: ledgers, MULTI, no faults.
#  B) TEST-ONLY kernel (qemu-test/Image-TESTONLY-dummy-hcd.gz = v5.3 config + USB_DUMMY_HCD) with a COPY of the
#     v5.3 image as a virtio disk (/dev/vda1 = the FAT partition): PID 1 runs the real USB TRANSFER path
#     (configfs gadget -> dummy_udc), reads the FAT back through the dummy host (usb-storage /dev/sda), unbinds, remounts.
# The H700 MUSB controller itself is not emulated by QEMU: B proves the PID 1 + kernel gadget stack, not the MUSB port.
set -u
V5=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v5
OUT=$V5/v5.3
export TMPDIR=$OUT/tmp; mkdir -p "$TMPDIR"
tar xzf "$OUT/scripts/qemutest.tgz" --no-same-owner -C "$OUT"
K="$OUT/tmp/v5.3-Image"; KT="$OUT/tmp/v5.3-Image-TESTONLY"; D="$OUT/tmp/v5.3-disk-copy.img"
zcat "$OUT/kernel/Image-v5.3-usb.gz" > "$K"
zcat "$OUT/qemu-test/Image-TESTONLY-dummy-hcd.gz" > "$KT"
timeout --kill-after=10s 1500 qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 4 -m 1024 \
  -kernel "$K" -initrd "$V5/handshakeos-initramfs-v5.3.cpio.gz" \
  -append "console=ttyAMA0 handshakeos.once panic=-1" -nographic -no-reboot -monitor none -serial stdio \
  > "$OUT/logs/qemu-pid1-v5.3.log" 2>&1
echo "QEMU_EXIT $?" >> "$OUT/logs/qemu-pid1-v5.3.log"
cp "$V5/handshakeos-rg35xx-2024-v5.3.img" "$D"
timeout --kill-after=10s 1500 qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 4 -m 1024 \
  -kernel "$KT" -initrd "$V5/handshakeos-initramfs-v5.3.cpio.gz" \
  -drive file="$D",if=none,format=raw,id=hsdisk -device virtio-blk-device,drive=hsdisk \
  -append "console=ttyAMA0 handshakeos.once handshakeos.usbselftest panic=-1" -nographic -no-reboot -monitor none -serial stdio \
  > "$OUT/logs/qemu-pid1-v5.3-usbselftest-TESTONLY.log" 2>&1
echo "QEMU_EXIT $?" >> "$OUT/logs/qemu-pid1-v5.3-usbselftest-TESTONLY.log"
rm -f "$K" "$KT" "$D"
for l in qemu-pid1-v5.3.log qemu-pid1-v5.3-usbselftest-TESTONLY.log; do
  echo "== $l"
  grep -aE '^ledger|^GB ledger|^GB xruns|^GB overruns|^MULTI systems_ok|USB_SELFTEST|FAULT|SD mounted|SD not found|QEMU_EXIT|dummy_hcd|g_hs|Kernel panic' "$OUT/logs/$l" | head -n 30
done
