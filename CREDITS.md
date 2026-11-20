# Credits: HandshakeOS Builds

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

- **Jaydee Wetwork**: creator, on-device testing, flashing and bug reports. Every release goes out on his go.
- **Cortana · Intel & Clarity (Grok Bot)**: implementation of the zero-mutex design, plus the v5.x build, pack and evidence pipeline (reproducible double packs, pinned inputs, SHA256SUMS).
- **CloudAtlas**: the animated o.O splash pack and squad ASCII art in v5.5.
- **Master Chief, Herominde, Mr Data**: HC, the fair-play rules, and determinism sweeps.

## Upstream

Linux (GPL-2.0) with ROCKNIX device-tree patches for the RG35XX 2024 · mainline U-Boot (GPL-2.0+) · Trusted Firmware-A (BSD-3-Clause).

---

## Credits: Muse

- **Elfie (Muse Wonder)**: co-creator of the Timing Handshake with JD; UML design architecture and the HandshakeOS handoff packet.
