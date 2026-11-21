#!/bin/bash
set -e
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
ROOT=$HS_ROOT/builds/v5/v5.8
KB=$ROOT/kbuild
echo "=== date ==="
date
echo "=== make procs (not /root/hs-v58) ==="
ps -ef | grep -E "[m]ake -C|/linux-7.3.5|build-kernel-v5.8" || true
echo "=== leftover /root/hs-v58 exists (must leave untouched) ==="
if [ -d /root/hs-v58 ]; then echo LEFTOVER_PRESENT; else echo LEFTOVER_ABSENT; fi
echo "=== artifacts ==="
for f in vmlinux arch/arm64/boot/Image arch/arm64/boot/Image.gz; do
  if [ -f "$KB/$f" ]; then ls -l "$KB/$f"; else echo "MISSING $f"; fi
done
echo "=== object counts ==="
echo -n "o_files "; find "$KB" -name "*.o" 2>/dev/null | wc -l
echo -n "a_files "; find "$KB" -name "built-in.a" 2>/dev/null | wc -l
echo "=== recent objs ==="
find "$KB" -name "*.o" -printf "%T@ %p\n" 2>/dev/null | sort -n | tail -8
echo "=== stamp ==="
ls -l "$ROOT/linux-7.3.5/.handshake-patches-applied" 2>/dev/null || echo no-stamp
