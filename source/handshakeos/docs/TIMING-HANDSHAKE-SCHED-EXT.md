# Phase 2: the Timing Handshake as a sched_ext scheduler

This note is the plan for moving the userspace baseline onto a Linux `sched_ext` CPU scheduler, then comparing the two. Nothing here is implemented yet. Phase 1 lives in userspace and already prints a ledger hash, jitter, and overrun counters. Phase 2 starts when that hash is stable on the handheld as well as on this box.

The Timing Handshake stands on exactly three pillars: **Time Serialization**, **control coupling**, and **event coupling**. A fourth has not been received. This is not the Thread Handshake.

## What Phase 1 already proved

A 3+1 seating (three worker threads pulling from one clock thread) runs the four Gemstone beats — publish, pull, barrier, commit — with atomics only. Missed windows WAIT (the phase extends; nothing is skipped). The watchdog FREEZEs at 250 ms, names the offender, and `resetAfterFreeze` bumps the generation so stale work dies. Every committed phase is folded into a hash-chained ledger in component-id order, so the hash is about the work and the order it lands, not about which thread ran it.

On the host, 100 consecutive runs of the 128-phase scoreboard produced the same ledger hash, and the hash did not move at 1, 2, 3, or 4 workers. Jitter (phase open-to-commit) and overrun counters are recorded beside it. That is the baseline `sched_ext` has to beat or at least match, hash included.

## Why sched_ext, and not a kernel fork

Mainline Linux 6.12 and newer can load a CPU scheduler written in restricted C (BPF) through `CONFIG_SCHED_CLASS_EXT`. If the scheduler misbehaves, the kernel falls back to its ordinary class. That is the safe way to try the handshake as a real OS-level scheduler without maintaining a patched kernel tree.

The kernel configuration we want on the RG35XX 2024 image (see `board/rg35xx-2024/`) already includes:

- `CONFIG_SCHED_CLASS_EXT=y`
- `CONFIG_BPF_SYSCALL=y` and a BPF JIT
- `CONFIG_PREEMPT=y`
- `CONFIG_HIGH_RES_TIMERS=y`
- `CONFIG_NO_HZ_FULL=y`

A reserved CPU (`nohz_full`, `isolcpus`) can hold the clock thread or the tick-critical work, the same way the handheld's four Cortex-A53 cores suggest a 3+1 seating.

## Mapping the four beats onto sched_ext

The clock thread remains the owner of Time Serialization. It is the time server: it decides the phase window and publishes it. Workers still do not decide when to run; they pull.

A first mapping, to be measured rather than assumed:

- **is it time / publish.** The scheduler, or a small clock task it always dispatches on the reserved CPU, opens the window when `CLOCK_MONOTONIC` reaches the deadline. `pollPhase` / `canTick` stay the userspace questions; the scheduler's job is to make the clock task runnable at that instant and to hold the others.
- **can I tick / pull.** A worker becomes eligible only after the window is published and only for the work due on its shard. Granting "can I tick?" is a dispatch, not a mutex. One writer per phase still holds.
- **barrier.** The authority still waits for every tick-critical shard. The scheduler must not starve a slow shard, and it must not skip it (WAIT, never skip). Overrun is counted, not hidden.
- **commit.** The commit hook and the ledger stay in the clock thread, in component-id order, after the barrier. `commitEpoch` advances only then.

Watchdog policy does not change. Past 250 ms the phase FREEZEs, `lastFreeze` names the offender, and `resetAfterFreeze` bumps the generation. Sleep and wake on the handheld should use the same primitive: a generation bump, nothing half-applied survives.

Control coupling and event coupling stay in the same bracket they already use: a PRE drain of stamped `(seq, tick)` events, and a POST apply of matured events in `(tick, seq)` order after commit. Buttons on this machine are a physical control stream. That is a later Phase 2 item (the tiny Game Boy-class emulator loop), not a change to the scheduler mapping itself.

## The comparison

Run the same 128-phase scoreboard, and later the same emulator loop, two ways:

1. The Phase 1 userspace baseline (this tree).
2. The same authority, with worker and clock threads scheduled by the `sched_ext` handshake class, on a kernel built from the board recipe.

Scoreboard, both ways:

- ledger hash, which must be identical to Phase 1 and identical across worker counts
- phase overruns, counted, not hidden
- jitter: p50, p95, p99, and max of phase open-to-commit time
- freeze count (should stay zero on the happy path)
- barrier wait totals

If the hash moves, the scheduler changed what the work is or the order it lands in, and the experiment has failed, however pretty the jitter looks. If the hash holds and jitter or overruns improve, that is a real result.

## What this pass does not do

It does not ship a BPF scheduler, and it does not compile the kernel. Those waits until the bootable image recipe in `board/rg35xx-2024/` has been run, the stock card has been backed up, and Phase 1 has been re-run on the device (or, for kernel work, first in QEMU).
