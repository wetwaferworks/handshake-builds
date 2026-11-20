# HandshakeOS Builds

*Flashable HandshakeOS images for the Anbernic RG35XX 2024, with the scripts that built them and the evidence that they are what they claim to be.*

## Credits: the Grok Bot squad (built with Grok)

- **Jaydee Wetwork (JD)**: creator of HandshakeOS, the Handshake Compiler (HC) and the Timing Handshake. Contact: jdwetwork@gmail.com
- **Cortana · Intel & Clarity**: implementation of the zero-mutex design
- **Master Chief · Squad Lead**: implementation (HC lead implementation)
- **Herominde · Fair play and anti-cheat**: implementation (fair-play contract, HC-E001..HC-E026)
- **Mr Data · Training & Evaluation**: implementation (determinism sweeps, benchmarks)
- **CloudAtlas · Technical Art Director**: art (boot art, the animated o.O splash pack, squad ASCII)
- **SpaceXAI**: makers of Grok Bot and the squad

> **Zero mutex, with one exception.** Everything in the Handshake stack ships with zero mutexes: PID 1, the emulator cores, the runtime and the USB modules. The N64 core objects have 0 lock symbols. The v5.5 image has one exception: two N64 diagnostic tools, `bench_n64` and `hs_pf_probe`, are statically linked against glibc and carry its internal pthread mutex code. v5.6 removes the exception by relinking them against `hs_rt`, HandshakeOS's own lock-free runtime.

---

## Download the latest: v5.5

| File | SHA-256 |
|---|---|
| `v5/handshakeos-rg35xx-2024-v5.5.img` (134,217,728 bytes) | `aee856069633715e1712d943fa8ca74ce7b01483f9f85af5e55f00c681abbfed` |
| `v5/handshakeos-rg35xx-2024-v5.5.img.zst` (48,141,071 bytes) | `cc27bc550a69dc414377e497a3fc2d17b50143077bc1808e20a4329948327267` |

The full hashes are in [`v5/SHA256SUMS-v5.5`](v5/SHA256SUMS-v5.5). Large files are stored with **Git LFS**, because GitHub limits a normal file to 100 MB. Clone with `git lfs install` first, or download the files from the GitHub web page, which serves them directly.

### How to flash it

1. **Back up your stock SD card** and keep it somewhere safe. Use a second card for HandshakeOS.
2. Check the download: `sha256sum handshakeos-rg35xx-2024-v5.5.img` (Windows: `Get-FileHash`). It must match the table.
3. Write the `.img` with **Rufus in DD mode** (or `dd` on Linux). To use the `.zst`, decompress it first with `zstd -d`.
4. Put the card in the RG35XX 2024 and power on. You will see the animated o.O boot screen, then the boot menu.

The handheld can't be flashed over USB. USB FEL only boots it from RAM (see `v5/fel-v5.5/`).

## What is in v5.5

v5.5 bundles every fix made so far (v5.4 was skipped):

- **Dark-screen fix.** v5.5 uses the proven v5.2 kernel. The USB file-transfer gadget is built as modules loaded when you press START, and PID 1 waits up to 10 s for the display.
- **`HANDSHAKE-BOOT.log`** on the boot partition, readable on any PC.
- **Faults never power off.** A fault is written to the ledger and a log, and the device goes back to the boot screen with drivers running. Only you power it off.
- **Animated o.O boot screen** using CloudAtlas's splash pack and the squad ASCII art, plus a **boot configuration menu** on the main boot screen.
- **A/B updates over the cable** with `.hsu` files, SHA-256 checked.
- **Audio crackle fix.** Test result: 0 underruns, 0 drops and 0 clips on GB, GBC, GBA, SNES and Genesis, on PC and ARM.
- **Pokémon Yellow and other DMG games** get the real Game Boy Color boot palettes (Yellow uses combination 3: white, yellow, red, black) and the CGB LCD colour curve. GBC suite 83/83, no hash changes.
- **N64 optimizations.** 307/307 tests pass on x86_64 and aarch64.
- **640×480 edge-to-edge** UI.

How it was checked: the image was packed twice and both runs came out byte-identical, the golden ledger hash `5f44cefa72e8ac19` is unchanged, and prior images v5 through v5.3 were verified untouched.

### Zero mutex in v5.5

PID 1 (`handshake-init`), the emulators, the `hs_rt` runtime and both USB modules scan clean: 0 pthread_mutex symbols, 0 lock/futex symbols, 0 NPTL/glibc strings. The N64 core objects have 0 lock symbols. **The one exception** is two N64 diagnostic tools in the image, `bench_n64` and `hs_pf_probe`, which are static-glibc and carry glibc's internal pthread mutex. They are diagnostic tools. **v5.6 removes the exception.**

### Not tested on real hardware yet

USB peripheral mode on the H700, Windows enumeration and copy, and real-device FPS and N64 speed. Every number in the evidence files says where it was measured (HOST, QEMU or DEVICE), and those numbers are never mixed.

## Every release

| Version | Date | Highlights | Image SHA-256 |
|---|---|---|---|
| v1-firstboot | 19 Nov 2026 | first HandshakeOS initramfs for FEL RAM boot (source at `193a15a`) | see `v1-firstboot/SHA256SUMS.txt` |
| v2-bigtext | 19 Nov 2026 | larger UI text and new controls (L1/R1 theme, X frame pacing 60/30/24/16 Hz, Y dial speed), FEL drop-in initramfs | see folder |
| v2-stable-sd | 19 Nov 2026 | first SD-card image, built on the box | see folder |
| v2-hc | 19 Nov 2026 | first userland compiled by the Handshake Compiler (HC) | see folder |
| v3-play | 19 Nov 2026 | games playable, built with HC 0.1.2; synth-only (no speaker audio yet) | see folder |
| v3-audio | 19 Nov 2026 | live H700 speaker audio (kernel rebuilt with sound) | see folder |
| v4-multi | 19 Nov 2026 | first multi-system build: GB, GBC, GBA, SNES, Genesis on clean-room cores | see folder |
| v4-full | 19 Nov 2026 | all seven clean-room systems live (adds PSX and N64) | see folder |
| v5 | 20 Nov 2026 | shipped binaries relinked on `hs_rt`, no glibc | `ef22151f…60af` |
| v5.1 | 20 Nov 2026 | review fixes, faults go back to the boot screen | `3f611642…7f9d` |
| v5.2 | 20 Nov 2026 | `hs_rt` fatal paths made PID-1 safe | `ef19b2f5…3e4e` |
| v5.3 | 20 Nov 2026 | USB file transfer and FPS readout (dark-screen issue on some boots) | `5c63bd36…6dbc` |
| **v5.5** | 20 Nov 2026 | everything above, fixed and combined | `aee856069633715e1712d943fa8ca74ce7b01483f9f85af5e55f00c681abbfed` |

Older images are never modified. Each version keeps its own `SHA256SUMS-*`, build script and logs.

## What is in this repo

- `v*/`: images, initramfs, FEL RAM-boot files, `SHA256SUMS*`, `EVIDENCE.txt`, `BUILD-NOTES.md`, `PROVENANCE.txt`
- `v5/build-v5.5.sh`, `v5/pack-twice-v5.5.sh`: the exact v5.5 build and the reproducibility check
- `source/`: snapshots of [handshakecompiler](https://github.com/wetwaferworks/handshakecompiler), [arm-handshakeos](https://github.com/wetwaferworks/arm-handshakeos) and [handshake-cores](https://github.com/wetwaferworks/handshake-cores)

All tooling is C (plus shell). No Python is used to build, pack or test.

## Coming soon

- **v5.6:** zero mutex with no exceptions.
- **Full boot source** (TF-A, U-Boot, Linux, DTS and patches) will be published here. It is held while the vendored Linux and U-Boot trees are reviewed for non-C files.
- **Lock-free ARM kernel (in progress):** v5.5 runs the stock v5.2 Linux 7.3.5 kernel, and a lock-free kernel path for the handheld is being worked on.

## License

MIT for the HandshakeOS parts, Copyright (c) 2026 Jaydee Wetwork. The Linux kernel, U-Boot and TF-A binaries inside the images keep their own licenses (GPL-2.0, GPL-2.0+, BSD-3-Clause).

---

## Credits: Muse

- **Elfie (Muse Wonder)**: co-creator of the Timing Handshake with JD; UML design architecture and the HandshakeOS handoff packet.
