/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * Build-time packed CloudAtlas o.O splash pack (8 x 640x480 RGB565) plus a
 * built-in 38x9 HandshakeOS / o.O ASCII animation. Runtime has no PNG decoder.
 * Credit CloudAtlas for the graphic pack. Public text is "HandshakeOS" only.
 * A missing pack or blit fault is reported; never powers off. */
#ifndef HS_SPLASH_H
#define HS_SPLASH_H

#include "hs_fb.h"
#include "hs_theme.h"
#include <stddef.h>
#include <stdint.h>

#ifndef HS_SPLASH_PACK
#define HS_SPLASH_PACK "/splashes/pack.bin"
#endif

#define HS_SPLASH_MAX        16
#define HS_SPLASH_ID_MAX     16
#define HS_SPLASH_TITLE_MAX  48
#define HS_SPLASH_ARTIST_MAX 32
#define HS_SPLASH_ASCII_C    38
#define HS_SPLASH_ASCII_R    9

typedef enum {
    HS_SPLASH_GRAPHIC = 0,
    HS_SPLASH_ASCII   = 1
} hs_splash_type_t;

struct hs_splash_ent {
    char             id[HS_SPLASH_ID_MAX];
    char             title[HS_SPLASH_TITLE_MAX];
    char             artist[HS_SPLASH_ARTIST_MAX];
    hs_splash_type_t type;
    hs_theme_id_t    theme;
    uint16_t         nframes;
    uint32_t         off;
    uint32_t         size;
};

struct hs_splash {
    uint8_t            *pack;
    size_t              pack_len;
    int                 n;
    int                 loaded;
    char                err[96];
    struct hs_splash_ent ent[HS_SPLASH_MAX];
};

void hs_splash_init(struct hs_splash *s);
/* 0 if a pack was loaded, -1 on missing/corrupt (err set, n may be 0). Never powers off. */
int  hs_splash_load(struct hs_splash *s, const char *path);
void hs_splash_free(struct hs_splash *s);
int  hs_splash_find(const struct hs_splash *s, const char *id);
const struct hs_splash_ent *hs_splash_at(const struct hs_splash *s, int i);

/* Blit entry `idx` (frame `frame` for ASCII, ignored for a still). `pulse` is
 * 0..256 brightness (256 = 100%). Bottom 36 px stays the theme background. */
void hs_splash_blit(struct hs_fb *fb, const struct hs_splash *s, int idx,
                    int frame, int pulse, hs_theme_id_t theme);

struct hs_cfg;
struct hs_input;

/* Try drm/fbdev for up to max_wait_ms (100 ms steps). `once` caps the wait at 300 ms
 * so QEMU --once proofs do not sit in a 10 s black screen. `wait_ms` / `mode` are set
 * as soon as hardware appears or the budget expires. 0 / -1. */
int hs_boot_wait_display(struct hs_fb *fb, int once, int max_wait_ms,
                         int *wait_ms, char *mode, size_t mode_n);

/* Animate (or show one still) until min_ms have elapsed or A/START/MENU is pressed.
 * `open_menu` is set on a button. Never powers off. */
void hs_boot_animate(struct hs_fb *fb, struct hs_input *in,
                     const struct hs_splash *sp, const char *splash_id,
                     int animation, hs_theme_id_t theme,
                     int min_ms, int *open_menu);

#endif
