#!/usr/bin/env bash
# Continue the v5.8 kernel build after 0001-0004 already landed.
set -eu
SRC=/root/hs-v58/linux-7.3.5
APPLY="$(dirname "$0")/apply-0005"
if [ ! -x "${APPLY}" ]; then
  gcc -O2 -o /tmp/apply-0005-v58 "$(dirname "$0")/apply-0005.c"
  APPLY=/tmp/apply-0005-v58
fi
BUILD="$(dirname "$0")/build-kernel-v5.8.sh"
"${APPLY}" "${SRC}/kernel/locking/hs_lockstat.c"
# Confirm the new proc files are wired
grep -q handshake_holds "${SRC}/kernel/locking/hs_lockstat.c"
grep -q handshake_spinsites "${SRC}/kernel/locking/hs_lockstat.c"
date > "${SRC}/.handshake-patches-applied"
echo "0001-0004 + 0005 stamped" >> "${SRC}/.handshake-patches-applied"
echo "STAMPED, continuing build"
exec bash "${BUILD}"
