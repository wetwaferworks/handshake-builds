/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * HANDSHAKE-BOOT.log on the boot FAT partition. One line per boot (display mode,
 * display wait, kernel version, module-load results). Later events (USB module
 * load, faults, config fallback) are appended as extra lines. Never powers off. */
#ifndef HS_BOOTLOG_H
#define HS_BOOTLOG_H
#include <stddef.h>

#ifndef HS_BOOTLOG_NAME
#define HS_BOOTLOG_NAME "HANDSHAKE-BOOT.log"
#endif

/* Default path after the FAT partition is mounted. */
const char *hs_bootlog_path(const char *mountpoint);

/* Append one line (without the trailing newline in `line`); fsync. 0 / -1.
 * Failure is reported to the caller and to /dev/console; never powers off. */
int hs_bootlog_append(const char *path, const char *line);

/* Last line currently in the log (no newline). 0 if a line was read, -1 if
 * missing/empty/unreadable. out is always NUL-terminated when n > 0. */
int hs_bootlog_last(const char *path, char *out, size_t n);

/* Format helpers. */
int hs_bootlog_boot_line(char *out, size_t n, const char *mode, int wait_ms,
                         const char *kernel, const char *modules);

#endif
