/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * Regression tests for the hs_rt review fixes (REVIEW HS-M2..HS-L6). Linked against hs_rt only
 * (-nostdlib, like the shipped binaries) and run under qemu-aarch64. Pass = "HS_RT_REVIEW PASS".
 *   M2  aligned_alloc: n + align + header overflow -> NULL, never a short block
 *   M3  qsort: n * size overflow traps instead of a short scratch buffer (checked by inspection;
 *       the normal path is exercised here)
 *   L1  fread/fwrite: size * nmemb overflow -> 0 items, destination untouched
 *   L2  setup_buf reads st_mode / st_blksize via memcpy (round trip through a buffered FILE)
 *   L3  pthread_create fails cleanly if the guard page cannot be set (normal path exercised) */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
/* Opaque sizes and entry points: keep the compiler from folding the overflow cases at build time
   (GCC -Walloc-size-larger-than and _FORTIFY_SOURCE would reject the constants). */
static volatile size_t g_huge_al = (size_t)1 << 62, g_huge_n = (size_t)-1 - 64, g_m40 = (size_t)1 << 40;
static volatile size_t g_r62 = (size_t)1 << 62, g_w63 = (size_t)1 << 63;
static size_t (*volatile g_fread)(void *, size_t, size_t, FILE *) = fread;
static size_t (*volatile g_fwrite)(const void *, size_t, size_t, FILE *) = fwrite;
#define CHECK(c, what) do { if (c) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); fails++; } } while (0)

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }
static void *thr(void *p) { *(int *)p = 7; return p; }

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "hs_rt_review.tmp";
    unsigned char buf[64], probe[64];
    int v[9] = { 5, 3, 9, 1, 7, 2, 8, 6, 4 }, i, sorted = 1, tv = 0;
    void *p;
    FILE *f;
    pthread_t th;

    p = aligned_alloc(g_huge_al, 64);
    CHECK(p == NULL, "aligned_alloc(2^62, 64) -> NULL");
    p = aligned_alloc(4096, g_huge_n);
    CHECK(p == NULL, "aligned_alloc(4096, SIZE_MAX-64) -> NULL");
    p = aligned_alloc(256, 1000);
    CHECK(p != NULL && ((uintptr_t)p & 255u) == 0, "aligned_alloc(256, 1000) aligned");
    if (p) { memset(p, 0xA5, 1000); free(p); }

    qsort(v, 9, sizeof(int), cmp_int);
    for (i = 1; i < 9; i++) if (v[i - 1] > v[i]) sorted = 0;
    CHECK(sorted, "qsort sorts");

    f = fopen(path, "w");
    CHECK(f != NULL, "fopen w");
    if (f) {
        CHECK(g_fwrite(buf, g_w63, 4, f) == 0, "fwrite overflow -> 0");
        CHECK(fwrite("handshake", 1, 9, f) == 9, "fwrite 9 bytes");
        fclose(f);
    }
    f = fopen(path, "r");
    CHECK(f != NULL, "fopen r");
    if (f) {
        memset(probe, 0x5A, sizeof probe);
        memcpy(buf, probe, sizeof buf);
        CHECK(g_fread(buf, g_r62, 8, f) == 0 && memcmp(buf, probe, sizeof buf) == 0,
              "fread overflow -> 0, dst untouched");
        CHECK(fread(buf, 1, 9, f) == 9 && memcmp(buf, "handshake", 9) == 0, "fread round trip");
        fclose(f);
    }
    unlink(path);

    CHECK(calloc(g_m40, g_m40) == NULL, "calloc overflow -> NULL");
    CHECK(pthread_create(&th, NULL, thr, &tv) == 0 && pthread_join(th, NULL) == 0 && tv == 7, "pthread_create/join");

    printf("HS_RT_REVIEW %s fails=%d\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
