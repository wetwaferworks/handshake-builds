#!/bin/bash
# detach leftover loops from the killed test
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
for d in /dev/loop0 /dev/loop1 /dev/loop2; do
  umount -l $HS_ROOT/builds/v5/v5.6/qemu-test/hsroms-mnt 2>/dev/null || true
  losetup -d "$d" 2>/dev/null || true
done
losetup -a || true
echo CLEANUP_OK
