/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * Regression test for REVIEW HS-O1: every hs_rt fatal path, in PID 1, records a fault-ledger entry,
 * appends a HANDSHAKE-FAULTS.log line and re-enters the boot screen instead of exiting. Linked against
 * hs_rt only; run under qemu-aarch64 once per stage (build-v5.2.sh). The test forces the PID 1 branch
 * (hs_rt_fault_force_pid1) and installs a re-entry probe; the probe is the only exit.
 * usage: test-hs-rt-fault STAGE LOGFILE   (HS_FAULT_SEQ / HS_FAULT_LEDGER may be preset) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int hs_rt_fault_force_pid1;
extern const char *hs_rt_fault_log_path;
extern const char *hs_rt_fault_reexec_path;
extern void (*hs_rt_fault_reenter)(const char *why, unsigned seq, uint64_t ledger);
void *__memcpy_chk(void *d, const void *s, size_t n, size_t dlen);
void *__memmove_chk(void *d, const void *s, size_t n, size_t dlen);
void *__memset_chk(void *d, int c, size_t n, size_t dlen);
char *__strncpy_chk(char *d, const char *s, size_t n, size_t dlen);
int __snprintf_chk(char *buf, size_t n, int flag, size_t slen, const char *fmt, ...);
char *__fgets_chk(char *s, size_t slen, int n, FILE *f);
size_t __fread_chk(void *d, size_t dlen, size_t sz, size_t n, FILE *f);
void __stack_chk_fail(void);

static int g_stage;
static const char *g_log;
static volatile size_t g_n;
static void (*volatile g_free)(void *) = free;
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void probe(const char *why, unsigned seq, uint64_t ledger)
{
    char last[256] = { 0 }, buf[256];
    int lines = 0;
    FILE *f = fopen(g_log, "r");
    unsigned want = (unsigned)atoi(getenv("HS_FAULT_SEQ") ? getenv("HS_FAULT_SEQ") : "0") + 1u;
    char tag[32];
    int ok;
    if (f) {
        while (fgets(buf, sizeof buf, f)) { lines++; memcpy(last, buf, sizeof last); }
        fclose(f);
    }
    snprintf(tag, sizeof tag, "FAULT #%u HS_RT", seq);
    ok = seq == want && lines == (int)want && strstr(last, tag) != NULL && strstr(last, why) != NULL &&
         strstr(last, "-> boot screen") != NULL && ledger != 0xcbf29ce484222325ull;
    printf("HS_RT_FAULT stage=%d %s seq=%u log_lines=%d ledger=%016llx reenter=boot exit_avoided=1\n",
           g_stage, ok ? "PASS" : "FAIL", seq, lines, (unsigned long long)ledger);
    fflush(stdout);
    hs_rt_fault_force_pid1 = 0;   /* leave the forced-PID1 branch so the test process can end */
    exit(ok ? 0 : 1);
}

int main(int argc, char **argv)
{
    char d[16], s[64];
    int v[4] = { 3, 1, 2, 0 };
    if (argc < 3) return 2;
    g_stage = atoi(argv[1]);
    g_log = argv[2];
    memset(d, 0, sizeof d); memset(s, 'x', sizeof s); s[63] = 0;
    hs_rt_fault_force_pid1 = 1;
    hs_rt_fault_log_path = g_log;
    hs_rt_fault_reenter = probe;
    if (g_stage == 14) {   /* real re-entry: no probe; the fault path execve()s this binary in place */
        const char *sq = getenv("HS_FAULT_SEQ"), *lg = getenv("HS_FAULT_LEDGER");
        hs_rt_fault_reenter = NULL;
        hs_rt_fault_reexec_path = argv[0];
        if (sq != NULL && lg != NULL && atoi(sq) == 14) {   /* we are the re-entered image */
            char buf[256], last[256] = { 0 }; int lines = 0; FILE *f = fopen(g_log, "r");
            if (f) { while (fgets(buf, sizeof buf, f)) { lines++; memcpy(last, buf, sizeof last); } fclose(f); }
            snprintf(buf, sizeof buf, "fault_ledger=%s", lg);
            printf("HS_RT_FAULT stage=14 %s reexec=1 seq=%s log_lines=%d ledger=%s exit_avoided=1\n",
                   (lines == 14 && strstr(last, "FAULT #14 HS_RT") && strstr(last, buf) && strstr(last, "exit() called in PID 1")) ? "PASS" : "FAIL", sq, lines, lg);
            fflush(stdout); hs_rt_fault_force_pid1 = 0; exit(0);
        }
        exit(5);   /* first image: exit() in PID 1 -> fault -> execve(argv[0]) */
    }
    switch (g_stage) {
    case 1: { static unsigned char blk[64]; unsigned char *volatile p = blk + 32; g_free(p); break; }          /* mem.c invalid free */
    case 2: g_n = (size_t)-1 / 2; qsort(v, g_n, 4, cmp_int); break;          /* qsort overflow */
    case 3: g_n = (size_t)1 << 45; qsort(v, g_n, 8, cmp_int); break;         /* qsort OOM */
    case 4: g_n = 32; __memcpy_chk(d, s, g_n, sizeof d); break;               /* fortify memcpy */
    case 5: g_n = 32; __memmove_chk(d, s, g_n, sizeof d); break;
    case 6: g_n = 32; __memset_chk(d, 0, g_n, sizeof d); break;
    case 7: g_n = 32; __strncpy_chk(d, s, g_n, sizeof d); break;
    case 8: g_n = 64; __snprintf_chk(d, g_n, 1, sizeof d, "%s", s); break;   /* stdio fortify */
    case 9: { g_n = 64; (void)!__fgets_chk(d, sizeof d, (int)g_n, stdin); break; }
    case 10: { g_n = 8; (void)!__fread_chk(d, sizeof d, g_n, 4, stdin); break; }
    case 11: { const char *volatile fmt = "%Q"; printf(fmt, 1); break; }     /* unsupported conversion */
    case 12: __stack_chk_fail(); break;
    case 13: exit(3);                                                          /* exit() in PID 1 */
    default: return 2;
    }
    printf("HS_RT_FAULT stage=%d FAIL fatal path returned\n", g_stage);
    return 1;
}
