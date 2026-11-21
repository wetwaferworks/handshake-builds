#!/bin/bash
set -eu
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
# this file is copied next to the patched build.sh in v5.7
SRC=/tmp/snes-build-v57.sh
if [ -f "$ROOT/builds/v5/v5.7/snes-build.sh" ]; then
  SRC="$ROOT/builds/v5/v5.7/snes-build.sh"
fi
cp -f "$SRC" "$ROOT/hs-cores/snes/build.sh"
if [ -f "$ROOT/handshakeos/cores/snes/build.sh" ]; then
  cp -f "$SRC" "$ROOT/handshakeos/cores/snes/build.sh"
fi
echo "BUILD_SH_APPLIED"
grep -n LDFLAGS "$ROOT/hs-cores/snes/build.sh" | head
