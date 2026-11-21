#!/bin/bash
set -eu
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
HOS="$ROOT/handshakeos"
TMP="$ROOT/builds/v5/v5.7/src-items26"
mkdir -p "$TMP"
tar -xf "$ROOT/builds/v5/v5.7/v57-items26.tar" -C "$TMP"
cp -f "$TMP/bootmenu.c"      "$HOS/src/bootmenu.c"
cp -f "$TMP/cfg.c"           "$HOS/src/cfg.c"
cp -f "$TMP/usb.c"           "$HOS/src/usb.c"
cp -f "$TMP/ui.c"            "$HOS/src/ui.c"
cp -f "$TMP/play.c"          "$HOS/src/play.c"
cp -f "$TMP/init.c"          "$HOS/src/init.c"
cp -f "$TMP/hs_backlight.h"  "$HOS/include/hs_backlight.h"
cp -f "$TMP/hs_play.h"       "$HOS/include/hs_play.h"
cp -f "$TMP/hs_cfg.h"        "$HOS/include/hs_cfg.h"
cp -f "$TMP/hs_usb.h"        "$HOS/include/hs_usb.h"
echo "ITEMS26_APPLIED"
# quick markers
grep -n "scale 2, same as the main menu" "$HOS/src/bootmenu.c" | head -1
grep -n "HS_PLAY_POWEROFF" "$HOS/include/hs_play.h" | head -1
grep -n "R2 toggles" "$HOS/src/play.c" | head -1
grep -n "COLOR AND SHADING" "$HOS/src/play.c" | head -1
grep -n "mmcblk0p2" "$HOS/src/usb.c" | head -1
grep -n "POWER_HOLD_NS" "$HOS/src/init.c" | head -1
