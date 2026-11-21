#!/usr/bin/env bash
# Pack HandshakeOS v5.9: Handshake phase-4 kernel (0001-0004 on the v5.2 / v3-audio
# device config) + CDC-ACM debug + USB gadget modules. Writes only *-v5.9* names
# and builds/v5/handshakeos-rg35xx-2024-v5.9.img (never v5..v5.8 artifacts (kernel still comes from v5.8)).
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
export SOURCE_DATE_EPOCH=1795046400  # 2026-11-19 00:00:00 UTC
export TZ=UTC
export PATH="$PATH:/usr/sbin:/sbin"
T=$HS_ROOT
ROOT=$T/handshakeos
V5=$T/builds/v5
OUT=$V5/v5.9
export TMPDIR="$OUT/tmp"; mkdir -p "$TMPDIR"
UBOOT=$T/builds/v3-audio/fel/u-boot-sunxi-with-spl-lpddr3.bin
# v5.8 Handshake-patched device kernel (built in builds/v5/v5.8/kernel).
# Keep the proven NV3052C DTB (814740b1) unless a rebuilt dtb is bit-identical.
KGZ=$V5/v5.8/kernel/Image.gz
DTB=$T/builds/v3-audio/fel/sun50i-h700-rg35xx-2024-nv3052c-proven.dtb
FLASH=$V5/v5.9/FLASH-INSTRUCTIONS.txt
UIMG="$V5/handshakeos-initramfs-v5.9.uimg"
CPIO="$V5/handshakeos-initramfs-v5.9.cpio.gz"
IMG="$V5/handshakeos-rg35xx-2024-v5.9.img"
ST="$OUT/staging"
FEL="$V5/fel-v5.9"
MODS=$V5/v5.8/kernel/modules
PACK=$OUT/splashes/pack.bin
if [ ! -f "$PACK" ] && [ -f "$V5/v5.7/splashes/pack.bin" ]; then
  mkdir -p "$OUT/splashes"
  cp "$V5/v5.7/splashes/pack.bin" "$PACK"
fi
if [ ! -f "$MODS/libcomposite.ko" ]; then
  echo "missing $MODS/libcomposite.ko (build the v5.8 kernel first)" >&2
  exit 1
fi

sha256sum "$UBOOT" | grep -q '^17ce4fb5'
sha256sum "$DTB"   | grep -q '^814740b1'
test -f "$KGZ" || { echo "missing $KGZ" >&2; exit 1; }
test -f "$FLASH" || { echo "missing $FLASH" >&2; exit 1; }
echo "INPUTS PINNED: uboot 17ce4fb5 dtb 814740b1 kernel $(sha256sum "$KGZ" | awk '{print substr($1,1,8)}') flash-instructions $(sha256sum "$FLASH" | awk '{print substr($1,1,8)}')"

# ---- initramfs ----
RD=$(mktemp -d)
trap 'rm -rf "$RD"' EXIT
mkdir -p "$RD"/{proc,sys,dev,roms,bin,etc,mnt/sd,mnt/hsroms,modules,splashes}
cp "$OUT/aarch64/handshake-init" "$RD/init"; chmod 755 "$RD/init"
cp "$OUT/aarch64/handshake-gb" "$RD/bin/handshake-gb"; chmod 755 "$RD/bin/handshake-gb"
# Optional N64 tools (built by hs-cores/n64/build.sh); skip if absent.
for t in bench_n64 hs_pf_probe; do
  if [ -x "$T/hs-cores/n64/build/aarch64/$t" ]; then
    cp "$T/hs-cores/n64/build/aarch64/$t" "$RD/bin/$t"
    chmod 755 "$RD/bin/$t"
  elif [ -x "$OUT/aarch64/$t" ]; then
    cp "$OUT/aarch64/$t" "$RD/bin/$t"
    chmod 755 "$RD/bin/$t"
  fi
done
# USB gadget modules (v5.2 vermagic). Never ship dummy_hcd.ko in the device image.
if [ ! -f "$MODS/libcomposite.ko" ]; then
  echo "missing $MODS/libcomposite.ko" >&2; exit 1
fi
# USB gadget modules (v5.8 vermagic). Never ship dummy_hcd.ko in the device image.
cp "$MODS/libcomposite.ko" "$MODS/usb_f_mass_storage.ko" "$MODS/usb_f_acm.ko" "$RD/modules/"
for extra in usb_configfs.ko u_serial.ko; do
  if [ -f "$MODS/$extra" ]; then
    cp "$MODS/$extra" "$RD/modules/"
  fi
done
if [ -f "$PACK" ]; then
  cp "$PACK" "$RD/splashes/pack.bin"
else
  echo "missing splash pack $PACK" >&2; exit 1
fi
cp "$ROOT/roms/libbet.gb" "$ROOT/roms/libbet.LICENSE" "$ROOT/roms/manifest.txt" "$RD/roms/"
cp -a "$OUT"/roms/. "$RD/roms/"
{
  echo "# HandshakeOS v5.9 built-in clean-room demos (all MIT, generated in-tree, no third-party assets)"
  echo "# file  sha256  system  source"
  for f in handshake-demo.gb hs_cgb_test.gbc hs_gba_demo.gba hs_mode1_demo.sfc hs_mode7_floor.sfc hs_genesis_demo.md \
           psx/psx_3d_demo.exe n64/hs_n64_demo.z64; do
    case $f in
      handshake-demo.gb) s="GB   tools/make_handshake_demo_gb.c";;
      hs_cgb_test.gbc) s="GBC  hs-cores tests/test_hs_gbc.c";;
      hs_gba_demo.gba) s="GBA  hs-gba tests/test_hs_gba.c";;
      hs_mode1_demo.sfc) s="SNES hs-snes tests/snes_testroms.c tr_build_mode1_rom";;
      hs_mode7_floor.sfc) s="SNES hs-snes tests/snes_testroms.c tr_build_mode7_rom";;
      hs_genesis_demo.md) s="GEN  hs-genesis tests/test_hs_genesis.c";;
      psx/psx_3d_demo.exe) s="PS1  hs-psx tools/gen_demo.c";;
      n64/hs_n64_demo.z64) s="N64  hs-n64 tools/mk_n64_demo.c";;
    esac
    echo "$f  $(sha256sum "$OUT/roms/$f" | cut -d' ' -f1)  $s"
  done
} > "$RD/roms/manifest-v5.txt"
cp "$RD/roms/manifest-v5.txt" "$V5/roms-manifest-v5.9.txt"
cat > "$RD/etc/handshakeos-release" <<'REL'
HandshakeOS RG35XX 2024
release: v5.9 (v5.8 kernel + v5.9 userspace, CDC-ACM USB debug, hsdebug)
kernel: Linux 7.3.5 + ROCKNIX + Handshake 0001-0004 (phase-4 options on, cmdline fallback via boot-fallback.scr); USB libcomposite + mass-storage + CDC-ACM as modules
init: handshake-init (static, hc 0.1.4, hs_rt runtime: no libc, no mutex)
systems: GB GBC GBA SNES GENESIS PS1 N64 (all 7 clean-room cores linked and live)
aa: PS1 N64 1X (default) FXAA 2X 4X 8X SSAA, SELECT+R1 cycles in game
splash: CloudAtlas HandshakeOS o.O pack (8 graphic) + built-in HandshakeOS o.O ASCII
REL
find "$RD" -exec touch -h -d '2026-11-19 00:00:00 UTC' {} +
"$OUT/tools/hs-mkcpio" "$RD" | gzip -9n > "$CPIO"
cpio -it --quiet < <(zcat "$CPIO") | wc -l | sed "s/^/CPIO_ENTRIES /"
mkimage -A arm64 -O linux -T ramdisk -C gzip -n "HandshakeOS initramfs" -d "$CPIO" "$UIMG" >/dev/null
mkimage -l "$UIMG"
# A/B copies of the same payload (inactive slot is the fallback target)
cp -a "$UIMG" "$OUT/handshakeos-initramfs-A.uimg"
cp -a "$UIMG" "$OUT/handshakeos-initramfs-B.uimg"

# ---- FEL kit ----
rm -rf "$FEL"; mkdir -p "$FEL"
cp "$UBOOT" "$FEL/u-boot-sunxi-with-spl-lpddr3.bin"
cp "$DTB" "$FEL/sun50i-h700-rg35xx-2024-nv3052c-proven.dtb"
cp "$KGZ" "$FEL/Image-v5.9.gz"
cp "$UIMG" "$FEL/handshakeos-initramfs-v5.uimg"
cp "$MODS/libcomposite.ko" "$MODS/usb_f_mass_storage.ko" "$FEL/" 2>/dev/null || true
cat > "$FEL/boot-v5-fel.txt" <<'BT'
echo "HandshakeOS v5.9 FEL RAM boot (Handshake phase-4 kernel)"
unzip 0x44000000 0x40080000
setenv bootargs "console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0 handshake_sched=1 handshake_sched.timekeeper=1 handshake_sched.local_timers=1 handshake_sched.defer=1 handshake_sched.futex=2 handshake_sched.futex_exit=1 handshake_sched.futex_nophash=1 handshake_sched.futex_rescue_us=1500 handshake_sched.tty_output=1"
booti 0x40080000 0x4ff00000 0x4fa00000
BT
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v5 FEL boot" -d "$FEL/boot-v5-fel.txt" "$FEL/boot-v5-fel.scr" >/dev/null
printf '%s\r\n' \
'@echo off' \
'setlocal' \
'cd /d "%~dp0"' \
'echo HandshakeOS v5.9 FEL RAM boot (v5.2 kernel, LPDDR3, NV3052C DTB)' \
'echo All 7 clean-room systems: GB / GBC / GBA / SNES / Genesis / PS1 / N64.' \
'echo Zadig WinUSB must be bound to VID 1F3A PID EFE8. Tap Reset into FEL first.' \
'echo.' \
'set SUNXI_FEL_SPL_WAIT_MS=1000' \
'sunxi-fel.exe -v -p uboot u-boot-sunxi-with-spl-lpddr3.bin write 0x44000000 Image-v5.9.gz write 0x4FA00000 sun50i-h700-rg35xx-2024-nv3052c-proven.dtb write 0x4FF00000 handshakeos-initramfs-v5.uimg write 0x4FC00000 boot-v5-fel.scr' \
'echo.' \
'echo sunxi-fel exited %ERRORLEVEL%.' \
'pause' > "$FEL/07-ramboot-v5.cmd"

# ---- FAT32 staging ----
rm -rf "$ST"; mkdir -p "$ST/ROMS"
cp "$KGZ" "$ST/Image.gz"
cp "$KGZ" "$ST/Image-A.gz"
cp "$KGZ" "$ST/Image-B.gz"
cp "$DTB" "$ST/sun50i-h700-anbernic-rg35xx-2024.dtb"
cp "$UIMG" "$ST/handshakeos-initramfs-v5.uimg"
cp "$UIMG" "$ST/handshakeos-initramfs-A.uimg"
cp "$UIMG" "$ST/handshakeos-initramfs-B.uimg"
cp "$UBOOT" "$ST/u-boot-sunxi-with-spl.bin"
cp "$FLASH" "$ST/FLASH-INSTRUCTIONS.txt"
if [ -f "$V5/v5.9/docs/HSDEBUG.md" ]; then
  cp "$V5/v5.9/docs/HSDEBUG.md" "$ST/HSDEBUG.md"
elif [ -f "$V5/v5.8/docs/HSDEBUG.md" ]; then
  cp "$V5/v5.8/docs/HSDEBUG.md" "$ST/HSDEBUG.md"
fi
if [ -f "$V5/v5.9/host/hsdebug.exe" ]; then
  cp "$V5/v5.9/host/hsdebug.exe" "$ST/hsdebug.exe"
elif [ -f "$V5/v5.8/host/hsdebug.exe" ]; then
  cp "$V5/v5.8/host/hsdebug.exe" "$ST/hsdebug.exe"
fi
cat > "$ST/boot.cmd" <<'BC'
echo "HandshakeOS v5.9 SD boot (A/B slots)"
setenv boot_ok 0
if fatload mmc 0:1 0x45000000 SLOT-B; then
  echo "trying slot B"
  if load mmc 0:1 0x44000000 Image-B.gz; then
    unzip 0x44000000 0x40080000
    if load mmc 0:1 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024.dtb; then
      if load mmc 0:1 0x4FF00000 handshakeos-initramfs-B.uimg; then
        setenv boot_ok 1
      fi
    fi
  fi
  if test ${boot_ok} = 0; then
    echo "slot B failed, falling back to A"
  fi
fi
if test ${boot_ok} = 0; then
  echo "trying slot A"
  if load mmc 0:1 0x44000000 Image-A.gz; then
    unzip 0x44000000 0x40080000
    if load mmc 0:1 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024.dtb; then
      if load mmc 0:1 0x4FF00000 handshakeos-initramfs-A.uimg; then
        setenv boot_ok 1
      fi
    fi
  fi
fi
if test ${boot_ok} = 0; then
  echo "legacy Image.gz fallback"
  load mmc 0:1 0x44000000 Image.gz
  unzip 0x44000000 0x40080000
  load mmc 0:1 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024.dtb
  load mmc 0:1 0x4FF00000 handshakeos-initramfs-v5.uimg
fi
setenv bootargs "console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0 handshake_sched=1 handshake_sched.timekeeper=1 handshake_sched.local_timers=1 handshake_sched.defer=1 handshake_sched.futex=2 handshake_sched.futex_exit=1 handshake_sched.futex_nophash=1 handshake_sched.futex_rescue_us=1500 handshake_sched.tty_output=1"
booti 0x40080000 0x4ff00000 0x4fa00000
BC
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v5.9 SD boot" -d "$ST/boot.cmd" "$ST/boot.scr" >/dev/null
# Stock-scheduler fallback: copy boot-fallback.scr over boot.scr on the FAT volume.
sed "s/handshake_sched=1 handshake_sched.timekeeper=1 handshake_sched.local_timers=1 handshake_sched.defer=1 handshake_sched.futex=2 handshake_sched.futex_exit=1 handshake_sched.futex_nophash=1 handshake_sched.futex_rescue_us=1500 handshake_sched.tty_output=1/handshake_sched=off handshake_sched.timekeeper=0 handshake_sched.local_timers=0 handshake_sched.defer=0 handshake_sched.futex=0 handshake_sched.futex_exit=0 handshake_sched.futex_nophash=0 handshake_sched.tty_output=0 handshake_sched.futex_rescue_us=0/" "$ST/boot.cmd" > "$ST/boot-fallback.cmd"
# Also retitle the fallback script echo line.
sed -i "s/HandshakeOS v5.9 SD boot/HandshakeOS v5.9 SD boot (stock scheduler fallback)/" "$ST/boot-fallback.cmd"
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v5.9 fallback" -d "$ST/boot-fallback.cmd" "$ST/boot-fallback.scr" >/dev/null
# Verbose-log boot script (hs.log=verbose). Copy over boot.scr to use it.
cp "$ST/boot.cmd" "$ST/boot-verbose.cmd"
sed -i 's/handshake_sched.tty_output=1/handshake_sched.tty_output=1 hs.log=verbose/' "$ST/boot-verbose.cmd"
sed -i 's/HandshakeOS v5.9 SD boot/HandshakeOS v5.9 SD boot (hs.log=verbose)/' "$ST/boot-verbose.cmd"
mkimage -A arm -O linux -T script -C none -n "HandshakeOS v5.9 verbose log" -d "$ST/boot-verbose.cmd" "$ST/boot-verbose.scr" >/dev/null
printf '%s\r\n' \
"HandshakeOS v5.9 cold boot" \
"==============================" \
"Anbernic RG35XX 2024 (Allwinner H700, 1 GB LPDDR3, NV3052C panel)." \
"" \
"On power-on, U-Boot loads from this microSD, runs /boot.scr, and boots" \
"Linux 7.3.5 (the proven v5.2 / v3-audio kernel), the NV3052C device tree," \
"and the v5.8 initramfs. A/B slots: Image-A.gz / Image-B.gz with fallback." \
"START in GAMES enters USB MODE and exports the ROM volume (p1 HANDSHAKEOS on a fresh v5.9 flash, or p2 HSROMS if that older layout is still present) as a USB drive." \
"A CDC-ACM serial port is also present so hsdebug can pull live lockstat while you play." \
"To fall back to the stock scheduler, copy boot-fallback.scr over boot.scr on this volume." \
"" \
"v5 plays all seven systems with clean-room HandshakeOS cores:" \
"Game Boy, Game Boy Color, Game Boy Advance, Super Nintendo, Sega Genesis," \
"PlayStation (PS1) and Nintendo 64." \
"Hold A/START/MENU on the boot splash, or press MENU on the main screen," \
"for the boot config menu (splash, animation, theme, A/B slot)." \
"" \
"Eight built-in demos are inside the initramfs, so GAMES works with no ROMs." \
"Put your own games in the ROMS folders (see ROMS/README.txt)." \
"On first boot of a fresh flash, p1 HANDSHAKEOS grows to the end of the card second" \
"FAT32 partition labelled HANDSHAKEOS (one volume, grown on first boot) (readable on Windows). ROMs there are" \
"scanned alongside the folders on this boot partition." \
"" \
"Do not rename the files in the root of this partition (boot.scr, Image.gz," \
"Image-A.gz, Image-B.gz, the device tree, or the initramfs images)." \
> "$ST/README.txt"
# reuse v5.3 ROMS readme text from pack template (abbreviated ok if we copy)
if [ -f "$V5/v5.3/staging/ROMS/README.txt" ]; then
  cp "$V5/v5.3/staging/ROMS/README.txt" "$ST/ROMS/README.txt"
else
  printf '%s\r\n' "HandshakeOS ROMs" "Put games in the system folders." > "$ST/ROMS/README.txt"
fi

find "$ST" -exec touch -h -d "2026-11-19 00:00:00 UTC" {} +
# Pre-sized FAT32 p1: 32 KiB clusters, FAT tables for 256 GiB, packed to
# the FAT32 minimum (~2.07 GiB including the 4 MiB lead-in). Sparse.
P1_START=8192
gcc -O2 -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -I"$T/handshakeos/include" \
  -o "$OUT/host/hs-fat32-presize" \
  "$T/handshakeos/tools/hs-fat32-presize.c" \
  "$T/handshakeos/src/rompart.c" "$T/handshakeos/src/bootlog.c"
rm -f "$IMG"
"$OUT/host/hs-fat32-presize" "$IMG" "$P1_START"
# U-Boot SPL in the gap before p1 (8 KiB). Never overlaps the MBR or p1.
if [ -z "$UBOOT" ]; then
  echo "UBOOT not set" >&2
  exit 1
fi
dd if="$UBOOT" of="$IMG" bs=1024 seek=8 conv=notrunc status=none
MTOOLS_SKIP_CHECK=1
export MTOOLS_SKIP_CHECK
FATOFF="${IMG}@@$((P1_START * 512))"
for d in ROMS ROMS/GB ROMS/GBC ROMS/GBA ROMS/SNES ROMS/GENESIS ROMS/PSX ROMS/N64; do
  mmd -i "$FATOFF" "::/$d" || true
done
for f in boot.scr boot.cmd boot-fallback.scr boot-fallback.cmd boot-verbose.scr boot-verbose.cmd \
         Image.gz Image-A.gz Image-B.gz sun50i-h700-anbernic-rg35xx-2024.dtb \
         handshakeos-initramfs-v5.uimg handshakeos-initramfs-A.uimg handshakeos-initramfs-B.uimg \
         README.txt FLASH-INSTRUCTIONS.txt HSDEBUG.md hsdebug.exe; do
  if [ -f "$ST/$f" ]; then
    mcopy -m -i "$FATOFF" "$ST/$f" "::/$f"
  fi
done
mcopy -m -i "$FATOFF" "$ST/ROMS/README.txt" ::/ROMS/README.txt
# fsck.fat has no --offset; use a loop device on p1.
loop=$(losetup -f --show -o $((P1_START * 512)) "$IMG")
fsck.fat -n "$loop"
losetup -d "$loop"

rm -f "$IMG.zst"; zstd -19 -q -T0 "$IMG" -o "$IMG.zst"
sha256sum "$IMG" "$IMG.zst" "$UIMG" "$CPIO" "$V5/roms-manifest-v5.9.txt"
echo PACK OK
