#!/usr/bin/env bash
# Build the v5.8 HandshakeOS RG35XX 2024 kernel inside the handshake tree
# (WSL /mnt/c/.../handshake/builds/v5/v5.8). Does not use /root/hs-v58,
# does not touch v5.7 images, git, or pchie. No Python.
# Official Handshake patches 0001-0004 are applied as-is (not edited).
set -eu

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
TREE=$HS_ROOT
V58=${TREE}/builds/v5/v5.8
XFER=${V58}/xfer
SRC=${V58}/linux-7.3.5
KBUILD=${V58}/kbuild
STAGING=${V58}/kernel
LOGDIR=${V58}/logs
WANT_ZST_SHA=c23025ff66ecc76278089649d6270a666dd7acc5a77ae49e40b78fcef7eedc87
CFG_SHA=6f81a09474b301a150dde0b881b28a7e3d7f53976d271965fb8b54f8e8a3b687
P1_SHA=b9280421305257c7b6bb5e05eac80021873ce95a0817e88e01a47e2ed372edb9
P2_SHA=454f37f7a50ef45ec65aa69167554b5590228fcc88941152d745c10dd6df546b
P3_SHA=49b16f46c79c1a7a0670509d492694be3fd0f2043fab06f2a247f9ece76ebe03
P4_SHA=786c2084f3bd914b29ce474c5834dd7ee2749bab718cb08841648a4a96e6a1dc

WIN_V58="${HANDSHAKE_ROOT_WIN:-handshake\\builds\\v5\\v5.8}"

mkdir -p "${LOGDIR}" "${STAGING}" "${STAGING}/modules" "${STAGING}/qemu-modules"
echo "===== v5.8 kernel build $(date) ====="
echo "TREE=${TREE}"
echo "SRC=${SRC}"
echo "KBUILD=${KBUILD}"
id
uname -a
nproc
df -h / /mnt/c | sed -n '1,8p'

need() { command -v "$1" >/dev/null || { echo "MISSING $1"; exit 1; }; }
need tar
need zstd
need patch
need make
need sha256sum
need gcc

# Prefer gcc-14 if present (v5.2 used Debian 14.2.0-19; WSL has Ubuntu 14.2.0).
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
if command -v aarch64-linux-gnu-gcc-14 >/dev/null; then
  export CC=aarch64-linux-gnu-gcc-14
  ${CC} --version | head -1
else
  ${CROSS_COMPILE}gcc --version | head -1
fi

# ---- case-sensitive work dirs on NTFS (required for the kernel tree) ----
enable_cs() {
  # $1 = Windows path of an existing empty directory
  fsutil.exe file setCaseSensitiveInfo "$1" enable
}

# Unpack meta (patches + v3-audio config) if needed
META=${V58}/meta
if [ ! -f "${META}/xfer/kernel-7.3.5-v3-audio.config" ]; then
  mkdir -p "${META}"
  tar -C "${META}" -xf "${XFER}/v58-meta.tar"
fi
CFG=${META}/xfer/kernel-7.3.5-v3-audio.config
PAT=${META}/patches
test -f "${CFG}"
echo "${CFG_SHA}  ${CFG}" | sha256sum -c
echo "${P1_SHA}  ${PAT}/0001-handshake-tick-tock-phase-scheduler-and-lockstat.patch" | sha256sum -c
echo "${P2_SHA}  ${PAT}/0002-handshake-timekeeper-local-timers-defer-wake-worker-pin.patch" | sha256sum -c
echo "${P3_SHA}  ${PAT}/0003-handshake-futex-phase-handoff-exit-handshake-lockuse.patch" | sha256sum -c
echo "${P4_SHA}  ${PAT}/0004-handshake-futex-slots-v2-rescue-nophash-tty-output-spinlock.patch" | sha256sum -c

APPLY5=${V58}/scripts/apply-0005
if [ ! -x "${APPLY5}" ]; then
  gcc -O2 -o "${APPLY5}" "${V58}/scripts/apply-0005.c"
fi
test -x "${APPLY5}"

# ---- assemble vendor tarball from the already-copied parts ----
ZST=${XFER}/linux-7.3.5-vendor.tar.zst
if [ ! -f "${ZST}" ] || ! echo "${WANT_ZST_SHA}  ${ZST}" | sha256sum -c; then
  echo "Assembling vendor tarball from parts..."
  cat "${XFER}/linux-7.3.5-vendor.tar.zst.part0" \
      "${XFER}/linux-7.3.5-vendor.tar.zst.part1" \
      "${XFER}/linux-7.3.5-vendor.tar.zst.part2" > "${ZST}"
  echo "${WANT_ZST_SHA}  ${ZST}" | sha256sum -c
fi

# ---- extract into a case-sensitive directory ----
if [ ! -f "${SRC}/Makefile" ]; then
  if [ -e "${SRC}" ]; then
    echo "ERROR: ${SRC} exists but has no Makefile; not clobbering it"
    exit 1
  fi
  mkdir -p "${SRC}"
  if ! enable_cs "${WIN_V58}\\linux-7.3.5"; then
    echo "ERROR: could not enable case sensitivity on ${SRC}"
    echo "A kernel tree cannot be built on a case-insensitive NTFS folder."
    rmdir "${SRC}" 2>/dev/null || true
    exit 1
  fi
  echo "Extracting linux-7.3.5 into the handshake tree (this takes a few minutes)..."
  zstd -dc "${ZST}" | tar -C "${V58}" -xf -
fi
test -f "${SRC}/Makefile" || { echo "MISSING ${SRC}/Makefile"; exit 1; }

# Restore vmlinux* linker scripts accidentally dropped by --exclude=vmlinux
if [ ! -f "${SRC}/include/asm-generic/vmlinux.lds.h" ] || \
   [ ! -f "${SRC}/arch/arm64/kernel/vmlinux.lds.S" ]; then
  test -f "${XFER}/vmlinux-restore.tar" || { echo "MISSING vmlinux-restore.tar"; exit 1; }
  echo "Restoring vmlinux* linker scripts"
  tar -C "${V58}" -xf "${XFER}/vmlinux-restore.tar"
fi
test -f "${SRC}/include/asm-generic/vmlinux.lds.h" || { echo "MISSING vmlinux.lds.h"; exit 1; }
test -f "${SRC}/arch/arm64/kernel/vmlinux.lds.S" || { echo "MISSING vmlinux.lds.S"; exit 1; }
grep -q '^VERSION = 7$' "${SRC}/Makefile" || { echo "BAD VERSION"; exit 1; }
grep -q '^PATCHLEVEL = 3$' "${SRC}/Makefile" || { echo "BAD PATCHLEVEL"; exit 1; }
grep -q '^SUBLEVEL = 5$' "${SRC}/Makefile" || { echo "BAD SUBLEVEL"; exit 1; }
echo "SOURCE OK ${SRC}"

# ---- apply Handshake patches once ----
STAMPP=${SRC}/.handshake-patches-applied
if [ ! -f "${STAMPP}" ]; then
  echo "Applying Handshake 0001-0004 + 0005..."
  for p in \
    "${PAT}/0001-handshake-tick-tock-phase-scheduler-and-lockstat.patch" \
    "${PAT}/0002-handshake-timekeeper-local-timers-defer-wake-worker-pin.patch" \
    "${PAT}/0003-handshake-futex-phase-handoff-exit-handshake-lockuse.patch" \
    "${PAT}/0004-handshake-futex-slots-v2-rescue-nophash-tty-output-spinlock.patch"
  do
    echo "PATCH $(basename "$p")"
    patch -d "${SRC}" -p1 --forward < "$p"
  done
  "${APPLY5}" "${SRC}/kernel/locking/hs_lockstat.c"
  grep -q handshake_holds "${SRC}/kernel/locking/hs_lockstat.c"
  grep -q handshake_spinsites "${SRC}/kernel/locking/hs_lockstat.c"
  date > "${STAMPP}"
  echo "0001-0004 + 0005 applied" >> "${STAMPP}"
else
  echo "Patches already applied ($(tr '\n' ' ' < "${STAMPP}"))"
fi

# ---- out-of-tree kbuild dir, also case-sensitive ----
if [ ! -d "${KBUILD}" ]; then
  mkdir -p "${KBUILD}"
  if ! enable_cs "${WIN_V58}\\kbuild"; then
    echo "ERROR: could not enable case sensitivity on ${KBUILD}"
    exit 1
  fi
fi

# Vendor tree may ship a .config; an O= build refuses a dirty source tree.
echo "make mrproper in source (keeps patched .c/.h, drops in-tree .config)"
make -C "${SRC}" mrproper

# Merge v3-audio device config + Handshake phase-4 options (awk, no Python).
cp "${CFG}" "${KBUILD}/.config"
awk '
  BEGIN {
    drop["CONFIG_LOCALVERSION"]=1
    drop["CONFIG_LOCALVERSION_AUTO"]=1
    drop["CONFIG_HANDSHAKE_SCHED"]=1
    drop["CONFIG_HANDSHAKE_TIMEKEEPER"]=1
    drop["CONFIG_HANDSHAKE_LOCAL_TIMERS"]=1
    drop["CONFIG_HANDSHAKE_DEFER_WAKE"]=1
    drop["CONFIG_HANDSHAKE_WORKER_PIN"]=1
    drop["CONFIG_HANDSHAKE_FUTEX"]=1
    drop["CONFIG_HANDSHAKE_FUTEX_EXIT"]=1
    drop["CONFIG_HANDSHAKE_LOCKSTAT"]=1
    drop["CONFIG_HANDSHAKE_FUTEX_NOPHASH"]=1
    drop["CONFIG_HANDSHAKE_TTY_OUTPUT"]=1
    drop["CONFIG_HANDSHAKE_FUTEX_RESCUE"]=1
  }
  {
    key=$0
    sub(/=.*/, "", key)
    sub(/^# /, "", key)
    sub(/ is not set$/, "", key)
    if (key in drop) next
    print
  }
' "${KBUILD}/.config" > "${KBUILD}/.config.merged"
cat >> "${KBUILD}/.config.merged" << 'EOF'
CONFIG_LOCALVERSION="-hs-v58"
# CONFIG_LOCALVERSION_AUTO is not set
CONFIG_HANDSHAKE_SCHED=y
CONFIG_HANDSHAKE_TIMEKEEPER=y
CONFIG_HANDSHAKE_LOCAL_TIMERS=y
CONFIG_HANDSHAKE_DEFER_WAKE=y
CONFIG_HANDSHAKE_WORKER_PIN=y
CONFIG_HANDSHAKE_FUTEX=y
CONFIG_HANDSHAKE_FUTEX_EXIT=y
CONFIG_HANDSHAKE_LOCKSTAT=y
CONFIG_HANDSHAKE_FUTEX_NOPHASH=y
CONFIG_HANDSHAKE_TTY_OUTPUT=y
CONFIG_HANDSHAKE_FUTEX_RESCUE=y
CONFIG_USB_GADGET=y
CONFIG_USB_LIBCOMPOSITE=m
CONFIG_USB_F_ACM=m
CONFIG_USB_CONFIGFS=m
CONFIG_USB_CONFIGFS_ACM=y
CONFIG_USB_CONFIGFS_MASS_STORAGE=y
CONFIG_USB_F_MASS_STORAGE=m
CONFIG_USB_DUMMY_HCD=m
EOF
mv "${KBUILD}/.config.merged" "${KBUILD}/.config"

echo "olddefconfig..."
make -C "${SRC}" O="${KBUILD}" olddefconfig

echo "======= Handshake + USB + H700 config after olddefconfig ======="
grep -E '^CONFIG_(HANDSHAKE_SCHED|HANDSHAKE_TIMEKEEPER|HANDSHAKE_LOCAL_TIMERS|HANDSHAKE_DEFER_WAKE|HANDSHAKE_WORKER_PIN|HANDSHAKE_FUTEX|HANDSHAKE_FUTEX_EXIT|HANDSHAKE_LOCKSTAT|HANDSHAKE_FUTEX_NOPHASH|HANDSHAKE_TTY_OUTPUT|HANDSHAKE_FUTEX_RESCUE|LOCALVERSION|USB_LIBCOMPOSITE|USB_F_ACM|USB_CONFIGFS|USB_F_MASS_STORAGE|USB_DUMMY_HCD|USB_GADGET|DRM_PANEL_NEWVISION_NV3052C|MFD_AXP20X|AXP20X_POWER|SND_SUN4I_CODEC|USB_MUSB|MMC_SUNXI)=' \
  "${KBUILD}/.config" || true

for s in CONFIG_HANDSHAKE_SCHED CONFIG_HANDSHAKE_LOCKSTAT CONFIG_HANDSHAKE_FUTEX \
         CONFIG_HANDSHAKE_FUTEX_RESCUE CONFIG_HANDSHAKE_FUTEX_NOPHASH \
         CONFIG_HANDSHAKE_TTY_OUTPUT CONFIG_HANDSHAKE_TIMEKEEPER \
         CONFIG_HANDSHAKE_LOCAL_TIMERS CONFIG_HANDSHAKE_DEFER_WAKE \
         CONFIG_HANDSHAKE_WORKER_PIN CONFIG_HANDSHAKE_FUTEX_EXIT \
         CONFIG_DRM_PANEL_NEWVISION_NV3052C CONFIG_USB_F_ACM \
         CONFIG_USB_F_MASS_STORAGE CONFIG_USB_CONFIGFS_ACM \
         CONFIG_SND_SUN4I_CODEC CONFIG_MMC_SUNXI
 do
  grep -q "^${s}=[ym]" "${KBUILD}/.config" || { echo "MISSING ${s}"; exit 1; }
done

echo "======= starting Image / dtbs / modules ======="
# NTFS is I/O heavy; leave a couple of cores free.
JOBS=$(nproc)
if [ "${JOBS}" -gt 4 ]; then JOBS=$((JOBS-2)); fi
echo "make -j${JOBS}"
{
  echo "compiler: $(${CC:-${CROSS_COMPILE}gcc} --version | head -1)"
  echo "src: ${SRC}"
  echo "kbuild: ${KBUILD}"
  echo "start: $(date)"
} > "${STAGING}/BUILD-INFO.txt"

set +e
make -C "${SRC}" O="${KBUILD}" -j"${JOBS}" Image Image.gz dtbs modules
RC=$?
set -e
echo "BUILD_RC=${RC} at $(date)"
if [ "${RC}" -ne 0 ]; then
  echo "BUILD FAILED"
  exit "${RC}"
fi

cp -a "${KBUILD}/arch/arm64/boot/Image" "${STAGING}/Image"
cp -a "${KBUILD}/arch/arm64/boot/Image.gz" "${STAGING}/Image.gz"
find "${KBUILD}/arch/arm64/boot/dts" \( -name '*rg35xx*2024*.dtb' -o -name '*h700*rg35xx*.dtb' \) > "${STAGING}/dtb-list.txt" || true
while IFS= read -r d; do
  [ -f "$d" ] || continue
  cp -a "$d" "${STAGING}/$(basename "$d")"
done < "${STAGING}/dtb-list.txt"

MODDEST=${V58}/modinst
rm -rf "${MODDEST}"
make -C "${SRC}" O="${KBUILD}" INSTALL_MOD_PATH="${MODDEST}" modules_install
find "${MODDEST}" \( -name 'libcomposite.ko' -o -name 'usb_f_acm.ko' -o -name 'usb_f_mass_storage.ko' \
  -o -name 'usb_configfs.ko' -o -name 'u_serial.ko' -o -name 'dummy_hcd.ko' \) > "${STAGING}/mod-list.txt"
while IFS= read -r m; do
  bn=$(basename "$m")
  if [ "$bn" = "dummy_hcd.ko" ]; then
    cp -a "$m" "${STAGING}/qemu-modules/"
  else
    cp -a "$m" "${STAGING}/modules/"
  fi
done < "${STAGING}/mod-list.txt"

for m in libcomposite.ko usb_f_mass_storage.ko usb_f_acm.ko; do
  test -f "${STAGING}/modules/${m}" || { echo "MISSING MODULE ${m}"; exit 1; }
done

{
  echo "end: $(date)"
  echo "BUILD_RC=${RC}"
  sha256sum "${STAGING}/Image" "${STAGING}/Image.gz" "${STAGING}/modules/"*.ko
  ls -l "${STAGING}/Image" "${STAGING}/Image.gz" "${STAGING}/modules" "${STAGING}"/*.dtb 2>/dev/null || true
  echo "vermagic:"
  modinfo -F vermagic "${STAGING}/modules/libcomposite.ko" 2>/dev/null || true
  echo "HANDSHAKE config:"
  grep '^CONFIG_HANDSHAKE' "${KBUILD}/.config"
} | tee -a "${STAGING}/BUILD-INFO.txt"

cp -a "${KBUILD}/.config" "${STAGING}/kernel-7.3.5-v5.8.config"
diff -u "${CFG}" "${STAGING}/kernel-7.3.5-v5.8.config" > "${STAGING}/config-diff-vs-v3-audio.txt" || true
echo "KERNEL BUILD OK"
