# RG35XX 2024 board recipe

Target: Anbernic RG35XX 2024 (base model). Allwinner H700, four Cortex-A53 cores, **1 GB LPDDR3** on the verified board (DCDC3 1.2 V, DRAM clock 672 MHz), 640×480 LCD, no Wi-Fi, no Bluetooth. Some units are LPDDR4; that is the alternate revision.

Default U-Boot/SPL is the **LPDDR3** build (`anbernic_rg35xx_h700_lpddr3_defconfig`). Use the mainline LPDDR4 defconfig only on a confirmed LPDDR4 board.

## Before anything else

1. Run [`windows-backup-stock-sd.ps1`](windows-backup-stock-sd.ps1) (or `dd` plus SHA-256 on any Unix) against the **stock** card.
2. Verify the image. Put the stock card back in the device and keep playing on it.
3. All HandshakeOS work happens on a **second** SD card in TF1.

Play wins ties. The machine that plays Pokemon is never the machine an experiment can brick.

## What to build

| Piece | Version / config | Notes |
|-------|------------------|--------|
| Trusted Firmware-A | `PLAT=sun50i_h616`, current LTS or stable tag | Open, BSD-style. Produces `bl31.bin`. |
| U-Boot | mainline + [`patches/u-boot/`](patches/u-boot/), `anbernic_rg35xx_h700_lpddr3_defconfig` | LPDDR3 (`CONFIG_SUNXI_DRAM_H616_LPDDR3`, 672 MHz, DCDC3 1.2 V). Pass `BL31=`. Product: `u-boot-sunxi-with-spl-lpddr3.bin`. LPDDR4 boards: `anbernic_rg35xx_h700_defconfig`. |
| Linux | **≥ 6.12** (floor: `CONFIG_SCHED_CLASS_EXT`); this image uses 7.3.5 | Device tree: the NV3052C-clean `sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb` (`panel@0` compatible `anbernic,rg35xx-plus-panel` only, so the built-in `nv3052c` driver binds with no `/lib/firmware/panels/*.panel` file). |
| Config fragment | [`kernel.config.fragment`](kernel.config.fragment) | `CONFIG_PREEMPT=y`, `CONFIG_HIGH_RES_TIMERS=y`, `CONFIG_NO_HZ_FULL=y`, `CONFIG_SCHED_CLASS_EXT=y`, `CONFIG_BPF_SYSCALL=y`, Panfrost, evdev, AXP717. |
| LCD / HDMI / TF2 / OTG | [`patches/README.md`](patches/README.md) | Not in vanilla 2024 DTS. Start from the ROCKNIX H700 Linux patches, under GPL-2.0, in the kernel tree only. |

Console: `ttyS0,115200`. Sunxi SPL is written at an 8 KiB offset (`eGON.BT0` at 0x2004).

## Running the recipe

The script checks the ARM64 cross compiler and prints the exact commands. It does **not** download the kernel unless you pass `--fetch`.

```sh
# from the HandshakeOS tree, on a Linux box
./board/rg35xx-2024/build-bootstack.sh
# CROSS_COMPILE=aarch64-linux-gnu- KERNEL_VERSION=6.12.58 ./board/rg35xx-2024/build-bootstack.sh --fetch
```

Userland: `make cross` in the repo root produces static `build/aarch64/handshake-init` (becomes `/init`) and `build/aarch64/handshake-gb`.

Assemble the flashable image with [`assemble-img.sh`](assemble-img.sh). Defaults are the verified LPDDR3 SPL, the NV3052C-clean DTB, and the static ARM64 userland. The product is `handshakeos-rg35xx-2024.img` (plus `.zst` and SHA-256 sums). See [`FLASH-INSTRUCTIONS.txt`](FLASH-INSTRUCTIONS.txt).

## Boot image layout

MBR, 248 MiB:

- 0..8 MiB gap; LPDDR3 `u-boot-sunxi-with-spl` at 8 KiB (`eGON.BT0` at 0x2004)
- p1 64 MiB FAT32 `HS_BOOT` (bootable): `Image`, NV3052C-clean DTB, `extlinux/extlinux.conf`, `boot.scr` (`mkimage -A arm -T script`)
- p2 80 MiB ext4 `HS_ROOT`: static `/init` (`handshake-init`), `/bin/handshake-gb`, `/etc/handshakeos-release`, `/roms`
- p3 96 MiB FAT32 `HS_ROMS`: legal `.gb` files, visible from Windows

Kernel command line includes `handshakeos.serial`. `handshake-init` also understands `handshakeos.once` / `hs.once` and `hs.serial`.

## FEL RAM boot

A no-SD RAM boot kit lives in [`fel/`](fel/). Two hardware facts matter:

1. **U-Boot scripts must be `mkimage -A arm` (not `-A arm64`).** `sunxi-fel` only passes `fel_scriptaddr` to U-Boot for `IH_ARCH_ARM` scripts. An `arm64` script leaves U-Boot at its prompt after the green LED comes on.
2. **Use the NV3052C-clean DTB** so the panel driver binds without external firmware.

`fel/pack-ramdisk.sh` builds `handshakeos-initramfs.uimg`. `fel/mk-boot-scripts.sh` rebuilds `boot-ram.scr`, `boot-ram-gz.scr`, and `boot-ums.scr` as ARM scripts.

## QEMU virt

A real ARM64 Linux 6.12 kernel already boots in `qemu-system-aarch64` with static `handshake-init` as PID 1 and prints both handshake ledgers to the serial console. See [`board/qemu-virt/run-pid1.sh`](../qemu-virt/run-pid1.sh) and the captured excerpt in [`board/qemu-virt/pid1-serial.txt`](../qemu-virt/pid1-serial.txt).
