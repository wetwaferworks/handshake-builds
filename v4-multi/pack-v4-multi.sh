#!/usr/bin/env bash
# Pack HandshakeOS v4-multi: initramfs uimg, unique-named FEL kit, and a
# 128 MiB cold-boot microSD image. Reads (never writes) v3-audio + fel/fixed.
set -euo pipefail
export PATH="$PATH:/usr/sbin:/sbin"
ROOT=/workspace/handshakeos
OUT=/workspace/scratch/handshakeos/builds/v4-multi
UBOOT=/workspace/scratch/handshakeos/fel/fixed/u-boot-sunxi-with-spl-lpddr3.bin
KGZ=/workspace/scratch/handshakeos/builds/v3-audio/Image-v3-audio.gz
DTB=/workspace/scratch/handshakeos/fel/fixed/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb
UIMG="$OUT/handshakeos-initramfs-v4-multi.uimg"
IMG="$OUT/handshakeos-rg35xx-2024-v4-multi.img"
ST="$OUT/staging"
FEL="$OUT/fel"

# Proven inputs, by hash.
echo "17ce4fb5ce831b9f5c65d$(sha256sum "$UBOOT" | cut -c22-64)" >/dev/null
sha256sum "$UBOOT" | grep -q '^17ce4fb5'
sha256sum "$DTB"   | grep -q '^814740b1'
sha256sum "$KGZ"   | grep -q '^c206e1bc'

# ---- initramfs ----
RD=$(mktemp -d); CPIO="$OUT/handshakeos-initramfs-v4-multi.cpio.gz"
trap 'rm -rf "$RD"' EXIT
mkdir -p "$RD"/{proc,sys,dev,roms,bin,etc,mnt/sd}
cp "$OUT/aarch64/handshake-init" "$RD/init"; chmod 755 "$RD/init"
cp "$OUT/aarch64/handshake-gb" "$RD/bin/handshake-gb"; chmod 755 "$RD/bin/handshake-gb"
cp "$ROOT/roms/libbet.gb" "$ROOT/roms/libbet.LICENSE" "$ROOT/roms/manifest.txt" "$RD/roms/"
cp "$OUT"/roms/* "$RD/roms/"
{
  echo "# HandshakeOS v4-multi built-in clean-room demos (all MIT, generated in-tree, no third-party assets)"
  echo "# file  sha256  system  source"
  for f in handshake-demo.gb hs_cgb_test.gbc hs_gba_demo.gba hs_mode1_demo.sfc hs_mode7_floor.sfc hs_genesis_demo.md; do
    case $f in
      handshake-demo.gb) s="GB   tools/make_handshake_demo_gb.py";;
      hs_cgb_test.gbc) s="GBC  hs-cores tests/test_hs_gbc.c (out/x86_64/hs_cgb_test.gbc)";;
      hs_gba_demo.gba) s="GBA  hs-gba tests/test_hs_gba.c build_rom1 (Mode 0 + sprites + Direct Sound)";;
      hs_mode1_demo.sfc) s="SNES hs-snes tests/snes_testroms.c tr_build_mode1_rom";;
      hs_mode7_floor.sfc) s="SNES hs-snes tests/snes_testroms.c tr_build_mode7_rom";;
      hs_genesis_demo.md) s="GEN  hs-genesis tests/test_hs_genesis.c build_rom (VDP + YM2612 + PSG)";;
    esac
    echo "$f  $(sha256sum "$OUT/roms/$f" | cut -d' ' -f1)  $s"
  done
} > "$RD/roms/manifest-v4-multi.txt"
cp "$RD/roms/manifest-v4-multi.txt" "$OUT/roms-manifest-v4-multi.txt"
cat > "$RD/etc/handshakeos-release" <<'REL'
HandshakeOS RG35XX 2024
release: v4-multi
init: handshake-init (static, hc 0.1.4 @ ad4d8cb)
systems: GB GBC GBA SNES GENESIS (linked), PSX N64 (registry slots, core coming in v4-full)
REL
find "$RD" -exec touch -h -d '2026-11-19 00:00:00 UTC' {} +
( cd "$RD" && find . -print0 | sort -z | cpio --null -H newc -o --quiet --owner=0:0 ) | gzip -9n > "$CPIO"
mkimage -A arm64 -O linux -T ramdisk -C gzip -n "HandshakeOS initramfs" -d "$CPIO" "$UIMG" >/dev/null
mkimage -l "$UIMG"

# ---- FEL kit (unique names) ----
rm -rf "$FEL"; mkdir -p "$FEL"
cp "$UBOOT" "$FEL/u-boot-sunxi-with-spl-lpddr3.bin"
cp "$DTB" "$FEL/sun50i-h700-rg35xx-2024-nv3052c-proven.dtb"
cp "$KGZ" "$FEL/Image-v3-audio.gz"
cp "$UIMG" "$FEL/handshakeos-initramfs-v4-multi.uimg"
cat > "$FEL/boot-v4-multi-fel.txt" <<'BT'
echo "HandshakeOS v4-multi FEL RAM boot (Image-v3-audio.gz)"
unzip 0x44000000 0x40080000
setenv bootargs "console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0"
booti 0x40080000 0x4ff00000 0x4fa00000
BT
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v4-multi FEL boot" -d "$FEL/boot-v4-multi-fel.txt" "$FEL/boot-v4-multi-fel.scr" >/dev/null
printf '%s\r\n' \
'@echo off' \
'setlocal' \
'cd /d "%~dp0"' \
'echo HandshakeOS v4-multi FEL RAM boot (Image-v3-audio.gz, LPDDR3, NV3052C DTB)' \
'echo GB / GBC / GBA / SNES / Genesis clean-room cores, live H700 speaker audio.' \
'echo Zadig WinUSB must be bound to VID 1F3A PID EFE8. Tap Reset into FEL first.' \
'echo.' \
'set SUNXI_FEL_SPL_WAIT_MS=1000' \
'sunxi-fel.exe -v -p uboot u-boot-sunxi-with-spl-lpddr3.bin write 0x44000000 Image-v3-audio.gz write 0x4FA00000 sun50i-h700-rg35xx-2024-nv3052c-proven.dtb write 0x4FF00000 handshakeos-initramfs-v4-multi.uimg write 0x4FC00000 boot-v4-multi-fel.scr' \
'echo.' \
'echo sunxi-fel exited %ERRORLEVEL%.' \
'pause' > "$FEL/06-ramboot-v4-multi.cmd"

# ---- FAT32 staging ----
rm -rf "$ST"; mkdir -p "$ST/ROMS"
cp "$KGZ" "$ST/Image.gz"
cp "$DTB" "$ST/sun50i-h700-anbernic-rg35xx-2024.dtb"
cp "$UIMG" "$ST/handshakeos-initramfs-v4-multi.uimg"
cp "$UBOOT" "$ST/u-boot-sunxi-with-spl.bin"
cat > "$ST/boot.cmd" <<'BC'
echo "HandshakeOS v4-multi SD boot (Image.gz)"
load mmc 0:1 0x44000000 Image.gz
unzip 0x44000000 0x40080000
load mmc 0:1 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024.dtb
load mmc 0:1 0x4FF00000 handshakeos-initramfs-v4-multi.uimg
setenv bootargs "console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0"
booti 0x40080000 0x4ff00000 0x4fa00000
BC
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v4-multi SD boot" -d "$ST/boot.cmd" "$ST/boot.scr" >/dev/null
printf '%s\r\n' \
"HandshakeOS v4-multi cold boot" \
"==============================" \
"Anbernic RG35XX 2024 (Allwinner H700, 1 GB LPDDR3, NV3052C panel)." \
"" \
"On power-on, U-Boot loads from this microSD, runs /boot.scr, and boots" \
"Linux 7.3.5 (v3-audio kernel: speaker, AXP717 battery, power key, Panfrost)," \
"the NV3052C device tree, and the v4-multi initramfs." \
"" \
"v4-multi plays five systems with clean-room HandshakeOS cores:" \
"Game Boy, Game Boy Color, Game Boy Advance, Super Nintendo, Sega Genesis." \
"Six built-in demos are inside the initramfs, so GAMES works with no ROMs." \
"Put your own games in the ROMS folders (see ROMS/README.txt)." \
"" \
"Do not rename the files in the root of this partition (boot.scr, Image.gz," \
"the .dtb, or handshakeos-initramfs-v4-multi.uimg)." > "$ST/README.txt"
printf '%s\r\n' \
"HandshakeOS ROMS" \
"================" \
"Drag and drop your own legally owned game files into these folders:" \
"" \
"  ROMS/GB/       Game Boy (.gb)                 PLAYABLE (hs-gbc)" \
"  ROMS/GBC/      Game Boy Color (.gbc, .gb)     PLAYABLE (hs-gbc)" \
"  ROMS/GBA/      Game Boy Advance (.gba)        PLAYABLE (hs-gba)" \
"  ROMS/SNES/     Super Nintendo (.sfc .smc)     PLAYABLE (hs-snes)" \
"  ROMS/GENESIS/  Sega Genesis (.md .gen .bin)   PLAYABLE (hs-genesis)" \
"  ROMS/PSX/      PlayStation (.bin .cue .exe .chd)  listed, core coming in v4-full" \
"  ROMS/N64/      Nintendo 64 (.z64 .n64 .v64)       listed, core coming in v4-full" \
"" \
"Files directly in ROMS/ are recognised by extension (.bin = Genesis there;" \
"put PlayStation .bin files in ROMS/PSX). The GAMES list is sorted A-Z with a" \
"colour tag per system. Battery saves are written next to the game as" \
"<name>.sav (atomic: .sav.tmp, fsync, rename) every 10 s and on exit." \
"SNES special chips (SuperFX, SA-1, DSP-1..4) are not supported yet." \
"" \
"In a game:" \
"  MENU (or hold SELECT+START 1 s) = save and back to GAMES" \
"  R1 = SHARP UPSAMPLED / NEAREST, full-screen 640x480 (no black bars)" \
"  L1 = DMG palette (Game Boy) or HUD stats overlay (other systems)" \
"  GBA and SNES use L1/R1 as shoulder buttons: hold SELECT for the hotkeys" \
"  R2 (or Y on Game Boy / Color) = 3x fast forward, muted" \
"  Vol+ / Vol- = speaker volume" \
"In GAMES: X = rescan the card, L1/R1 = theme." > "$ST/ROMS/README.txt"

# ---- 128 MiB image: MBR, U-Boot at 8 KiB, FAT32 p1 at 4 MiB ----
FAT="$OUT/.p1.fat"
rm -f "$IMG" "$FAT"
truncate -s 128M "$IMG"
printf 'label: dos\nlabel-id: 0x48534f53\nunit: sectors\n\nstart=8192, size=253952, type=c, bootable\n' | sfdisk -q "$IMG"
truncate -s $((253952*512)) "$FAT"
mkfs.vfat -F 32 -n HANDSHAKEOS -h 8192 -i 48534f53 "$FAT" >/dev/null
for d in ROMS ROMS/GB ROMS/GBC ROMS/GBA ROMS/SNES ROMS/GENESIS ROMS/PSX ROMS/N64; do mmd -i "$FAT" "::/$d"; done
for f in boot.scr boot.cmd Image.gz sun50i-h700-anbernic-rg35xx-2024.dtb handshakeos-initramfs-v4-multi.uimg README.txt; do
  mcopy -i "$FAT" "$ST/$f" "::/$f"
done
mcopy -i "$FAT" "$ST/ROMS/README.txt" ::/ROMS/README.txt
fsck.fat -n "$FAT"
dd if="$FAT" of="$IMG" bs=512 seek=8192 conv=notrunc status=none
dd if="$UBOOT" of="$IMG" bs=1024 seek=8 conv=notrunc status=none
rm -f "$FAT"
rm -f "$IMG.zst"; zstd -19 -q -T0 "$IMG" -o "$IMG.zst"
echo PACK OK
