#!/bin/bash
set -x

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
umount -l $HS_ROOT/builds/v5/v5.6/qemu-test/hsroms-mnt 2>/dev/null || true
fuser -k /dev/loop0 2>/dev/null || true
sleep 1
losetup -d /dev/loop0 || losetup -D || true
losetup -a || true
echo CLEANUP2_OK
