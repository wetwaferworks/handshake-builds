#!/usr/bin/env bash
# Image-wide zero-mutex / zero-pthread scan for v5.7.
# Do not pipe the scan loop into tee: that would run it in a subshell and
# leave hits=0 (false CLEAN). slog + tee -a keeps hits in this shell.
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.7
LOG=$OUT/logs
CPIO=$V5/handshakeos-initramfs-v5.7.cpio.gz
RD=$OUT/tmp/image-scan
mkdir -p "$LOG" "$RD"
rm -rf "$RD"; mkdir -p "$RD"
( cd "$RD" && zcat "$CPIO" | cpio -id --quiet )

USER_BANNED='pthread|mutex|rwlock|spin_?lock|_cond_|barrier|sem_(wait|post|init|timedwait|trywait)|mtx_|cnd_|thrd_|futex|flock|lockf'
KMOD_BANNED='pthread'
hits=0
kmod_pthread=0
SCAN=$LOG/image-scan.txt
: > "$SCAN"
slog() { printf '%s\n' "$*" | tee -a "$SCAN"; }

slog "CPIO_DUMMY_HCD $(find "$RD" -iname '*dummy_hcd*' | wc -l) (want 0)"
slog "CPIO_LIBCOMPOSITE $(test -f "$RD/modules/libcomposite.ko" && echo PRESENT || echo MISSING)"
slog "CPIO_MASS_STORAGE $(test -f "$RD/modules/usb_f_mass_storage.ko" && echo PRESENT || echo MISSING)"
slog "CPIO_BENCH_N64 $(test -x "$RD/bin/bench_n64" && echo PRESENT || echo MISSING)"
slog "CPIO_HS_PF_PROBE $(test -x "$RD/bin/hs_pf_probe" && echo PRESENT || echo MISSING)"
slog "===== NM LOCK/THREAD SCAN ====="
while IFS= read -r f; do
  file "$f" | grep -q 'ELF' || continue
  case "$f" in
    *.ko)
      u=$(nm -a "$f" 2>/dev/null | grep -E "$KMOD_BANNED" || true)
      km=$(nm -a "$f" 2>/dev/null | awk '{print $NF}' | grep -E 'mutex|rwlock|spinlock|pthread' | sort -u || true)
      if [ -n "$u" ]; then
        slog "HIT_GLIBC_PTHREAD $f"; printf '%s\n' "$u" | tee -a "$SCAN"
        kmod_pthread=$((kmod_pthread+1))
        hits=$((hits+1))
      else
        slog "CLEAN_NO_PTHREAD $f"
      fi
      if [ -n "$km" ]; then
        slog "KMOD_KERNEL_LOCK_SYMS $f (kernel-side, not a userspace fail):"
        printf '%s\n' "$km" | tee -a "$SCAN"
      fi
      ;;
    *)
      u=$(nm -a "$f" 2>/dev/null | grep -E "$USER_BANNED" || true)
      if [ -n "$u" ]; then
        slog "HIT $f"; printf '%s\n' "$u" | tee -a "$SCAN"
        hits=$((hits+1))
      else
        slog "CLEAN $f"
      fi
      ;;
  esac
done < <(find "$RD" -type f | sort)
slog "LOCK_HITS $hits KMOD_PTHREAD $kmod_pthread"
logged=$(awk '/^LOCK_HITS /{print $2}' "$SCAN")
[ "$hits" = "$logged" ] || { echo "SCAN_COUNT_MISMATCH hits=$hits logged=$logged"; exit 1; }
[ "$hits" = 0 ] || { echo "IMAGE_ZERO_MUTEX_FAIL hits=$hits"; exit 1; }
echo IMAGE_ZERO_MUTEX_CLEAN | tee -a "$SCAN"
