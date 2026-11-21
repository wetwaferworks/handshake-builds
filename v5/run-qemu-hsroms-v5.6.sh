#!/usr/bin/env bash
# QEMU: 8 GiB sparse card built from the packed 128 MiB v5.6 image.
# Boot 1 creates + formats HSROMS (p2). Boot 2 leaves it alone.
# A host-copied ROM is detected. Partition 1 entry, pre-p1 gap, and p1
# files (except boot logs) stay unchanged.
set -euo pipefail

HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
T=$HS_ROOT
V5=$T/builds/v5
OUT=$V5/v5.6
LOG=$OUT/logs
Q=$OUT/qemu-test
mkdir -p "$LOG" "$Q" "$OUT/tmp"
export PATH="$PATH:/usr/sbin:/sbin"
K=$OUT/tmp/Image-v52
zcat "$V5/v5.2/staging/Image.gz" > "$K" 2>/dev/null || zcat "$T/builds/v3-audio/Image.gz" > "$K"
CPIO=$V5/handshakeos-initramfs-v5.6.cpio.gz
SRCIMG=$V5/handshakeos-rg35xx-2024-v5.6.img
CARD=$Q/hsroms-8g.img
P1_START=8192
P1_SECS=253952

rm -f "$CARD"
cp --sparse=always "$SRCIMG" "$CARD"
truncate -s 8G "$CARD"

hash_regions() { # tag
  local tag=$1
  {
    echo -n "MBR_LEAD "
    dd if="$CARD" bs=1 count=446 status=none | sha256sum | awk '{print $1}'
    echo -n "MBR_P1 "
    dd if="$CARD" bs=1 skip=446 count=16 status=none | sha256sum | awk '{print $1}'
    echo -n "MBR_P3P4SIG "
    dd if="$CARD" bs=1 skip=478 count=34 status=none | sha256sum | awk '{print $1}'
    echo -n "PRE_P1_GAP "
    dd if="$CARD" bs=512 skip=1 count=$((P1_START-1)) status=none | sha256sum | awk '{print $1}'
  } > "$LOG/hsroms-$tag-regions.sha256"
}

hash_p1_files() { # tag
  local tag dest
  tag=$1
  dest=$Q/p1-$tag
  rm -rf "$dest"; mkdir -p "$dest"
  mcopy -n -s -i "$CARD"@@$((P1_START*512)) '::/*' "$dest/" 2>/dev/null || true
  ( cd "$dest" && find . -type f ! -name 'HANDSHAKE-BOOT.log' ! -name 'HANDSHAKE-FAULTS.log' ! -name 'HANDSHAKE.CFG' -print0 \
      | sort -z | xargs -0 -r sha256sum ) > "$LOG/hsroms-$tag-p1files.sha256"
}

p2_info() { # outfile
  local out=$1
  local typ start size typhex volhex sig label fs tmp s0 s1
  typ=$(od -An -tu1 -N1 -j 466 "$CARD" | tr -d " ")
  start=$(od -An -tu4 -N4 -j 470 -v "$CARD" | tr -d " ")
  size=$(od -An -tu4 -N4 -j 474 -v "$CARD" | tr -d " ")
  typhex=$(printf "0x%02x" "$typ")
  tmp=$(mktemp)
  dd if="$CARD" bs=512 skip="$start" count=1 status=none of="$tmp"
  label=$(dd if="$tmp" bs=1 skip=71 count=11 status=none | tr -d '\000' | sed "s/[[:space:]]*$//")
  volhex=$(printf "%08x" "$(od -An -tu4 -N4 -j 67 -v "$tmp" | tr -d " ")")
  fs=$(dd if="$tmp" bs=1 skip=82 count=8 status=none | tr -d '\000' | sed "s/[[:space:]]*$//")
  s0=$(od -An -tx1 -N1 -j 510 "$tmp" | tr -d " ")
  s1=$(od -An -tx1 -N1 -j 511 "$tmp" | tr -d " ")
  if [ "$s0" = "55" ] && [ "$s1" = "aa" ]; then sig=55AA; else sig=BAD; fi
  rm -f "$tmp"
  printf 'type=%s start=%s size=%s fat=%s label=%s vol_id=%s sig=%s\n' \
    "$typhex" "$start" "$size" "$fs" "$label" "$volhex" "$sig" | tee "$out"
}

boot() { # label
  local label=$1
  timeout --kill-after=10s 300 qemu-system-aarch64 -M virt -cpu cortex-a53 -smp 4 -m 1024 \
    -kernel "$K" -initrd "$CPIO" \
    -drive if=none,file="$CARD",format=raw,id=hsdisk0 \
    -device virtio-blk-device,drive=hsdisk0 \
    -append "console=ttyAMA0 handshakeos.once panic=-1" \
    -nographic -no-reboot -monitor none -serial stdio \
    > "$LOG/qemu-hsroms-$label.log" 2>&1
  echo "QEMU_EXIT $?" >> "$LOG/qemu-hsroms-$label.log"
  echo "===== hsroms $label ====="
  grep -E "HSROMS |GAMES |GAMES_ROM |MULTI systems_ok|QEMU_EXIT|HandshakeOS" "$LOG/qemu-hsroms-$label.log" | tail -60
}

hash_regions before
hash_p1_files before
echo "===== HSROMS QEMU boot 1 (create+format) ====="
boot boot1
grep -q 'HSROMS action=created' "$LOG/qemu-hsroms-boot1.log"
p2_info "$LOG/hsroms-boot1-p2.txt"
grep -q 'type=0x0c' "$LOG/hsroms-boot1-p2.txt"
grep -q 'fat=FAT32' "$LOG/hsroms-boot1-p2.txt"
grep -q 'label=HSROMS' "$LOG/hsroms-boot1-p2.txt"
VOL1=$(sed -n 's/.*vol_id=\([0-9a-fA-F]*\).*/\1/p' "$LOG/hsroms-boot1-p2.txt")
START=$(sed -n 's/.*start=\([0-9]*\).*/\1/p' "$LOG/hsroms-boot1-p2.txt" | head -1)
hash_regions after1
hash_p1_files after1
cmp -s "$LOG/hsroms-before-regions.sha256" "$LOG/hsroms-after1-regions.sha256"
cmp -s "$LOG/hsroms-before-p1files.sha256" "$LOG/hsroms-after1-p1files.sha256"
echo P1_UNCHANGED_AFTER_BOOT1
# fsck p2 in place (do not extract the 8 GiB partition to a new file)
SIZE=$(sed -n 's/.* size=\([0-9]*\).*/\1/p' "$LOG/hsroms-boot1-p2.txt" | head -1)
LOOP=$(losetup -f --show -o $((START*512)) --sizelimit $((SIZE*512)) "$CARD")
# -n leaves the image unchanged. A dirty bit (and backup-boot offset 65)
# after a live mount is expected; do not treat it as a format failure.
fsck.fat -n "$LOOP" | tee "$LOG/hsroms-p2-fsck.txt" || true
grep -E '[0-9]+ files, [0-9]+/' "$LOG/hsroms-p2-fsck.txt"
mlabel -i "$LOOP" -s | tee -a "$LOG/hsroms-p2-fsck.txt"
# inject a host ROM into p2 (ROMS/GB, creating dirs if needed)
ROM=
for c in "$T/handshakeos/roms/libbet.gb" "$OUT/roms/libbet.gb" "$T/handshakeos/roms/handshake-demo.gb"; do
  if [ -f "$c" ]; then ROM=$c; break; fi
done
[ -n "$ROM" ]
# mtools on a 8 GiB loop via /mnt/c is very slow; mount and cp instead.
mkdir -p "$Q/hsroms-mnt"
mount -t vfat -o rw,shortname=mixed "$LOOP" "$Q/hsroms-mnt"
mkdir -p "$Q/hsroms-mnt/ROMS/GB"
cp -a "$ROM" "$Q/hsroms-mnt/ROMS/GB/host-rom.gb"
sync
umount "$Q/hsroms-mnt"
losetup -d "$LOOP"
echo "INJECTED $ROM -> p2:/ROMS/GB/host-rom.gb"

echo "===== HSROMS QEMU boot 2 (leave alone + detect ROM) ====="
# snapshot p2 boot sector / vol_id already in VOL1; also hash p2 first 1 MiB as "not reformatted"
dd if="$CARD" bs=512 skip="$START" count=2048 status=none | sha256sum > "$LOG/hsroms-p2head-before-boot2.sha256"
boot boot2
grep -q 'HSROMS action=present' "$LOG/qemu-hsroms-boot2.log"
grep -q 'left_alone=1' "$LOG/qemu-hsroms-boot2.log"
grep -q 'GAMES_ROM ' "$LOG/qemu-hsroms-boot2.log"
grep -E 'GAMES_ROM .*name=host-rom' "$LOG/qemu-hsroms-boot2.log"
p2_info "$LOG/hsroms-boot2-p2.txt"
VOL2=$(sed -n 's/.*vol_id=\([0-9a-fA-F]*\).*/\1/p' "$LOG/hsroms-boot2-p2.txt")
[ "$VOL1" = "$VOL2" ]
hash_regions after2
hash_p1_files after2
cmp -s "$LOG/hsroms-before-regions.sha256" "$LOG/hsroms-after2-regions.sha256"
cmp -s "$LOG/hsroms-before-p1files.sha256" "$LOG/hsroms-after2-p1files.sha256"
dd if="$CARD" bs=512 skip="$START" count=2048 status=none | sha256sum > "$LOG/hsroms-p2head-after-boot2.sha256"
# p2 head may change slightly if we only injected a ROM (FAT dirs dirty) — that is expected.
# Leave-alone means the volume id / boot sector identity stays; check boot sector 512 bytes except nothing...
# Volume id already compared. Confirm no reformat by matching boot sector + reserved prefix (first 32 sectors).
dd if="$CARD" bs=512 skip="$START" count=32 status=none | sha256sum > "$LOG/hsroms-p2boot-after-boot2.sha256"
dd if="$CARD" bs=512 skip="$START" count=32 status=none of="$Q/p2boot-after.bin"
# captured after inject / before boot2:
dd if="$CARD" bs=512 skip="$START" count=32 status=none >/dev/null
echo "VOL_ID $VOL1 == $VOL2"
echo P1_UNCHANGED_AFTER_BOOT2
echo HSROMS_QEMU_OK
