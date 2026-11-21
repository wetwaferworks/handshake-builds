#!/bin/bash
set -eu
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
echo "=== looking for HASH mode1 ==="
# narrow paths only
for dir in \
  "$ROOT/hs-cores/snes" \
  "$ROOT/hs-cores" \
  "$ROOT/builds/v5" \
  "$ROOT/handshakeos/cores/snes"; do
  [ -d "$dir" ] || continue
  echo "-- $dir --"
  grep -R -n --include='*.log' --include='*.txt' --include='*.md' --include='ledger*' \
    -e 'HASH mode1' -e 'HASH mode7' -e 'mode1 video=' "$dir" 2>/dev/null | head -40 || true
done
# also known out/ files
find "$ROOT/hs-cores/snes" -maxdepth 3 -type f \( -name '*.log' -o -name 'out.txt' -o -name '*hash*' \) 2>/dev/null | head
