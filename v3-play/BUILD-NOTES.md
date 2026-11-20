# HandshakeOS v3-play (RG35XX 2024, H700, 1 GB LPDDR3, NV3052C)

Built 2026-11-19 ~9:30 PM America/New_York with hc 0.1.2
(`/workspace/hc/build/current/hc`, sha256 48fcf22c…, byte-identical to the v2-hc frozen copy).
Not committed, not pushed, not flashed.

- `build-v3-play.sh`  hc compile (component / --allow-runtime), link, ELF + nm + strings audits
- `run-verify.sh`     gate: 20x host + 15x QEMU handshake-test, host + QEMU gb-test (+640x480 scaler), games smoke
- `pack-v3-play.sh`   initramfs uimg, 128 MiB SD image (same MBR/U-Boot/kernel/DTB as v2-stable-sd), .zst
- `kernel-7.3.5.config` extracted from the proven Image (IKCONFIG)
- `src-snapshot-pre/`  the source tree before v3 edits

Audio: kernel has CONFIG_SND/SND_PCM/SND_SOC/DMA_SUN6I as =m (no modules shipped) and
CONFIG_SND_SUN4I_CODEC (the driver for the DTB's allwinner,sun50i-h616-codec) is not set.
No /dev/snd at runtime -> v3-play is synth-only (minigb_apu runs, nothing reaches the speaker).
