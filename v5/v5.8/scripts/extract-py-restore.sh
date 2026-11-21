#!/bin/bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
OUT=$HS_ROOT/builds/v5/v5.8
tar -xf "$OUT/xfer/py-restore.tar" -C "$OUT"
test -f "$OUT/linux-7.3.5/drivers/gpu/drm/msm/registers/gen_header.py"
wc -c "$OUT/linux-7.3.5/drivers/gpu/drm/msm/registers/gen_header.py"
echo RESTORE_OK
