HandshakeOS FEL kit for the Anbernic RG35XX 2024 (Allwinner H700)
=============================================================
Built 2026-11-19, America/New_York.
Default DRAM: LPDDR3 1024 MiB (DCDC3 1.2 V, 672 MHz). LPDDR4 is the alternate.
DTB: NV3052C-clean (anbernic,rg35xx-plus-panel only).
Boot scripts are mkimage -A arm (required by sunxi-fel).
A live kit already staged on TheWorkstation at D:\scratch\lithoos\fel\ was
left untouched so an in-progress FEL test is not disrupted.

Windows already saw the handheld in FEL mode:
  USB\VID_1F3A&PID_EFE8\...

Do NOT run sunxi-fel or xfel against the device until Zadig has bound
WinUSB to that Allwinner USB device (VID 1F3A, PID EFE8). Binding the
wrong device will make a regular USB peripheral unhappy.

1) Zadig (you do this)
----------------------
- Plug the RG35XX in FEL mode (the Windows "ding" is the right sign).
- Run Drivers\zadig-2.9.exe (official copy from the xfel v1.4.0 release).
- Options -> List All Devices.
- Select the Allwinner USB device (VID 1F3A, PID EFE8).
- Driver: WinUSB. Install.
- When it shows WinUSB for that device, come back here.

2) Check the tools (no device I/O required for --help)
------------------------------------------------------
  Double-click 01-check-tools.cmd
  or:
  sunxi-fel.exe --help
  sunxi-fel.exe --list-socs
  (You want to see SoC id 0x1823 listed as H616; H700 uses the same id.)
  xfel.exe  (prints its usage; official MIT build v1.4.0)

3) Payload A — live HandshakeOS in RAM (no SD write)
-------------------------------------------------
  Double-click 02-ramboot.cmd
  This uses the proven U-Boot v2026.10 + Linux 7.3.5 Image + DTB +
  handshakeos-initramfs, then starts U-Boot, which sources boot-ram.scr.

  You should get:
    - UART (if attached): "HandshakeOS PID 1" + handshake ledger
    - The 640x480 Deep Purple shell on the panel, live Gemstone dial,
      jitter stats, and the legal GB demo (handshake-demo.gb) on the right.
    - It STAYS on screen. Theme with L1/R1, menu tiles for power/reboot.
      It only reboots if you choose REBOOT, or if handshakeos.once is on the
      cmdline (it is not, in this script).

  Optional faster transfer: 02-ramboot-gz.cmd ships Image.gz (~15 MiB)
  to 0x44000000 and unzips to 0x40080000. Default 02-ramboot.cmd is the
  safer first try (raw Image, no unzip).

4) Payload B — RG35XX becomes a USB microSD reader
--------------------------------------------------
  Put the experimental microSD in TF1 (NOT the stock card).
  Double-click 03-ums-card-reader.cmd
  This loads the UMS-enabled U-Boot and runs:  mmc rescan; ums 0 mmc 0

  Windows should then see a raw disk. Flash D:\scratch\handshakeos\
  handshakeos-rg35xx-2024.img with Rufus / Raspberry Pi Imager / Etcher,
  after checking the SHA-256 in that folder. Eject, power the
  handheld normally from the card.

  If U-Boot UMS does not enumerate a disk, say so and we will add a
  Linux g_mass_storage initramfs (the 7.3.5 kernel already has the
  gadget core; mass-storage was modular and is not in this first
  RAM image).

Files of note
-------------
  sunxi-fel.exe     built from linux-sunxi/sunxi-tools master 34dfa20
                    with x86_64-w64-mingw32, statically linked libusb-1.0
                    (only KERNEL32, msvcrt, WS2_32). H616/H700 id 0x1823.
  xfel.exe + libusb-1.0.dll
                    official xboot/xfel v1.4.0 Windows release (MIT)
  u-boot-sunxi-with-spl-lpddr3.bin
                    proven v2026.10 LPDDR3 build (eGON.BT0), for RAM boot
  u-boot-ums-with-spl-lpddr3.bin
                    same LPDDR3 defconfig + MUSB gadget + CMD_USB_MASS_STORAGE
  Image / Image.gz  Linux 7.3.5 aarch64
  sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb
  handshakeos-initramfs.uimg   static handshake-init as /init + roms
  boot-ram.scr / boot-ums.scr / boot-ram-gz.scr   (mkimage -A arm)
  RAM-MAP.txt       exact addresses and overlap check
  SHA256SUMS.txt

This kit does not touch NanoQuant or D:\Private.
