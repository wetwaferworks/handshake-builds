#!/bin/bash
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
P=$HS_ROOT/builds/v5/v5.7/previews
sha256sum "$P"/*.png
# also MULTI from once log and LOCK_HITS confirmation, kernel ko report
L=$HS_ROOT/builds/v5/v5.7/logs
grep -E 'KMOD_KERNEL|CLEAN_NO_PTHREAD|LOCK_HITS' "$L/image-scan.txt"
# qemu once first boot more context
grep -E 'MULTI |boot mode|HandshakeOS' "$L/qemu-pid1-once.log" 2>/dev/null | head
ls "$L"/qemu-pid1*.log 2>/dev/null
