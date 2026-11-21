#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
T=$HS_ROOT
echo "===== handshake.c / headers pthread refs ====="
rg -n "pthread_|thrd_" "$T/handshakeos/src/handshake.c" "$T/handshakeos/include" "$T/handshakeos/src" --glob '*.c' --glob '*.h' | head -80
echo "===== nm handshake.o (all pthread-related, and T/U) ====="
aarch64-linux-gnu-nm "$T/builds/v5/v5.6/aarch64/handshake.o" | grep -E "pthread|thread"
echo "===== objdump handshake.o relocs ====="
aarch64-linux-gnu-objdump -d "$T/builds/v5/v5.6/aarch64/handshake.o" | grep -E "pthread|thread" | head -40