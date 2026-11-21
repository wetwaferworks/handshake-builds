#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
f=$HS_ROOT/handshakeos/src/handshake.c
sed -n '1,40p' "$f"
echo "===== 420-500 ====="
sed -n '420,500p' "$f"
echo "===== sys.c pthread ====="
sed -n '270,360p' $HS_ROOT/handshakeos/src/hs_rt/sys.c