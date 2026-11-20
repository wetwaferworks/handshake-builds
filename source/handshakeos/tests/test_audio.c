/* SPDX-License-Identifier: MIT */
/* v3-audio: resampler + graceful synth-only fallback (no hardware needed). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_count;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail_count++; \
    } \
} while (0)

static struct hs_audio g_a;
static int16_t g_in[548 * 2];
static int16_t g_out[2048 * 2];

int main(void)
{
    struct hs_audio *a = &g_a;
    unsigned long long total_in = 0, total_out = 0;
    int i, rc, max_step = 0;
    int16_t last = 0;
    int chan_bad = 0;
    int have_last = 0;

    /* 1. synth-only: missing devices never crash, writes are no-ops */
    rc = hs_audio_open_dev(a, "/nonexistent/controlC9", "/nonexistent/pcmC9D0p", 32731);
    CHECK(rc < 0);
    CHECK(!a->live);
    CHECK(strcmp(hs_audio_status(a), "SYNTH ONLY") == 0);
    CHECK(hs_audio_write(a, g_in, 548) == 0);
    CHECK(hs_audio_silence(a, 512) == 0);
    CHECK(hs_audio_set_volume(a, 150) == 0 && hs_audio_volume(a) == 100);
    CHECK(hs_audio_set_volume(a, -5) == 0 && hs_audio_volume(a) == 0);
    hs_audio_close(a);

    /* 2. resampler 32731 -> 48000: rate and continuity on a slow ramp */
    memset(a, 0, sizeof(*a));
    a->src_rate = 32731;
    a->rate = 48000;
    a->step = (uint32_t)(((uint64_t)a->src_rate << 16) / a->rate);
    for (i = 0; i < 600; i++) { /* ~10 s of Game Boy frames */
        int k;
        size_t n;
        for (k = 0; k < 548; k++) {
            int t = ((i * 548 + k) % 4000) - 2000;
            int v = (t < 0 ? -t : t) - 1000; /* triangle, slope 1 */
            g_in[k * 2] = (int16_t)v;
            g_in[k * 2 + 1] = (int16_t)(-v);
        }
        n = hs_audio_resample(a, g_in, 548, g_out, 2048);
        total_in += 548;
        total_out += n;
        for (k = 0; k < (int)n; k++) {
            if (have_last && i > 0) {
                int d = g_out[k * 2] - last;
                if (d < 0) {
                    d = -d;
                }
                if (d > max_step) {
                    max_step = d;
                }
            }
            last = g_out[k * 2];
            have_last = 1;
            if (g_out[k * 2] + g_out[k * 2 + 1] > 1 || g_out[k * 2] + g_out[k * 2 + 1] < -1) {
                chan_bad++; /* channels must stay independent mirror images */
            }
        }
    }
    {
        double ratio = (double)total_out / (double)total_in;
        double want = 48000.0 / 32731.0;
        printf("resample in=%llu out=%llu ratio=%.5f want=%.5f max_step=%d\n",
               total_in, total_out, ratio, want, max_step);
        CHECK(ratio > want * 0.9995 && ratio < want * 1.0005);
        CHECK(max_step <= 1);
        CHECK(chan_bad == 0);
        printf("channel mirror errors=%d\n", chan_bad); /* 0.68 src step per out sample -> |d| <= 1 */
    }

    if (fail_count == 0) {
        printf("ALL AUDIO TESTS PASSED\n");
        return 0;
    }
    printf("%d checks failed\n", fail_count);
    return 1;
}
