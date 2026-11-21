#!/bin/bash
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
ROOT=$HS_ROOT
HOS="$ROOT/handshakeos"
echo "=== Makefile first 80 lines ==="
sed -n '1,80p' "$HOS/Makefile"
echo "=== Makefile audio/test/snes ==="
grep -n -E -i 'audio|pace|snes|test_' "$HOS/Makefile" | head -80
echo "=== ls tests after apply ==="
ls "$HOS/tests" | grep -E 'snes|pace|audio'
echo "=== hos cores/snes tests? ==="
ls "$HOS/cores/snes" | head
echo "=== v5.6 build mentions of test_audio ==="
grep -n 'test_audio' "$ROOT/builds/v5/build-v5.6.sh" 2>/dev/null | head
# any run script
ls "$ROOT/builds/v5/v5.6" 2>/dev/null | head
grep -R -l 'test_audio_pace' "$ROOT/builds/v5" "$HOS/tools" 2>/dev/null | head
# hs-cores run-tests
sed -n '1,80p' "$ROOT/hs-cores/run-tests.sh"
