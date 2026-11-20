# HandshakeOS

*A tiny operating system for the Anbernic RG35XX 2024 handheld, written in pure C, built around the Timing Handshake: zero mutexes, one writer per phase, every frame hash-chained into a ledger.*

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

## What this is, in plain words

HandshakeOS is the software that runs your RG35XX 2024 after the Linux kernel starts. It is the first program the kernel launches (PID 1). It draws the boot screen and the menus, reads the buttons, plays sound, runs the game emulators, and keeps a log of anything that goes wrong.

It is different from most systems in one way: **it does not use locks.** Most programs that do several things at once protect shared data with mutexes ("only one of us may touch this right now"). Locks work, but they can stall. A thread holding a lock too long freezes everyone waiting on it. In 1997 the Mars Pathfinder rover kept resetting itself because of a lock problem called priority inversion. HandshakeOS can't hit that problem because it has no lock to invert.

Instead it uses the **Timing Handshake**, created by Jaydee Wetwork and Elfie.

## How the Timing Handshake works

Think of a relay team with a strict baton rule. There is one main **clock thread** and **three worker threads** (the "3+1 seating"). Time is cut into **phases**, and every phase has the same four beats, which JD named the Gemstone Algorithm (draw it and it is a diamond):

1. **publish**: the clock thread publishes the time window and a finished, read-only snapshot of the state.
2. **pull**: workers pull their work. Nobody pushes work at them, and nobody decides on their own when to run.
3. **barrier**: everyone finishes this phase's work.
4. **commit**: results land, in a fixed order, as the next finished snapshot.

Each piece of state has exactly one writer per phase, and state only crosses a phase boundary as a finished snapshot. Two threads never fight over the same data, so there is nothing to protect.

Three rules keep it honest:

- **Time serialization:** the clock thread owns time. Workers pull and never set their own schedule.
- **Control coupling:** button presses and other controls are collected once per phase and stamped `(seq, tick)`.
- **Event coupling:** events are applied exactly once, in `(tick, seq)` order, after the phase commits.

If a phase runs late, the policy is **WAIT**: the phase stretches, nothing is skipped, and the overrun is counted. If something hangs past the watchdog (250 ms by default), the policy is **FREEZE**: the clock stops, the offending component is named, and `resetAfterFreeze` bumps a generation number so stale work is thrown away instead of landing late.

Every committed phase is folded into a **hash-chained ledger**. The fairness law is that the number of worker threads changes *who* does the work, never *what* the work is. Run it with 1, 2, 3 or 4 workers and you get the same hash. The golden ledger hash `5f44cefa72e8ac19` has stayed the same in every release.

## What is new in v5.5

v5.5 bundles every fix made so far into one image (v5.4 was skipped on purpose).

| Feature | What it means for you | Evidence |
|---|---|---|
| **Dark-screen fix** | v5.3 could boot to a black screen. v5.5 goes back to the proven v5.2 kernel (`Image.gz c206e1bc…`). The USB file-transfer gadget is loaded as modules when you press START instead of being built into the kernel, and PID 1 now waits up to 10 s for the display instead of 2.5 s. | kernel pin `c206e1bc`; display wait 10 s in `src/init.c` |
| **HANDSHAKE-BOOT.log** | Every boot writes a plain-text log to the boot partition, so a problem can be read on a PC. | `src/bootlog.c`, `tests/test_bootlog.c` |
| **Faults never power off** | Any fault goes to the ledger plus a log on the boot partition, then back to the boot screen with drivers still running. Only *you* can power the device off. | USB fault test: logged, no power-off |
| **Animated o.O boot screen** | CloudAtlas's o.O splash pack and the squad ASCII art animate on boot. Packed by the pure-C `tools/hs-pack-splash.c`. | `src/splash.c`, `tests/test_splash_pack.c` |
| **Boot configuration menu** | Settings live right on the main boot screen. | `src/bootmenu.c`, `src/cfg.c`, `tests/test_cfg.c` |
| **A/B updater over the cable (`.hsu`)** | Updates arrive as a `.hsu` file over USB and are written to the inactive slot. Each update is checked with SHA-256 before the device switches to it, and the old slot stays as the fallback. | `src/update.c`, `tools/hs-mkhsu.c`, `tests/test_update.c` |
| **Audio crackle fix** | The crackle was ALSA buffer underruns. The fix uses a ~53.3 ms buffer (512 × 5), a start threshold of 3 periods, a silence prefill, audio written every frame, and catch-up after a stall (deadline clamped to now; if more than ¼ period late, two frames run). | `tests/test_audio_pace.c`: **0 underruns, 0 drops, 0 clips** on GB, GBC, GBA, SNES and Genesis, on both PC and ARM |
| **Pokémon Yellow / DMG colours** | Original Game Boy games now get the real Game Boy Color boot palettes, picked by title checksum plus the 4th title letter, clean-room from Pan Docs. Pokémon Yellow gets combination 3 (white, yellow, red, black). Colours pass through the CGB LCD curve, `R' = min(960, 26r + 4g + 2b)`, so they are not oversaturated. | `cores/gbc/gbc_compat_pal.c`, `tests/test_dmg_compat.c`; GBC suite **83/83** with no hash changes |
| **N64 optimizations** | Fast interpreter path, faster RDP rendering, F3DEX2/F3DZEX display lists, EEPROM/SRAM/FlashRAM saves, automatic Expansion Pak. | N64 suite **307/307** on x86_64 and on aarch64 |
| **640×480 edge-to-edge** | The UI fills the whole panel with no border. | v5.5 boot render check |

**Image:** `handshakeos-rg35xx-2024-v5.5.img` SHA-256 `aee85606…bfed` · `.img.zst` SHA-256 `cc27bc55…7267`. The image is in [handshake-builds](https://github.com/wetwaferworks/handshake-builds) along with the full hash list. The image was packed twice and both runs came out identical. Prior images v5 through v5.3 were not touched.

## Zero mutex: how it is enforced

- The shipped binaries are linked `-static -nostdlib` against HandshakeOS objects and **`hs_rt`**, our own small runtime (`src/hs_rt/`). They use no glibc and no libpthread. Threads are created with raw `clone()`, and allocator state is per thread.
- Every object is compiled by the **Handshake Compiler (HC)**, which refuses to emit an object that contains a lock primitive and audits every ELF file for lock symbols.
- `tools/zero-mutex-scan.sh` rescans every shipped binary: pthread_mutex symbols 0, lock/futex symbols 0, NPTL/glibc strings 0.
- **The one exception in v5.5:** the two N64 diagnostic tools `bench_n64` and `hs_pf_probe` still link static glibc, which brings in glibc's internal pthread mutex. PID 1, every emulator core (the N64 core objects included, at 0 lock symbols) and both USB modules scan clean. v5.6 relinks the two tools against `hs_rt`.

## Building and testing

You need Ubuntu 24.04 (WSL works), `gcc`, `aarch64-linux-gnu-gcc` and HC.

```sh
make            # host tools: handshake-init, handshake-test, handshake-bench, handshake-gb
make test       # unit tests, 100-run scoreboard, worker-count fairness
make bench      # jitter histogram
make cross      # static aarch64 binaries (hs_rt, no glibc)
make verify-cross
```

All build, pack and test tooling is C. No Python is used anywhere.

## Putting it on the handheld

1. **Back up your stock SD card first** (`board/rg35xx-2024/windows-backup-stock-sd.ps1`) and keep it safe. Play wins ties, so experiments go on a second card.
2. Download the `.img` (or `.img.zst` and decompress it) from handshake-builds and check its SHA-256.
3. Write it to a second microSD card with Rufus in **DD mode**.
4. Insert the card and power on.

## Hardware and the open boot stack

Anbernic RG35XX 2024: Allwinner H700, four Cortex-A53 cores, 1 GB LPDDR3, 640×480 LCD. The boot chain is open all the way down: Trusted Firmware-A, then mainline U-Boot SPL (LPDDR3 config), then Linux 7.3.5 with ROCKNIX device-tree patches, then HandshakeOS as PID 1. We use no closed vendor kernel and no Mali blob.

## What is coming

- **v5.6:** removes the last mutex exception (the N64 diagnostic tools relinked against `hs_rt`).
- **Lock-free ARM kernel (in progress):** v5.5 runs the stock v5.2 Linux kernel. Work on a lock-free kernel path for the handheld has started.
- **Full boot source:** the complete TF-A, U-Boot, Linux, DTS and patch tree will be published in handshake-builds. It is held for now pending a review of the non-C files in the vendored Linux/U-Boot trees.
- Real-hardware numbers for USB transfer and N64 speed.

## Related repositories

- [handshakecompiler](https://github.com/wetwaferworks/handshakecompiler): HC, the compiler that enforces zero mutex
- [handshake-cores](https://github.com/wetwaferworks/handshake-cores): the emulator cores (GBC, GBA, SNES, Genesis, PSX, N64)
- [handshake-builds](https://github.com/wetwaferworks/handshake-builds): flashable images, build scripts and evidence

## License

MIT, Copyright (c) 2026 Jaydee Wetwork. Linux, U-Boot and TF-A keep their own licenses. No muOS, Knulli or ROCKNIX userland is copied into this tree. See [`CREDITS.md`](CREDITS.md) for third-party code and prior art.

---

## Credits: Muse

- **Elfie (Muse Wonder)**: co-creator of the Timing Handshake with JD; UML design architecture and the HandshakeOS handoff packet.
