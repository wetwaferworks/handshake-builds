# HandshakeOS RG35XX 2024 — v2-stable SD image

Built 2026-11-19, 7:15 PM America/New_York, strictly on the box.
Not copied to TheWorkstation, not flashed, not committed, not pushed.
Previous builds (`builds/v1-firstboot`, `builds/v2-bigtext`) and the earlier
248 MiB image in `/workspace/scratch/handshakeos/images/` were left untouched.

This is a compact cold-bootable microSD image of the exact proven v2 FEL
RAM-boot stack, for JD's Anbernic RG35XX 2024 (Allwinner H700, **1 GB LPDDR3**,
NV3052C panel). Wait until JD plugs in a second microSD (TF1) and confirms
before flashing.

## Deliverables

| File | Size | SHA-256 |
|---|---|---|
| `handshakeos-rg35xx-2024-v2-stable.img` | 128 MiB (134,217,728 B) | `68dcc3093ef4c4e3ff4b85383bc1ab5c363ad2f591ef1f95573ba3bffb68d4b3` |
| `handshakeos-rg35xx-2024-v2-stable.img.zst` | 15.5 MiB (16,238,856 B) | `756374bcf7c1f76bcb0c18ddf38ad0f701f367988274724893ec80824fead3c7` |

zstd -19 round-trip was verified: decompressed bytes match the raw image hash.

## Image layout (MBR, 128 MiB)

- Bytes `0x0000..0x1FFF`: MBR only (disk id `0x48534f53`). No U-Boot here.
- Offset `8 KiB` (sector 16): proven LPDDR3 `u-boot-sunxi-with-spl.bin` (758,289 B).
  - `eGON.BT0` at `0x2004`.
  - SPL length in the eGON header is `0xA000` (40 KiB).
  - FIT / FDT magic `d00dfeed` at SD offset `0xC000` (48 KiB, sector 96).
  - Payload ends at byte 766,481, leaving ~3.27 MiB of margin before partition 1.
- Partition 1: starts at sector 8192 (4 MiB), 253,952 sectors (124 MiB), type
  `0x0c` (FAT32 LBA), bootable flag set, volume label `HANDSHAKEOS`.
  `fsck.fat -n` is clean. BPB hidden-sector count is 8192.

FAT32 contents (extracted with `mcopy` and hashed byte-for-byte against the
proven v2 FEL artifacts):

| File | SHA-256 | Source |
|---|---|---|
| `boot.scr` | `faf4047a37bde1a83e3e737e1ab3d7187cf322c0ac7b0d0cfeba49b98f72c5dd` | built this run, `-A arm` |
| `boot.cmd` | `721e718ae1ddc3030d1f9e36ee663e04df0ba0b068eac4fd42ed2b03c4012b0b` | readable copy of the script |
| `Image.gz` | `df4093235c8ef6d826960d869923ae7a75d6bcb598e5d53f7a24d08e0ec46f2b` | proven Linux 7.3.5 (`fel/build/Image.gz`; decompresses to `Image` `3ac6f48c…`) |
| `sun50i-h700-anbernic-rg35xx-2024.dtb` | `814740b1b25b160f1109febe66e0ec26bf261fc169368126612bcc40113d4547` | proven **NV3052C-only** DTB (`fel/fixed/sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb`). Filename is the one `boot.scr` loads. |
| `handshakeos-initramfs-v2.uimg` | `df58ec87131e2aecbf9caecd2604a6c4eeff8dafc647c54e401eea4b01cdd065` | proven v2 initramfs (`builds/v2-bigtext/`) |
| `README.txt` and `ROMS/README.txt` | `cd55947157124765c72eecd23109d54b41c4be4186f5a0719b500026bee4a702` | v2-stable cold-boot note; `ROMS/` is an empty future ROM slot |

No `LithoOS` / `ho.Os` strings in `boot.cmd` or either `README.txt`.

## LPDDR3 U-Boot findings (no rebuild required)

The successful v1/v2 FEL boots used `fel/fixed/u-boot-sunxi-with-spl-lpddr3.bin`
(SHA-256 `17ce4fb5ce831b9f5c656b508e18e65ce1e184451ae412e1fedaf82a0f2d8d8d`),
built from `anbernic_rg35xx_h700_lpddr3_defconfig` (U-Boot v2026.10) plus the
SRAM breadcrumb patch. That is the binary in this image. There was no separate
`u-boot-lpddr3-diag.bin`; the “diag” breadcrumbs only log SPL progress to
SRAM `0x49F00`. `SUNXI_SPL_BC_RETURN_TO_FEL` is written only on the existing
FEL return path (`spl_board_load_image` → `return_to_fel`). It is not taken
when the BootROM loads the SPL from SD.

Inspected `.config` / embedded default environment (not guessed):

- `CONFIG_SUNXI_DRAM_H616_LPDDR3=y`, `CONFIG_DRAM_CLK=672`, DCDC3 1.2 V,
  ROCKNIX LPDDR3 DX/CA/ODT/TPR timings. `CONFIG_SUNXI_DRAM_H616_LPDDR4` is
  **not** set. The later LPDDR4 `anbernic_rg35xx_h700_defconfig` binary was
  **not** used.
- MMC SD boot is fully enabled: `CONFIG_SPL_MMC=y`, `CONFIG_MMC_SUNXI=y`,
  `CONFIG_CMD_MMC=y`, `CONFIG_CMD_FAT=y`, `CONFIG_CMD_SOURCE=y`,
  `CONFIG_CMD_BOOTI=y`, `CONFIG_CMD_UNZIP=y`.
- Autoboot is normal: `CONFIG_BOOTDELAY=2`,
  `CONFIG_BOOTCOMMAND="run distro_bootcmd"`. No keyed autoboot, no boot-retry
  trap, no UMS-only build (that is a separate `u-boot-ums-with-spl-lpddr3.bin`).
- Cold SD-boot flow: BROM loads SPL from sector 16 (8 KiB). SPL then loads
  U-Boot proper from
  `max(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR=0x40, spl_size/512)` plus
  `CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_DATA_PART_OFFSET=0x10` when `part==0`,
  after `board_spl_mmc_get_uboot_raw_sector()` (`arch/arm/mach-sunxi/board.c`).
  That evaluates to sector 96 (48 KiB) — exactly 8 KiB write offset + 40 KiB
  SPL — where the FIT sits. Standard `dd … seek=16` layout is correct.
- Distro boot looks for `boot.scr.uimg` then `boot.scr` under prefixes
  `/` and `/boot/` on each partition of `mmc0` (`boot_targets=fel mmc0 pxe dhcp`).
  So a root `/boot.scr` on `mmc 0:1` is the path this image uses.
  `extlinux/extlinux.conf` is also searched, but this image does not ship one.
- Default load addresses (1 GB DRAM `0x40000000–0x7FFFFFFF`):
  - `kernel_addr_r=0x40080000` (uncompressed Image, 43,735,552 B, ends `0x42A34A00`)
  - `kernel_comp_addr_r=0x44000000` (Image.gz staging, 15,316,385 B, ends `0x44E9B8A1`)
  - `fdt_addr_r=0x4FA00000`
  - `scriptaddr=0x4FC00000` (distro loads `boot.scr` here; the script itself
    does not overwrite this)
  - `ramdisk_addr_r=0x4FF00000` (v2 uimg 797,970 B, ends `0x4FFC2D12`)
  - No overlap between Image, Image.gz staging, U-Boot `TEXT_BASE=0x4a000000`,
    DTB, script, or initramfs.

## boot.scr / booti notes

`booti` in this U-Boot requires an uncompressed ARM64 `Image`. The proven FEL
path wrote `Image.gz` to `0x44000000`, ran `unzip 0x44000000 0x40080000`, then
`booti 0x40080000 0x4ff00000 0x4fa00000` with bootargs
`console=ttyS0,115200 console=tty0 earlycon=uart8250,mmio32,0x05000000 fbcon=map:0`.
`boot.cmd` is that same sequence with `load mmc 0:1` in front of each payload.

`boot.scr` is `mkimage -A arm -O linux -T script -C none` (legacy uImage
`ih_arch=2` / IH_ARCH_ARM). That matches the proven FEL `boot-ram-gz.scr`
this same U-Boot already sourced. The initramfs remains `-A arm64` / AArch64
RAMDisk (gzip), as built for v2.

## What was not used

- LPDDR4 `u-boot-sunxi-with-spl-lpddr4.bin` (`ff970500…`).
- The stock (non-NV3052C-only) DTB `sun50i-h700-anbernic-rg35xx-2024.dtb`
  (`aa027a2a…`), which still carries a `panel-mipi-dpi-spi` fallback.
- v1 initramfs and the earlier 248 MiB 3-partition image under
  `scratch/handshakeos/images/` (that one already had the LPDDR3 U-Boot at
  8 KiB, but a different rootfs / bootargs, partition 1 at 8 MiB, and it
  predates the proven v2 initramfs).
