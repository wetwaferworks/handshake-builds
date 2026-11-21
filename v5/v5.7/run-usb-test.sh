#!/bin/bash
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
HOS="$ROOT/handshakeos"
cd "$HOS"
# see how it was built previously
head -40 tests/test_usb_gadget.c
echo '======= compile ======='
gcc -std=c11 -Wall -Wextra -Werror -O2 -D_GNU_SOURCE -Iinclude -o /tmp/test_usb_gadget tests/test_usb_gadget.c src/usb.c
echo compile_rc=$?
/tmp/test_usb_gadget; echo test_rc=$?
