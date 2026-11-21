#!/bin/bash
set -e
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
T=$HS_ROOT
cd "$T/handshakeos"
INC="-Iinclude -Isrc -Icores/include -Icores/common -Icores/gbc -Icores/gba -Icores/snes -Icores/genesis -Icores/psx -Icores/n64"
CFLAGS="-std=c11 -Wall -Wextra -Werror -O0 -D_GNU_SOURCE"
for f in src/debug.c src/usb.c src/mod.c src/bootmenu.c src/play.c src/init.c; do
  echo "CHECK $f"
  gcc $CFLAGS $INC -fsyntax-only "$f"
done
echo SYNTAX_OK
