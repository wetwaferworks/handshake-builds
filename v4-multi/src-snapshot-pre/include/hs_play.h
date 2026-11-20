/* SPDX-License-Identifier: MIT */
#ifndef HS_PLAY_H
#define HS_PLAY_H

#include "hs_theme.h"

struct hs_fb;
struct hs_input;
struct hs_audio;

struct hs_play_ctx {
    struct hs_fb    *fb;
    struct hs_input *in;
    hs_theme_id_t   *theme;      /* shared with the shell; L1/R1 in the picker */
    int              workers;
    const char      *sd_root;    /* default /mnt/sd */
    int              try_mount;  /* 1 on the device (PID 1) */
    const char      *builtin;    /* /roms/handshake-demo.gb */
    struct hs_audio *audio;      /* v3-audio: live speaker, or NULL / synth-only */
};

/* GAMES picker + full-screen play. Blocks until B / MENU in the picker. */
int hs_play_run(struct hs_play_ctx *c);

/* Host previews (headless fb). */
int hs_play_snapshot_picker(struct hs_play_ctx *c, const char *png);
int hs_play_snapshot_rom(struct hs_fb *fb, const char *rom, unsigned frames,
                         int nearest, int palette, const char *toast, const char *png);

/* v3-audio: Vol+/Vol- handling shared by shell, picker and play.
   Returns 1 if the volume changed. */
int hs_play_volume_keys(struct hs_audio *audio, const struct hs_input *in);

/* v3-audio self-test: play `rom` live for `seconds` with the scoreboard
   button script, feeding the speaker. Prints one AUDIOTEST line. */
int hs_play_audio_test(struct hs_audio *audio, const char *rom, double seconds);

#endif /* HS_PLAY_H */
