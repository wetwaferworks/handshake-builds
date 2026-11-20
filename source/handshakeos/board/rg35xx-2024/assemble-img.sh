#!/bin/bash
# Assemble handshakeos-rg35xx-2024.img from already-built TF-A, LPDDR3 U-Boot, Linux, and handshake-init.
# Default SPL is the verified LPDDR3 build (DCDC3 1.2 V, 672 MHz). Override UBOOT for LPDDR4 boards.
# Does not remount host disks. Builds partition images, then concatenates them.
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
SCR="${SCR:-/workspace/scratch/handshakeos}"
OUT="${OUT:-$SCR/images/handshakeos-rg35xx-2024.img}"
UBOOT="${UBOOT:-$SCR/fel/fixed/u-boot-sunxi-with-spl-lpddr3.bin}"
KERNEL="${KERNEL:-$SCR/src/linux-7.3.5/arch/arm64/boot/Image}"
DTB="${DTB:-$SCR/fel/fixed/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb}"
INIT="${INIT:-$ROOT/build/aarch64/handshake-init}"
GB="${GB:-$ROOT/build/aarch64/handshake-gb}"
ROMS="${ROMS:-$ROOT/roms}"

# Geometry (512-byte sectors). SPL lives in the 8 MiB gap at offset 8 KiB.
#  p1 FAT32 HS_BOOT  64 MiB
#  p2 ext4  HS_ROOT  80 MiB
#  p3 FAT32 HS_ROMS  96 MiB
#  total 8 + 64 + 80 + 96 = 248 MiB
GAP_MIB=8
BOOT_MIB=64
ROOT_MIB=80
ROMS_MIB=96
TOTAL_MIB=$((GAP_MIB + BOOT_MIB + ROOT_MIB + ROMS_MIB))

# Prefer the verified NV3052C-clean DTB. If only the dts is present, compile it.
if [ ! -f "$DTB" ] && [ -f "$ROOT/board/rg35xx-2024/dts/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dts" ]; then
    DTB="$SCR/fel/fixed/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb"
    mkdir -p "$(dirname "$DTB")"
    dtc -q -I dts -O dtb -o "$DTB"         "$ROOT/board/rg35xx-2024/dts/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dts"
fi

for f in "$UBOOT" "$KERNEL" "$DTB" "$INIT" "$GB"; do
    if [ ! -f "$f" ]; then
        echo "missing $f" >&2
        exit 1
    fi
done

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

# --- boot FAT ---
BOOT_IMG="$STAGE/boot.fat"
truncate -s ${BOOT_MIB}M "$BOOT_IMG"
/sbin/mkfs.vfat -F 32 -n HS_BOOT "$BOOT_IMG"
mmd -i "$BOOT_IMG" ::/extlinux
mcopy -i "$BOOT_IMG" "$KERNEL" ::/Image
# NV3052C-clean DTB, also installed under the familiar mainline filename.
mcopy -i "$BOOT_IMG" "$DTB" ::/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb
mcopy -i "$BOOT_IMG" "$DTB" ::/sun50i-h700-anbernic-rg35xx-2024.dtb
cat > "$STAGE/extlinux.conf" << 'EOF'
UI
DEFAULT handshakeos
LABEL handshakeos
  LINUX /Image
  FDT /sun50i-h700-anbernic-rg35xx-2024.dtb
  APPEND console=ttyS0,115200 earlycon=uart8250,mmio32,0x05000000 root=LABEL=HS_ROOT rootwait rw init=/init handshakeos.serial
EOF
mcopy -i "$BOOT_IMG" "$STAGE/extlinux.conf" ::/extlinux/extlinux.conf
# SD-card boot.scr (ARM IH_ARCH, same as the FEL scripts). U-Boot sources this
# if extlinux is missing; both are installed.
cat > "$STAGE/boot.txt" << 'EOF'
echo "HandshakeOS SD boot"
setenv bootargs "console=ttyS0,115200 earlycon=uart8250,mmio32,0x05000000 root=LABEL=HS_ROOT rootwait rw init=/init handshakeos.serial"
load mmc 0:1 ${kernel_addr_r} /Image
load mmc 0:1 ${fdt_addr_r} /sun50i-h700-anbernic-rg35xx-2024.dtb
booti ${kernel_addr_r} - ${fdt_addr_r}
EOF
# sunxi-fel only hands fel_scriptaddr to IH_ARCH_ARM scripts; stay consistent
# on the card as well so a typed `source` never surprises us.
mkimage -A arm -T script -C none -n "HandshakeOS SD boot" -d "$STAGE/boot.txt" "$STAGE/boot.scr" >/dev/null
mcopy -i "$BOOT_IMG" "$STAGE/boot.scr" ::/boot.scr
mcopy -i "$BOOT_IMG" "$STAGE/boot.txt" ::/boot.txt

# --- root ext4 ---
ROOT_DIR="$STAGE/root"
mkdir -p "$ROOT_DIR"/{proc,sys,dev,roms,bin,etc}
cp "$INIT" "$ROOT_DIR/init"
chmod 755 "$ROOT_DIR/init"
cp "$GB" "$ROOT_DIR/bin/handshake-gb"
chmod 755 "$ROOT_DIR/bin/handshake-gb"
cp "$ROMS"/*.gb "$ROOT_DIR/roms/"
cp "$ROMS/manifest.txt" "$ROOT_DIR/roms/"
# also drop licenses if present
cp "$ROMS"/*.LICENSE "$ROOT_DIR/roms/" 2>/dev/null || true
# a tiny motd
cat > "$ROOT_DIR/etc/handshakeos-release" << EOF
HandshakeOS RG35XX 2024
init: handshake-init (static)
EOF
ROOT_IMG="$STAGE/root.ext4"
/sbin/mkfs.ext4 -q -F -L HS_ROOT -b 4096 -d "$ROOT_DIR" "$ROOT_IMG" ${ROOT_MIB}M

# --- roms FAT (Windows-visible) ---
ROMS_IMG="$STAGE/roms.fat"
truncate -s ${ROMS_MIB}M "$ROMS_IMG"
/sbin/mkfs.vfat -F 32 -n HS_ROMS "$ROMS_IMG"
mcopy -i "$ROMS_IMG" "$ROMS"/*.gb ::
mcopy -i "$ROMS_IMG" "$ROMS/manifest.txt" ::
[ -f "$ROMS/libbet.LICENSE" ] && mcopy -i "$ROMS_IMG" "$ROMS/libbet.LICENSE" ::
cat > "$STAGE/README-ROMS.txt" << 'EOF'
HS_ROMS — drop legal, open-source / public-domain Game Boy .gb files here
from Windows Explorer. Commercial ROMs are not included and should not be
added unless you own the rights.

Shipped:
  handshake-demo.gb   MIT, original HandshakeOS demo
  libbet.gb       zlib, Libbet and the Magic Floor v0.08 (Damian Yerrick)
See manifest.txt and libbet.LICENSE.
EOF
mcopy -i "$ROMS_IMG" "$STAGE/README-ROMS.txt" ::

# --- assemble the disk image ---
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
truncate -s ${TOTAL_MIB}M "$OUT"

# MBR partition table
# first-lba: 8MiB / 512 = 16384
BOOT_START=$((GAP_MIB * 2048))
BOOT_SIZE=$((BOOT_MIB * 2048))
ROOT_START=$((BOOT_START + BOOT_SIZE))
ROOT_SIZE=$((ROOT_MIB * 2048))
ROMS_START=$((ROOT_START + ROOT_SIZE))
ROMS_SIZE=$((ROMS_MIB * 2048))

/usr/sbin/sfdisk --quiet "$OUT" << EOF
label: dos
unit: sectors
start=${BOOT_START}, size=${BOOT_SIZE}, type=c, bootable
start=${ROOT_START}, size=${ROOT_SIZE}, type=83
start=${ROMS_START}, size=${ROMS_SIZE}, type=c
EOF

# SPL + U-Boot at 8 KiB (sector 16). Magic eGON.BT0 must land at 0x2004.
dd if="$UBOOT" of="$OUT" bs=1024 seek=8 conv=notrunc status=none
dd if="$BOOT_IMG" of="$OUT" bs=512 seek=${BOOT_START} conv=notrunc status=none
dd if="$ROOT_IMG" of="$OUT" bs=512 seek=${ROOT_START} conv=notrunc status=none
dd if="$ROMS_IMG" of="$OUT" bs=512 seek=${ROMS_START} conv=notrunc status=none

# Verify
magic=$(dd if="$OUT" bs=1 skip=$((0x2004)) count=8 status=none)
echo "image $OUT bytes $(stat -c %s "$OUT")"
if [ "$magic" = "eGON.BT0" ]; then
    echo "eGON.BT0 at 0x2004: $magic OK"
else
    echo "eGON.BT0 at 0x2004: $magic FAIL"
    exit 2
fi
/usr/sbin/fdisk -l "$OUT" || true
sha256sum "$OUT" | tee "$OUT.sha256"
zstd -19 -f -T0 "$OUT" -o "$OUT.zst"
sha256sum "$OUT.zst" | tee "$OUT.zst.sha256"
ls -lh "$OUT" "$OUT.zst" "$OUT.sha256"
echo "assembled $OUT"
