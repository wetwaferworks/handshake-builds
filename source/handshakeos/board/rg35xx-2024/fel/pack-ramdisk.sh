#!/bin/bash
# Pack handshakeos-initramfs.uimg: static handshake-init as /init, handshake-gb,
# handshakeos-release, and the legal ROMs. The uImage itself is AArch64 (a
# Linux ramdisk). The *boot scripts* that load it must be ARM (see
# mk-boot-scripts.sh).
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)"
INIT="${INIT:-$ROOT/build/aarch64/handshake-init}"
GB="${GB:-$ROOT/build/aarch64/handshake-gb}"
ROMS="${ROMS:-$ROOT/roms}"
OUT="${OUT:-$ROOT/build/handshakeos-initramfs.uimg}"

for f in "$INIT" "$GB" "$ROMS/handshake-demo.gb" "$ROMS/libbet.gb" "$ROMS/manifest.txt"; do
    if [ ! -f "$f" ]; then
        echo "missing $f" >&2
        exit 1
    fi
done

STAGE=$(mktemp -d)
CPIO=$(mktemp --tmpdir handshakeos-initramfs.XXXXXX.cpio.gz)
trap 'rm -rf "$STAGE" "$CPIO"' EXIT
mkdir -p "$STAGE"/{proc,sys,dev,roms,bin,etc}
cp "$INIT" "$STAGE/init"
chmod 755 "$STAGE/init"
cp "$GB" "$STAGE/bin/handshake-gb"
chmod 755 "$STAGE/bin/handshake-gb"
cp "$ROMS/handshake-demo.gb" "$ROMS/libbet.gb" "$ROMS/manifest.txt" "$STAGE/roms/"
[ -f "$ROMS/libbet.LICENSE" ] && cp "$ROMS/libbet.LICENSE" "$STAGE/roms/"
cat > "$STAGE/etc/handshakeos-release" << 'REL'
HandshakeOS RG35XX 2024
init: handshake-init (static)
REL

# device nodes are optional in this initramfs; handshake-init mounts devtmpfs
# when running as PID 1. Include a console node anyway for early printk.
if [ "$(id -u)" -eq 0 ]; then
    mknod -m 622 "$STAGE/dev/console" c 5 1 || true
    mknod -m 666 "$STAGE/dev/null" c 1 3 || true
fi

# Write the archive outside the staging tree so find/cpio cannot pack it
# into itself. Sort the file list for a stable archive.
( cd "$STAGE" && find . -print0 | sort -z | cpio --null -H newc -o --quiet ) | gzip -9 > "$CPIO"
mkdir -p "$(dirname "$OUT")"
mkimage -A arm64 -O linux -T ramdisk -C gzip -n "HandshakeOS initramfs" \
    -d "$CPIO" "$OUT" >/dev/null
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
mkimage -l "$OUT"
