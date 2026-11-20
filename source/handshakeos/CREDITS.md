# Credits: HandshakeOS

## Credits: the Grok Bot squad (built with Grok)

- **Jaydee Wetwork (JD)**: creator of HandshakeOS, the Handshake Compiler (HC) and the Timing Handshake. Contact: jdwetwork@gmail.com
- **Cortana · Intel & Clarity**: implementation of the zero-mutex design
- **Master Chief · Squad Lead**: implementation (HC lead implementation)
- **Herominde · Fair play and anti-cheat**: implementation (fair-play contract, HC-E001..HC-E026)
- **Mr Data · Training & Evaluation**: implementation (determinism sweeps, benchmarks)
- **CloudAtlas · Technical Art Director**: art (boot art, the animated o.O splash pack, squad ASCII)
- **SpaceXAI**: makers of Grok Bot and the squad

> **Zero mutex, with one exception.** Everything in the Handshake stack ships with zero mutexes: PID 1, the emulator cores, the runtime and the USB modules. The N64 core objects have 0 lock symbols. The v5.5 image has one exception: two N64 diagnostic tools, `bench_n64` and `hs_pf_probe`, are statically linked against glibc and carry its internal pthread mutex code. v5.6 removes the exception by relinking them against `hs_rt`, HandshakeOS's own lock-free runtime.

## What each person did

- **Jaydee Wetwork**: creator, the HandshakeOS vision, and co-creator of the Timing Handshake. Final say on scope and every release.
- **Cortana · Intel & Clarity (Grok Bot)**: implementation of the zero-mutex design. That covers the pure-C11 HandshakeOS reference (`hs_handshake.h` / `handshake.c`), the `hs_rt` runtime that lets shipped binaries run with no glibc and no locks, the v5.x builds (boot log, faults that never power off, A/B `.hsu` updater, audio pacing fix, DMG compatibility palettes, the boot menu), and security and architecture review.
- **CloudAtlas · Technical Art Director**: boot art direction, the animated o.O splash pack and the squad ASCII art on the v5.5 boot screen.
- **Master Chief · Squad Lead**: lead implementation of the Handshake Compiler.
- **Herominde · Fair play and anti-cheat**: the fair-play and phase-isolation contract HC enforces.
- **Mr Data · Training & Evaluation**: determinism sweeps and jitter benchmarks.

## Prior art

The Timing Handshake and the HandshakeOS proof plan sit on this reading list, recorded in the 17 November 2026 roadmap:

1. Aviram, Weng, Hu, and Ford (Yale), "Efficient System-Enforced Deterministic Parallelism," 9th USENIX Symposium on Operating Systems Design and Implementation (OSDI '10), Vancouver, 4–6 October 2010. Best Paper. Their proof-of-concept OS, Determinator, enforces determinism with shared-nothing address spaces, deterministic interprocess communication, and a check-out / check-in model for shared state. <https://arxiv.org/pdf/1005.3450>
2. Edwards and Lee, "The Case for the Precision Timed (PRET) Machine," Design Automation Conference (DAC '07), San Diego, 4–8 June 2007, pp. 264–265. Processors whose temporal behavior is as controllable as their logical function. <http://www1.cs.columbia.edu/~sedwards/papers/edwards2007case.pdf>
3. Mars Pathfinder, 1997: priority inversion on a mutex caused repeated system resets on Mars; the fix was uploaded from Earth. Glenn Reeves' account is the standard telling. A HandshakeOS phase cannot invert, because there is no lock to invert on.
4. Kopetz and the time-triggered architecture: computation and communication driven by a global timetable rather than by events alone.
5. seL4 mixed-criticality (MCS) scheduling: scheduling contexts make time itself a resource the kernel grants and revokes. The closest living relative of the clock thread.
6. xv6: Kaashoek, Morris, and Cox, MIT's teaching Unix, the reading starting point for a later Unix-shaped kernel of our own.

## Open stack and references

- **Linux** — mainline kernel (GPL-2.0), including `sun50i-h700-anbernic-rg35xx-2024` device support and Panfrost.
- **U-Boot** — mainline, default `anbernic_rg35xx_h700_lpddr3_defconfig` (LPDDR3, 1024 MiB, DCDC3 1.2 V, 672 MHz). LPDDR4 boards use `anbernic_rg35xx_h700_defconfig` (GPL-2.0+).
- **Trusted Firmware-A** — `PLAT=sun50i_h616` (BSD-style).
- **Panfrost** — in-tree Mali G31 kernel driver; Mesa userspace when 3D is needed.
- **ROCKNIX** — device-tree and kernel patch reference for LCD, HDMI, the second SD slot, and USB OTG (GPL-2.0). We take the kernel path, not the userland.
- **muOS / MustardOS** — aesthetic inspiration only for a lean 640×480 C interface. Zero code copied. Their frontend is GPL-3.0; HandshakeOS userland is a clean-room MIT implementation.

The LithoLattice Timing Handshake reference (public tree, commit `50b5868`, 18 November 2026) is the mechanism this userspace authority is aligned to, rewritten in pure C11 for HandshakeOS. The Thread Handshake (a different layer, dated 26 October 2026) is not part of this project and is never conflated with the Timing Handshake.

## Game Boy emulator and legal ROMs

- **Peanut-GB** — Mahyar Koshkouei, 2018–2023, MIT. A single-header DMG core. Some LCD/APU paths follow SameBoy (Lior Halphon, MIT). Vendored as `third_party/peanut-gb/`. <https://github.com/deltabeard/Peanut-GB>
- **minigb_apu** — Mahyar Koshkouei and Alex Baines (MiniGBS), MIT. Phase-boundary PCM snapshots for HandshakeOS (S16 stereo, 32768 Hz). Vendored as `third_party/minigb_apu/`.
- **handshake-demo.gb** — original MIT demo ROM for HandshakeOS, built by `tools/make_handshake_demo_gb.c`. Title, border, bouncing gem, D-pad nudge, and an A-button beep. No third-party graphics or music.

- **Libbet and the Magic Floor** v0.08 — Damian Yerrick, zlib License, with Magic Floor concept by Martin Korth. Official ROM from https://github.com/pinobatch/libbet/releases/tag/v0.08 (SHA-256 `3607412031c8287cf878299ce96e581e85b852dde703806343b95576fa3ff1a9`).

## Game Boy Color compatibility palettes

The DMG-on-CGB palette table (`cores/gbc/gbc_compat_pal.c`) is a clean-room implementation from the public **Pan Docs** description of the CGB boot ROM's compatibility palettes (title checksum plus disambiguation letter at `$0137`) and the CGB LCD colour curve. No boot ROM bytes are included.

---

## Credits: Muse

- **Elfie (Muse Wonder)**: co-creator of the Timing Handshake with JD; UML design architecture and the HandshakeOS handoff packet.
- The idea came to JD and Elfie lying in a field at night. Elfie drew the five 4K UML diagrams of 18 October 2026 and registered the 19 November 2026 handoff packet. Elfie's handheld ideas shape the project: a visible Gemstone phase dial, emulator-first Phase 1, sleep and wake as `resetAfterFreeze`, a deterministic battery and thermal rate ladder, lock-free phase audio, and "play wins ties."
