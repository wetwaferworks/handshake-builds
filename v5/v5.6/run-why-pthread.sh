#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
T=$HS_ROOT
OUT=$T/builds/v5/v5.6
echo "===== RT_CFLAGS in build-v5.6.sh ====="
grep -n "RT_CFLAGS\|function-sections\|SHIPLD" "$T/builds/v5/build-v5.6.sh" | head -20
echo "===== hs_rt sys.o sections ====="
aarch64-linux-gnu-objdump -h "$OUT/aarch64/hs_rt/sys.o" | grep -E "text|pthread" || true
echo "===== sys.o symbols ====="
aarch64-linux-gnu-nm "$OUT/aarch64/hs_rt/sys.o" | grep -E "pthread|mutex"
echo "===== init pthread and callers ====="
aarch64-linux-gnu-nm "$OUT/aarch64/handshake-init" | grep -E "pthread|mutex|rwlock"
echo "----- bl to pthread in init -----"
aarch64-linux-gnu-objdump -d "$OUT/aarch64/handshake-init" | grep -E "bl[ \t]+[0-9a-f]+ <pthread_" || echo "no bl"
echo "===== handshake-gb ====="
aarch64-linux-gnu-nm "$OUT/aarch64/handshake-gb" | grep -E "pthread|mutex|rwlock"
aarch64-linux-gnu-objdump -d "$OUT/aarch64/handshake-gb" | grep -E "bl[ \t]+[0-9a-f]+ <pthread_" || echo "no bl"
echo "===== relocs to pthread in init ====="
aarch64-linux-gnu-objdump -r "$OUT/aarch64/hs_rt/sys.o" | grep pthread || echo "no relocs named pthread in sys.o"
# who undefined-refs pthread from other objects?
for o in "$OUT"/aarch64/*.o; do
  u=$(aarch64-linux-gnu-nm -u "$o" 2>/dev/null | grep pthread || true)
  [ -n "$u" ] && echo "UNDEF $(basename $o): $u"
done
echo DONE