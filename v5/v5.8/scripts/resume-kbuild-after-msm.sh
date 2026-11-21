#!/bin/bash
# Resume the already-configured handshake-tree kbuild after restoring
# vendor-dropped upstream .py generators (msm gen_header.py). Does not
# mrproper, reconfig, or touch /root/hs-v58.
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
T=$HS_ROOT
OUT=$T/builds/v5/v5.8
SRC=$OUT/linux-7.3.5
KB=$OUT/kbuild
cd "$SRC"
if [ ! -f drivers/gpu/drm/msm/registers/gen_header.py ]; then
  echo "MISSING gen_header.py after restore" >&2
  exit 1
fi
if [ ! -f "$KB/.config" ]; then
  echo "MISSING kbuild .config" >&2
  exit 1
fi
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
echo "======= resume Image / dtbs / modules $(date) ======="
set +e
make O="$KB" -j10 Image Image.gz dtbs modules
rc=$?
set -e
echo "BUILD_RC=$rc at $(date)"
if [ "$rc" -ne 0 ]; then
  echo "BUILD FAILED"
  exit "$rc"
fi
if [ ! -f "$KB/arch/arm64/boot/Image.gz" ]; then
  echo "MISSING Image.gz"
  exit 1
fi
echo "======= KERNEL BUILD OK ======="
ls -l "$KB/arch/arm64/boot/Image" "$KB/arch/arm64/boot/Image.gz" "$KB/vmlinux"
sha256sum "$KB/arch/arm64/boot/Image.gz"
