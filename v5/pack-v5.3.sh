#!/usr/bin/env bash
# Pack HandshakeOS v5.3 (v5.2 + USB TRANSFER + FPS readout; same FAT32 layout and FAT file names as v5/v5.2; new kernel
# Image-v5.3-usb.gz = v3-audio config + built-in configfs USB mass-storage gadget). Writes only *-v5.3* names in builds/v5,
# builds/v5/fel-v5.3 and builds/v5/v5.3 (never the v5..v5.2 artifacts): initramfs uimg, unique-named FEL kit, and a
# 128 MiB cold-boot microSD image. Reads (never writes) builds/v3-audio.
set -euo pipefail
export SOURCE_DATE_EPOCH=1795046400  # 2026-11-19 00:00:00 UTC, fixed for reproducible mkimage/mkfs/mtools
export TZ=UTC
export PATH="$PATH:/usr/sbin:/sbin"
ROOT=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/handshakeos
V5=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v5
OUT=$V5/v5.3
export TMPDIR="$OUT/tmp"; mkdir -p "$TMPDIR"
UBOOT=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v3-audio/fel/u-boot-sunxi-with-spl-lpddr3.bin
KGZ=$OUT/kernel/Image-v5.3-usb.gz
DTB=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v3-audio/fel/sun50i-h700-rg35xx-2024-nv3052c-proven.dtb
UIMG="$V5/handshakeos-initramfs-v5.3.uimg"
IMG="$V5/handshakeos-rg35xx-2024-v5.3.img"
ST="$OUT/staging"
FEL="$V5/fel-v5.3"

# Proven inputs, by hash.
sha256sum "$UBOOT" | grep -q '^17ce4fb5'
sha256sum "$DTB"   | grep -q '^814740b1'
sha256sum "$KGZ"   | grep -q '^bf581c90'
sha256sum "$OUT/aarch64/handshake-init" | grep -q '^d2db4a06'
sha256sum "$OUT/aarch64/handshake-gb"   | grep -q '^e588245c'
sha256sum "$V5/FLASH-INSTRUCTIONS.txt" | grep -q '^f471a0ca'
echo "INPUTS PINNED: uboot 17ce4fb5 dtb 814740b1 (unchanged, dr_mode=otg) kernel bf581c90 (v5.3 usb) init d2db4a06 gb e588245c flash-instructions f471a0ca"

# ---- initramfs ----
RD=$(mktemp -d); CPIO="$V5/handshakeos-initramfs-v5.3.cpio.gz"
trap 'rm -rf "$RD"' EXIT
mkdir -p "$RD"/{proc,sys,dev,roms,bin,etc,mnt/sd}
cp "$OUT/aarch64/handshake-init" "$RD/init"; chmod 755 "$RD/init"
cp "$OUT/aarch64/handshake-gb" "$RD/bin/handshake-gb"; chmod 755 "$RD/bin/handshake-gb"
cp "$ROOT/roms/libbet.gb" "$ROOT/roms/libbet.LICENSE" "$ROOT/roms/manifest.txt" "$RD/roms/"
cp -a "$OUT"/roms/. "$RD/roms/"
{
  echo "# HandshakeOS v5 built-in clean-room demos (all MIT, generated in-tree, no third-party assets)"
  echo "# file  sha256  system  source"
  for f in handshake-demo.gb hs_cgb_test.gbc hs_gba_demo.gba hs_mode1_demo.sfc hs_mode7_floor.sfc hs_genesis_demo.md \
           psx/psx_3d_demo.exe n64/hs_n64_demo.z64; do
    case $f in
      handshake-demo.gb) s="GB   tools/make_handshake_demo_gb.c";;
      hs_cgb_test.gbc) s="GBC  hs-cores tests/test_hs_gbc.c (out/x86_64/hs_cgb_test.gbc)";;
      hs_gba_demo.gba) s="GBA  hs-gba tests/test_hs_gba.c build_rom1 (Mode 0 + sprites + Direct Sound)";;
      hs_mode1_demo.sfc) s="SNES hs-snes tests/snes_testroms.c tr_build_mode1_rom";;
      hs_mode7_floor.sfc) s="SNES hs-snes tests/snes_testroms.c tr_build_mode7_rom";;
      hs_genesis_demo.md) s="GEN  hs-genesis tests/test_hs_genesis.c build_rom (VDP + YM2612 + PSG)";;
      psx/psx_3d_demo.exe) s="PS1  hs-psx tools/gen_demo.c (real MIPS PS-EXE: GTE 3D + GPU + SPU)";;
      n64/hs_n64_demo.z64) s="N64  hs-n64 tools/mk_n64_demo.c (CIC-6102 .z64: VR4300 + Fast3D + AI audio)";;
    esac
    echo "$f  $(sha256sum "$OUT/roms/$f" | cut -d' ' -f1)  $s"
  done
} > "$RD/roms/manifest-v5.txt"
cp "$RD/roms/manifest-v5.txt" "$V5/roms-manifest-v5.3.txt"
cat > "$RD/etc/handshakeos-release" <<'REL'
HandshakeOS RG35XX 2024
release: v5.3 (v5.2 + USB TRANSFER: GAMES START exports the SD FAT partition over USB-C; live FPS readout + HANDSHAKE-FPS.log)
kernel: Linux 7.3.5 v5.3 (v3-audio config + USB_CONFIGFS / USB_F_MASS_STORAGE / USB_LIBCOMPOSITE built in)
init: handshake-init (static, hc 0.1.4 @ ad4d8cb, hs_rt runtime: no libc, no mutex)
systems: GB GBC GBA SNES GENESIS PS1 N64 (all 7 clean-room cores linked and live)
aa: PS1 N64 1X (default) FXAA 2X 4X 8X SSAA, SELECT+R1 cycles in game
REL
find "$RD" -exec touch -h -d '2026-11-19 00:00:00 UTC' {} +
"$OUT/tools/hs-mkcpio" "$RD" | gzip -9n > "$CPIO"   # pure C11, built by build-v5.sh with HC
cpio -it --quiet < <(zcat "$CPIO") | wc -l | sed "s/^/CPIO_ENTRIES /"
mkimage -A arm64 -O linux -T ramdisk -C gzip -n "HandshakeOS initramfs" -d "$CPIO" "$UIMG" >/dev/null
mkimage -l "$UIMG"

# ---- FEL kit (unique names) ----
rm -rf "$FEL"; mkdir -p "$FEL"
cp "$UBOOT" "$FEL/u-boot-sunxi-with-spl-lpddr3.bin"
cp "$DTB" "$FEL/sun50i-h700-rg35xx-2024-nv3052c-proven.dtb"
cp "$KGZ" "$FEL/Image-v5.3-usb.gz"
cp "$UIMG" "$FEL/handshakeos-initramfs-v5.uimg"
cat > "$FEL/boot-v5-fel.txt" <<'BT'
echo "HandshakeOS v5.3 FEL RAM boot (Image-v5.3-usb.gz)"
unzip 0x44000000 0x40080000
setenv bootargs "console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0"
booti 0x40080000 0x4ff00000 0x4fa00000
BT
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v5 FEL boot" -d "$FEL/boot-v5-fel.txt" "$FEL/boot-v5-fel.scr" >/dev/null
printf '%s\r\n' \
'@echo off' \
'setlocal' \
'cd /d "%~dp0"' \
'echo HandshakeOS v5.3 FEL RAM boot (Image-v5.3-usb.gz, LPDDR3, NV3052C DTB)' \
'echo All 7 clean-room systems: GB / GBC / GBA / SNES / Genesis / PS1 / N64, live H700 speaker audio.' \
'echo Zadig WinUSB must be bound to VID 1F3A PID EFE8. Tap Reset into FEL first.' \
'echo.' \
'set SUNXI_FEL_SPL_WAIT_MS=1000' \
'sunxi-fel.exe -v -p uboot u-boot-sunxi-with-spl-lpddr3.bin write 0x44000000 Image-v5.3-usb.gz write 0x4FA00000 sun50i-h700-rg35xx-2024-nv3052c-proven.dtb write 0x4FF00000 handshakeos-initramfs-v5.uimg write 0x4FC00000 boot-v5-fel.scr' \
'echo.' \
'echo sunxi-fel exited %ERRORLEVEL%.' \
'pause' > "$FEL/07-ramboot-v5.cmd"

# ---- FAT32 staging ----
rm -rf "$ST"; mkdir -p "$ST/ROMS"
cp "$KGZ" "$ST/Image.gz"
cp "$DTB" "$ST/sun50i-h700-anbernic-rg35xx-2024.dtb"
cp "$UIMG" "$ST/handshakeos-initramfs-v5.uimg"
cp "$UBOOT" "$ST/u-boot-sunxi-with-spl.bin"
cp "$V5/FLASH-INSTRUCTIONS.txt" "$ST/FLASH-INSTRUCTIONS.txt"
cat > "$ST/boot.cmd" <<'BC'
echo "HandshakeOS v5.3 SD boot (Image.gz)"
load mmc 0:1 0x44000000 Image.gz
unzip 0x44000000 0x40080000
load mmc 0:1 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024.dtb
load mmc 0:1 0x4FF00000 handshakeos-initramfs-v5.uimg
setenv bootargs "console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0"
booti 0x40080000 0x4ff00000 0x4fa00000
BC
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v5.3 SD boot" -d "$ST/boot.cmd" "$ST/boot.scr" >/dev/null
printf '%s\r\n' \
"HandshakeOS v5.3 cold boot" \
"==============================" \
"Anbernic RG35XX 2024 (Allwinner H700, 1 GB LPDDR3, NV3052C panel)." \
"" \
"On power-on, U-Boot loads from this microSD, runs /boot.scr, and boots" \
"Linux 7.3.5 (v5.3 kernel: speaker, AXP717 battery, power key, Panfrost," \
"USB mass storage), the NV3052C device tree, and the v5.3 initramfs." \
"" \
"v5 plays all seven systems with clean-room HandshakeOS cores:" \
"Game Boy, Game Boy Color, Game Boy Advance, Super Nintendo, Sega Genesis," \
"PlayStation (PS1) and Nintendo 64." \
"Eight built-in demos are inside the initramfs, so GAMES works with no ROMs." \
"Put your own games in the ROMS folders (see ROMS/README.txt)." \
"" \
"USB TRANSFER (v5.3): in GAMES press START, plug the USB-C port into the" \
"PC, and this partition shows up as a drive (HANDSHAKEOS). Copy games into" \
"ROMS/<SYSTEM>, eject the drive in Windows, then press B on the handheld." \
"" \
"Do not rename the files in the root of this partition (boot.scr, Image.gz," \
"the .dtb, or handshakeos-initramfs-v5.uimg)." > "$ST/README.txt"
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
"  ROMS/PSX/      PlayStation PS-X EXE (.exe .psexe .psx .bin)  PLAYABLE (hs-psx)" \
"  ROMS/N64/      Nintendo 64 (.z64 .v64 .n64)  PLAYABLE (hs-n64, any byte order)" \
"" \
"PS1: PS-X EXE programs only for now (no CD-ROM images: .cue/.chd/disc .bin" \
"are not supported yet). Files directly in ROMS/ are recognised by extension" \
"(.bin = Genesis there; put PS-X EXE .bin files in ROMS/PSX). The GAMES list is sorted A-Z with a" \
"colour tag per system. Battery saves are written next to the game as" \
"<name>.sav (atomic: .sav.tmp, fsync, rename) every 10 s and on exit." \
"SNES special chips (SuperFX, SA-1, DSP-1..4) are not supported yet." \
"" \
"In a game:" \
"  MENU (or hold SELECT+START 1 s) = save and back to GAMES" \
"  R1 = SHARP UPSAMPLED / NEAREST, full-screen 640x480 (no black bars)" \
"  L1 = DMG palette (Game Boy) or HUD stats overlay (other systems)" \
"  GBA, SNES, PS1 and N64 use L1/R1 as shoulder buttons: hold SELECT for the hotkeys" \
"  PS1 / N64: SELECT+R1 = next anti-aliasing mode, shown as a toast:" \
"             AA: 1X (default) -> FXAA -> 2X SSAA -> 4X SSAA -> 8X SSAA -> 1X" \
"             SELECT+L1 = HUD (FPS + current AA mode)" \
"  PS1 pad: B Cross, A Circle, X Triangle, Y Square, L1/R1/L2/R2 as on the pad" \
"  N64 pad: B = A, Y = B, L2 = Z, L1/R1 = L/R, X = C-Up, A = C-Down, R2 = C-Right" \
"  R2 (or Y on Game Boy / Color) = 3x fast forward, muted (2D systems)" \
"  Vol+ / Vol- = speaker volume" \
"  FPS readout (top right, on by default): FPS = game speed, DRAW = frames" \
"             shown per second, MS = work per shown frame. SELECT+L2 = on/off." \
"             Each play session appends one timing line to HANDSHAKE-FPS.log" \
"             in the root of this card." \
"In GAMES: X = rescan the card, L1/R1 = theme," \
"          START = USB TRANSFER (eject on the PC, then B; ROMS rescan)." > "$ST/ROMS/README.txt"

find "$ST" -exec touch -h -d "2026-11-19 00:00:00 UTC" {} +
# ---- 128 MiB image: MBR, U-Boot at 8 KiB, FAT32 p1 at 4 MiB ----
FAT="$OUT/.p1.fat"
rm -f "$IMG" "$FAT"
truncate -s 128M "$IMG"
printf 'label: dos\nlabel-id: 0x48534f53\nunit: sectors\n\nstart=8192, size=253952, type=c, bootable\n' | sfdisk -q "$IMG"
truncate -s $((253952*512)) "$FAT"
mkfs.vfat -F 32 -h 8192 -i 48534f53 "$FAT" >/dev/null
# label via mtools: dosfstools 4.2 stamps the label entry with the wall clock, mtools honours SOURCE_DATE_EPOCH
mlabel -i "$FAT" -N 48534f53 ::HANDSHAKEOS
for d in ROMS ROMS/GB ROMS/GBC ROMS/GBA ROMS/SNES ROMS/GENESIS ROMS/PSX ROMS/N64; do mmd -i "$FAT" "::/$d"; done
for f in boot.scr boot.cmd Image.gz sun50i-h700-anbernic-rg35xx-2024.dtb handshakeos-initramfs-v5.uimg README.txt FLASH-INSTRUCTIONS.txt; do
  mcopy -m -i "$FAT" "$ST/$f" "::/$f"
done
mcopy -m -i "$FAT" "$ST/ROMS/README.txt" ::/ROMS/README.txt
fsck.fat -n "$FAT"
dd if="$FAT" of="$IMG" bs=512 seek=8192 conv=notrunc status=none
dd if="$UBOOT" of="$IMG" bs=1024 seek=8 conv=notrunc status=none
rm -f "$FAT"
rm -f "$IMG.zst"; zstd -19 -q -T0 "$IMG" -o "$IMG.zst"
sha256sum "$IMG" "$IMG.zst" "$UIMG" "$CPIO" "$V5/roms-manifest-v5.3.txt"
echo PACK OK
