#!/bin/sh
# HandshakeOS boot-stack recipe for the Anbernic RG35XX 2024.
# Default DRAM is LPDDR3 (verified 1024 MiB, DCDC3 1.2 V, 672 MHz).
# LPDDR4 is the alternate board revision (anbernic_rg35xx_h700_defconfig).
# Default: check the cross toolchain and print exact commands.
# It does NOT download or compile Linux, U-Boot, or TF-A unless you pass --fetch.
# Fetching the kernel is a long step; do not do it just to see that the recipe works.
#
# Usage:
#   ./board/rg35xx-2024/build-bootstack.sh           # verify toolchain, print plan
#   ./board/rg35xx-2024/build-bootstack.sh --fetch    # actually download and build
#
# Versions are overridable. 6.12 is the floor (sched_ext). Prefer a current
# stable; ROCKNIX was on 7.2 as of 2026-08-17.

set -eu

KERNEL_VERSION="${KERNEL_VERSION:-6.12.58}"
UBOOT_VERSION="${UBOOT_VERSION:-}"
TFA_VERSION="${TFA_VERSION:-}"
ARCH=arm64
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
JOBS="${JOBS:-$(nproc)}"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
WORK="${WORK:-$ROOT/build/bootstack}"
FETCH=0

for arg in "$@"; do
    case "$arg" in
        --fetch) FETCH=1 ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

echo "HandshakeOS boot-stack recipe"
echo "  kernel    Linux ${KERNEL_VERSION}  (override KERNEL_VERSION, floor is 6.12)"
echo "  U-Boot    ${UBOOT_VERSION:-latest mainline (set UBOOT_VERSION to pin a tag)}"
echo "            defconfig anbernic_rg35xx_h700_lpddr3_defconfig (LPDDR3, default)"
echo "            LPDDR4 alternate: anbernic_rg35xx_h700_defconfig"
echo "  TF-A      ${TFA_VERSION:-latest (set TFA_VERSION to pin a tag)}     PLAT=sun50i_h616"
echo "  cross     ${CROSS_COMPILE}gcc"
echo "  work      ${WORK}"
echo

if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
    echo "missing ${CROSS_COMPILE}gcc" >&2
    echo "install gcc-aarch64-linux-gnu and try again." >&2
    exit 1
fi

echo "cross compiler:"
"${CROSS_COMPILE}gcc" --version | head -n 1
echo "ok: toolchain is present"
echo

echo "Plan (run with --fetch to execute):"
echo "  1. Image the stock SD card first. See windows-backup-stock-sd.ps1."
echo "     Work only on a second card in TF1."
echo "  2. Build TF-A:"
echo "       git clone --depth 1 ${TFA_VERSION:+--branch ${TFA_VERSION} }https://github.com/ARM-software/arm-trusted-firmware"
echo "       make -C arm-trusted-firmware PLAT=sun50i_h616 CROSS_COMPILE=${CROSS_COMPILE} bl31 -j${JOBS}"
echo "  3. Build mainline U-Boot with that BL31, LPDDR3 (default):"
echo "       git clone --depth 1 ${UBOOT_VERSION:+--branch ${UBOOT_VERSION} }https://github.com/u-boot/u-boot"
echo "       git -C u-boot apply ${ROOT}/board/rg35xx-2024/patches/u-boot/*.patch"
echo "       make -C u-boot anbernic_rg35xx_h700_lpddr3_defconfig"
echo "       make -C u-boot BL31=../arm-trusted-firmware/build/sun50i_h616/release/bl31.bin CROSS_COMPILE=${CROSS_COMPILE} -j${JOBS}"
echo "       # product: u-boot/u-boot-sunxi-with-spl.bin  (LPDDR3)"
echo "       # LPDDR4 boards: use anbernic_rg35xx_h700_defconfig instead."
echo "  4. Build mainline Linux + fragment + the LCD/OTG/MMC1 patches in patches/README.md:"
echo "       git clone --depth 1 --branch v${KERNEL_VERSION} https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git"
echo "       # or a 7.x stable of the ROCKNIX vintage, if you prefer"
echo "       make -C linux ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig"
echo "       scripts/kconfig/merge_config.sh -m linux/.config kernel.config.fragment"
echo "       make -C linux ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} olddefconfig"
echo "       make -C linux ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image dtbs -j${JOBS}"
echo "       # dtb: sun50i-h700-anbernic-rg35xx-2024.dtb  (keep the rev6 variant handy)"
echo "  5. SD layout (sunxi):"
echo "       SPL+U-Boot at 8 KiB (dd if=u-boot-sunxi-with-spl.bin of=/dev/sdX bs=1024 seek=8)"
echo "       FAT boot partition: Image, dtb, extlinux.conf"
echo "       ext4 root: handshake-init as /init, plus /proc /sys /dev"
echo "       Boot DTB is the NV3052C-clean tree (panel compatible"
echo "       anbernic,rg35xx-plus-panel only; no external .panel firmware)."
echo "  6. Console: ttyS0,115200"
echo

if [ "$FETCH" -eq 0 ]; then
    echo "Stopping here (no --fetch). Toolchain verified, recipe is ready."
    exit 0
fi

echo "--fetch is set: this will download TF-A, U-Boot, and Linux. Continuing."
mkdir -p "$WORK"
cd "$WORK"

if [ ! -d arm-trusted-firmware ]; then
    if [ -n "${TFA_VERSION}" ]; then
        git clone --depth 1 --branch "${TFA_VERSION}" https://github.com/ARM-software/arm-trusted-firmware
    else
        git clone --depth 1 https://github.com/ARM-software/arm-trusted-firmware
    fi
fi
make -C arm-trusted-firmware PLAT=sun50i_h616 CROSS_COMPILE="${CROSS_COMPILE}" bl31 -j"${JOBS}"

if [ ! -d u-boot ]; then
    if [ -n "${UBOOT_VERSION}" ]; then
        git clone --depth 1 --branch "${UBOOT_VERSION}" https://github.com/u-boot/u-boot
    else
        git clone --depth 1 https://github.com/u-boot/u-boot
    fi
fi
# Apply the verified H700 LPDDR3 defconfig and SPL breadcrumb patches.
for pat in "${ROOT}/board/rg35xx-2024/patches/u-boot/"*.patch; do
    git -C u-boot apply "${pat}"
done
make -C u-boot anbernic_rg35xx_h700_lpddr3_defconfig
make -C u-boot \
    BL31="${WORK}/arm-trusted-firmware/build/sun50i_h616/release/bl31.bin" \
    CROSS_COMPILE="${CROSS_COMPILE}" -j"${JOBS}"

if [ ! -d linux ]; then
    git clone --depth 1 --branch "v${KERNEL_VERSION}" \
        https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux
fi
make -C linux ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" defconfig
# merge_config lives in the kernel tree
linux/scripts/kconfig/merge_config.sh -m -O linux \
    linux/.config "${ROOT}/board/rg35xx-2024/kernel.config.fragment"
make -C linux ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" olddefconfig
make -C linux ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" Image dtbs -j"${JOBS}"

echo "Build finished under ${WORK}."
echo "Remember: the LCD still needs the patches in patches/README.md before the panel will light."
