/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-multi: clean 7-system emulator registry.
 *
 * One uniform zero-mutex adapter over the clean-room HandshakeOS cores:
 *   SYS_GB, SYS_GBC  -> hs-gbc        SYS_GBA     -> hs-gba
 *   SYS_SNES         -> hs-snes       SYS_GENESIS -> hs-genesis
 *   SYS_PSX, SYS_N64 -> registry slots + ROM extensions wired, core not
 *                       linked yet ("Core coming in v4-full").
 *
 * Every emulated frame runs through the real Timing Handshake as two
 * phases (all work is pulled by worker shards, no locks anywhere):
 *   phase A (group 1)  Core 0: CPU + bus, writes the BACK snapshot
 *   barrier + commit   clock thread, all cores quiescent: BACK -> FRONT
 *   phase B (group 2)  Core 1: video -> full 640x480 edge-to-edge frame
 *                      Core 2: audio synth -> lock-free SPSC PCM ring
 * The shell thread drains the PCM ring (48 kHz stereo) into src/audio.c
 * and reads the framebuffer strictly between emu_run_frames() calls.
 * This header deliberately does not include hs_core.h (its HS_BTN_* names
 * collide with hs_input.h); pad bits below use the same bit layout. */
#ifndef HS_EMU_SYS_H
#define HS_EMU_SYS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    EMU_SYS_NONE = -1,
    EMU_SYS_GB = 0,
    EMU_SYS_GBC,
    EMU_SYS_GBA,
    EMU_SYS_SNES,
    EMU_SYS_GENESIS,
    EMU_SYS_PSX,
    EMU_SYS_N64,
    EMU_SYS_COUNT
} emu_sys_t;

/* Unified pad bits (identical layout to hs_core.h HS_BTN_*). */
enum {
    EMU_PAD_UP = 1u << 0, EMU_PAD_DOWN = 1u << 1, EMU_PAD_LEFT = 1u << 2,
    EMU_PAD_RIGHT = 1u << 3, EMU_PAD_A = 1u << 4, EMU_PAD_B = 1u << 5,
    EMU_PAD_X = 1u << 6, EMU_PAD_Y = 1u << 7, EMU_PAD_L = 1u << 8,
    EMU_PAD_R = 1u << 9, EMU_PAD_L2 = 1u << 10, EMU_PAD_R2 = 1u << 11,
    EMU_PAD_START = 1u << 14, EMU_PAD_SELECT = 1u << 15
};

#define EMU_FB_W 640
#define EMU_FB_H 480
#define EMU_PCM_RATE 48000u
#define EMU_PCM_RING 16384u   /* stereo frames, power of two */

struct emu_sys_info {
    emu_sys_t   id;
    const char *tag;      /* "GB", "GBC", "GBA", "SNES", "GEN", "PSX", "N64" */
    const char *name;     /* "Game Boy" ... */
    const char *folder;   /* ROMS/<folder> */
    const char *exts;     /* space separated, lower case, with dot */
    const char *core;     /* "hs-gbc" ... */
    int         linked;   /* 1 = playable in this build */
    uint32_t    rgb;      /* tag colour, 0xRRGGBB */
    uint32_t    fps_num, fps_den;
};

const struct emu_sys_info *emu_sys_info(emu_sys_t sys);
int  emu_sys_linked(emu_sys_t sys);
int  emu_sys_count_linked(void);
/* Pick a system from a ROM path. folder_hint (e.g. "PSX") breaks .bin
   ties; with no hint .bin means Genesis. EMU_SYS_NONE if unknown. */
emu_sys_t emu_sys_from_path(const char *path, const char *folder_hint);
emu_sys_t emu_sys_from_folder(const char *folder);
/* 1 if `name` has one of sys's extensions (case-insensitive). */
int  emu_sys_ext_match(emu_sys_t sys, const char *name);
/* Status text shown when a ROM's core is not linked yet. */
const char *emu_sys_unlinked_notice(void); /* "Core coming in v4-full" */

struct emu;

struct emu_stats {
    uint64_t frames;
    uint64_t phases;
    uint64_t ledger;       /* Timing Handshake ledger hash */
    uint64_t video_hash;   /* FNV-1a of the last full 640x480 frame */
    uint64_t audio_hash;   /* FNV-1a over every PCM sample produced */
    uint64_t pcm_frames;   /* stereo frames produced by the core */
    uint64_t pcm_nonzero;  /* non-zero samples produced */
    uint64_t pcm_dropped;  /* ring full (consumer too slow) */
    uint64_t freezes;
};

/* The ROM is copied. workers: Timing Handshake worker shards (3 = 3+1).
   Returns NULL with *err set (-ENOSYS for an unlinked system). */
struct emu *emu_create(emu_sys_t sys, const uint8_t *rom, size_t len, int workers, int *err);
void        emu_destroy(struct emu *e);
emu_sys_t   emu_system(const struct emu *e);

/* Run n frames with pad held. Video is rendered for the last frame only
   when render_last_only (turbo); audio is synthesised every frame. */
int  emu_run_frames(struct emu *e, uint32_t n, uint32_t pad, int render_last_only);
const uint32_t *emu_framebuffer(const struct emu *e); /* 640x480 XRGB8888 */
void emu_set_nearest(struct emu *e, int nearest);
int  emu_nearest(const struct emu *e);
void emu_set_dmg_palette(struct emu *e, unsigned pal); /* GB only, 0..3 */
void emu_set_rtc(struct emu *e, int64_t epoch_sec);
const char *emu_dmg_palette_name(unsigned pal);

/* Shell thread, between emu_run_frames(): drain up to max stereo frames. */
size_t emu_pcm_take(struct emu *e, int16_t *out, size_t max_frames);

/* Battery saves. size 0 = none. dirty compares against the last
   emu_sram_mark_clean() / load. */
size_t emu_sram_size(struct emu *e);
size_t emu_sram_get(struct emu *e, uint8_t *buf, size_t cap);
int    emu_sram_put(struct emu *e, const uint8_t *buf, size_t len);
int    emu_sram_dirty(struct emu *e);
void   emu_sram_mark_clean(struct emu *e);

void emu_stats(const struct emu *e, struct emu_stats *out);

#endif /* HS_EMU_SYS_H */
