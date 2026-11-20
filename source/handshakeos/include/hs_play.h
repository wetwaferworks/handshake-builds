/* SPDX-License-Identifier: MIT */
#ifndef HS_PLAY_H
#define HS_PLAY_H

#include <stddef.h>

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
    /* v5.3: USB TRANSFER failure -> PID 1 fault ledger + HANDSHAKE-FAULTS.log (NULL = console only) */
    void           (*note_fault)(unsigned step, int err);
};

/* GAMES picker + full-screen play. Blocks until B / MENU in the picker. */
int hs_play_run(struct hs_play_ctx *c);

/* Host previews (headless fb). */
int hs_play_snapshot_usb(struct hs_fb *fb, hs_theme_id_t theme, int host, const char *png); /* v5.3 */
int hs_play_snapshot_picker(struct hs_play_ctx *c, const char *png);
int hs_play_snapshot_rom(struct hs_fb *fb, const char *rom, unsigned frames,
                         int nearest, int palette, const char *toast, const char *png);

/* v3-audio: Vol+/Vol- handling shared by shell, picker and play.
   Returns 1 if the volume changed. */
int hs_play_volume_keys(struct hs_audio *audio, const struct hs_input *in);

/* v3-audio self-test: play `rom` live for `seconds` with the scoreboard
   button script, feeding the speaker. Prints one AUDIOTEST line. */
int hs_play_audio_test(struct hs_audio *audio, const char *rom, double seconds);

/* v4-full self-test: run all 8 built-in demos in `dir` (7 systems; PS1 /
   N64 demos in dir/psx and dir/n64) for `frames` frames through the
   registry, appending one MULTI line each to out. With aa_cycle the PS1 /
   N64 demos also cycle 1X, FXAA, 2X, 4X, 8X live (one MULTI-AA line each).
   *systems_ok = distinct systems that passed. Returns demos that passed,
   or -1 if any core state leaked. */
#define HS_MULTI_DEMOS 8
int hs_play_multi_selftest(const char *dir, unsigned frames, int workers, int aa_cycle,
                           int *systems_ok, char *out, size_t n);

#endif /* HS_PLAY_H */
