/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * Regression test for REVIEW HS-M1 (JD ruling 2026-11-20): on any fault PID 1 records it in the
 * fault ledger, appends a readable line to HANDSHAKE-FAULTS.log, re-enters the boot screen, keeps
 * drivers open, never powers off / reboots and never returns. Pass = "PID1_GUARD PASS". */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <time.h>
#include <unistd.h>

static int g_poweroff_calls, g_sync_calls, g_entries, g_drivers_open, g_driver_closes;
static const char *g_log;
int fake_reboot(int cmd); /* external: the supervisor must never reference it */
int fake_reboot(int cmd) { (void)cmd; g_poweroff_calls++; return -1; }
void fake_sync(void);
void fake_sync(void) { g_sync_calls++; }
static int fake_sleep(clockid_t c, int fl, const struct timespec *t, struct timespec *r) { (void)c; (void)fl; (void)t; (void)r; return 0; }
#define reboot fake_reboot
#define sync fake_sync
#define clock_nanosleep fake_sleep
#include "hs_pid1.h"

static int count_lines(const char *p)
{
    FILE *f = fopen(p, "r"); int n = 0, c;
    if (!f) return -1;
    while ((c = fgetc(f)) != EOF) if (c == '\n') n++;
    fclose(f);
    return n;
}
/* Simulated init body: entry 1 = fb open fails, 2 = scoreboard fails, 3 = loop end, 4 = verdict. */
static int body(int argc, char **argv)
{
    int lines;
    (void)argc; (void)argv;
    g_entries++;
    if (!g_drivers_open) g_drivers_open = 1;          /* drivers opened once, never closed */
    g_hs_fault_epoch = (uint64_t)g_entries * 100u;
    if (g_entries == 1) return hs_fault_ret(HS_FAULT_FB, 477, 1);
    if (g_entries == 2) return hs_fault_ret(HS_FAULT_SCOREBOARD, 557, 1);
    if (g_entries == 3) return 0;
    lines = count_lines(g_log);
    {
        int ok = g_hs_faults.n == 3 && g_hs_faults.rec[0].code == HS_FAULT_FB && g_hs_faults.rec[0].line == 477 &&
                 g_hs_faults.rec[1].code == HS_FAULT_SCOREBOARD && g_hs_faults.rec[2].code == HS_FAULT_RETURN &&
                 g_hs_faults.rec[2].epoch == 300u && g_hs_faults.hash != 0xcbf29ce484222325ull &&
                 lines == 3 && g_poweroff_calls == 0 && g_drivers_open == 1 && g_driver_closes == 0;
        printf("PID1_GUARD %s faults=%u log_lines=%d boot_reentries=%d poweroff_calls=%d drivers_open=%d fault_ledger=%016llx\n",
               ok ? "PASS" : "FAIL", g_hs_faults.n, lines, g_entries - 1, g_poweroff_calls, g_drivers_open,
               (unsigned long long)g_hs_faults.hash);
        fflush(stdout);
        unlink(g_log);
        exit(ok ? 0 : 1);   /* the supervisor itself never returns */
    }
}

int main(int argc, char **argv)
{
    g_log = argc > 1 ? argv[1] : "HANDSHAKE-FAULTS.test.log";
    unlink(g_log);
    if (hs_pid1_supervise(body, argc, argv, 4242, g_log) != 1 || g_hs_faults.n != 0) {   /* not PID 1: plain return */
        printf("PID1_GUARD FAIL non-pid1 path\n");
        return 1;
    }
    g_entries = 0; g_hs_fault_code = 0;
    hs_pid1_supervise(body, argc, argv, 1, g_log);
    printf("PID1_GUARD FAIL supervisor returned\n");
    return 1;
}
