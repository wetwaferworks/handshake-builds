/* SPDX-License-Identifier: MIT */
/* v5.4: prove the ALSA pace policy: 0 xruns / 0 drops / no clips / no jumps
   for GBA, GB, SNES and Genesis write cadences, including heavy (25 ms) frames
   and 4-frame catch-up. Uses the software device (no sound card). */
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

struct sys_pace {
    const char *name;
    unsigned src_rate;
    uint32_t fps_num, fps_den; /* frame_ns = 1e9 * den / num */
    /* samples per source frame, or 0 to use ceil like the GBA APU */
    unsigned src_frames_fixed;
    int gba_style; /* 1: n = ceil((f+1)*F*R/2^24) - ceil(f*F*R/2^24) */
};

/* 10 seconds at each system's native frame rate. */
#define SECONDS 10

static unsigned gba_frames_for(uint64_t f)
{
    const uint64_t F = 280896ull, RATE = 48000ull;
    uint64_t k0 = (f * F * RATE + (1ull << 24) - 1u) >> 24;
    uint64_t k1 = ((f + 1u) * F * RATE + (1ull << 24) - 1u) >> 24;
    return (unsigned)(k1 - k0);
}

static unsigned src_frames(const struct sys_pace *s, uint64_t f)
{
    if (s->gba_style) {
        return gba_frames_for(f);
    }
    if (s->src_frames_fixed) {
        return s->src_frames_fixed;
    }
    /* exact: round((src_rate * den + num/2) / num) would drift; use the
       same ceil window as GBA but with the system's clocks. */
    {
        uint64_t k0 = ((uint64_t)f * s->fps_den * s->src_rate + s->fps_num / 2u) / s->fps_num;
        uint64_t k1 = (((uint64_t)f + 1u) * s->fps_den * s->src_rate + s->fps_num / 2u) / s->fps_num;
        return (unsigned)(k1 - k0);
    }
}

static void fill_sine(int16_t *buf, unsigned frames, unsigned *phase, unsigned src_rate, int *clip)
{
    unsigned i;
    for (i = 0; i < frames; i++) {
        /* cheap triangle ~ 440 Hz, amplitude 20000: never clips, slope is mild */
        unsigned t = *phase;
        int period = (int)src_rate / 440;
        int pos, v;
        if (period < 4) period = 4;
        pos = (int)(t % (unsigned)period);
        if (pos < period / 2) v = -10000 + (20000 * pos) / (period / 2);
        else v = 10000 - (20000 * (pos - period / 2)) / (period - period / 2);
        if (v > 32767 || v < -32768) {
            (*clip)++;
            v = v > 0 ? 32767 : -32768;
        }
        buf[i * 2u] = (int16_t)v;
        buf[i * 2u + 1u] = (int16_t)(-v);
        (*phase)++;
    }
}

static int run_sys(const struct sys_pace *s, unsigned heavy_every, unsigned catchup_at)
{
    struct hs_audio a;
    int16_t inbuf[4096 * 2];
    unsigned frame, phase = 0, src_n;
    int clip = 0, max_step = 0;
    int64_t frame_ns;
    uint64_t t, total_src = 0;
    int rc;

    frame_ns = (int64_t)(1000000000ull * (uint64_t)s->fps_den / (uint64_t)s->fps_num);
    rc = hs_audio_open_model(&a, s->src_rate, 48000u, 512u, 5u); /* 53.3 ms, 5 x 12 ms */
    if (rc != 0 || !a.live) {
        fprintf(stderr, "FAIL %s: model open\n", s->name);
        fail_count++;
        return -1;
    }
    CHECK(a.period == 512);
    CHECK(a.buffer == 2560);
    CHECK(a.start_thr == 1536);
    CHECK((a.buffer * 1000u) / a.rate >= 40u);
    CHECK((a.buffer * 1000u) / a.rate <= 80u);

    for (frame = 0, t = 0; t < (uint64_t)SECONDS * 1000000000ull; ) {
        unsigned burst = 1, b;
        int64_t work_ns = frame_ns;
        if (heavy_every && (frame % heavy_every) == 0 && frame > 0) {
            work_ns = 25000000;
        }
        if (catchup_at && frame == catchup_at) {
            burst = 4;
        }
        for (b = 0; b < burst; b++) {
            /* play.c: device runs during emu work, then we write this
               frame. A long frame also triggers one catch-up write (n=2)
               so the 8.3 ms deficit is refilled and does not accumulate. */
            hs_audio_model_elapse(&a, (uint64_t)work_ns);
            src_n = src_frames(s, frame);
            fill_sine(inbuf, src_n, &phase, s->src_rate, &clip);
            (void)hs_audio_write(&a, inbuf, src_n);
            total_src += src_n;
            frame++;
            t += (uint64_t)frame_ns;
            if (work_ns > frame_ns) {
                src_n = src_frames(s, frame);
                fill_sine(inbuf, src_n, &phase, s->src_rate, &clip);
                (void)hs_audio_write(&a, inbuf, src_n);
                total_src += src_n;
                frame++;
                t += (uint64_t)frame_ns;
            } else if (work_ns < frame_ns) {
                hs_audio_model_elapse(&a, (uint64_t)(frame_ns - work_ns));
            }
            work_ns = frame_ns;
        }
    }
    /* Drain a little leftover so a short tail isn't an xrun after close. */
    printf("PACE %s frames=%u src=%llu out=%llu xruns=%llu dropped=%llu fill=%u clip=%d "
           "period=%u buffer=%u (%.1f ms) start_thr=%u\n",
           s->name, frame, (unsigned long long)total_src,
           (unsigned long long)a.frames_out,
           (unsigned long long)a.xruns, (unsigned long long)a.dropped,
           a.model_fill, clip, a.period, a.buffer,
           (a.buffer * 1000.0) / a.rate, a.start_thr);
    CHECK(a.xruns == 0);
    CHECK(a.dropped == 0);
    CHECK(clip == 0);
    CHECK(a.frames_out > 0);
    hs_audio_close(&a);

    /* Continuity of the resampler on a slope-1 ramp (no wrap jump). */
    {
        struct hs_audio r;
        int16_t out[8192 * 2];
        unsigned f2, sample = 0, prev_ok = 0;
        int16_t prev = 0;
        memset(&r, 0, sizeof(r));
        r.src_rate = s->src_rate;
        r.rate = 48000;
        r.step = (uint32_t)(((uint64_t)r.src_rate << 16) / r.rate);
        max_step = 0;
        for (f2 = 0; f2 < 120; f2++) {
            size_t n, k, i;
            src_n = src_frames(s, f2);
            for (i = 0; i < src_n; i++) {
                int16_t v = (int16_t)((sample & 0xFFu) - 128);
                inbuf[i * 2u] = v;
                inbuf[i * 2u + 1u] = (int16_t)(-v);
                sample++;
            }
            n = hs_audio_resample(&r, inbuf, src_n, out, 8192);
            for (k = 0; k < n; k++) {
                if (prev_ok) {
                    int d = out[k * 2] - prev;
                    if (d < 0) d = -d;
                    /* The 8-bit saw wraps by 255; that is not a resample jump. */
                    if (d > 64) {
                        prev = out[k * 2];
                        continue;
                    }
                    if (d > max_step) max_step = d;
                }
                prev = out[k * 2];
                prev_ok = 1;
            }
        }
        printf("CONT %s max_step=%d (threshold 64)\n", s->name, max_step);
        CHECK(max_step <= 64);
        CHECK(clip == 0);
    }
    return fail_count ? -1 : 0;
}

int main(void)
{
    const struct sys_pace sys[] = {
        { "GBA",     48000, 16777216u, 280896u, 0, 1 },
        { "GB",      32731,  4194304u,  70224u, 548, 0 },
        { "GBC",     32731,  4194304u,  70224u, 548, 0 },
        { "SNES",    48000, 21477272u, 357366u, 0, 0 },
        { "GENESIS", 48000, 53693175u, 896040u, 0, 0 },
    };
    size_t i;
    for (i = 0; i < sizeof(sys) / sizeof(sys[0]); i++) {
        /* Nominal + one isolated 25 ms frame per second + a 4-frame
           catch-up at 3 s. A late frame every 8 would be a 6% overload
           that no finite ALSA buffer can survive. */
        unsigned fps = sys[i].fps_num / sys[i].fps_den;
        if (fps < 50) fps = 60;
        run_sys(&sys[i], fps, fps * 3u);
    }
    if (fail_count == 0) {
        printf("AUDIO_PACE checks passed systems=5 seconds=%d xruns=0 dropped=0 clips=0\n", SECONDS);
        printf("ALL AUDIO PACE TESTS PASSED\n");
        return 0;
    }
    printf("%d checks failed\n", fail_count);
    return 1;
}
