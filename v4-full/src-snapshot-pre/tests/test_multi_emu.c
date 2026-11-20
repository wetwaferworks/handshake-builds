/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-multi gate: boots GB, GBC, GBA, SNES (Mode 1 + Mode 7)
   and Genesis demo ROMs through the 7-system registry and the real
   zero-mutex Timing Handshake phase loop (3 worker shards), then checks:
   full 640x480 fill with no black bars, non-zero audio through the
   lock-free PCM ring and the src/audio.c resampler, rerun determinism
   with 3 and 1 workers, registry extension routing, the unlinked-core
   path, and an atomic .sav round trip. Prints MULTI lines whose hashes
   the gate compares across x86_64 and ARM64. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "emu_sys.h"
#include "hs_audio.h"
#include "hs_emusave.h"
#include "hs_png.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail_count;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail_count++; \
    } \
} while (0)

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *b;
    long len;

    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    b = (uint8_t *)malloc((size_t)len + 1u);
    if (b == NULL || fread(b, 1, (size_t)len, fp) != (size_t)len) {
        free(b);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *n = (size_t)len;
    return b;
}

struct run_result {
    uint64_t ledger, video, audio, ring_hash;
    uint64_t pcm_frames, pcm_nonzero, ring_frames, ring_nonzero, resampled, dropped, freezes;
    uint64_t edge_black, black_rows, black_cols, colours;
};

/* Deterministic button script: idle, then D-pad / face buttons. */
static uint32_t script_pad(uint32_t f)
{
    uint32_t ph = (f / 15u) % 6u;
    static const uint32_t k[6] = { 0, EMU_PAD_RIGHT, EMU_PAD_A, EMU_PAD_DOWN | EMU_PAD_B,
                                   EMU_PAD_LEFT, EMU_PAD_START };
    return f < 30u ? 0u : k[ph];
}

static int16_t g_pcm[EMU_PCM_RING * 2];
static int16_t g_rs[EMU_PCM_RING * 2];
static struct hs_audio g_aud;

static int run_sys(emu_sys_t sys, const uint8_t *rom, size_t len, uint32_t frames, int workers,
                   int nearest, const char *png, struct run_result *r)
{
    int err = 0;
    struct emu *e = emu_create(sys, rom, len, workers, &err);
    struct emu_stats st;
    const uint32_t *fb;
    uint32_t f;
    int x, y;
    uint64_t rh = 0xcbf29ce484222325ull;

    memset(r, 0, sizeof(*r));
    if (e == NULL) {
        fprintf(stderr, "emu_create(%s) failed: %d\n", emu_sys_info(sys)->tag, err);
        return -1;
    }
    emu_set_nearest(e, nearest);
    memset(&g_aud, 0, sizeof(g_aud));
    g_aud.src_rate = EMU_PCM_RATE;
    g_aud.rate = 48000;
    g_aud.step = (uint32_t)(((uint64_t)g_aud.src_rate << 16) / g_aud.rate);
    for (f = 0; f < frames; f++) {
        size_t n, i;
        if (emu_run_frames(e, 1, script_pad(f), 0) != 0) {
            fprintf(stderr, "run_frames failed at %u\n", f);
            emu_destroy(e);
            return -1;
        }
        /* shell side of the lock-free ring -> speaker resampler */
        n = emu_pcm_take(e, g_pcm, EMU_PCM_RING);
        r->ring_frames += n;
        for (i = 0; i < n * 2u; i++) {
            r->ring_nonzero += g_pcm[i] != 0;
            rh = (rh ^ (uint16_t)g_pcm[i]) * 0x100000001b3ull;
        }
        r->resampled += hs_audio_resample(&g_aud, g_pcm, n, g_rs, EMU_PCM_RING);
    }
    emu_stats(e, &st);
    r->ledger = st.ledger;
    r->video = st.video_hash;
    r->audio = st.audio_hash;
    r->ring_hash = rh;
    r->pcm_frames = st.pcm_frames;
    r->pcm_nonzero = st.pcm_nonzero;
    r->dropped = st.pcm_dropped;
    r->freezes = st.freezes;
    fb = emu_framebuffer(e);
    /* black-bar audit: every edge pixel, plus whole black rows/cols */
    for (x = 0; x < EMU_FB_W; x++) {
        r->edge_black += (fb[x] & 0xFFFFFFu) == 0;
        r->edge_black += (fb[(EMU_FB_H - 1) * EMU_FB_W + x] & 0xFFFFFFu) == 0;
    }
    for (y = 1; y < EMU_FB_H - 1; y++) {
        r->edge_black += (fb[y * EMU_FB_W] & 0xFFFFFFu) == 0;
        r->edge_black += (fb[y * EMU_FB_W + EMU_FB_W - 1] & 0xFFFFFFu) == 0;
    }
    for (y = 0; y < EMU_FB_H; y++) {
        int blk = 1;
        for (x = 0; x < EMU_FB_W && blk; x++) {
            blk = (fb[y * EMU_FB_W + x] & 0xFFFFFFu) == 0;
        }
        r->black_rows += (uint64_t)blk;
    }
    for (x = 0; x < EMU_FB_W; x++) {
        int blk = 1;
        for (y = 0; y < EMU_FB_H && blk; y++) {
            blk = (fb[y * EMU_FB_W + x] & 0xFFFFFFu) == 0;
        }
        r->black_cols += (uint64_t)blk;
    }
    {
        /* distinct colours on a 16-px grid (cheap "not blank" check) */
        uint32_t seen[512];
        int ns = 0, k;
        for (y = 0; y < EMU_FB_H; y += 16) {
            for (x = 0; x < EMU_FB_W; x += 16) {
                uint32_t c = fb[y * EMU_FB_W + x] & 0xFFFFFFu;
                for (k = 0; k < ns && seen[k] != c; k++) {
                }
                if (k == ns && ns < 512) {
                    seen[ns++] = c;
                }
            }
        }
        r->colours = (uint64_t)ns;
    }
    if (png != NULL) {
        CHECK(hs_write_png(png, fb, EMU_FB_W, EMU_FB_H, EMU_FB_W * 4) == 0);
    }
    emu_destroy(e);
    return 0;
}

struct demo {
    emu_sys_t   sys;
    const char *label;
    const char *file;
    const char *png;
    int         need_audio; /* the Mode 7 floor demo is video-only */
};

int main(int argc, char **argv)
{
    static const struct demo k_demo[] = {
        { EMU_SYS_GB,      "GB",         "handshake-demo.gb",  "v4-multi-gb-640x480.png", 1 },
        { EMU_SYS_GBC,     "GBC",        "hs_cgb_test.gbc",    "v4-multi-gbc-640x480.png", 1 },
        { EMU_SYS_GBA,     "GBA",        "hs_gba_demo.gba",    "v4-multi-gba-640x480.png", 1 },
        { EMU_SYS_SNES,    "SNES",       "hs_mode1_demo.sfc",  "v4-multi-snes-640x480.png", 1 },
        { EMU_SYS_SNES,    "SNES-MODE7", "hs_mode7_floor.sfc", "v4-multi-snes-mode7-640x480.png", 0 },
        { EMU_SYS_GENESIS, "GENESIS",    "hs_genesis_demo.md", "v4-multi-genesis-640x480.png", 1 },
    };
    const char *roms = "roms", *out = NULL;
    uint32_t frames = 90;
    size_t d;
    int i, systems_ok = 0;
    uint64_t chain = 0xcbf29ce484222325ull;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--roms=", 7) == 0) {
            roms = argv[i] + 7;
        } else if (strncmp(argv[i], "--out=", 6) == 0) {
            out = argv[i] + 6;
        } else if (strncmp(argv[i], "--frames=", 9) == 0) {
            frames = (uint32_t)atoi(argv[i] + 9);
        }
    }
    CHECK(frames >= 60);

    /* ---- registry ---- */
    CHECK(EMU_SYS_COUNT == 7);
    CHECK(emu_sys_count_linked() == 5);
    CHECK(emu_sys_from_path("/r/Zelda.GB", NULL) == EMU_SYS_GB);
    CHECK(emu_sys_from_path("/r/a.gbc", NULL) == EMU_SYS_GBC);
    CHECK(emu_sys_from_path("/r/a.gb", "GBC") == EMU_SYS_GBC);
    CHECK(emu_sys_from_path("/r/a.GBA", NULL) == EMU_SYS_GBA);
    CHECK(emu_sys_from_path("/r/a.sfc", NULL) == EMU_SYS_SNES);
    CHECK(emu_sys_from_path("/r/a.smc", NULL) == EMU_SYS_SNES);
    CHECK(emu_sys_from_path("/r/a.md", NULL) == EMU_SYS_GENESIS);
    CHECK(emu_sys_from_path("/r/a.gen", NULL) == EMU_SYS_GENESIS);
    CHECK(emu_sys_from_path("/r/a.bin", NULL) == EMU_SYS_GENESIS);
    CHECK(emu_sys_from_path("/r/a.bin", "GENESIS") == EMU_SYS_GENESIS);
    CHECK(emu_sys_from_path("/r/a.bin", "PSX") == EMU_SYS_PSX);
    CHECK(emu_sys_from_path("/r/a.cue", NULL) == EMU_SYS_PSX);
    CHECK(emu_sys_from_path("/r/a.chd", NULL) == EMU_SYS_PSX);
    CHECK(emu_sys_from_path("/r/a.exe", NULL) == EMU_SYS_PSX);
    CHECK(emu_sys_from_path("/r/a.z64", NULL) == EMU_SYS_N64);
    CHECK(emu_sys_from_path("/r/a.n64", NULL) == EMU_SYS_N64);
    CHECK(emu_sys_from_path("/r/a.v64", NULL) == EMU_SYS_N64);
    CHECK(emu_sys_from_path("/r/a.txt", NULL) == EMU_SYS_NONE);
    CHECK(emu_sys_from_path("/r/noext", NULL) == EMU_SYS_NONE);
    CHECK(!emu_sys_linked(EMU_SYS_PSX) && !emu_sys_linked(EMU_SYS_N64));
    CHECK(strcmp(emu_sys_unlinked_notice(), "Core coming in v4-full") == 0);
    {
        uint8_t dummy[1024];
        int err = 0;
        memset(dummy, 0, sizeof(dummy));
        CHECK(emu_create(EMU_SYS_PSX, dummy, sizeof(dummy), 3, &err) == NULL && err == -ENOSYS);
        CHECK(emu_create(EMU_SYS_N64, dummy, sizeof(dummy), 3, &err) == NULL && err == -ENOSYS);
    }
    printf("REGISTRY 7 slots, %d linked (GB GBC GBA SNES GEN), PSX/N64 -> \"%s\"\n",
           emu_sys_count_linked(), emu_sys_unlinked_notice());

    /* ---- every demo through the phase loop ---- */
    for (d = 0; d < sizeof(k_demo) / sizeof(k_demo[0]); d++) {
        char path[1024], png[1024];
        size_t len = 0;
        uint8_t *rom;
        struct run_result a, b, c;
        int ok;

        snprintf(path, sizeof(path), "%s/%s", roms, k_demo[d].file);
        rom = slurp(path, &len);
        CHECK(rom != NULL);
        if (rom == NULL) {
            continue;
        }
        if (out != NULL) {
            snprintf(png, sizeof(png), "%s/%s", out, k_demo[d].png);
        }
        ok = run_sys(k_demo[d].sys, rom, len, frames, 3, 0, out ? png : NULL, &a) == 0;
        ok = ok && run_sys(k_demo[d].sys, rom, len, frames, 3, 0, NULL, &b) == 0;
        ok = ok && run_sys(k_demo[d].sys, rom, len, frames, 1, 0, NULL, &c) == 0;
        CHECK(ok);
        if (!ok) {
            free(rom);
            continue;
        }
        printf("MULTI sys=%-10s frames=%u ledger=%016llx video=%016llx audio=%016llx ring=%016llx "
               "pcm=%llu nonzero=%llu ring_frames=%llu resampled=%llu dropped=%llu "
               "edge_black=%llu black_rows=%llu black_cols=%llu colours=%llu freezes=%llu\n",
               k_demo[d].label, frames, (unsigned long long)a.ledger, (unsigned long long)a.video,
               (unsigned long long)a.audio, (unsigned long long)a.ring_hash,
               (unsigned long long)a.pcm_frames, (unsigned long long)a.pcm_nonzero,
               (unsigned long long)a.ring_frames, (unsigned long long)a.resampled,
               (unsigned long long)a.dropped, (unsigned long long)a.edge_black,
               (unsigned long long)a.black_rows, (unsigned long long)a.black_cols,
               (unsigned long long)a.colours, (unsigned long long)a.freezes);
        CHECK(a.ledger == b.ledger && a.video == b.video && a.audio == b.audio);
        CHECK(a.ledger == c.ledger && a.video == c.video && a.audio == c.audio); /* 1 vs 3 workers */
        CHECK(a.edge_black == 0 && a.black_rows == 0 && a.black_cols == 0);
        CHECK(a.colours >= 2);
        CHECK(a.pcm_frames > 0);
        CHECK(!k_demo[d].need_audio || a.pcm_nonzero > 0);
        CHECK(a.ring_frames == a.pcm_frames && a.ring_nonzero == a.pcm_nonzero);
        CHECK(a.resampled + 2u >= a.ring_frames && a.resampled <= a.ring_frames + 2u);
        CHECK(a.dropped == 0 && a.freezes == 0);
        {
            /* NEAREST mode must also fill edge to edge */
            struct run_result n;
            CHECK(run_sys(k_demo[d].sys, rom, len, 60, 3, 1, NULL, &n) == 0);
            CHECK(n.edge_black == 0 && n.black_rows == 0 && n.black_cols == 0);
            printf("MULTI-NEAREST sys=%-10s video=%016llx edge_black=%llu\n", k_demo[d].label,
                   (unsigned long long)n.video, (unsigned long long)n.edge_black);
        }
        chain = (chain ^ a.ledger) * 0x100000001b3ull;
        chain = (chain ^ a.video) * 0x100000001b3ull;
        chain = (chain ^ a.audio) * 0x100000001b3ull;
        systems_ok++;
        free(rom);
    }
    CHECK(systems_ok == (int)(sizeof(k_demo) / sizeof(k_demo[0])));

    /* ---- atomic .sav round trip: SNES demo with an 8 KiB battery SRAM header ---- */
    {
        char tmpl[] = "/tmp/hs-multi-sav-XXXXXX";
        char rp[1200], sav[1100];
        size_t len = 0;
        uint8_t *rom;
        int done = 0;
        snprintf(rp, sizeof(rp), "%s/hs_mode1_demo.sfc", roms);
        rom = slurp(rp, &len);
        if (rom != NULL && mkdtemp(tmpl) != NULL) {
            int err;
            /* SNES header: force 8 KiB battery SRAM (0x7FD8 = 3, 0x7FD6 = 0x02). */
            uint8_t *r2 = (uint8_t *)malloc(len);
            struct emu *e;
            memcpy(r2, rom, len);
            r2[0x7FD8] = 3;
            r2[0x7FD6] = 0x02;
            e = emu_create(EMU_SYS_SNES, r2, len, 3, &err);
            if (e != NULL && emu_sram_size(e) > 0) {
                size_t n = emu_sram_size(e);
                uint8_t *buf = (uint8_t *)malloc(n), *back = (uint8_t *)malloc(n);
                size_t k;
                struct emu *e2;
                for (k = 0; k < n; k++) {
                    buf[k] = (uint8_t)(k * 7u + 3u);
                }
                snprintf(sav, sizeof(sav), "%s/game.sav", tmpl);
                CHECK(emu_sram_put(e, buf, n) == 0);
                CHECK(!emu_sram_dirty(e));
                CHECK(emu_run_frames(e, 2, 0, 0) == 0);
                CHECK(hs_emu_save_sav(e, sav) == 0);
                snprintf(rp, sizeof(rp), "%s.tmp", sav);
                CHECK(access(rp, F_OK) != 0); /* .sav.tmp renamed away */
                e2 = emu_create(EMU_SYS_SNES, r2, len, 3, &err);
                CHECK(e2 != NULL && hs_emu_load_sav(e2, sav) == (long)n);
                CHECK(e2 != NULL && emu_sram_get(e2, back, n) == n && memcmp(back, buf, n) == 0);
                printf("SAV snes sram=%zu B atomic round trip %s\n", n,
                       (e2 && memcmp(back, buf, n) == 0) ? "OK" : "BAD");
                done = 1;
                emu_destroy(e2);
                unlink(sav);
                free(buf);
                free(back);
            }
            emu_destroy(e);
            free(r2);
            rmdir(tmpl);
        }
        free(rom);
        CHECK(done);
    }

    printf("MULTI-CHAIN %016llx systems=%d\n", (unsigned long long)chain, systems_ok);
    if (fail_count == 0) {
        printf("ALL MULTI-EMU TESTS PASSED\n");
        return 0;
    }
    printf("MULTI-EMU FAILURES: %d\n", fail_count);
    return 1;
}
