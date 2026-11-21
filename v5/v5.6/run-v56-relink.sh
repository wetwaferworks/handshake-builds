#!/usr/bin/env bash
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.6
LOG=$OUT/logs
mkdir -p "$LOG" "$OUT/tmp" "$OUT/modules" "$OUT/previews"
export PATH="$PATH:/usr/sbin:/sbin"
export TMPDIR=$OUT/tmp

# Never touch v5.5 (or earlier) images.
# Seed v5.6 with ship modules (dummy_hcd is test-only; pack will not include it).
cp -a "$V5/v5.5/modules/." "$OUT/modules/"
# Confirm we have not already mutated v5.5
sha256sum "$V5/handshakeos-rg35xx-2024-v5.5.img" "$V5/handshakeos-rg35xx-2024-v5.5.img.zst" | tee "$LOG/v55-intact-start.sha256"
grep -q '^aee856069633715e1712d943fa8ca74ce7b01483f9f85af5e55f00c681abbfed ' "$LOG/v55-intact-start.sha256"
grep -q '^cc27bc550a69dc414377e497a3fc2d17b50143077bc1808e20a4329948327267 ' "$LOG/v55-intact-start.sha256"

# Keep the previous (pre -ffunction-sections) v5.6 SHA256s in the log.
{
  echo "# v5.6 pack A 3:48 PM ET (n64 tools on hs_rt; pthread stubs still in init/gb)"
  echo "0d2a71c437a51b57aa689bc5f1429b89e7599f74a2372b1a38bb095b69938805  $V5/handshakeos-rg35xx-2024-v5.6.img"
  echo "7e490e1c8bd6fe5c17c91643289fa0d3eef37a7374aa59b05f9f32711fb32cd0  $V5/handshakeos-rg35xx-2024-v5.6.img.zst"
  echo "# v5.6 pack B 4:59 PM ET (function-sections; n64 tools CLEAN; init/gb still had pthread_* via hs_hs_start)"
  echo "4a05222abf9ea252168e3c680817ea73ad09d5ae74e244e57b2fd7b313ed23d8  $V5/handshakeos-rg35xx-2024-v5.6.img"
  echo "3ba041ea44681748b6ca42be12048286e54b32d098d037e17654e86d53639865  $V5/handshakeos-rg35xx-2024-v5.6.img.zst"
} | tee "$LOG/image-v5.6-prev.sha256"
cat "$LOG/image-v5.6-prev.sha256" >> "$LOG/image-v5.6-history.sha256"

echo "===== PRIORS BEFORE (v5 through v5.5) ====="
sha256sum \
  "$V5/handshakeos-rg35xx-2024-v5.img" \
  "$V5/handshakeos-rg35xx-2024-v5.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.1.img" \
  "$V5/handshakeos-rg35xx-2024-v5.1.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.2.img" \
  "$V5/handshakeos-rg35xx-2024-v5.2.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.3.img" \
  "$V5/handshakeos-rg35xx-2024-v5.3.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.5.img" \
  "$V5/handshakeos-rg35xx-2024-v5.5.img.zst" \
  > "$LOG/priors-before.sha256"
cat "$LOG/priors-before.sha256"

echo "===== BUILD v5.6 ====="
cd "$T/handshakeos"
bash "$V5/build-v5.6.sh" > "$LOG/build-v5.6.txt" 2>&1
tail -40 "$LOG/build-v5.6.txt"
grep -q '^BUILD_V5_6_OK' "$LOG/build-v5.6.txt"

echo "===== PACK TWICE (v5.6 only) ====="
bash "$V5/pack-twice-v5.6.sh" 2>&1 | tee "$LOG/pack-twice.txt"
grep -q '^PACK_TWICE_OK' "$LOG/pack-twice.txt"

echo "===== IMAGE-WIDE ZERO-MUTEX / ZERO-PTHREAD ====="
CPIO=$V5/handshakeos-initramfs-v5.6.cpio.gz
RD=$OUT/tmp/image-scan
rm -rf "$RD"; mkdir -p "$RD"
( cd "$RD" && zcat "$CPIO" | cpio -id --quiet )
# Userspace ELFs must define 0 lock/mutex/rwlock/thread symbols.
# Kernel .ko Linux mutex_* is a kernel-side USB API; report it, do not count as a userspace fail.
# Only glibc/pthread symbols in a .ko increment the fail count.
# Do not pipe this loop into tee: that would run it in a subshell and leave hits=0 (false CLEAN).
USER_BANNED='pthread|mutex|rwlock|spin_?lock|_cond_|barrier|sem_(wait|post|init|timedwait|trywait)|mtx_|cnd_|thrd_|futex|flock|lockf'
KMOD_BANNED='pthread'
hits=0
kmod_pthread=0
SCAN=$LOG/image-scan.txt
: > "$SCAN"
slog() { printf '%s
' "$*" | tee -a "$SCAN"; }
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
        slog "HIT_GLIBC_PTHREAD $f"; printf '%s
' "$u" | tee -a "$SCAN"
        kmod_pthread=$((kmod_pthread+1))
        hits=$((hits+1))
      else
        slog "CLEAN_NO_PTHREAD $f"
      fi
      if [ -n "$km" ]; then slog "KMOD_KERNEL_LOCK_SYMS $f (kernel-side, not a userspace fail):"; printf '%s
' "$km" | tee -a "$SCAN"; fi
      ;;
    *)
      u=$(nm -a "$f" 2>/dev/null | grep -E "$USER_BANNED" || true)
      if [ -n "$u" ]; then
        slog "HIT $f"; printf '%s
' "$u" | tee -a "$SCAN"
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

echo "===== QEMU PID1 ====="
bash "$V5/run-qemu-pid1-v5.6.sh" 2>&1 | tee "$LOG/qemu-pid1-run.txt"
grep -q 'MULTI systems_ok=7' "$LOG/qemu-pid1-run.txt"
grep -q 'USB_SELFTEST dummy_hcd OK' "$LOG/qemu-pid1-run.txt"
grep -q 'QEMU_PID1_DONE' "$LOG/qemu-pid1-run.txt"

echo "===== PRIORS AFTER ====="
sha256sum \
  "$V5/handshakeos-rg35xx-2024-v5.img" \
  "$V5/handshakeos-rg35xx-2024-v5.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.1.img" \
  "$V5/handshakeos-rg35xx-2024-v5.1.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.2.img" \
  "$V5/handshakeos-rg35xx-2024-v5.2.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.3.img" \
  "$V5/handshakeos-rg35xx-2024-v5.3.img.zst" \
  "$V5/handshakeos-rg35xx-2024-v5.5.img" \
  "$V5/handshakeos-rg35xx-2024-v5.5.img.zst" \
  > "$LOG/priors-after.sha256"
if cmp -s "$LOG/priors-before.sha256" "$LOG/priors-after.sha256"; then
  echo PRIORS_UNTOUCHED | tee "$LOG/priors-check.txt"
else
  echo PRIORS_DIFFER | tee "$LOG/priors-check.txt"
  diff -u "$LOG/priors-before.sha256" "$LOG/priors-after.sha256" || true
  exit 1
fi
# explicit v5.5 pin
grep -q '^aee856069633715e1712d943fa8ca74ce7b01483f9f85af5e55f00c681abbfed ' "$LOG/priors-after.sha256"
grep -q '^cc27bc550a69dc414377e497a3fc2d17b50143077bc1808e20a4329948327267 ' "$LOG/priors-after.sha256"

echo "===== v5.6 SHA256 ====="
sha256sum "$V5/handshakeos-rg35xx-2024-v5.6.img" "$V5/handshakeos-rg35xx-2024-v5.6.img.zst" | tee "$LOG/image-v5.6.sha256"
echo V56_ALL_DONE
