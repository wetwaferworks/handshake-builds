/* SPDX-License-Identifier: MIT */
#ifndef HS_GB_INCLUDED
#define HS_GB_INCLUDED

#include "hs_handshake.h"

struct hs_fb;

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define HS_GB_W 160
#define HS_GB_H 144
#define HS_GB_SCRIPT_MAX 256

struct hs_gb_stats {
    uint64_t frames;
    uint64_t ledger_hash;
    uint64_t frame_crc;
    uint64_t audio_crc;
    uint64_t epoch;
    uint64_t monatomic;
    uint64_t xruns;
    uint64_t overruns;
    uint64_t phases;
};

struct hs_gb;

struct hs_gb *hs_gb_create(void);
void             hs_gb_destroy(struct hs_gb *g);

int  hs_gb_load_rom(struct hs_gb *g, const char *path);
int  hs_gb_load_rom_mem(struct hs_gb *g, const uint8_t *data, size_t len);

/* Scripted button event: absolute JOYPAD_* mask (Peanut-GB defines) applied at tick. */
int  hs_gb_script(struct hs_gb *g, uint64_t tick, uint8_t joy_mask);

int  hs_gb_start(struct hs_gb *g, int workers);
void hs_gb_stop(struct hs_gb *g);

/* Run `frames` handshake phases, one GB frame each. */
int  hs_gb_run_frames(struct hs_gb *g, uint64_t frames);

void hs_gb_stats(const struct hs_gb *g, struct hs_gb_stats *out);
const uint8_t *hs_gb_framebuffer(const struct hs_gb *g); /* 160*144, values 0-3 */
uint64_t       hs_gb_ledger_hash(const struct hs_gb *g);
uint64_t       hs_gb_xruns(const struct hs_gb *g);

/* Scoreboard helpers: default script on handshake-demo.gb, `frames` default 90. */
int hs_gb_scoreboard(const char *rom_path, int workers, uint64_t frames,
                        struct hs_gb_stats *out);

void hs_gb_blit(struct hs_fb *fb, int x, int y, int scale,
                   const uint8_t *fb160, const uint32_t pal[4]);

/* ---- v3 live play ---- */
/* Live joypad (JOYPAD_* bits, 1 = pressed). The first call switches the
   core from the scripted event ledger to this value; store it in PRE,
   before hs_gb_run_frames() releases the phase. */
void     hs_gb_set_joypad(struct hs_gb *g, uint8_t joy_mask);
/* Battery-backed cartridge RAM (NULL / 0 when the cart has none). */
uint8_t *hs_gb_cart_ram(struct hs_gb *g, size_t *size);
int      hs_gb_sram_dirty(struct hs_gb *g);
void     hs_gb_sram_mark_clean(struct hs_gb *g);
/* Header title (0x134..0x143), printable ASCII only. */
void     hs_gb_rom_title(const struct hs_gb *g, char *buf, size_t n);

/* ---- v3-audio ---- */
/* Live mode only: move queued interleaved S16 stereo samples (all frames
   run since the last take) into `out`. Call between hs_gb_run_frames(). */
size_t   hs_gb_pcm_take(struct hs_gb *g, int16_t *out, size_t max_samples);
/* Effective APU output rate (stereo frames per second), ~32731 Hz. */
unsigned hs_gb_pcm_rate_hz(void);

#endif
