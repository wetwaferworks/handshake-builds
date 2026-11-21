#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
OUT=$HS_ROOT/builds/v5/v5.6
echo "===== objects with U hs_hs_ or pthread ====="
for o in "$OUT"/aarch64/*.o; do
  u=$(aarch64-linux-gnu-nm -u "$o" 2>/dev/null | awk '{print $NF}' | grep -E '^(hs_hs_|pthread_)' || true)
  [ -n "$u" ] && printf '%s\n%s\n' "-- $(basename $o) --" "$u"
done
echo "===== defined in handshake.o ====="
aarch64-linux-gnu-nm "$OUT/aarch64/handshake.o" | grep -E "hs_hs_|pthread_|T " | head -40