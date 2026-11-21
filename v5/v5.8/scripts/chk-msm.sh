#!/bin/bash
set -e
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
SRC=$HS_ROOT/builds/v5/v5.8/linux-7.3.5
KB=$HS_ROOT/builds/v5/v5.8/kbuild
echo "=== source generated dir ==="
ls -la "$SRC/drivers/gpu/drm/msm/generated" 2>&1 | head -40
echo "=== kbuild generated dir ==="
ls -la "$KB/drivers/gpu/drm/msm/generated" 2>&1 | head -20
echo "=== missing header? ==="
ls -la "$SRC/drivers/gpu/drm/msm/generated/a2xx.xml.h" 2>&1
ls -la "$KB/drivers/gpu/drm/msm/generated/a2xx.xml.h" 2>&1
echo "=== case-sensitive flags ==="
# fsutil from windows; also check if Foo/foo can coexist later
echo "=== how many xml.h in source ==="
find "$SRC/drivers/gpu/drm/msm" -iname "*xml.h" 2>/dev/null | wc -l
find "$SRC/drivers/gpu/drm/msm" -iname "*xml.h" 2>/dev/null | head
echo "=== Makefile mention ==="
rg -l "a2xx.xml.h|gen_header" "$SRC/drivers/gpu/drm/msm" 2>/dev/null | head
