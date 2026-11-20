@echo off
setlocal
cd /d "%~dp0"
echo HandshakeOS v3-audio FEL RAM boot (Image-v3-audio.gz, LPDDR3, NV3052C DTB)
echo Zadig WinUSB must be bound to VID 1F3A PID EFE8. Tap Reset into FEL first.
echo.
set SUNXI_FEL_SPL_WAIT_MS=1000
sunxi-fel.exe -v -p uboot u-boot-sunxi-with-spl-lpddr3.bin write 0x44000000 Image-v3-audio.gz write 0x4FA00000 sun50i-h700-rg35xx-2024-nv3052c-proven.dtb write 0x4FF00000 handshakeos-initramfs-v3-audio.uimg write 0x4FC00000 boot-v3-audio-fel.scr
echo.
echo sunxi-fel exited %ERRORLEVEL%.
pause
