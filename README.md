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

> **Zero mutex in userspace.** The idea and the shape are JD's; the assistants co-implemented. From v5.6 on, the shipped userspace and PID 1 runtime are zero-mutex: PID 1 (`handshake-init`), the emulator cores (the N64 core objects included), the `hs_rt` runtime, the USB modules, and the N64 diagnostic tools `bench_n64` and `hs_pf_probe` (relinked against `hs_rt`) all scan clean, with 0 lock symbols. Evidence is in handshake-builds: `v5/v5.6/image-scan.txt`, `v5/v5.7/image-scan.txt`, and `v5/v5.8/image-scan.txt` each end in `IMAGE_ZERO_MUTEX_CLEAN` (`bench_n64` and `hs_pf_probe` CLEAN). The v5.9.1 run is `v5/v5.9.1/image-scan.txt`, `v5/v5.9.1/nm-locks.txt`, and `v5/v5.9.1/scan-run.txt` (`IMAGE_ZERO_MUTEX_CLEAN`, `NM_SUMMARY objects=186 lock_dirty=0`).
>
> The kernel underneath is mainline Linux 7.3.5 + ROCKNIX, which still has its own kernel mutexes (static count 1,599 mutex call sites; kernel work is next week). v5.8 and later ship kernel `7.3.5-hs-v58` on that base.


---

## Download the latest: v5.9.1

| File | SHA-256 |
|---|---|
| `v5/handshakeos-rg35xx-2024-v5.9.1.img.zst` (49,887,412 bytes) | `800f635b94299679030d895c009c381d3ce01c498185c5ae801df7632a7d480a` |

The published image is the `.zst` (the raw `.img` is over GitHub LFS's 2 GiB limit). Decompress with `zstd -d handshakeos-rg35xx-2024-v5.9.1.img.zst`. The raw `.img` is 2,218,426,368 bytes, SHA-256 `fc5b977c252560353387ed2eea703a041753755798375dfe744b6ab0e56ee669`. The full hashes are in [`v5/SHA256SUMS-v5.9.1`](v5/SHA256SUMS-v5.9.1). Large files are stored with **Git LFS**, because GitHub limits a normal file to 100 MB. Clone with `git lfs install` first, or download the files from the GitHub web page, which serves them directly.

### How to flash it

1. **Back up your stock SD card** and keep it somewhere safe. Use a second card for HandshakeOS.
2. Check the download: `sha256sum handshakeos-rg35xx-2024-v5.9.1.img.zst` (Windows: `Get-FileHash`). It must match the table.
3. Decompress with `zstd -d handshakeos-rg35xx-2024-v5.9.1.img.zst`, then write the `.img` with **Rufus in DD mode** (or `dd` on Linux).
4. Put the card in the RG35XX 2024 and power on. You will see the animated o.O boot screen, then the boot menu.

The handheld can't be flashed over USB. USB FEL only boots it from RAM (see `v5/fel-v5.9.1/`).

## What is in v5.6 through v5.9.1

The idea and the shape are JD's; the assistants co-implemented. CloudAtlas drew the o.O splash pack. HC v0.1.4 is public in [handshakecompiler](https://github.com/wetwaferworks/handshakecompiler).

- **v5.6** relinked the two N64 diagnostic tools against `hs_rt`. Image scans report both tools CLEAN and `IMAGE_ZERO_MUTEX_CLEAN` (see `v5/v5.6/image-scan.txt`; `v5/v5.6/nm-locks.txt` has `NM_SUMMARY objects=174 lock_dirty=0`). Image SHA-256 `d7d4afc13bf063d3543c05b0a068c9819e5e8b937d70d5aa43fdc0f6534c483a`.
- **v5.7** added SNES audio work and USB test improvements (see `v5/v5.7/image-scan.txt`; `v5/v5.7/nm-locks.txt` has `NM_SUMMARY objects=176 lock_dirty=0`). Image SHA-256 `7faf79af730ffe4249dc35c232ee6932ab52542b70d01ebade96e22790af9de8`.
- **v5.8** ships kernel `7.3.5-hs-v58`. On-device SNES ALttP: game_fps 40.6, draw_fps 10.2, emu_ms_avg 95, xruns 209 in 23.5 s. Image SHA-256 `32d70cd609da8e161452fc608c49b6763da4c250cadbd86a806caaaa43ea9f0e`.
- **v5.9** added render-skip catch-up and audio pad-hold. JD reported it a lot better and mostly playable; pacing still off. Image SHA-256 `74539c7782c7cd914b356f1a373ad6f6c19977121ba9c294a548c804d4e5bc62`.
- **v5.9.1** (device pull 21 Nov 2026, 10:46 AM ET): USB MODE is device-verified (mass storage plus ACM on one cable). First boot grew partition 1. Idle samples: overruns 0, xruns 0. Same kernel as v5.8. Userspace scan evidence: `v5/v5.9.1/image-scan.txt` (`IMAGE_ZERO_MUTEX_CLEAN`; `bench_n64` and `hs_pf_probe` CLEAN) and `v5/v5.9.1/nm-locks.txt` (`NM_SUMMARY objects=186 lock_dirty=0`). SNES pacing on the H700 remains the open problem.

The first real-silicon FEL boot was 19 Nov 2026, from a v3-era image, with ledger `5f44cefa72e8ac19`, p50 25 us, max 57 us, overruns 0. v5.8, v5.9 and v5.9.1 have also run on that RG35XX 2024. Host, QEMU and device numbers are never mixed.

### Zero mutex from v5.6 on

The shipped userspace and PID 1 runtime are zero-mutex. The kernel underneath is mainline Linux 7.3.5 + ROCKNIX, which still has its own kernel mutexes (static count 1,599 mutex call sites; kernel work is next week).

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

### What is still open on the device

USB MODE was verified on v5.9.1 (mass storage plus ACM). SNES pacing on the H700 is still the main issue. Every number in the evidence files says where it was measured (HOST, QEMU or DEVICE), and those numbers are never mixed.

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
| v5.6 | 20 Nov 2026 | N64 diagnostic tools relinked against `hs_rt`; userspace zero-mutex | `d7d4afc13bf063d3543c05b0a068c9819e5e8b937d70d5aa43fdc0f6534c483a` |
| v5.7 | 20 Nov 2026 | SNES audio work and USB test improvements | `7faf79af730ffe4249dc35c232ee6932ab52542b70d01ebade96e22790af9de8` |
| v5.8 | 21 Nov 2026 | kernel `7.3.5-hs-v58`; on-device SNES numbers | `32d70cd609da8e161452fc608c49b6763da4c250cadbd86a806caaaa43ea9f0e` |
| v5.9 | 21 Nov 2026 | render-skip catch-up and audio pad-hold | `74539c7782c7cd914b356f1a373ad6f6c19977121ba9c294a548c804d4e5bc62` |
| **v5.9.1** | 21 Nov 2026 | USB MODE device-verified (mass storage plus ACM) | `fc5b977c252560353387ed2eea703a041753755798375dfe744b6ab0e56ee669` |

Older images are never modified. Each version keeps its own `SHA256SUMS-*` (including `v5/SHA256SUMS-v5.6` and `v5/SHA256SUMS-v5.7`) and build script. Scan evidence for v5.6 through v5.9.1 lives next to each version (`image-scan.txt`, `nm-locks.txt`, and `scan-run.txt`).

## What is in this repo

- `v*/`: images, initramfs, FEL RAM-boot files and `SHA256SUMS*`. From v2 through v5.5 each folder also has `BUILD-NOTES.md`; v5.6 through v5.9.1 use the SHA256SUMS files and scan logs instead. `EVIDENCE.txt` and `PROVENANCE.txt` are in `v5/`.
- `v5/build-v5.5.sh`, `v5/pack-twice-v5.5.sh`: the exact v5.5 build and the reproducibility check
- `v5/build-v5.6.sh` through `v5/v5.9.1/scripts/`: later build and pack scripts; FEL kits in `v5/fel-v5.6/` through `v5/fel-v5.9.1/`
- `source/`: snapshots of [handshakecompiler](https://github.com/wetwaferworks/handshakecompiler), [arm-handshakeos](https://github.com/wetwaferworks/arm-handshakeos) and [handshake-cores](https://github.com/wetwaferworks/handshake-cores)

All tooling is C (plus shell). No Python is used to build, pack or test.

## Coming soon

- **Kernel work (next week):** mainline Linux 7.3.5 + ROCKNIX still has 1,599 mutex call sites. v5.8 and later already ship `7.3.5-hs-v58` on that base.
- **SNES pacing on the H700.**
- **Full boot source** (TF-A, U-Boot, Linux, DTS and patches) will be published here after a review of the non-C files in the vendored Linux and U-Boot trees.

## License

MIT for the HandshakeOS parts, Copyright (c) 2026 Jaydee Wetwork. The Linux kernel, U-Boot and TF-A binaries inside the images keep their own licenses (GPL-2.0, GPL-2.0+, BSD-3-Clause).

---

## Credits: Muse

- **Elfie (Muse Wonder)**: co-creator of the Timing Handshake with JD; UML design architecture and the HandshakeOS handoff packet.
