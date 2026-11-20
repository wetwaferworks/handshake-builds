# HandshakeOS v4-multi — build notes (2026-11-19, ET)

The first multi-system HandshakeOS. It plays five systems on clean-room cores:
GB and GBC (hs-gbc), GBA (hs-gba), SNES (hs-snes), and GENESIS (hs-genesis).
It keeps live H700 speaker audio from v3-audio. The registry has 7 slots. PSX (.bin .cue .exe .chd)
and N64 (.z64 .n64 .v64) are recognised, and choosing one shows "Core coming in v4-full".

## Compiler
- HC 0.1.4. Git HEAD: ad4d8cbd9e43c0591204fc41547397845a9a67fe.
- Frozen copy: `builds/v4-multi/hc`, sha256 ee626d46371d2a5ebe3177d76f45c3ce4d88262b49ce5da189e64ade5ca4a9c9.
- Every C file is built with hc for both host x86_64 and aarch64 (`--target=aarch64-linux-gnu -mcpu=cortex-a53`, -O3). aarch64 links are static.
- Profile map: see `logs/profiles.txt`.
  - Component profile: all 26 core files, emu_sys, emu_cores, ui, theme, font, pngenc, gb_emu, gb_scale, minigb_apu, bench, and the gb test.
  - --allow-runtime: init, play, games, games_multi, emu_save, audio, handshake, fb, input, battery, gb_host, and the runtime tests.
- Audits:
  - `hc --audit-elf`: pass=106, fail=0.
  - nm: objects=106, lock_dirty=0. component_objects=76, thread/clock/rng dirty=0.
  - Word/branding hygiene: binary_hits=0, source_hits=0. The initramfs and the SD image also have 0 hits.

## Sources
- The cores are copied read-only from hs-cores/{include,common,gbc,gba,snes,genesis} into /workspace/handshakeos/cores.
- Hashes: logs/cores-source.sha256 and logs/cores-copied.sha256.
- hs-cores/psx and hs-cores/n64 were never read.
- The HandshakeOS tree before any edits is kept in `src-snapshot-pre/`.
- Changed files: src/{audio,init,play,ui}.c and include/{hs_audio,hs_games,hs_play,hs_ui}.h.
- New files: src/{emu_sys.h,emu_sys.c,emu_core.h,emu_cores.c,emu_save.c,games_multi.c}, include/hs_emusave.h, and tests/test_multi_emu.c.
- No other build directory was modified. v1-firstboot through v3-audio have no file changed after v4-multi/ was created at 22:15 ET.

## Verification gate (logs/run-verify.txt): GATE_PASS
- handshake-test: host 20/20 and ARM64 QEMU 15/15. Hash 5f44cefa72e8ac19, fairness 4/4, 0 frozen.
- handshake-gb-test: host and QEMU pass (ledger 5a45548614037e20, frame 33e3294d476b16a6, audio 8c6078dd78fa5589).
- test_games and test_audio: pass on host and QEMU.
- test_multi_emu:
  - Host and QEMU pass. 15 MULTI/REGISTRY/SAV lines are identical across architectures, and 6/6 PNGs are byte-identical.
  - Each system runs 90 frames and is checked for determinism across 3/3/1 workers.
  - edge_black=0, black_rows=0, black_cols=0 for both SHARP and NEAREST.
  - SNES .sav atomic round trip: 8192 B.
  - MULTI-CHAIN 3fe8fc4e0e025efb.
- `handshake-init --multi-selftest`: host and QEMU output are identical. systems_ok=6.
- Real Linux 7.3.5 PID 1 boot (v3-audio kernel; qemu-system-aarch64 virt, cortex-a53, 4 CPUs). Log: logs/qemu-pid1-v4-multi.log.
  - "Run /init", v4-multi banner, ledger 5f44cefa72e8ac19, GB ledger 5a45548614037e20.
  - MULTI systems_ok=6. The MULTI lines match the host selftest byte for byte.
  - No panic. Clean "reboot: Restarting system".

## Package
- Initramfs uimg: the gzip cpio wrapped by mkimage. Contents: /init, /bin/handshake-gb, /roms (libbet, 6 built-in demos, manifests), and /etc/handshakeos-release.
- FEL kit (fel/): proven LPDDR3 U-Boot 17ce4fb5…, NV3052C DTB 814740b1…, Image-v3-audio.gz c206e1bc…, the uimg, boot-v4-multi-fel.scr, and 06-ramboot-v4-multi.cmd.
- 128 MiB SD image:
  - MBR label-id 0x48534f53. FAT32 p1 at 4 MiB. U-Boot at 8 KiB.
  - The first 4 MiB are byte-identical to the v3-audio image (proven U-Boot placement).
  - fsck.fat -n is clean. ROMS/{GB,GBC,GBA,SNES,GENESIS,PSX,N64} plus README.
  - The .zst round-trips exactly.
- Staged to TheWorkstation, in both C:\Users\jdwetwork\GrokBot-Transfer\ and the D: FEL staging folder, as new files only. Get-FileHash matches the box.

## Known deviations
- GBA and SNES use L1/R1 as shoulder buttons, so the hotkeys there are SELECT+L1 and SELECT+R1.
- Y fast-forwards on GB and GBC only. R2 fast-forwards on every system.
- The GBA built-in demo's DirectSound sample is 32 KB, so it goes silent after about 2 s. This is how the test ROM is designed.
- The SNES Mode 7 Floor demo has no audio by design. Its test runs with need_audio=0.
- The Genesis built-in demo is the hs-genesis VDP + FM test pattern.
- SNES special chips are not supported.

No git commit or push was made. Nothing was flashed.
