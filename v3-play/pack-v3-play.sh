#!/usr/bin/env bash
# Pack HandshakeOS v3-play: FEL initramfs uimg + 128 MiB cold-boot microSD image.
set -euo pipefail
export PATH="$PATH:/usr/sbin:/sbin"
ROOT=/workspace/handshakeos
OUT=/workspace/scratch/handshakeos/builds/v3-play
V2SD=/workspace/scratch/handshakeos/builds/v2-stable-sd/staging
UBOOT=/workspace/scratch/handshakeos/fel/fixed/u-boot-sunxi-with-spl-lpddr3.bin
KGZ=/workspace/scratch/handshakeos/fel/build/Image.gz
DTB=/workspace/scratch/handshakeos/fel/fixed/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb
UIMG="$OUT/handshakeos-initramfs-v3-play.uimg"
IMG="$OUT/handshakeos-rg35xx-2024-v3-play.img"
ST="$OUT/staging"

# Same proven inputs as v2-stable-sd.
cmp "$UBOOT" "$V2SD/u-boot-sunxi-with-spl.bin"
cmp "$KGZ" "$V2SD/Image.gz"
cmp "$DTB" "$V2SD/sun50i-h700-anbernic-rg35xx-2024.dtb"

# ---- initramfs ----
RD=$(mktemp -d); CPIO=$(mktemp --tmpdir hs-v3.XXXXXX.cpio.gz)
trap 'rm -rf "$RD" "$CPIO"' EXIT
mkdir -p "$RD"/{proc,sys,dev,roms,bin,etc,mnt/sd}
cp "$OUT/aarch64/handshake-init" "$RD/init"; chmod 755 "$RD/init"
cp "$OUT/aarch64/handshake-gb" "$RD/bin/handshake-gb"; chmod 755 "$RD/bin/handshake-gb"
cp "$ROOT/roms/handshake-demo.gb" "$ROOT/roms/libbet.gb" "$ROOT/roms/libbet.LICENSE" "$ROOT/roms/manifest.txt" "$RD/roms/"
cat > "$RD/etc/handshakeos-release" <<'REL'
HandshakeOS RG35XX 2024
release: v3-play
init: handshake-init (static, hc 0.1.2)
REL
( cd "$RD" && find . -print0 | sort -z | cpio --null -H newc -o --quiet --owner=0:0 ) | gzip -9n > "$CPIO"
mkimage -A arm64 -O linux -T ramdisk -C gzip -n "HandshakeOS initramfs" -d "$CPIO" "$UIMG" >/dev/null
mkimage -l "$UIMG"

# ---- FAT32 staging ----
rm -rf "$ST"; mkdir -p "$ST/ROMS"
cp "$KGZ" "$ST/Image.gz"
cp "$DTB" "$ST/sun50i-h700-anbernic-rg35xx-2024.dtb"
cp "$UIMG" "$ST/handshakeos-initramfs-v3-play.uimg"
cp "$UBOOT" "$ST/u-boot-sunxi-with-spl.bin"
cat > "$ST/boot.cmd" <<'BC'
echo "HandshakeOS v3-play SD boot (Image.gz)"
load mmc 0:1 0x44000000 Image.gz
unzip 0x44000000 0x40080000
load mmc 0:1 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024.dtb
load mmc 0:1 0x4FF00000 handshakeos-initramfs-v3-play.uimg
setenv bootargs "console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0"
booti 0x40080000 0x4ff00000 0x4fa00000
BC
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v3-play SD boot" -d "$ST/boot.cmd" "$ST/boot.scr" >/dev/null
printf '%s\r\n' \
"HandshakeOS v3-play cold boot" \
"=============================" \
"Anbernic RG35XX 2024 (Allwinner H700, 1 GB LPDDR3, NV3052C panel)." \
"" \
"On power-on, U-Boot loads from this microSD, runs /boot.scr, and boots" \
"Linux 7.3.5, the NV3052C device tree, and the v3-play initramfs." \
"" \
"Put your Game Boy games in ROMS/GB (see ROMS/README.txt)." \
"" \
"Do not rename the files in the root of this partition (boot.scr, Image.gz," \
"the .dtb, or handshakeos-initramfs-v3-play.uimg)." > "$ST/README.txt"
printf '%s\r\n' \
"HandshakeOS ROMS" \
"================" \
"Drag and drop your own legally owned game files into these folders:" \
"" \
"  ROMS/GB/       Game Boy (.gb)          PLAYABLE NOW in v3-play" \
"  ROMS/GBC/      Game Boy Color (.gbc)   folder ready, emulator coming" \
"  ROMS/GBA/      Game Boy Advance (.gba) folder ready, emulator coming" \
"  ROMS/SNES/     Super Nintendo (.sfc .smc) folder ready, emulator coming" \
"  ROMS/GENESIS/  Sega Genesis (.md .bin .gen) folder ready, emulator coming" \
"  ROMS/PSX/      PlayStation (.cue/.bin .chd) folder ready, emulator coming" \
"  ROMS/N64/      Nintendo 64 (.z64 .n64 .v64) folder ready, emulator coming" \
"" \
"Game Boy: .gb files in ROMS/GB (or directly in ROMS) show up in the GAMES" \
"menu, sorted A-Z. Battery saves are written next to the game as <name>.sav." \
"" \
"In a game: MENU (or hold SELECT+START) = back to GAMES, saves first." \
"L1 = palette, R1 = UPSAMPLED / SHARP NEAREST, hold Y or R2 = fast forward." \
"In GAMES: X = rescan the card." > "$ST/ROMS/README.txt"

# ---- 128 MiB image: MBR, U-Boot at 8 KiB, FAT32 p1 at 4 MiB ----
FAT="$OUT/.p1.fat"
rm -f "$IMG" "$FAT"
truncate -s 128M "$IMG"
printf 'label: dos\nlabel-id: 0x48534f53\nunit: sectors\n\nstart=8192, size=253952, type=c, bootable\n' | sfdisk -q "$IMG"
truncate -s $((253952*512)) "$FAT"
mkfs.vfat -F 32 -n HANDSHAKEOS -h 8192 -i 48534f53 "$FAT" >/dev/null
for d in ROMS ROMS/GB ROMS/GBC ROMS/GBA ROMS/SNES ROMS/GENESIS ROMS/PSX ROMS/N64; do mmd -i "$FAT" "::/$d"; done
for f in boot.scr boot.cmd Image.gz sun50i-h700-anbernic-rg35xx-2024.dtb handshakeos-initramfs-v3-play.uimg README.txt; do
  mcopy -i "$FAT" "$ST/$f" "::/$f"
done
mcopy -i "$FAT" "$ST/ROMS/README.txt" ::/ROMS/README.txt
fsck.fat -n "$FAT"
dd if="$FAT" of="$IMG" bs=512 seek=8192 conv=notrunc status=none
dd if="$UBOOT" of="$IMG" bs=1024 seek=8 conv=notrunc status=none
rm -f "$FAT"
rm -f "$IMG.zst"; zstd -19 -q -T0 "$IMG" -o "$IMG.zst"
echo PACK OK
