# HandshakeOS RG35XX 2024 v5 - build notes

v5 is HandshakeOS rebuilt in the consortium tree (C:\Users\jdwetwork\.aaa.www.inc\handshake\)
with the shipped binaries relinked against hs_rt, the in-house runtime, and repacked with the
corrected FLASH-INSTRUCTIONS.txt (f471a0ca...) in the FAT root. Inputs pinned by hash before
packing; two pack runs byte-identical; fsck.fat clean; SHA256SUMS passes sha256sum -c;
run-verify GATE_PASS. Evidence: EVIDENCE.txt.

## Flashing
The only flash path is writing the .img to a microSD card in a card reader.
The handheld cannot be flashed over USB; USB FEL only boots it from RAM
(fel/07-ramboot-v5.cmd). There is no SD flasher.

## Zero mutexes
Every core is pure C11 under MIT, with zero mutexes.
HandshakeOS v5 ships zero mutexes: the shipped binaries, handshake-init (/init) and
handshake-gb (/bin/handshake-gb), contain no mutex, lock, futex or NPTL code, dead or alive.

- Shipped binaries are linked -static -nostdlib -nostartfiles against HandshakeOS objects
  and hs_rt only (handshakeos/src/hs_rt: start.S, sys.c, mem.c, stdio.c, hs_rt.h; MIT).
  No glibc, no libpthread, no libm, no crt files. -lgcc is on the link line and resolves
  nothing: no libgcc member is linked.
- hs_rt: raw aarch64 syscalls; threads by raw clone() with the kernel-cleared tid word
  (pthread_join sleep-polls it, no futex); allocator state is entirely per thread (no shared
  state, nothing to guard); stdio streams carry no lock (one thread per stream), the open-stream
  list is lock-free (CAS push, CAS reclaim); LL/SC outline atomics only.
- Rescan of the shipped binaries (tools/zero-mutex-scan.sh): pthread_mutex symbols 0,
  lock/futex symbols 0, NPTL / pthread_mutex / lll_lock / __libc_lock strings 0,
  glibc strings 0, no PT_TLS, no PT_INTERP.
- Object scan: NM_SUMMARY objects=134 lock_dirty=0; NM_HS_RT objects=4 lock_dirty=0.
- The x86_64 host builds and the aarch64 test harness binaries (handshake-test,
  handshake-bench, handshake-gb-test, handshake-games-test, handshake-audio-test,
  handshake-multi-test) are not shipped; they keep the v4-full link recipe (glibc).

## Build
- build-v5.sh: HC v0.1.4 @ ad4d8cb (hc/build/current/hc, sha256 ee626d46...) compiles all
  objects; aarch64-linux-gnu-gcc 13.3.0 (Ubuntu 24.04 cross toolchain) is HC's backend and
  compiles hs_rt freestanding. ELF_AUDIT pass=134 fail=0.
- pack-v5.sh: new pins init 3cb5a8b1, gb 11505247. The initramfs is written by
  tools/hs-mkcpio (handshakeos/tools/hs_mkcpio.c, pure C11, built by build-v5.sh with HC; fixed
  modes, owners, inodes, times: the tree is on NTFS, where every file
  reads back 0777). The FAT label is set with mlabel (mtools honours SOURCE_DATE_EPOCH;
  dosfstools 4.2 stamps the label with the wall clock).

## Pure C tooling (2026-11-20)
Every generator, packer and checker in the tree is C11 (MIT), compiled with HC --allow-runtime.
No Python is invoked by any build, pack or gate step; the .py sources were deleted.
- handshakeos/tools/make_handshake_demo_gb.c -> roms/handshake-demo.gb (byte-identical, 4a56fc02)
- hs-cores/psx/tools/gen_demo.c -> tests/psx_3d_demo.exe (byte-identical, 6e31fc92)
- hs-cores/psx/tools/gen_spu_gauss.c -> src/psx_spu_gauss.h (tables identical; comment names .c)
- hs-cores/n64/tools/mk_n64_demo.c -> roms/hs_n64_demo.z64 (byte-identical, 3ca4749d)
- hs-cores/genesis/tools/gen_ym_tables.c -> src/gen_ym_tables.h (tables identical; comment names .c)
- handshakeos/tools/hs_mkcpio.c -> builds/v5/tools/hs-mkcpio (repacks the previous v5 cpio.gz
  4914fdbd byte-identically)
- handshakeos/tools/hs_pngcmp.c + hs_pngread.h (own inflate) -> builds/v5/tools/hs-pngcmp
  (run-verify.sh CORE-SUITE)
- hs-cores/tests/acid2_compare.c (run-tests.sh), handshakeos/board/qemu-virt/pack-initramfs.c
  (run-pid1.sh, mtime from SOURCE_DATE_EPOCH), hs-cores/gba/tools/bench_report.c +
  build/bench.sh; assemble-img.sh checks eGON.BT0 with dd; run-verify.sh times with date +%s%N.

## Timing (each number labeled by where it was measured)
- HOST (x86_64, TheWorkstation WSL Ubuntu-24.04, logs/run-verify.txt): handshake-test 20/20
  PASS, ledger 5f44cefa72e8ac19, fairness 4/4, frozen_fail=0, about 1.06 s per run.
- QEMU user mode (qemu-aarch64-static 8.2.2, logs/run-verify.txt): handshake-test 15/15 PASS,
  ledger 5f44cefa72e8ac19, fairness 4/4, frozen_fail=0, about 1.31 s per run.
- QEMU system, v5 (logs/qemu-pid1-v5.log: Linux 7.3.5 Image-v3-audio, virt, 4x Cortex-A53,
  shipped hs_rt /init as PID 1): ledger 5f44cefa72e8ac19, epoch 128, workers 3,
  p50 542944 ns, overruns 0; GB ledger 5a45548614037e20, GB xruns 0, GB overruns 0.
  Emulator timing.
- DEVICE (RG35XX 2024, first real-silicon FEL boot, 2026-11-19, v3-era build, read from
  JD's video stills): ledger 5f44cefa72e8ac19, p50 25 us, max 57 us, overruns 0;
  GB demo xruns 0, overruns 0. These are from the v3-era device run, not v5:
  v5 has not been run on the device yet.
HOST, QEMU and DEVICE numbers are never merged.

## Provenance
See PROVENANCE.txt.
