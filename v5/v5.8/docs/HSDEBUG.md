# hsdebug — HandshakeOS v5.8 USB debug (read-only)

Pull live kernel contention data from a flashed RG35XX 2024 while a game
runs. The device exposes a USB CDC-ACM serial port next to the HSROMS
mass-storage LUN. Everything on this path is read-only: there is no
command that writes storage, flashes, or powers the unit off. A fault in
the debug service is logged to HANDSHAKE-BOOT.log and returns to the
boot screen; it does not power off.

## On Windows (JD's laptop) or WSL

Plug the handheld in over USB after it has reached the boot menu or a
game. Windows should enumerate a COM port (Device Manager → Ports).

```
hsdebug --port COM3 --log hsdebug-session.csv --interval 1s
```

Auto-detect the port and write under the handshake tree:

```
hsdebug --interval 1s
```

That creates `handshake\_ops\device-logs\hsdebug-YYYYMMDD-HHMMSS.csv`
and prints the stream until Ctrl+C.

List ports:

```
hsdebug --list-ports
```

From WSL the same binary interface works with a Linux build:

```
./hsdebug --port /dev/ttyACM0 --log ~/session.csv --interval 1s
```

## After a play session

```
hsdebug --summarize handshake\_ops\device-logs\session.csv
```

Prints, per game/system tag, the top spin-contention sites, runtime
mutex/rwsem uses by site, lock-class hold times, and contended counts,
plus phase overruns and futex rescue / delivery latency.

## Stream format (CSV)

Every sample is tagged with the running system and game:

| line   | meaning |
|--------|---------|
| TICK   | t_ms, sys, game, ledger, events, phase p50/max, overruns, xruns, futex rescue count, delivery avg/max ns, CPU temp/freq, battery, memory |
| LOCK   | lockstat class contended counts (spin, mutex, rwsem) |
| HOLD   | max/avg hold per lock class (rq_lock, futex_hb_lock, tty_output_spin) |
| SITE   | spin slow-path sites: n, wait avg/max, symbol |
| LOCKUSE| runtime mutex/rwsem uses by site |
| FUTEX  | rescue / delivery latency (from /proc/handshake_futex) |
| SCHED  | phase scheduler stats (from /proc/handshake_sched) |

Commands the host may send (all read-only): `help`, `stats`, `ledger`,
`lockstat`, `log`, `stream on`, `stream off`, `interval 1s`. Anything
that looks like `power`, `flash`, `write`, `reboot`, `halt`, or `format`
is rejected with `ERR read-only`.

## Build

```
# Linux
gcc -O2 -o hsdebug hsdebug.c

# Windows cross from WSL / Linux
x86_64-w64-mingw32-gcc -O2 -o hsdebug.exe hsdebug.c
```
