@echo off
setlocal
cd /d "%~dp0"
echo HandshakeOS FEL RAM boot (Image.gz, LPDDR3, NV3052C DTB)
echo Only run this AFTER Zadig WinUSB is bound to VID 1F3A PID EFE8.
echo Tap Reset first so this starts from a fresh FEL. No SD card in TF1.
echo.
set SUNXI_FEL_SPL_WAIT_MS=1000
sunxi-fel.exe -v -p uboot u-boot-sunxi-with-spl-lpddr3.bin write 0x44000000 Image.gz write 0x4FA00000 sun50i-h700-anbernic-rg35xx-2024-nv3052c.dtb write 0x4FF00000 handshakeos-initramfs.uimg write 0x4FC00000 boot-ram-gz.scr
echo.
echo sunxi-fel exited %ERRORLEVEL%. The panel should show HandshakeOS and stay up.
pause
