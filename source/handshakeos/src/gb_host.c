/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hs_fb.h"
#include "hs_gb.h"
#include "hs_theme.h"
#include "hs_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --rom=PATH.gb           default roms/handshake-demo.gb\n"
            "  --frames=N              default 90\n"
            "  --workers=N             default 3\n"
            "  --snapshot=PATH.png     Deep Purple shell + live GB frame\n"
            "  --scoreboard            100-run + 1/2/3/4 worker fairness\n"
            "  --runs=N                scoreboard runs, default 100\n",
            argv0);
}

static int render_gb_snapshot(const char *path, const struct hs_gb *g,
                              const struct hs_gb_stats *st)
{
    struct hs_fb fb;
    const struct hs_palette *pal;
    const uint32_t gbp[4] = {
        0x00f4e8ffu, /* light lilac */
        0x00c084e0u,
        0x00703aa8u,
        0x001a0828u  /* deep purple */
    };
    const int scale = 2;
    const int gw = HS_GB_W * scale;
    const int gh = HS_GB_H * scale;
    int gx, gy;
    hs_beat_t beat;
    int frozen;

    if (hs_fb_open(&fb, "headless") != 0) {
        fprintf(stderr, "headless fb open failed\n");
        return 1;
    }
    pal = hs_theme_get(HS_THEME_PURPLE);
    hs_fb_clear(&fb, pal->bg);

    hs_fb_fill(&fb, 0, 0, fb.w, 40, pal->bar);
    hs_fb_fill(&fb, 0, 38, fb.w, 2, pal->accent);
    hs_fb_text(&fb, 12, 12, 2, pal->accent2, "HandshakeOS");
    hs_fb_printf(&fb, 200, 16, 1, pal->dim, "Deep Purple  +  Peanut-GB");
    hs_fb_printf(&fb, 500, 16, 1, pal->text, "DMG");

    /* Left status rail */
    hs_fb_fill_round(&fb, 12, 56, 200, 392, 8, pal->tile);
    hs_fb_rect(&fb, 12, 56, 200, 392, 1, pal->border);
    hs_fb_text(&fb, 24, 68, 1, pal->dim, "HANDSHAKE");
    hs_fb_text(&fb, 24, 84, 1, pal->text, "3+1 seating");
    hs_fb_text(&fb, 24, 112, 1, pal->dim, "EPOCH");
    hs_fb_printf(&fb, 24, 126, 2, pal->value, "%llu",
                    (unsigned long long)st->epoch);
    hs_fb_text(&fb, 24, 160, 1, pal->dim, "LEDGER");
    hs_fb_printf(&fb, 24, 174, 1, pal->accent2, "%016llx",
                    (unsigned long long)st->ledger_hash);
    hs_fb_text(&fb, 24, 200, 1, pal->dim, "FRAME CRC");
    hs_fb_printf(&fb, 24, 214, 1, pal->text, "%016llx",
                    (unsigned long long)st->frame_crc);
    hs_fb_text(&fb, 24, 240, 1, pal->dim, "AUDIO CRC");
    hs_fb_printf(&fb, 24, 254, 1, pal->text, "%016llx",
                    (unsigned long long)st->audio_crc);
    hs_fb_text(&fb, 24, 280, 1, pal->dim, "AUDIO XRUNS");
    hs_fb_printf(&fb, 24, 294, 2, st->xruns ? pal->warn : pal->good, "%llu",
                    (unsigned long long)st->xruns);
    hs_fb_text(&fb, 24, 330, 1, pal->dim, "OVERRUNS");
    hs_fb_printf(&fb, 24, 344, 2, st->overruns ? pal->warn : pal->good, "%llu",
                    (unsigned long long)st->overruns);
    hs_fb_text(&fb, 24, 380, 1, pal->dim, "WATCHDOG");
    hs_fb_text(&fb, 24, 394, 1, pal->good, "OK");
    hs_fb_printf(&fb, 24, 416, 1, pal->dim, "frames %llu",
                    (unsigned long long)st->frames);

    /* GB bezel, scale 2 = 320x288, sits to the right of the rail. */
    gx = 236;
    gy = 72;
    hs_fb_fill_round(&fb, gx - 16, gy - 24, gw + 32, gh + 56, 10, pal->surface);
    hs_fb_rect(&fb, gx - 16, gy - 24, gw + 32, gh + 56, 2, pal->accent);
    hs_fb_text(&fb, gx, gy - 18, 1, pal->dim, "HandshakeOS demo (MIT, original)");
    hs_gb_blit(&fb, gx, gy, scale, hs_gb_framebuffer(g), gbp);
    hs_fb_printf(&fb, gx, gy + gh + 8, 1, pal->text,
                    "xruns %llu   epoch %llu",
                    (unsigned long long)st->xruns,
                    (unsigned long long)st->epoch);

    /* Live Gemstone dial, lower-left of the bezel's right margin. */
    beat = HS_BEAT_COMMIT;
    frozen = 0;
    hs_ui_draw_gemstone(&fb, 560, 400, 48, pal, beat, frozen);

    hs_fb_fill(&fb, 0, fb.h - 28, fb.w, 28, pal->bar);
    hs_fb_printf(&fb, 12, fb.h - 20, 1, pal->dim,
                    "phase-boundary PCM  +  hash-chained ledger  +  zero mutexes");

    hs_fb_flip(&fb);
    if (hs_fb_snapshot_png(&fb, path) != 0) {
        fprintf(stderr, "snapshot %s failed\n", path);
        hs_fb_close(&fb);
        return 1;
    }
    fprintf(stderr, "wrote %s\n", path);
    hs_fb_close(&fb);
    return 0;
}

static int run_once(const char *rom, int workers, uint64_t frames,
                    struct hs_gb **keep, struct hs_gb_stats *st)
{
    struct hs_gb *g = hs_gb_create();
    uint64_t t;
    int rc;

    if (g == NULL) {
        return -1;
    }
    rc = hs_gb_load_rom(g, rom);
    if (rc != 0) {
        fprintf(stderr, "load %s failed: %d\n", rom, rc);
        hs_gb_destroy(g);
        return rc;
    }
    (void)hs_gb_script(g, 0, 0);
    for (t = 20; t < 40 && t < frames; t++) {
        (void)hs_gb_script(g, t, 0x10);
    }
    for (t = 40; t < 60 && t < frames; t++) {
        (void)hs_gb_script(g, t, 0x11);
    }
    for (t = 60; t < frames; t++) {
        (void)hs_gb_script(g, t, 0x80);
    }
    rc = hs_gb_start(g, workers);
    if (rc != 0) {
        fprintf(stderr, "start failed: %d\n", rc);
        hs_gb_destroy(g);
        return rc;
    }
    rc = hs_gb_run_frames(g, frames);
    hs_gb_stats(g, st);
    if (keep != NULL) {
        *keep = g;
        return rc;
    }
    hs_gb_stop(g);
    hs_gb_destroy(g);
    return rc;
}

static int run_scoreboard(const char *rom, uint64_t frames, int runs)
{
    struct hs_gb_stats first, st;
    int workers, i, rc;
    int fails = 0;

    memset(&first, 0, sizeof(first));
    printf("GB scoreboard  rom=%s  frames=%llu  runs=%d\n",
           rom, (unsigned long long)frames, runs);

    for (i = 0; i < runs; i++) {
        rc = run_once(rom, 3, frames, NULL, &st);
        if (rc != 0) {
            fprintf(stderr, "run %d failed rc=%d\n", i, rc);
            return 1;
        }
        if (i == 0) {
            first = st;
            printf("  golden  ledger=%016llx  frame=%016llx  audio=%016llx"
                   "  epoch=%llu  xruns=%llu  overruns=%llu\n",
                   (unsigned long long)st.ledger_hash,
                   (unsigned long long)st.frame_crc,
                   (unsigned long long)st.audio_crc,
                   (unsigned long long)st.epoch,
                   (unsigned long long)st.xruns,
                   (unsigned long long)st.overruns);
        } else if (st.ledger_hash != first.ledger_hash
                   || st.frame_crc != first.frame_crc
                   || st.audio_crc != first.audio_crc
                   || st.xruns != 0 || st.overruns != 0) {
            fprintf(stderr, "MISMATCH run %d ledger=%016llx frame=%016llx "
                            "audio=%016llx xruns=%llu overruns=%llu\n",
                    i,
                    (unsigned long long)st.ledger_hash,
                    (unsigned long long)st.frame_crc,
                    (unsigned long long)st.audio_crc,
                    (unsigned long long)st.xruns,
                    (unsigned long long)st.overruns);
            fails++;
        }
    }
    printf("  %d/%d identical ledger+frame+audio  xruns=%llu  overruns=%llu\n",
           runs - fails, runs,
           (unsigned long long)first.xruns,
           (unsigned long long)first.overruns);

    printf("  fairness:");
    for (workers = 1; workers <= 4; workers++) {
        rc = run_once(rom, workers, frames, NULL, &st);
        if (rc != 0 || st.ledger_hash != first.ledger_hash
            || st.frame_crc != first.frame_crc
            || st.audio_crc != first.audio_crc
            || st.xruns != 0 || st.overruns != 0) {
            printf("  %d=FAIL(%016llx xrun=%llu ov=%llu)", workers,
                   (unsigned long long)st.ledger_hash,
                   (unsigned long long)st.xruns,
                   (unsigned long long)st.overruns);
            fails++;
        } else {
            printf("  %d=ok", workers);
        }
    }
    printf("\n");
    return fails == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *rom = "roms/handshake-demo.gb";
    const char *snapshot = NULL;
    int workers = 3;
    int scoreboard = 0;
    int runs = 100;
    uint64_t frames = 90;
    int i, rc;
    struct hs_gb *kept = NULL;
    struct hs_gb_stats st;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--rom=", 6) == 0) {
            rom = argv[i] + 6;
        } else if (strncmp(argv[i], "--frames=", 9) == 0) {
            frames = (uint64_t)strtoull(argv[i] + 9, NULL, 10);
        } else if (strncmp(argv[i], "--workers=", 10) == 0) {
            workers = atoi(argv[i] + 10);
        } else if (strncmp(argv[i], "--snapshot=", 11) == 0) {
            snapshot = argv[i] + 11;
        } else if (strcmp(argv[i], "--scoreboard") == 0) {
            scoreboard = 1;
        } else if (strncmp(argv[i], "--runs=", 7) == 0) {
            runs = atoi(argv[i] + 7);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (scoreboard) {
        return run_scoreboard(rom, frames, runs);
    }

    memset(&st, 0, sizeof(st));
    rc = run_once(rom, workers, frames, snapshot ? &kept : NULL, &st);
    if (rc != 0) {
        return 1;
    }
    printf("ledger %016llx  frame %016llx  audio %016llx  epoch %llu  "
           "xruns %llu  overruns %llu  frames %llu\n",
           (unsigned long long)st.ledger_hash,
           (unsigned long long)st.frame_crc,
           (unsigned long long)st.audio_crc,
           (unsigned long long)st.epoch,
           (unsigned long long)st.xruns,
           (unsigned long long)st.overruns,
           (unsigned long long)st.frames);
    if (snapshot) {
        rc = render_gb_snapshot(snapshot, kept, &st);
        hs_gb_stop(kept);
        hs_gb_destroy(kept);
        return rc;
    }
    return 0;
}
