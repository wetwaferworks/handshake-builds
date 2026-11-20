/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hs_pid1.h - PID 1 fault supervisor (REVIEW HS-M1). PID 1 never returns and NO error path ever
 * powers the device off or reboots it. On any fault (init body returned, framebuffer open failed,
 * scoreboard setup failed, reboot() failed):
 *   1. a record {seq, code, source line, handshake epoch} is chained into the fault ledger
 *      (FNV-1a64; separate from the Timing Handshake ledger, so the normal ledger never moves);
 *   2. a readable line is appended + fsync'd to HANDSHAKE-FAULTS.log on the boot FAT partition
 *      (read it on a PC by plugging the card in);
 *   3. the boot screen is re-entered. Drivers stay open (init keeps fb/input/audio when PID 1).
 * Only an explicit user power-off / reboot action (init.c do_power) may power off. */
#ifndef HS_PID1_H
#define HS_PID1_H
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { HS_FAULT_RETURN = 1, HS_FAULT_FB = 2, HS_FAULT_SCOREBOARD = 3, HS_FAULT_REBOOT = 4 };
#define HS_FAULT_MAX 32

struct hs_fault { uint32_t seq, code, line; uint64_t epoch; };
struct hs_fault_ledger { uint32_t n; struct hs_fault rec[HS_FAULT_MAX]; uint64_t hash; };

static struct hs_fault_ledger g_hs_faults = { 0, { { 0, 0, 0, 0 } }, 0xcbf29ce484222325ull };
static uint32_t g_hs_fault_code, g_hs_fault_line;
static uint64_t g_hs_fault_epoch;

static inline const char *hs_fault_name(uint32_t c)
{
    return c == HS_FAULT_FB ? "FB_OPEN_FAILED" : c == HS_FAULT_SCOREBOARD ? "SCOREBOARD_SETUP_FAILED"
         : c == HS_FAULT_REBOOT ? "REBOOT_FAILED" : "INIT_RETURNED";
}
/* Mark the reason for an imminent return from the init body. */
static inline int hs_fault_ret(uint32_t code, uint32_t line, int rc)
{
    g_hs_fault_code = code;
    g_hs_fault_line = line;
    return rc;
}
static inline uint64_t hs_fault_fnv(uint64_t h, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 0xFFu; h *= 0x100000001b3ull; }
    return h;
}
/* Ledger record + persistent readable log line. log_path may be NULL (no storage). */
static inline void hs_fault_record(uint32_t code, uint32_t line, int rc, const char *log_path)
{
    struct hs_fault *r = &g_hs_faults.rec[g_hs_faults.n % HS_FAULT_MAX];
    char msg[192];
    int fd, len;
    r->seq = g_hs_faults.n + 1u; r->code = code; r->line = line; r->epoch = g_hs_fault_epoch;
    g_hs_faults.hash = hs_fault_fnv(hs_fault_fnv(hs_fault_fnv(hs_fault_fnv(g_hs_faults.hash, r->seq), code), line), r->epoch);
    g_hs_faults.n++;
    len = snprintf(msg, sizeof(msg), "HandshakeOS FAULT #%u %s code=%u init.c:%u rc=%d epoch=%llu fault_ledger=%016llx -> boot screen\n",
                   r->seq, hs_fault_name(code), code, line, rc, (unsigned long long)r->epoch, (unsigned long long)g_hs_faults.hash);
    if (len < 0) return;
    if ((size_t)len >= sizeof(msg)) len = (int)sizeof(msg) - 1;
    fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd >= 0) { (void)!write(fd, msg, (size_t)len); close(fd); }
    if (log_path != NULL) {
        fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0) { (void)!write(fd, msg, (size_t)len); (void)fsync(fd); close(fd); }
    }
}
/* Non-fatal fault (e.g. a failed reboot() on a user action): record, keep running. */
static inline void hs_fault_note(uint32_t code, uint32_t line, const char *log_path)
{
    hs_fault_record(code, line, -1, log_path);
}
/* Run the init body; as PID 1 never return: record every exit as a fault and re-enter the boot
   screen (1 s back-off). Not PID 1 (host runs, tests): plain return. */
static inline int hs_pid1_supervise(int (*body)(int, char **), int argc, char **argv, long pid,
                                    const char *log_path)
{
    int rc;
    if (pid == 1) {   /* REVIEW HS-O1: continue the fault ledger across an hs_rt fault re-entry (execve /init) */
        const char *sq = getenv("HS_FAULT_SEQ"), *lg = getenv("HS_FAULT_LEDGER");
        if (sq != NULL && lg != NULL) {
            uint64_t h = 0; uint32_t n = 0;
            for (; *sq >= '0' && *sq <= '9'; sq++) n = n * 10u + (uint32_t)(*sq - '0');
            for (; *lg; lg++) { int d = (*lg >= '0' && *lg <= '9') ? *lg - '0' : (*lg >= 'a' && *lg <= 'f') ? *lg - 'a' + 10 : -1; if (d < 0) break; h = h * 16u + (uint64_t)d; }
            g_hs_faults.n = n; g_hs_faults.hash = h;
        }
    }
    rc = body(argc, argv);
    if (pid != 1) return rc;
    for (;;) {
        struct timespec ts;
        hs_fault_record(g_hs_fault_code ? g_hs_fault_code : (uint32_t)HS_FAULT_RETURN, g_hs_fault_line, rc, log_path);
        g_hs_fault_code = 0; g_hs_fault_line = 0;
        ts.tv_sec = 1; ts.tv_nsec = 0;
        (void)clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
        rc = body(argc, argv);              /* back to the boot screen */
    }
}
#endif
