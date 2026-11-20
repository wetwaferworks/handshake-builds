/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hs_gb.h"
#include "hs_gbscale.h"
#include "hs_fb.h"

#include <stdio.h>
#include <string.h>

#ifndef ROM_PATH
#define ROM_PATH "roms/handshake-demo.gb"
#endif

#ifndef GB_FRAMES
#define GB_FRAMES 90
#endif

#ifndef GB_RUNS
#define GB_RUNS 100
#endif

static int fail_count;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail_count++; \
    } \
} while (0)

/* v3: 160x144 -> 640x480 full-screen scaler gate. */
static uint64_t fnv_rows(const struct hs_fb *fb)
{
    uint64_t h = 0xcbf29ce484222325ull;
    int y, x;

    for (y = 0; y < fb->h; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)fb->pix +
                                                 (size_t)y * (size_t)fb->stride_bytes);
        for (x = 0; x < fb->w; x++) {
            h = hs_fnv1a64(h, row[x]);
        }
    }
    return h;
}

static int scaler_gate(const char *png_out)
{
    static struct hs_gbscale sc;
    struct hs_fb fb;
    struct hs_gb *g;
    uint32_t pal[4];
    unsigned wsum[HS_GB_H];
    int y, x, bad = 0, zero_edges = 0, m;
    uint64_t h[2];

    hs_gbscale_init(&sc);
    /* Every output row's weights sum to 256, and every source row gets the
       same total height: 480/144 * 256 = 853.33 -> 853 or 854. */
    for (m = 0; m < HS_GBSCALE_COUNT; m++) {
        memset(wsum, 0, sizeof(wsum));
        for (y = 0; y < 480; y++) {
            const struct hs_gbscale_row *r = &sc.lut[m][y];
            if (r->w0 + r->w1 != 256u || r->y0 >= HS_GB_H || r->y1 >= HS_GB_H) {
                bad++;
            }
            wsum[r->y0] += r->w0;
            wsum[r->y1] += r->w1;
        }
        if (m == HS_GBSCALE_UPSAMPLED) {
            for (y = 0; y < HS_GB_H; y++) {
                if (wsum[y] < 852u || wsum[y] > 855u) {
                    bad++;
                }
            }
        }
    }
    if (sc.lut[0][0].y0 != 0 || sc.lut[0][479].y0 != 143) {
        bad++;
    }
    CHECK(bad == 0);

    if (hs_fb_open(&fb, "headless") != 0) {
        CHECK(0);
        return 1;
    }
    g = hs_gb_create();
    CHECK(g != NULL);
    CHECK(hs_gb_load_rom(g, ROM_PATH) == 0);
    hs_gb_set_joypad(g, 0);
    CHECK(hs_gb_start(g, 3) == 0);
    CHECK(hs_gb_run_frames(g, GB_FRAMES) == 0);
    hs_dmg_pal_get(HS_DMG_PAL_OLIVE, pal);
    for (m = 0; m < 2; m++) {
        sc.mode = m == 0 ? HS_GBSCALE_UPSAMPLED : HS_GBSCALE_NEAREST;
        hs_fb_clear(&fb, 0x000000u);
        hs_gbscale_blit(&sc, &fb, hs_gb_framebuffer(g), pal);
        for (x = 0; x < 640; x++) {
            if (fb.pix[x] == 0u || fb.pix[(size_t)479 * 640u + (size_t)x] == 0u) {
                zero_edges++;
            }
        }
        for (y = 0; y < 480; y++) {
            if (fb.pix[(size_t)y * 640u] == 0u || fb.pix[(size_t)y * 640u + 639u] == 0u) {
                zero_edges++;
            }
        }
        h[m] = fnv_rows(&fb);
    }
    CHECK(zero_edges == 0);
    printf("scaler 640x480 upsampled=%016llx nearest=%016llx lut_bad=%d zero_edge_px=%d\n",
           (unsigned long long)h[0], (unsigned long long)h[1], bad, zero_edges);
    if (png_out != NULL) {
        sc.mode = HS_GBSCALE_UPSAMPLED;
        hs_gbscale_blit(&sc, &fb, hs_gb_framebuffer(g), pal);
        CHECK(hs_fb_snapshot_png(&fb, png_out) == 0);
        printf("wrote %s\n", png_out);
    }
    hs_gb_stop(g);
    hs_gb_destroy(g);
    hs_fb_close(&fb);
    return (bad == 0 && zero_edges == 0) ? 0 : 1;
}


int main(int argc, char **argv)
{
    struct hs_gb_stats first, st;
    int i, w, rc;

    memset(&first, 0, sizeof(first));
    printf("handshake-gb-test  rom=%s  frames=%d  runs=%d\n",
           ROM_PATH, GB_FRAMES, GB_RUNS);

    for (i = 0; i < GB_RUNS; i++) {
        rc = hs_gb_scoreboard(ROM_PATH, 3, GB_FRAMES, &st);
        CHECK(rc == 0);
        if (rc != 0) {
            break;
        }
        CHECK(st.xruns == 0);
        CHECK(st.overruns == 0);
        CHECK(st.frames == (uint64_t)GB_FRAMES);
        CHECK(st.epoch == (uint64_t)GB_FRAMES);
        if (i == 0) {
            first = st;
            printf("golden ledger=%016llx frame=%016llx audio=%016llx "
                   "epoch=%llu xruns=%llu overruns=%llu\n",
                   (unsigned long long)st.ledger_hash,
                   (unsigned long long)st.frame_crc,
                   (unsigned long long)st.audio_crc,
                   (unsigned long long)st.epoch,
                   (unsigned long long)st.xruns,
                   (unsigned long long)st.overruns);
        } else {
            CHECK(st.ledger_hash == first.ledger_hash);
            CHECK(st.frame_crc == first.frame_crc);
            CHECK(st.audio_crc == first.audio_crc);
        }
    }
    printf("%d/%d identical ledger+frame+audio\n", GB_RUNS, GB_RUNS);

    printf("fairness:");
    for (w = 1; w <= 4; w++) {
        rc = hs_gb_scoreboard(ROM_PATH, w, GB_FRAMES, &st);
        CHECK(rc == 0);
        CHECK(st.ledger_hash == first.ledger_hash);
        CHECK(st.frame_crc == first.frame_crc);
        CHECK(st.audio_crc == first.audio_crc);
        CHECK(st.xruns == 0);
        CHECK(st.overruns == 0);
        printf("  %d=%s", w, (st.ledger_hash == first.ledger_hash) ? "ok" : "FAIL");
    }
    printf("\n");

    if (scaler_gate(argc > 1 ? argv[1] : NULL) == 0) {
        printf("SCALER 640x480 EDGE-TO-EDGE PASS\n");
    }

    if (fail_count == 0) {
        printf("ALL GB TESTS PASSED\n");
        return 0;
    }
    printf("%d checks failed\n", fail_count);
    return 1;
}
