#!/bin/bash
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
SRC=$HS_ROOT/builds/v5/v5.8/linux-7.3.5
MSM=$SRC/drivers/gpu/drm/msm
echo "=== registers dir ==="
ls -la "$MSM/registers" 2>&1 | head -20
echo "=== adreno xml ==="
ls "$MSM/registers/adreno/a2xx.xml" 2>&1
ls "$MSM/registers/gen_header.py" 2>&1
echo "=== count xml in registers ==="
find "$MSM/registers" -type f 2>/dev/null | wc -l
echo "=== leftover make still? ==="
ps -p 3651 -o pid,cmd 2>&1 || echo MAKE_DEAD
pgrep -af 'build-kernel-v5.8-tree' || echo SCRIPT_DEAD
