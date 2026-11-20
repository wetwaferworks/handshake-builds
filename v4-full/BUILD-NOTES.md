# HandshakeOS v4-full: build notes (2026-11-19, ET)

All seven clean-room systems are live in one bootable RG35XX 2024 build:
- GB and GBC (hs-gbc)
- GBA (hs-gba)
- SNES (hs-snes)
- GENESIS (hs-genesis)
- PS1 (hs-psx)
- N64 (hs-n64)

Every core is pure C11 under MIT, with zero mutexes. It runs as 3-phase Timing Handshake work: CPU, then video plus upscaler, then audio.

PS1 and N64 support live anti-aliasing with five modes: 1X (the default), FXAA, 2X SSAA, 4X SSAA and 8X SSAA. Every mode is downsampled to a full 640x480 frame with no black bars.

## Compiler
- HC 0.1.4 at git HEAD ad4d8cbd9e43c0591204fc41547397845a9a67fe.
- The frozen copy is `builds/v4-full/hc`, sha256 ee626d46371d2a5ebe3177d76f45c3ce4d88262b49ce5da189e64ade5ca4a9c9. It is byte-identical to /workspace/hc/build/current/hc.
- Every C file is built with hc twice:
  - x86_64 host.
  - aarch64 with `--target=aarch64-linux-gnu -mcpu=cortex-a53 -O3`, linked statically.
- `-ffp-contract=off` is used on all PS1 and N64 core files and on src/emu_cores.c.
- Objects: 134 in total (67 per architecture). The component profile covers 104 of them, including all 30 PS1 and N64 core objects.
- Audit results (logs/elf-audit.txt, logs/nm-locks.txt):
  - `hc --audit-elf`: pass=134, fail=0.
  - nm lock scan: objects=134, lock_dirty=0.
  - nm thread/clock/RNG scan: component_objects=104, dirty=0.
- Word and old-branding hygiene:
  - binary_hits=0, source_hits=0.
  - A full grep of builds/v4-full/ and the HandshakeOS tree found 0 hits for either.

## Sources
- The HandshakeOS tree is /workspace/handshakeos. It was snapshotted into `src-snapshot-pre/` before any edit (Makefile, include, src, tests, cores, roms).
  - /workspace/scratch/handshakeos/src holds only the kernel and ATF sources. Nothing there was touched.
- hs-psx and hs-n64 were copied read-only from hs-cores/{psx,n64}/{include,src} into /workspace/handshakeos/cores/{psx,n64}.
  - Hashes are in logs/cores3d-source.sha256 and logs/cores3d-copied.sha256.
  - Every copied file is byte-identical to its source. No core file was modified.
- The demos come from the verified suites, byte-identical. The build checks each with `cmp`:
  - roms/psx/psx_3d_demo.exe (sha256 6e31fc92…)
  - roms/n64/hs_n64_demo.z64 (sha256 3ca4749d…)
- Changed files:
  - src/{emu_sys.h,emu_sys.c,emu_core.h,emu_cores.c,games_multi.c,play.c,init.c}
  - include/{hs_games.h,hs_play.h}
  - tests/test_multi_emu.c
- No other build directory was modified. Proof is in logs/prior-builds-untouched.txt: sha256 and size+mtime manifests, taken before and after, are identical for v1-firstboot through v4-multi.

## Integration
- Registry:
  - All 7 systems are linked. The picker shows "7 SYSTEMS LIVE" and colour-coded tags: [GB] [GBC] [GBA] [SNES] [GEN] [PS1] [N64].
  - PS1 accepts .exe, .psexe and .psx files anywhere, and .bin files inside ROMS/PSX.
  - N64 accepts .z64, .v64 and .n64 files. The core detects byte order itself.
  - The search covers SD ROMS/<SYSTEM>, the SD ROMS root, and the initramfs /roms/psx and /roms/n64 folders. Extra files dropped into the /roms folders are listed too.
- Built-in demos: there are 8 of them covering all 7 systems. The PS1 demo is /roms/psx/psx_3d_demo.exe and the N64 demo is /roms/n64/hs_n64_demo.z64.
- Memory is allocated per system, lazily:
  - Core state is allocated in emu_core_create and freed in emu_core_destroy: PS1 12,201,216 B and N64 11,940,480 B, including RAM/RDRAM, VRAM, SPU RAM and the SSAA strip.
  - Idle cores hold 0 B.
  - Switching through all 7 systems 3 times gave live_after=0 and heap delta=0 on both architectures (MEM-SWITCH and MEM-HEAP lines).
- Audio: PS1 SPU mixes at 44.1 kHz and the core resamples it to 48 kHz. N64 AI produces 48 kHz. Both enter the same lock-free PCM ring that feeds src/audio.c at 48 kHz, the existing multi-emu contract.
- The 3D cores get a 120 s freeze ceiling instead of 5 s, because one pure-software 8X SSAA frame can take seconds. 2D settings are unchanged.

## Controls (in game)
- MENU, or holding SELECT+START for 1 s, saves and returns to GAMES. Vol+/- sets the speaker volume.
- PS1 and N64:
  - **SELECT+R1** = next AA mode, in the order 1X → FXAA → 2X SSAA → 4X SSAA → 8X SSAA → 1X. A toast shows the new mode: "AA: 1X", "AA: FXAA", "AA: 2X SSAA", "AA: 4X SSAA" or "AA: 8X SSAA".
  - The launch toast reads "AA: 1X  SELECT+R1 = NEXT AA".
  - SELECT+L1 = HUD, showing FPS and the current AA mode.
- PS1 pad: B = Cross, A = Circle, X = Triangle, Y = Square. L1, R1, L2 and R2 work as on the pad. START and SELECT pass through.
- N64 pad: B = A, Y = B, L2 = Z, L1/R1 = L/R, X = C-Up, A = C-Down, R2 = C-Right, START = START. C-Left has no button.
- 2D systems: unchanged from v4-multi.
  - R1 toggles SHARP/NEAREST. L1 cycles the DMG palette on GB, or toggles the HUD on other systems.
  - GBA and SNES need SELECT held for these hotkeys.
  - R2 fast-forwards. Y also fast-forwards on GB and GBC.

## Verification gate (logs/run-verify.txt): GATE_PASS
- handshake-test:
  - Host 20/20 and ARM64 QEMU 15/15.
  - Hash 5f44cefa72e8ac19, fairness 4/4, 0 frozen.
- handshake-gb-test: passes on host and QEMU. Ledger 5a45548614037e20, frame 33e3294d476b16a6, audio 8c6078dd78fa5589, xruns=0.
- test_games and test_audio: pass on host and QEMU.
- test_multi_emu (host and qemu-aarch64-static):
  - All 7 systems, 8 demos.
  - 35 MULTI/REGISTRY/SAV/MEM lines, byte-identical across architectures.
  - 18/18 PNGs byte-identical across architectures.
  - PS1 and N64 were checked in every AA mode (MULTI-AA lines): edge_black=0, black_rows=0, black_cols=0, all 5 modes distinct.
  - 2X with 1 worker matches 3 workers.
  - Live cycling matches a run that never switched modes: FXAA,2X,4X,8X→1X and 1X,FXAA,2X,4X→8X.
- handshake-init --multi-selftest:
  - Host and QEMU output are identical: `MULTI systems_ok=7 demos_ok=8`.
  - MULTI-AA lines are OK for PS1 and N64.
- Regression and equivalence (logs/equivalence.txt):
  - The 12 GB/GBC/GBA/SNES/GEN test lines and the 6 selftest lines are byte-identical to v4-multi.
  - The 20 PS1/N64 AA previews (5 modes × 2 cores × 2 architectures) are pixel-identical to the verified hs-psx (frame index 60) and hs-n64 (frame 90) suite PNGs.
- Picker: games-picker-v4-full.png is byte-identical on host and QEMU, and was inspected visually.
- All 38 preview PNGs are 640x480 with 0 black edge pixels.
- Real Linux 7.3.5 PID 1 boot (log: logs/qemu-pid1-v4-full.log):
  - Kernel: the v3-audio kernel, decompressed from Image-v3-audio.gz. Setup: qemu-system-aarch64 virt, 4× Cortex-A53, 1 GiB, `handshakeos.once`.
  - The log shows "Run /init", ledger 5f44cefa72e8ac19 and GB ledger 5a45548614037e20.
  - `MULTI systems_ok=7 demos_ok=8`. The live AA cycle is OK on PS1 and N64.
  - Every MULTI line matches the host selftest byte for byte, after CR stripping.
  - No panic, no OOM, no freeze. Clean "reboot: Restarting system" at 32.7 s.

## Package
- `handshakeos-initramfs-v4-full.cpio.gz`: 1,349,895 B.
- `handshakeos-initramfs-v4-full.uimg`: 1,349,959 B (1.29 MiB). Built by mkimage as an AArch64 ramdisk; the payload is the cpio.gz.
  - Contents: /init, /bin/handshake-gb, /roms (libbet, 6 flat 2D demos, psx/, n64/, manifests), /etc/handshakeos-release.
- FEL kit (fel/):
  - Proven LPDDR3 U-Boot 17ce4fb5…, NV3052C DTB 814740b1…, and Image-v3-audio.gz c206e1bc…. All are unchanged copies.
  - The uimg, boot-v4-full-fel.scr, and 07-ramboot-v4-full.cmd.
- 128 MiB SD image:
  - MBR label-id 0x48534f53. FAT32 p1 at 4 MiB. U-Boot at 8 KiB.
  - The first 4 MiB are byte-identical to the v3-audio image.
  - fsck.fat -n is clean. The .zst round-trips exactly.
- SHA256SUMS covers every deliverable and passes `sha256sum -c`.

## Known limitations
- PS1 runs PS-X EXE programs only. There is no CD-ROM (.cue/.chd/disc .bin), MDEC or memory card yet. Commercial games would also need an external BIOS.
- N64 runs with 4 MiB RDRAM (no Expansion Pak). There are no saves, and C-Left has no button.
- 4X and 8X SSAA are pure software. On the H700 they are slideshow speed, so 1X stays the default.
- The Panfrost Mali-G31 backend hooks exist in both cores, but no GPU backend is implemented yet.
- 2D-system deviations are the same as in v4-multi.

No git commit or push was made. Nothing was flashed.
