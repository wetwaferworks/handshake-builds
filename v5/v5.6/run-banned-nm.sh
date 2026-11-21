#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
INI=$HS_ROOT/builds/v5/v5.6/aarch64/handshake-init
GB=$HS_ROOT/builds/v5/v5.6/aarch64/handshake-gb
USER_BANNED='pthread|mutex|rwlock|spin_?lock|_cond_|barrier|sem_(wait|post|init|timedwait|trywait)|mtx_|cnd_|thrd_|futex|flock|lockf'
echo "===== ALL banned-pattern symbols in init ====="
aarch64-linux-gnu-nm -a "$INI" | grep -E "$USER_BANNED" || true
echo "===== handshake-gb ====="
aarch64-linux-gnu-nm -a "$GB" | grep -E "$USER_BANNED" || true
echo "===== is hs_hs_start called from init.o? ====="
aarch64-linux-gnu-nm $HS_ROOT/builds/v5/v5.6/aarch64/init.o | grep -E "hs_hs_start|pthread" || true
aarch64-linux-gnu-nm $HS_ROOT/builds/v5/v5.6/aarch64/init.o -u | head