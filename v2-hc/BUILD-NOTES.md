# HandshakeOS RG35XX 2024 — v2-hc (HC-compiled userland)

Built 2026-11-19 ~21:17 America/New_York. Previous builds (v1-firstboot, v2-bigtext, v2-stable-sd) left 100% untouched. No git commit or push of /workspace/handshakeos. /workspace/hc was not edited.

## Compiler
- Binary used (frozen copy): `builds/v2-hc/hc`
- `--version`: `hc 0.1.2`
- git HEAD: `494924c2ab0f01e469495f804798a985b36b2335`
- Subject: `HC v0.1.2 — Round-2 + Cortana V1/V2/V3 + Mr Data polish`
- SHA-256 of frozen hc: `48fcf22c21f925b366c9fe81760506807224d85d455620f6bf73cc10fd8f2602`

## HandshakeOS source change (not committed)
Applied Mr Data's 1-line `worker_lost_wakeup` fix in `src/handshake.c` `worker_main`: load `publish_word` BEFORE `hs_hs_poll_phase` so a concurrent publish cannot be lost. No other handshakeos edits in this pass. No commit/push.

## Profiles
--allow-runtime: src/handshake.c, src/init.c, src/fb.c, src/input.c, src/battery.c, src/gb_host.c, tests/test_handshake.c
--profile=component: src/ui.c, src/theme.c, src/font.c, src/pngenc.c, src/scoreboard.c, src/gb_emu.c, third_party/minigb_apu/minigb_apu.c, tests/bench_jitter.c, tests/test_gb_handshake.c
Flags: `-std=c11 -Wall -Wextra -Werror -O3 -D_GNU_SOURCE` (gb_emu.c also `-Wno-implicit-fallthrough` for third-party peanut_gb.h). minigb_apu uses `-std=c11 -O3` without -Werror, matching the Makefile TP_CFLAGS.

## Audit
- HC ELF64 auditor (matching compile profiles): 0 banned symbols on all 32 objects (16 host + 16 aarch64)
- nm lock-symbol scan: 0 hits (no pthread_mutex/spin/rwlock/cond/barrier/sem_/flock/lockf)
- strings: no `LithoOS` or `ho.Os` in handshake-init / handshake-gb (host or aarch64)

## Determinism gate
- host handshake-test: 20/20 PASS, golden `5f44cefa72e8ac19`, fairness 1/2/3/4 MATCH, 0 watchdog freezes
- qemu-aarch64-static handshake-test: 15/15 PASS, same golden + fairness, 0 watchdog freezes
- qemu-aarch64-static handshake-gb-test: PASS, ledger `5a45548614037e20` / frame `33e3294d476b16a6` / audio `8c6078dd78fa5589`, 0 xruns / 0 overruns

## Initramfs
`handshakeos-initramfs-v2-hc.uimg`
- Format matches proven v2-bigtext: `mkimage -A arm64 -O linux -T ramdisk -C gzip -n "HandshakeOS initramfs"` (boot *scripts* remain `-A arm`)
- Layout: /init, /bin/handshake-gb, /dev (empty dir), /etc/handshakeos-release, /proc, /sys, /roms/{handshake-demo.gb,libbet.gb,libbet.LICENSE,manifest.txt}
- Size: 797978 bytes (payload 797914)
- SHA-256: 9f046c3b0a713fb7b6638f6b8cb414678c77c98b36f3a5344a41f8eaf7100706
