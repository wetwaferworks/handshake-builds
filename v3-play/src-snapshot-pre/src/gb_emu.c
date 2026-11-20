/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "hs_gb.h"
#include "hs_fb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define MINIGB_APU_AUDIO_FORMAT_S16SYS
#include "minigb_apu.h"

static struct minigb_apu_ctx g_apu;

uint8_t audio_read(const uint16_t addr)
{
    return minigb_apu_audio_read(&g_apu, addr);
}

void audio_write(const uint16_t addr, const uint8_t val)
{
    minigb_apu_audio_write(&g_apu, addr, val);
}

#define ENABLE_SOUND 1
#define PEANUT_GB_12_COLOUR 0
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "peanut_gb.h"
#pragma GCC diagnostic pop

/* AUDIO_SAMPLES_TOTAL is not an integer constant (float division). */
#define HS_GB_PCM_MAX 2048

struct hs_gb {
    struct gb_s gb;
    uint8_t *rom;
    size_t   rom_size;
    uint8_t *cart_ram;
    size_t   cart_ram_size;

    uint8_t  fb[HS_GB_H][HS_GB_W];
    int16_t  pcm[HS_GB_PCM_MAX];
    _Atomic  uint32_t pcm_ready; /* 1 = unconsumed snapshot */
    _Atomic  uint64_t xruns;

    uint8_t  joy_held; /* JOYPAD_* bits, 1 = pressed */
    int      started;
    struct hs_hs hs;

    uint64_t frame_crc;
    uint64_t audio_crc;
    uint64_t frames_done;
    uint64_t next_phase; /* v2: live calls continue the phase count */

    uint32_t n_script;
    struct { uint64_t tick; uint8_t mask; } script[HS_GB_SCRIPT_MAX];
};

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
    struct hs_gb *g = (struct hs_gb *)gb->direct.priv;

    if (addr >= g->rom_size) {
        return 0xFF;
    }
    return g->rom[addr];
}

static uint8_t cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
    struct hs_gb *g = (struct hs_gb *)gb->direct.priv;

    if (g->cart_ram == NULL || addr >= g->cart_ram_size) {
        return 0xFF;
    }
    return g->cart_ram[addr];
}

static void cart_ram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val)
{
    struct hs_gb *g = (struct hs_gb *)gb->direct.priv;

    if (g->cart_ram == NULL || addr >= g->cart_ram_size) {
        return;
    }
    g->cart_ram[addr] = val;
}

static void gb_err(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr)
{
    (void)gb;
    (void)err;
    (void)addr;
}

static void lcd_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line)
{
    struct hs_gb *g = (struct hs_gb *)gb->direct.priv;
    int x;

    if (line >= HS_GB_H) {
        return;
    }
    for (x = 0; x < HS_GB_W; x++) {
        g->fb[line][x] = (uint8_t)(pixels[x] & 0x03u);
    }
}

static uint64_t crc_frame(const struct hs_gb *g)
{
    uint64_t h = 0xcbf29ce484222325ull;
    int y, x;

    for (y = 0; y < HS_GB_H; y++) {
        for (x = 0; x < HS_GB_W; x++) {
            h = hs_fnv1a64(h, g->fb[y][x]);
        }
    }
    return h;
}

static uint64_t crc_audio(const int16_t *pcm, unsigned n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    unsigned i;

    for (i = 0; i < n; i++) {
        h = hs_fnv1a64(h, (uint16_t)pcm[i]);
    }
    return h;
}

static void apply_joy(struct hs_gb *g)
{
    /* Peanut-GB: 0 bit = pressed. */
    g->gb.direct.joypad = (uint8_t)(0xFFu ^ g->joy_held);
}

static uint64_t work_serial(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return hs_fnv1a64(0xcbf29ce484222325ull, phase ^ (cycle << 8) ^ 0xA11ull);
}

static uint64_t work_sha_a(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return hs_fnv1a64(0xcbf29ce484222325ull, phase + cycle * 3ull + 0xA0ull);
}

static uint64_t work_sha_b(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return hs_fnv1a64(0xcbf29ce484222325ull, (phase << 3) ^ cycle ^ 0xB0ull);
}

static uint64_t work_gb(uint64_t phase, uint64_t cycle, void *user)
{
    struct hs_gb *g = (struct hs_gb *)user;
    uint32_t ready;
    uint64_t ev;

    /* Control coupling: events were stamped (seq, tick) in PRE (scripted
       before start, or evdev in a later host). Apply those whose tick
       has matured for this PHASE — one GB frame per handshake phase. */
    ev = hs_hs_apply_matured(&g->hs, phase);
    (void)ev;
    (void)cycle;
    {
        uint32_t i;
        for (i = 0; i < g->hs.n_events; i++) {
            if (g->hs.events[i].tick > phase) {
                break;
            }
            g->joy_held = (uint8_t)g->hs.events[i].code;
        }
    }
    apply_joy(g);
    gb_run_frame(&g->gb);

    /* Phase-boundary audio snapshot: a finished PCM buffer, no lock. */
    ready = atomic_load_explicit(&g->pcm_ready, memory_order_acquire);
    if (ready != 0) {
        atomic_fetch_add_explicit(&g->xruns, 1, memory_order_relaxed);
    }
    minigb_apu_audio_callback(&g_apu, g->pcm);
    atomic_store_explicit(&g->pcm_ready, 1, memory_order_release);

    g->frame_crc = crc_frame(g);
    g->audio_crc = crc_audio(g->pcm, AUDIO_SAMPLES_TOTAL <= HS_GB_PCM_MAX
                                       ? AUDIO_SAMPLES_TOTAL
                                       : HS_GB_PCM_MAX);
    g->frames_done++;

    /* Consume the snapshot in the same phase (CRC is the consumer). */
    atomic_store_explicit(&g->pcm_ready, 0, memory_order_release);
    return hs_fnv1a64(g->frame_crc, g->audio_crc);
}

static uint64_t work_ctrl(uint64_t phase, uint64_t cycle, void *user)
{
    (void)user;
    return hs_fnv1a64(0xcbf29ce484222325ull, phase + 0xC0u + cycle);
}

struct hs_gb *hs_gb_create(void)
{
    struct hs_gb *g = (struct hs_gb *)calloc(1, sizeof(*g));

    if (g == NULL) {
        return NULL;
    }
    g->joy_held = 0;
    atomic_init(&g->pcm_ready, 0);
    atomic_init(&g->xruns, 0);
    return g;
}

void hs_gb_destroy(struct hs_gb *g)
{
    if (g == NULL) {
        return;
    }
    hs_gb_stop(g);
    free(g->rom);
    free(g->cart_ram);
    free(g);
}

int hs_gb_load_rom_mem(struct hs_gb *g, const uint8_t *data, size_t len)
{
    enum gb_init_error_e err;
    size_t ram;

    if (g == NULL || data == NULL || len < 0x150) {
        return -EINVAL;
    }
    free(g->rom);
    g->rom = (uint8_t *)malloc(len);
    if (g->rom == NULL) {
        return -ENOMEM;
    }
    memcpy(g->rom, data, len);
    g->rom_size = len;

    err = gb_init(&g->gb, rom_read, cart_ram_read, cart_ram_write, gb_err, g);
    if (err != GB_INIT_NO_ERROR) {
        return -EIO;
    }
    ram = 0;
    if (gb_get_save_size_s(&g->gb, &ram) != 0) {
        ram = 0;
    }
    if (ram > 0) {
        g->cart_ram = (uint8_t *)calloc(1, ram);
        if (g->cart_ram == NULL) {
            return -ENOMEM;
        }
        g->cart_ram_size = ram;
    }
    gb_init_lcd(&g->gb, lcd_line);
    minigb_apu_audio_init(&g_apu);
    apply_joy(g);
    return 0;
}

int hs_gb_load_rom(struct hs_gb *g, const char *path)
{
    FILE *fp;
    uint8_t *buf;
    long n;
    int rc;

    if (path == NULL) {
        return -EINVAL;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -errno;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0x150) {
        fclose(fp);
        return -EINVAL;
    }
    rewind(fp);
    buf = (uint8_t *)malloc((size_t)n);
    if (buf == NULL) {
        fclose(fp);
        return -ENOMEM;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return -EIO;
    }
    fclose(fp);
    rc = hs_gb_load_rom_mem(g, buf, (size_t)n);
    free(buf);
    return rc;
}

int hs_gb_script(struct hs_gb *g, uint64_t tick, uint8_t joy_mask)
{
    if (g == NULL) {
        return -EINVAL;
    }
    if (g->started) {
        return hs_hs_post_event(&g->hs, tick, joy_mask, joy_mask);
    }
    if (g->n_script >= HS_GB_SCRIPT_MAX) {
        return -ENOMEM;
    }
    g->script[g->n_script].tick = tick;
    g->script[g->n_script].mask = joy_mask;
    g->n_script++;
    return 0;
}

int hs_gb_start(struct hs_gb *g, int workers)
{
    struct hs_hs_config cfg;
    struct hs_hs_comp_desc d;

    if (g == NULL || g->rom == NULL) {
        return -EINVAL;
    }
    hs_hs_default_config(&cfg);
    cfg.worker_threads = workers;
    cfg.phases_per_cycle = 4;
    /* Scoreboard measures determinism, not wall-clock 60 Hz. Give
       qemu-aarch64-static enough budget so FREEZE / overrun stay at 0. */
    cfg.phase_budget_ns = 2000000000ull;      /* 2 s */
    cfg.watchdog_ceiling_ns = 5000000000ull;  /* 5 s */
    cfg.ledger = true;
    if (hs_hs_init(&g->hs, &cfg) != 0) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "serial";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    if (hs_hs_register(&g->hs, &d, work_serial, g) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "control";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.affine = true;
    d.tick_critical = true;
    if (hs_hs_register(&g->hs, &d, work_ctrl, g) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "gb-step";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.affine = true;
    d.tick_critical = true;
    if (hs_hs_register(&g->hs, &d, work_gb, g) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "worker-a";
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    if (hs_hs_register(&g->hs, &d, work_sha_a, g) == UINT32_MAX) {
        return -1;
    }

    memset(&d, 0, sizeof(d));
    d.name = "worker-b";
    d.cadence = HS_CADENCE_HALF_CYCLE;
    d.tick_critical = true;
    if (hs_hs_register(&g->hs, &d, work_sha_b, g) == UINT32_MAX) {
        return -1;
    }

    {
        uint32_t i;
        for (i = 0; i < g->n_script; i++) {
            if (hs_hs_post_event(&g->hs, g->script[i].tick,
                                    g->script[i].mask, g->script[i].mask) != 0) {
                hs_hs_fini(&g->hs);
                return -1;
            }
        }
    }
    if (hs_hs_start(&g->hs) != 0) {
        return -1;
    }
    g->started = 1;
    return 0;
}

void hs_gb_stop(struct hs_gb *g)
{
    if (g == NULL || !g->started) {
        return;
    }
    hs_hs_stop(&g->hs);
    g->started = 0;
}

int hs_gb_run_frames(struct hs_gb *g, uint64_t frames)
{
    uint64_t i;
    int ppc = 4;

    if (g == NULL || !g->started) {
        return -EINVAL;
    }
    /* v1 restarted at phase 0 on every call, so the live one-frame-per-call
       loop replayed phase 0 forever. v2 continues the count; a single
       N-frame scoreboard call is bit-identical to v1 (phases 0..N-1). */
    for (i = 0; i < frames; i++) {
        uint64_t ph = g->next_phase;
        if (!hs_hs_run_phase(&g->hs, ph, ph / (uint64_t)ppc, 0)) {
            return -EIO;
        }
        g->next_phase = ph + 1u;
        /* After commit, fold this frame's CRCs into the ledger again so
           the hash is unambiguously about the picture and the sound.
           run_phase already folded the work_gb return (frame_crc, audio_crc). */
    }
    return 0;
}

void hs_gb_stats(const struct hs_gb *g, struct hs_gb_stats *out)
{
    struct hs_hs_stats hs;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (g == NULL) {
        return;
    }
    hs_hs_stats(&g->hs, &hs);
    out->frames = g->frames_done;
    out->ledger_hash = hs_hs_ledger_hash(&g->hs);
    out->frame_crc = g->frame_crc;
    out->audio_crc = g->audio_crc;
    out->epoch = hs_hs_commit_epoch(&g->hs);
    out->monatomic = hs_hs_monatomic(&g->hs);
    out->xruns = atomic_load_explicit(&g->xruns, memory_order_relaxed);
    out->overruns = hs.overrun_phases;
    out->phases = hs.phases;
}

const uint8_t *hs_gb_framebuffer(const struct hs_gb *g)
{
    return g ? &g->fb[0][0] : NULL;
}

uint64_t hs_gb_ledger_hash(const struct hs_gb *g)
{
    return g ? hs_hs_ledger_hash(&g->hs) : 0;
}

uint64_t hs_gb_xruns(const struct hs_gb *g)
{
    return g ? atomic_load_explicit(&g->xruns, memory_order_relaxed) : 0;
}

int hs_gb_scoreboard(const char *rom_path, int workers, uint64_t frames,
                        struct hs_gb_stats *out)
{
    struct hs_gb *g;
    int rc;
    uint64_t t;

    if (frames == 0) {
        frames = 90;
    }
    g = hs_gb_create();
    if (g == NULL) {
        return -ENOMEM;
    }
    rc = hs_gb_load_rom(g, rom_path);
    if (rc != 0) {
        hs_gb_destroy(g);
        return rc;
    }
    /* Default script: after the title settles, hold right, then A (beep). */
    (void)hs_gb_script(g, 0, 0);
    for (t = 20; t < 40; t++) {
        (void)hs_gb_script(g, t, 0x10); /* JOYPAD_RIGHT */
    }
    for (t = 40; t < 60; t++) {
        (void)hs_gb_script(g, t, 0x10 | 0x01); /* right + A */
    }
    for (t = 60; t < frames; t++) {
        (void)hs_gb_script(g, t, 0x80); /* down */
    }

    rc = hs_gb_start(g, workers);
    if (rc != 0) {
        hs_gb_destroy(g);
        return rc;
    }
    rc = hs_gb_run_frames(g, frames);
    if (rc == 0 && out != NULL) {
        hs_gb_stats(g, out);
    }
    hs_gb_stop(g);
    hs_gb_destroy(g);
    return rc;
}


void hs_gb_blit(struct hs_fb *fb, int x, int y, int scale,
                   const uint8_t *fb160, const uint32_t pal[4])
{
    int sx, sy, j;

    if (fb == NULL || fb->pix == NULL || fb160 == NULL || pal == NULL || scale < 1) {
        return;
    }
    /* v2: clip once, then write rows directly (v1 made one clipped
       hs_fb_fill call per GB pixel: 23,040 calls a frame). */
    if (x < 0 || y < 0 || x + HS_GB_W * scale > fb->w || y + HS_GB_H * scale > fb->h) {
        return;
    }
    for (sy = 0; sy < HS_GB_H; sy++) {
        const uint8_t *src = fb160 + (size_t)sy * HS_GB_W;
        uint32_t *row = (uint32_t *)((uint8_t *)fb->pix
                                     + (size_t)(y + sy * scale) * (size_t)fb->stride_bytes) + x;
        if (scale == 1) {
            for (sx = 0; sx < HS_GB_W; sx++) {
                row[sx] = pal[src[sx] & 0x03u];
            }
        } else {
            for (sx = 0; sx < HS_GB_W; sx++) {
                uint32_t c = pal[src[sx] & 0x03u];
                for (j = 0; j < scale; j++) {
                    row[sx * scale + j] = c;
                }
            }
            for (j = 1; j < scale; j++) {
                memcpy((uint8_t *)row + (size_t)j * (size_t)fb->stride_bytes, row,
                       (size_t)HS_GB_W * (size_t)scale * 4u);
            }
        }
    }
    hs_fb_rect(fb, x - 2, y - 2, HS_GB_W * scale + 4, HS_GB_H * scale + 4, 2, pal[2]);
}
