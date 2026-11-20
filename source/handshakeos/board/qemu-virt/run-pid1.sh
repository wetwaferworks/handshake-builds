#!/bin/bash
# Boot a real ARM64 Linux kernel in qemu-system-aarch64 with HandshakeOS
# handshake-init as PID 1. Prints the 3+1 Timing Handshake ledger (and the
# legal GB demo hash) on the serial console, then reboots so -no-reboot exits.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
KERNEL="${KERNEL:-/tmp/kimg/debian-linux}"
INIT="${INIT:-$ROOT/build/aarch64/handshake-init}"
ROM="${ROM:-$ROOT/roms/handshake-demo.gb}"
OUT="${OUT:-/tmp/handshakeos-initramfs.cpio.gz}"
LOG="${LOG:-/tmp/handshakeos-qemu-serial.log}"

if [ ! -f "$KERNEL" ]; then
    echo "ARM64 kernel Image not found at $KERNEL" >&2
    echo "Place a virt-capable Image there (Debian trixie netboot linux works)." >&2
    exit 1
fi
if [ ! -x "$INIT" ]; then
    echo "static handshake-init missing: $INIT (run: make cross)" >&2
    exit 1
fi

# initramfs writer: board/qemu-virt/pack-initramfs.c (pure C11, built with HC; mtime from SOURCE_DATE_EPOCH)
HC="${HC:-$ROOT/../hc/build/current/hc}"
PACKDIR="${PACKDIR:-$ROOT/build/tools}"
mkdir -p "$PACKDIR"
"$HC" --allow-runtime -std=c11 -O2 -Wall -Wextra -Werror -c "$ROOT/board/qemu-virt/pack-initramfs.c" -o "$PACKDIR/pack-initramfs.o"
gcc -o "$PACKDIR/pack-initramfs" "$PACKDIR/pack-initramfs.o"
"$PACKDIR/pack-initramfs" "$INIT" "$ROM" | gzip -9n > "$OUT"
echo "initramfs $OUT ($(stat -c %s "$OUT") bytes gz)"

set +e
timeout --signal=KILL "${QEMU_TIMEOUT:-90}" \
    qemu-system-aarch64 \
        -machine virt \
        -cpu cortex-a53 \
        -smp 4 \
        -m 512M \
        -kernel "$KERNEL" \
        -initrd "$OUT" \
        -append "console=ttyAMA0 earlycon=pl011,0x9000000 rdinit=/init handshakeos.serial handshakeos.once" \
        -nographic \
        -no-reboot \
    2>&1 | tee "$LOG"
ec=${PIPESTATUS[0]}
set -e
echo "qemu exit $ec; serial log $LOG"
exit 0
