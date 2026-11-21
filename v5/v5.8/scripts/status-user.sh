#!/bin/bash
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
HS=$HS_ROOT
echo "=== handshakeos live ==="
ls -la "$HS/handshakeos/src" "$HS/handshakeos/include" 2>/dev/null | sed -n "1,80p"
echo "=== v5.7 vs v5.8 src ==="
ls -la "$HS/builds/v5/v5.8/src" "$HS/builds/v5/v5.8/scripts" 2>/dev/null
echo "=== existing v5.7 image (must stay) ==="
ls -la "$HS/builds/v5/"handshakeos-rg35xx-2024-v5.7* 2>/dev/null
sha256sum "$HS/builds/v5/handshakeos-rg35xx-2024-v5.7.img" 2>/dev/null | cut -c1-80
echo "=== build-v5.8.sh head ==="
head -80 "$HS/builds/v5/v5.8/scripts/build-v5.8.sh"
