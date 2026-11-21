#!/bin/bash
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
V5=$HS_ROOT/builds/v5
L=$V5/v5.7/logs
echo "======= HASHES ======="
cat "$L/image-v5.7.sha256"
ls -l "$V5/handshakeos-rg35xx-2024-v5.7.img" "$V5/handshakeos-rg35xx-2024-v5.7.img.zst" \
  "$V5/handshakeos-initramfs-v5.7.uimg" "$V5/handshakeos-initramfs-v5.7.cpio.gz"
echo "======= PACK ======="
grep -E 'PACK_REPRO|PACK_TWICE|CPIO_ENTRIES|PACK OK|INPUTS PINNED' "$L/pack-twice.txt"
echo "======= SCAN ======="
grep -E 'LOCK_HITS|IMAGE_ZERO|CPIO_|CLEAN |HIT ' "$L/image-scan.txt"
echo "======= BUILD CHECKS ======="
grep -E 'USB_SIM|USB_FPS|FPS_TEST|ALL CFG|V57_UNIT|SNES_AUDIO_PACE|ALL AUDIO PACE|ALL ROMPART|ALL DMG|BUILD_V5' "$L/build.txt"
echo "======= QEMU PID1 ======="
grep -E 'MULTI systems_ok|USB_SELFTEST|QEMU_PID1_DONE|QEMU_EXIT' "$L/qemu-pid1-run.txt"
echo "======= HSROMS + USB LUN ======="
grep -E 'HSROMS |USB_SELFTEST|USB_LUN|P1_UNCHANGED|GAMES |GAMES_ROM|HSROMS_QEMU|VOL_ID' "$L/qemu-hsroms-run.txt"
echo "======= PRIORS ======="
cat "$L/priors-check.txt"
echo "======= PREVIEWS ======="
ls -l "$V5/v5.7/previews/" 2>/dev/null || true
echo "======= v5.6 still ======="
sha256sum "$V5/handshakeos-rg35xx-2024-v5.6.img" "$V5/handshakeos-rg35xx-2024-v5.6.img.zst"
echo "======= CPIO_ENTRIES from pack-run2 ======="
grep CPIO "$L/pack-run2.txt"
date
