#!/bin/bash
# Rebuild FEL (and matching) U-Boot scripts as ARM (IH_ARCH_ARM).
# sunxi-fel only passes fel_scriptaddr to U-Boot for -A arm scripts.
# An -A arm64 script leaves the green LED on and U-Boot sitting at its prompt.
set -euo pipefail
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
OUT="${OUT:-$HERE}"

mk_one() {
    local src="$1" dest="$2" name="$3"
    mkimage -A arm -T script -C none -n "$name" -d "$src" "$dest" >/dev/null
    echo "wrote $dest"
    mkimage -l "$dest" | sed -n '1,6p'
}

mk_one "$HERE/boot-ram.txt"    "$OUT/boot-ram.scr"    "HandshakeOS FEL ramboot"
mk_one "$HERE/boot-ram-gz.txt" "$OUT/boot-ram-gz.scr" "HandshakeOS FEL ramboot gz"
mk_one "$HERE/boot-ums.txt"    "$OUT/boot-ums.scr"    "HandshakeOS FEL UMS"
