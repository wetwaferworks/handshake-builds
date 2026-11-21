#!/usr/bin/env bash
# Build the v5.8 HandshakeOS RG35XX 2024 kernel on WSL.
# Source: vendor linux-7.3.5 (v3-audio / v5.2, SHA pinned) + official
# handshake patches 0001-0004 (not edited) + additive 0005 proc exports.
# Build directory is on WSL ext4, not inside a box scratch tree.
# Does not touch git, v5.7 images, or pchie.
set -eu
# Avoid set -o pipefail so a leftover SIGPIPE on sha256sum | grep cannot abort
# a successful kernel compile.

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
XFER=$HS_ROOT/builds/v5/v5.8/xfer
STAMP=$HS_ROOT/builds/v5/v5.8
WANT_ZST_SHA=c23025ff66ecc76278089649d6270a666dd7acc5a77ae49e40b78fcef7eedc87
CFG_SHA=6f81a09474b301a150dde0b881b28a7e3d7f53976d271965fb8b54f8e8a3b687
P1_SHA=b9280421305257c7b6bb5e05eac80021873ce95a0817e88e01a47e2ed372edb9
P2_SHA=454f37f7a50ef45ec65aa69167554b5590228fcc88941152d745c10dd6df546b
P3_SHA=49b16f46c79c1a7a0670509d492694be3fd0f2043fab06f2a247f9ece76ebe03
P4_SHA=786c2084f3bd914b29ce474c5834dd7ee2749bab718cb08841648a4a96e6a1dc

# Prefer a writable Linux-native home; fall back to /opt or /tmp on the same ext4.
if [ -n "${HOME:-}" ] && [ -d "${HOME}" ] && [ -w "${HOME}" ]; then
  ROOT=${HOME}/hs-v58
else
  ROOT=/opt/hs-v58
  mkdir -p "${ROOT}" 2>/dev/null || ROOT=/tmp/hs-v58
fi
SRC=${ROOT}/linux-7.3.5
KBUILD=${ROOT}/kbuild
LOG=${ROOT}/build-kernel.log
STAGING=${STAMP}/kernel

mkdir -p "${ROOT}" "${STAGING}" "${STAMP}/logs"
# stdout/stderr stay on the terminal (the WSL wrapper captures them).
# Also copy a line-level log after the run via the caller if needed.
echo "===== v5.8 kernel build $(date) ====="
echo "LOG=${LOG}"
echo "ROOT=${ROOT}"
id
uname -a
nproc
df -h / "${HOME:-/}" /mnt/c | sed -n '1,8p'

need() { command -v "$1" >/dev/null || { echo "MISSING $1"; exit 1; }; }
need tar
need zstd
need patch
need make
need sha256sum

# Prefer a 14.x aarch64 GCC if present (v5.2 used Debian 14.2.0-19).
CROSS=aarch64-linux-gnu-
if command -v aarch64-linux-gnu-gcc-14 >/dev/null; then
  CROSS=aarch64-linux-gnu-gcc-14
  # CROSS_COMPILE wants the prefix, not the gcc name.
  CROSS=aarch64-linux-gnu-
  export CC=aarch64-linux-gnu-gcc-14
  export ${CC:+CC}
fi
export ARCH=arm64
export CROSS_COMPILE=${CROSS}
if [ -n "${CC:-}" ]; then
  ${CC} --version | head -1
else
  ${CROSS}gcc --version | head -1
fi

# ---- unpack official 0001-0004 + 0005 + v3-audio config from meta.tar ----
META=${ROOT}/meta
mkdir -p "${META}"
tar -C "${META}" -xf "${XFER}/v58-meta.tar"
# meta.tar layout: patches/0001..0004, xfer/kernel-7.3.5-v3-audio.config, xfer/dts
CFG=${META}/xfer/kernel-7.3.5-v3-audio.config
PAT=${META}/patches
test -f "${CFG}"
echo "${CFG_SHA}  ${CFG}" | sha256sum -c
echo "${P1_SHA}  ${PAT}/0001-handshake-tick-tock-phase-scheduler-and-lockstat.patch" | sha256sum -c
echo "${P2_SHA}  ${PAT}/0002-handshake-timekeeper-local-timers-defer-wake-worker-pin.patch" | sha256sum -c
echo "${P3_SHA}  ${PAT}/0003-handshake-futex-phase-handoff-exit-handshake-lockuse.patch" | sha256sum -c
echo "${P4_SHA}  ${PAT}/0004-handshake-futex-slots-v2-rescue-nophash-tty-output-spinlock.patch" | sha256sum -c

# 0005 may be next to the script or already in XFER
P5=
for c in \
  "${XFER}/0005-handshake-v58-proc-holds-sites.patch" \
  "${STAMP}/patches-v58/0005-handshake-v58-proc-holds-sites.patch" \
  "$(dirname "$0")/../patches-v58/0005-handshake-v58-proc-holds-sites.patch"
 do
  if [ -f "$c" ]; then P5=$c; break; fi
done
if [ -z "${P5}" ]; then
  echo "ERROR: 0005 patch not found"
  exit 1
fi
echo "0005 ${P5}"

# ---- assemble and verify the vendor linux tree ----
ZST=${ROOT}/linux-7.3.5-vendor.tar.zst
if [ ! -f "${ZST}" ] || ! echo "${WANT_ZST_SHA}  ${ZST}" | sha256sum -c; then
  echo "Assembling vendor tarball from parts..."
  cat "${XFER}/linux-7.3.5-vendor.tar.zst.part0" \
      "${XFER}/linux-7.3.5-vendor.tar.zst.part1" \
      "${XFER}/linux-7.3.5-vendor.tar.zst.part2" > "${ZST}"
  echo "${WANT_ZST_SHA}  ${ZST}" | sha256sum -c
fi

if [ ! -f "${SRC}/Makefile" ]; then
  echo "Extracting linux-7.3.5 (this takes a few minutes)..."
  mkdir -p "${ROOT}"
  # Don't use tar --zstd only; pipe through zstd for wider compatibility.
  zstd -dc "${ZST}" | tar -C "${ROOT}" -xf -
fi
test -f "${SRC}/Makefile" || { echo "MISSING ${SRC}/Makefile"; exit 1; }

# The vendor archive was packed with --exclude=vmlinux, which also dropped
# the 41 vmlinux.lds / vmlinux* linker scripts (same issue as the laptop
# builds/boot tree). Restore them from a small sidecar tarball (files taken
# from the already-restored box vendor tree, originally linux-7.3.5.tar.xz).
RESTORE_TAR=""
for c in   "${XFER}/vmlinux-restore.tar"   "${STAMP}/xfer/vmlinux-restore.tar"   "${ROOT}/vmlinux-restore.tar"
 do
  if [ -f "$c" ]; then RESTORE_TAR=$c; break; fi
done
if [ ! -f "${SRC}/include/asm-generic/vmlinux.lds.h" ] || [ ! -f "${SRC}/arch/arm64/kernel/vmlinux.lds.S" ]; then
  if [ -z "${RESTORE_TAR}" ]; then
    echo "MISSING vmlinux linker scripts and no vmlinux-restore.tar"
    exit 1
  fi
  echo "Restoring vmlinux* linker scripts from $(basename "${RESTORE_TAR}")"
  tar -C "${ROOT}" -xf "${RESTORE_TAR}"
fi
test -f "${SRC}/include/asm-generic/vmlinux.lds.h" || { echo "MISSING vmlinux.lds.h"; exit 1; }
test -f "${SRC}/arch/arm64/kernel/vmlinux.lds.S" || { echo "MISSING vmlinux.lds.S"; exit 1; }
# Confirm this is 7.3.5
grep -q '^VERSION = 7$' "${SRC}/Makefile" || { echo "BAD VERSION"; exit 1; }
grep -q '^PATCHLEVEL = 3$' "${SRC}/Makefile" || { echo "BAD PATCHLEVEL"; exit 1; }
grep -q '^SUBLEVEL = 5$' "${SRC}/Makefile" || { echo "BAD SUBLEVEL"; exit 1; }
echo "SOURCE OK ${SRC}"

# ---- apply Handshake patches once (stamp file) ----
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
    patch -d "${SRC}" -p1 --forward --dry-run < "$p" >/tmp/hs-v58-patch-dry.log 2>&1 || {
      echo "DRY-RUN FAILED for $p"
      cat /tmp/hs-v58-patch-dry.log
      exit 1
    }
    patch -d "${SRC}" -p1 --forward < "$p"
  done
  echo "PATCH 0005 (programmatic insert; official 0001-0004 untouched)"
  APPLY5=""
  for c in \
    "${STAMP}/scripts/apply-0005" \
    "$(dirname "$0")/apply-0005"
  do
    if [ -x "$c" ]; then APPLY5=$c; break; fi
  done
  if [ -z "${APPLY5}" ]; then
    SRC5=""
    for c in "${STAMP}/scripts/apply-0005.c" "$(dirname "$0")/apply-0005.c"; do
      if [ -f "$c" ]; then SRC5=$c; break; fi
    done
    if [ -z "${SRC5}" ]; then
      echo "MISSING apply-0005.c"
      exit 1
    fi
    gcc -O2 -o /tmp/apply-0005-v58 "${SRC5}"
    APPLY5=/tmp/apply-0005-v58
  fi
  "${APPLY5}" "${SRC}/kernel/locking/hs_lockstat.c"
  date > "${STAMPP}"
  echo "PATCHES APPLIED" >> "${STAMPP}"
else
  echo "Patches already applied ($(cat "${STAMPP}"))"
fi

# ---- config: exact v3-audio device config + Handshake phase-4 options ----
# Vendor tree ships a .config (and maybe generated headers). An O= build
# refuses to start unless the source tree is clean.
echo "make mrproper in source (keeps patched .c/.h, drops in-tree .config)"
make -C "${SRC}" mrproper
mkdir -p "${KBUILD}"
cp "${CFG}" "${KBUILD}/.config"
# Merge Handshake options without dropping any H700 driver from the device config.
# Use a fragment + scripts/kconfig/merge or sed on .config then olddefconfig.
FRAG=${ROOT}/handshake-v58.fragment
cat > "${FRAG}" << 'EOF'
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
# CDC-ACM already =m in v3-audio; keep them explicit
CONFIG_USB_GADGET=y
CONFIG_USB_LIBCOMPOSITE=m
CONFIG_USB_F_ACM=m
CONFIG_USB_CONFIGFS=m
CONFIG_USB_CONFIGFS_ACM=y
CONFIG_USB_CONFIGFS_MASS_STORAGE=y
CONFIG_USB_F_MASS_STORAGE=m
CONFIG_USB_DUMMY_HCD=m
EOF

# Drop AUTO localversion and blank LOCALVERSION from the copied config, then append fragment keys.
# olddefconfig will resolve dependencies.
awk -v fragfile="${FRAG}" '
BEGIN {
  nkeys = split("CONFIG_LOCALVERSION CONFIG_LOCALVERSION_AUTO CONFIG_HANDSHAKE_SCHED CONFIG_HANDSHAKE_TIMEKEEPER CONFIG_HANDSHAKE_LOCAL_TIMERS CONFIG_HANDSHAKE_DEFER_WAKE CONFIG_HANDSHAKE_WORKER_PIN CONFIG_HANDSHAKE_FUTEX CONFIG_HANDSHAKE_FUTEX_EXIT CONFIG_HANDSHAKE_LOCKSTAT CONFIG_HANDSHAKE_FUTEX_NOPHASH CONFIG_HANDSHAKE_TTY_OUTPUT CONFIG_HANDSHAKE_FUTEX_RESCUE", keys, " ")
}
{
  skip = 0
  for (i = 1; i <= nkeys; i++) {
    k = keys[i]
    if (index($0, k "=") == 1 || index($0, "# " k " ") == 1) { skip = 1; break }
  }
  if (!skip) print $0
}
END {
  print ""
  while ((getline f < fragfile) > 0) print f
  close(fragfile)
}
' "${KBUILD}/.config" > "${KBUILD}/.config.merged"
mv "${KBUILD}/.config.merged" "${KBUILD}/.config"
echo "config merged"

echo "olddefconfig..."
make -C "${SRC}" O="${KBUILD}" olddefconfig

echo "======= Handshake + USB config after olddefconfig ======="
grep -E '^CONFIG_(HANDSHAKE_|LOCALVERSION|USB_LIBCOMPOSITE|USB_F_ACM|USB_CONFIGFS|USB_F_MASS_STORAGE|USB_DUMMY_HCD|USB_GADGET|DRM_PANEL_NEWVISION_NV3052C|MFD_AXP20X|AXP20X_POWER|SND_SUN4I_CODEC|SND_SUN50I|SND_SIMPLE|USB_MUSB|MMC_SUNXI)=' \
  "${KBUILD}/.config" || true
# Fail if a required Handshake or H700 symbol dropped
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
# AXP717 may appear as AXP20X / POWER
grep -qE '^CONFIG_(MFD_AXP20X|AXP20X_POWER|BATTERY_AXP20X|INPUT_AXP20X_PEK)=' "${KBUILD}/.config" || {
  echo "WARNING: no AXP20X symbol found — inspect config"
}

echo "======= starting Image / dtbs / modules ======="
JOBS=$(nproc)
# Leave a core free if this is a large box
if [ "${JOBS}" -gt 2 ]; then JOBS=$((JOBS-1)); fi
echo "make -j${JOBS}"
# Record compiler in a sidecar
{
  echo "compiler: $(${CC:-${CROSS}gcc} --version | head -1)"
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
  tail -80 "${LOG}" || true
  exit "${RC}"
fi

# Stage outputs into the handshake tree
mkdir -p "${STAGING}/modules" "${STAGING}/qemu-modules"
cp -a "${KBUILD}/arch/arm64/boot/Image" "${STAGING}/Image"
cp -a "${KBUILD}/arch/arm64/boot/Image.gz" "${STAGING}/Image.gz"
# DTB names used historically
find "${KBUILD}/arch/arm64/boot/dts" -name '*rg35xx*2024*.dtb' -o -name '*h700*rg35xx*.dtb' | tee "${STAGING}/dtb-list.txt"
# Copy any matching dtb
while IFS= read -r d; do
  [ -f "$d" ] || continue
  cp -a "$d" "${STAGING}/$(basename "$d")"
done < "${STAGING}/dtb-list.txt"

# modules we ship / keep for qemu
# Find .ko by name after modules_install into a temp dest
MODDEST=${ROOT}/mods
rm -rf "${MODDEST}"
make -C "${SRC}" O="${KBUILD}" INSTALL_MOD_PATH="${MODDEST}" modules_install
echo "======= modules of interest ======="
find "${MODDEST}" -name 'libcomposite.ko' -o -name 'usb_f_acm.ko' -o -name 'usb_f_mass_storage.ko' \
  -o -name 'usb_configfs.ko' -o -name 'u_serial.ko' -o -name 'dummy_hcd.ko' | tee "${STAGING}/mod-list.txt"
while IFS= read -r m; do
  bn=$(basename "$m")
  if [ "$bn" = "dummy_hcd.ko" ]; then
    cp -a "$m" "${STAGING}/qemu-modules/"
  else
    cp -a "$m" "${STAGING}/modules/"
  fi
done < "${STAGING}/mod-list.txt"

# Required ship modules
for m in libcomposite.ko usb_f_mass_storage.ko usb_f_acm.ko; do
  test -f "${STAGING}/modules/${m}" || { echo "MISSING MODULE ${m}"; exit 1; }
done

{
  echo "end: $(date)"
  echo "BUILD_RC=${RC}"
  sha256sum "${STAGING}/Image" "${STAGING}/Image.gz" "${STAGING}/modules/"*.ko
  ls -l "${STAGING}/Image" "${STAGING}/Image.gz" "${STAGING}/modules" "${STAGING}"/*.dtb 2>/dev/null || true
  echo "vermagic:"
  modinfo -F vermagic "${STAGING}/modules/libcomposite.ko" 2>/dev/null || strings "${STAGING}/modules/libcomposite.ko" | grep -m1 'preempt' || true
  echo "HANDSHAKE config:"
  grep '^CONFIG_HANDSHAKE' "${KBUILD}/.config"
} | tee -a "${STAGING}/BUILD-INFO.txt"

cp -a "${KBUILD}/.config" "${STAGING}/kernel-7.3.5-v5.8.config"
# config diff vs v3-audio (Handshake + localversion only, expected)
diff -u "${CFG}" "${STAGING}/kernel-7.3.5-v5.8.config" > "${STAGING}/config-diff-vs-v3-audio.txt" || true
echo "KERNEL BUILD OK"
