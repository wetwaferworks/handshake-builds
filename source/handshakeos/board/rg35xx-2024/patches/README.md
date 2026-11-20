# ROCKNIX / mainline patches needed for a hardware boot

Vanilla mainline already contains `sun50i-h700-anbernic-rg35xx-2024.dts` (compatible `anbernic,rg35xx-2024`, author Ryan Walklin). It is enough for CPU, UART, AXP717, gpio-keys, LEDs, mmc0, USB in peripheral mode, and the Mali node. It is not enough to light the panel or to use the second SD slot or USB-C host.

Do not copy ROCKNIX userland, branding, or scripts. These patches are kernel and device-tree work, GPL-2.0, and they stay in the kernel tree.

Carry equivalents of, at least:

- LCD panel enablement and PWM backlight — ROCKNIX `0002-rg35xx-enable-HDMI-LCD.patch` is the usual starting point, plus the panel init firmware under `panels/anbernic,rg35xx-plus-panel.panel` (and the **rev6** variant if the first DTB shows a garbage screen). Treat panel firmware as a file shipped beside the kernel, not as something we reimplement in MIT C on day one.
- HDMI Type D connector — same patch family. H616/H700 DE33 work continued through 2026; do not assume vanilla HDMI just works.
- Second SD slot (TF2 / EXT) — vanilla 2024 DTS enables `mmc0` only (card detect on PF6).
- USB OTG host — vanilla sets `dr_mode = "peripheral"`. ROCKNIX `0152-rg35xx-2024-enable-usb-otg.patch` switches it to `"otg"`, with VBUS on PI16 and ID detect on PI4. Test storage and a USB Ethernet dongle only after that lands. This model has no onboard Wi-Fi or Bluetooth; a USB dongle is the only network path.

Sources, for reference, not as a fork:

- <https://github.com/ROCKNIX/distribution> (default branch `next`), directory `projects/ROCKNIX/devices/H700/patches/linux/`
- Device tree in mainline: `arch/arm64/boot/dts/allwinner/sun50i-h700-anbernic-rg35xx-2024.dts`
- U-Boot: the default HandshakeOS build is `configs/anbernic_rg35xx_h700_lpddr3_defconfig` (H616 LPDDR3, 1024 MiB, DCDC3 1.2 V, DRAM_CLK 672 MHz, AXP717), added by the patches in `patches/u-boot/`. Some units are LPDDR4; those use the mainline `anbernic_rg35xx_h700_defconfig` instead. The two verified U-Boot patches also add SPL SRAM progress breadcrumbs at 0x49f00.

This pass does not vendor those patches. Pull them when you are ready to build a bootable image, record the exact commits, and keep corresponding source with any image you distribute.
