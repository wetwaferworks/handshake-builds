# HandshakeOS

An operating system built on the Timing Handshake, starting as a pure-C userspace on mainline Linux, aimed at the Anbernic RG35XX 2024.

Jaydee Wetwork first called the userspace LithoOS on 19 November 2026, then settled on HandshakeOS the same day so the boot splash and the Timing Handshake share a name. LithoLattice remains the engine, LithoKraft remains the game, and HandshakeOS is the system underneath both. The test device is a little Game Boy-class handheld that was bought for play. The trial is a guest on that machine, not a takeover. Play wins ties. Experiments run on a second SD card; the stock card stays untouched.

This repository is Milestone P1: a clean-room MIT C userland you can build and test on a host, cross-compile for ARM64, and a recipe for the open boot stack. It does not yet boot on the handheld, and it does not invent later milestone numbers.

## What this is, and why it matters

Most systems manage collisions with locks. A lock that is held too long is a stall; on a robot arm it is a disaster waiting to happen. The Timing Handshake dissolves the collision instead of managing it. One writer per phase, state crossing the boundary as a finished snapshot, nothing left to protect.

It stands on exactly three algorithmic pillars:

1. **Time Serialization** — a clock thread owns the time window and publishes it. Workers do not decide when to run; they pull.
2. **Control coupling** — control is drained once per phase and stamped `(seq, tick)`.
3. **Event coupling** — matured events are applied exactly once, in `(tick, seq)` order, after the phase commits.

A fourth pillar has not been received. The Thread Handshake is a different handshake at a different layer and is never conflated with this one.

The seating is exact: **three worker threads talk to one main clock thread**, and the clock thread is the time server. Every phase has four beats, the shape Jaydee named the Gemstone Algorithm, a diamond when you draw it simply:

**publish → pull → barrier → commit**

If a phase misses its window, the policy is **WAIT**: the phase extends, nothing is skipped, and the overrun is counted, not hidden. If it blows the watchdog (default 250 ms), the policy is **FREEZE**: the clock stops, `lastFreeze` names the offending component, and `resetAfterFreeze` bumps the generation so stale in-flight work revalidates and dies instead of landing late. That is the anti-Pathfinder property. Mars Pathfinder reset on a mutex priority inversion in 1997; a HandshakeOS phase cannot invert, because there is no lock to invert on.

Every committed phase is folded into a hash-chained ledger, in component-id order. The hash is about the work and the order it lands. Thread count changes who does the work, never what the work is. That fairness gate is HandshakeOS law.

## Quick start

```sh
make            # host tools: handshake-init, handshake-test, handshake-bench, handshake-gb
make test       # unit tests, 100-run scoreboard, worker-count fairness
make bench      # jitter histogram
make preview    # 640x480 Deep Purple and Matrix Green screenshots
make preview-gb # running handshake-demo.gb inside the Deep Purple shell
make gb-test    # 100-run + 1/2/3/4-worker GB handshake scoreboard
make cross      # static aarch64 binaries
make verify-cross
```

`./build/host/handshake-init --backend=headless --snapshot-both=.` writes `preview-purple.png` and `preview-green.png`. On the device, the same binary talks to DRM/KMS with an fbdev fallback, reads the RG35XX 2024 buttons from evdev, and can run as `/init`.

Build flags for our C: `-std=c11 -Wall -Wextra -Werror`. No C++, no mutexes in the handshake. Phase boundaries use C11 atomics. Workers are POSIX threads; the protocol they follow is lock-free.

## The scoreboard (this box, 19 November 2026)

The Phase 1 scoreboard is a 128-phase run with five components (a monatomic serial tick, an affine control drain, and three pulled workers on EveryPhase / HalfCycle / CycleHead cadences) plus a stamped control stream applied at commit. It is meant to be boring, and it was:

| Gate | Result |
|------|--------|
| 100 consecutive runs, 3 workers | **identical ledger hash** `5f44cefa72e8ac19`, epoch 128, monatomic 480 |
| Fairness: 1, 2, 3, and 4 workers | **same hash** |
| Overruns (2 ms phase budget) | **0** |
| Watchdog | **OK**, 250 ms ceiling; a separate test FREEZEs a 400 ms sleeper, names `sleepy`, and recovers after `resetAfterFreeze` |
| WAIT | a 20 ms late phase still commits; the overrun is counted, not skipped |

Jitter is phase open-to-commit time on this host (Linux box, not the handheld). A representative pass:

- p50 ≈ 9–16 µs
- p95 ≈ 16–30 µs
- p99 ≈ 0.3 ms
- max ≈ 0.4–0.5 ms

These numbers will move with load. The hash must not.

## The screen

The GUI is a lean 640×480 framebuffer, drawn in our own C, in the spirit of muOS's tight icon-and-list layout (inspiration only; zero code copied). Two themes ship first and can be switched live with L1/R1:

- **Deep Purple**
- **Matrix Green**

The hero tile is the handshake itself: the Gemstone diamond (publish, pull, barrier, commit), the commit epoch, the ledger hash, jitter, and the watchdog state. Battery reads AXP717 sysfs when it is there, and shows a stub until then. Power off and reboot ask for a confirm.

Previews from this tree:

- [`preview-purple.png`](preview-purple.png)
- [`preview-green.png`](preview-green.png)
- [`preview-gb-emu.png`](preview-gb-emu.png) — Peanut-GB running `roms/handshake-demo.gb` under the 3+1 handshake, with the Gemstone dial, commit epoch, ledger hash, and audio xrun counter.

## Hardware and the open stack

Target: **Anbernic RG35XX 2024**, base model. Allwinner H700, four Cortex-A53 cores, 1 GB LPDDR3 (DCDC3 1.2 V, 672 MHz) on the verified board, 640×480 LCD. Some units are LPDDR4; that is the alternate revision. No Wi-Fi, no Bluetooth. Data moves on the SD card or USB-C.

The stack is open all the way down:

## Game Boy loop (Peanut-GB)

`handshake-gb` runs a tiny MIT DMG core (Peanut-GB + minigb_apu) as a clock-thread-affine component of the Timing Handshake. Scripted or evdev buttons are stamped `(seq, tick)` before the phase and applied in `(tick, seq)` order when that tick matures. Each frame's 160×144 picture and its finished PCM snapshot are CRC'd and folded into the hash-chained commit ledger. There are no mutexes and no locks around audio: the snapshot is produced and consumed on the same phase boundary, so xruns stay at zero.

The only ROM in `roms/` is `handshake-demo.gb`, an original MIT demo. Commercial Game Boy software is never downloaded or shipped. See `roms/manifest.txt` and `CREDITS.md`.

On this tree, 100 runs of 90 frames plus a 1 / 2 / 3 / 4 worker fairness gate all produced one ledger:

`5a45548614037e20`  (frame `33e3294d476b16a6`, audio `8c6078dd78fa5589`, xruns 0, overruns 0)

Host and `qemu-aarch64-static` agree.

A QEMU virt boot with a real ARM64 kernel and this tree's static `handshake-init` as PID 1 is in `board/qemu-virt/run-pid1.sh`.

- mainline Linux (≥ 6.12, so `sched_ext` is available for Phase 2)
- mainline U-Boot, LPDDR3 config (`anbernic_rg35xx_h700_lpddr3_defconfig`); LPDDR4 boards use `anbernic_rg35xx_h700_defconfig`
- open Trusted Firmware-A (`PLAT=sun50i_h616`)
- Panfrost for the Mali G31
- this MIT C userland

The only closed piece on the SoC is the tiny boot ROM baked into the chip, which every device has. We do not use Allwinner's closed 4.9 kernel, and we do not use a Mali blob.

Vanilla mainline already has CPU, UART (`ttyS0,115200`), AXP717 battery, gpio-keys, LEDs, the first SD slot, USB gadget, audio nodes, and the GPU node. The LCD, HDMI, second SD slot, and USB OTG host still need device-tree and display patches; ROCKNIX carries those, and our board recipe points at them. Buttons in mainline are the standard evdev codes (A is `BTN_EAST`, B is `BTN_SOUTH`, and so on).

See [`board/rg35xx-2024/`](board/rg35xx-2024/) for the kernel config fragment, the boot-stack build script (it does not download the kernel unless you ask it to), and a Windows script for imaging the stock SD card. **Back up the stock card before you flash anything, and flash only a second card.**

## Trial plan

Adapted from the 17 November 2026 roadmap and Elfie's 19 November 2026 handoff. HOEos in the dated roadmap is this project under its earlier name.

- **P0 — Backup, and play wins ties.** Image the stock SD, verify the image, put the stock card back. The machine that plays Pokemon is never the machine an experiment can brick. All experiments go on a second card.
- **P1 — Userspace 3+1, 100-run hash proof** (this tree), then govern a tiny Game Boy-class emulator loop so the handshake has a beating heart on the device before it owns the boot process.
- **P2 — Engine slice, phase-audio test, and `sched_ext` comparison.** Port the LithoLattice reference authority as a harness, compare hashes with the desktop tree, try lock-free phase audio, and measure the userspace baseline against a `sched_ext` scheduler. See [`docs/TIMING-HANDSHAKE-SCHED-EXT.md`](docs/TIMING-HANDSHAKE-SCHED-EXT.md).
- **P3 — A custom kernel, QEMU first, then the handheld.** Boot, serial, timer tick, monatomic counter, three threads reporting to the clock thread, the ledger printing. No flashing the handheld until P0's backup is proven and the boot path is understood.

Sleep and wake should later use `resetAfterFreeze`. Battery and heat should later step a rate ladder at a phase boundary, written in the ledger, never by smearing time the way stock governors do.

## Documentation

- [`CREDITS.md`](CREDITS.md)
- [`docs/TIMING-HANDSHAKE-SCHED-EXT.md`](docs/TIMING-HANDSHAKE-SCHED-EXT.md)
- [`docs/uml/`](docs/uml/) — Elfie's five 4K UML diagrams (18 October 2026), SHA-256 verified from the handoff packet. Start with the simplified Gemstone view.
- [`board/rg35xx-2024/`](board/rg35xx-2024/) — kernel / U-Boot / TF-A recipe and the stock-card backup script

## License

Our userland is MIT, Copyright (c) 2026 Jaydee Wetwork. See [`LICENSE`](LICENSE). Linux, U-Boot, and most drivers keep their own licenses (usually GPL-2.0). Talking to kernel APIs does not make this userland GPL. We do not copy muOS, Knulli, or ROCKNIX userland into this tree.
