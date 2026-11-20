@echo off
setlocal
cd /d "%~dp0"
echo === sunxi-fel.exe --help ===
sunxi-fel.exe --help
echo.
echo === sunxi-fel.exe --list-socs (look for 1823: H616) ===
sunxi-fel.exe --list-socs
echo.
echo === xfel.exe (official v1.4.0) ===
xfel.exe
echo.
echo If those printed cleanly, tools are fine.
echo Do not run 02-ramboot.cmd or 03-ums-card-reader.cmd until Zadig
echo has bound WinUSB to USB VID 1F3A PID EFE8.
pause
