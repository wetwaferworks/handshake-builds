#!/bin/bash
set -u
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
T=$HS_ROOT
echo "=== scripts mentioning LOCK_HITS ==="
grep -l 'LOCK_HITS\|IMAGE_ZERO_MUTEX\|zero.mutex\|zero-lock' \
  "$T/builds/v5"/*.sh "$T/builds/v5/v5.6"/*.sh "$T/builds/v5/v5.6/"*.sh 2>/dev/null | head
echo "=== ls builds/v5/*.sh ==="
ls "$T/builds/v5"/*.sh
echo "=== ls v5.6 scripts ==="
ls "$T/builds/v5/v5.6"/*.sh 2>/dev/null
echo "=== n64 tools ==="
ls -l "$T/hs-cores/n64/build/aarch64/bench_n64" "$T/hs-cores/n64/build/aarch64/hs_pf_probe" 2>/dev/null
ls -l "$T/builds/v5/v5.6/aarch64/bench_n64" "$T/builds/v5/v5.6/aarch64/hs_pf_probe" 2>/dev/null
echo "=== modules ==="
ls "$T/builds/v5/v5.6/modules" 2>/dev/null | head
echo "=== v5.6 image still there? ==="
sha256sum "$T/builds/v5/handshakeos-rg35xx-2024-v5.6.img" "$T/builds/v5/handshakeos-rg35xx-2024-v5.6.img.zst" | cut -c1-80
# find scanner file
find "$T/builds/v5" -maxdepth 2 -name '*scan*' -o -name '*mutex*' -o -name '*zero-lock*' 2>/dev/null | head
grep -l LOCK_HITS "$T/builds/v5/v5.6/"* "$T/builds/v5/"*.sh 2>/dev/null | head
# maybe inside pack or a run-all
grep -l LOCK_HITS $(find "$T/builds/v5" -maxdepth 3 -type f \( -name '*.sh' -o -name '*.txt' \) ) 2>/dev/null | head
