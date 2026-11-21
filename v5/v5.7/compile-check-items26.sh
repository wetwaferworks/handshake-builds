#!/bin/bash
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
HOS="$ROOT/handshakeos"
OUT="$ROOT/builds/v5/v5.7/compile-check-items26.log"
exec > >(tee "$OUT") 2>&1
cd "$HOS"
CF="-std=c11 -Wall -Wextra -Werror -O2 -D_GNU_SOURCE -Iinclude -Icores/snes -Isrc"
# cfg + tests
if [ -f tests/test_cfg.c ]; then
  echo "==== test_cfg ===="
  if gcc $CF -o /tmp/test_cfg tests/test_cfg.c src/cfg.c; then
    /tmp/test_cfg; echo "test_cfg rc=$?"
  else
    echo "test_cfg COMPILE_FAIL"
  fi
fi
# compile key units (not full link)
echo "==== compile units ===="
rc=0
for f in src/cfg.c src/bootmenu.c src/usb.c src/audio.c src/play.c src/init.c src/ui.c src/emu_sys.c; do
  echo "CC $f"
  if ! gcc $CF -Icores/include -Ithird_party/peanut-gb -Ithird_party/minigb_apu -c "$f" -o /tmp/$(basename $f .c).o; then
    echo "FAIL $f"
    rc=1
  fi
done
# look for test_usb
ls tests/test_usb*.c 2>/dev/null || true
echo "COMPILE_UNITS rc=$rc"
exit $rc
