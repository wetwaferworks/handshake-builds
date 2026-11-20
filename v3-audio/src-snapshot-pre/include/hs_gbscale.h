/* SPDX-License-Identifier: MIT */
#ifndef HS_GBSCALE_H
#define HS_GBSCALE_H

#include <stdint.h>

struct hs_fb;

/* v3 full-screen Game Boy presentation: 160x144 -> 640x480, edge to edge.
   Horizontal is an exact 4x replicate. Vertical is 10/3: either an
   area-weighted "sharp bilinear" blend (each output row covers 0.3 of a
   source row, so it touches at most two source rows) or plain nearest. */
typedef enum {
    HS_GBSCALE_UPSAMPLED = 0, /* default */
    HS_GBSCALE_NEAREST,
    HS_GBSCALE_COUNT
} hs_gbscale_mode_t;

typedef enum {
    HS_DMG_PAL_OLIVE = 0,  /* Classic Olive */
    HS_DMG_PAL_POCKET,     /* Pocket B&W */
    HS_DMG_PAL_PURPLE,     /* Deep Purple */
    HS_DMG_PAL_GREEN,      /* Matrix Green */
    HS_DMG_PAL_COUNT
} hs_dmg_pal_t;

/* One precomputed row of the 480-row vertical table. w0 + w1 == 256. */
struct hs_gbscale_row {
    uint8_t  y0;
    uint8_t  y1;
    uint16_t w0;
    uint16_t w1;
};

struct hs_gbscale {
    hs_gbscale_mode_t     mode;
    struct hs_gbscale_row lut[HS_GBSCALE_COUNT][480];
};

void        hs_gbscale_init(struct hs_gbscale *s);
const char *hs_gbscale_mode_name(hs_gbscale_mode_t m);
const char *hs_dmg_pal_name(hs_dmg_pal_t p);
void        hs_dmg_pal_get(hs_dmg_pal_t p, uint32_t out[4]);

/* Fill the whole 640x480 shadow framebuffer. fb must be 640x480. */
void hs_gbscale_blit(const struct hs_gbscale *s, struct hs_fb *fb,
                     const uint8_t *fb160, const uint32_t pal[4]);

/* Bottom HUD toast blended over the picture. alpha 0..256 (0 = nothing). */
void hs_gbscale_toast(struct hs_fb *fb, const char *text, int alpha);

#endif /* HS_GBSCALE_H */
