#!/usr/bin/env bash
# Rescan of the two shipped binaries: mutex symbols, NPTL/lock strings, glibc strings, link inputs.
V=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds/v5/v5.3
for b in "$V/aarch64/handshake-init" "$V/aarch64/handshake-gb"; do
  n=$(basename "$b")
  sym=$(aarch64-linux-gnu-nm "$b" | grep -c 'pthread_mutex')
  lck=$(aarch64-linux-gnu-nm "$b" | awk '{print $NF}' | grep -Eic 'pthread_mutex|pthread_cond|pthread_rwlock|pthread_spin|futex|lll_lock|__libc_lock|_IO_lock|mtx_|sem_')
  str=$(strings -a "$b" | rg -i 'nptl|pthread_mutex|lll_lock|__libc_lock' | wc -l)
  glc=$(strings -a "$b" | rg -i 'glibc|GNU C Library' | wc -l)
  tls=$(aarch64-linux-gnu-readelf -lW "$b" | grep -c ' TLS ')
  interp=$(aarch64-linux-gnu-readelf -lW "$b" | grep -c 'INTERP')
  echo "SCAN $n nm_pthread_mutex=$sym nm_lock_syms=$lck strings_nptl_mutex=$str strings_glibc=$glc PT_TLS=$tls PT_INTERP=$interp"
done
