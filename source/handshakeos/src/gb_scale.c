/* SPDX-License-Identifier: MIT */
/* HandshakeOS v3 full-screen Game Boy scaler. Pure integer math: the only
   divisions happen once in hs_gbscale_init() while building the table. */
#include "hs_gbscale.h"
#include "hs_fb.h"
#include "hs_gb.h"

#include <string.h>

#define OUT_W 640
#define OUT_H 480

static const uint32_t k_dmg_pals[HS_DMG_PAL_COUNT][4] = {
    [HS_DMG_PAL_OLIVE]  = { 0x9BBC0Fu, 0x8BAC0Fu, 0x306230u, 0x0F380Fu },
    [HS_DMG_PAL_POCKET] = { 0xF4F4F0u, 0xA8A8A0u, 0x55554Fu, 0x0C0C0Cu },
    [HS_DMG_PAL_PURPLE] = { 0xBE9FEAu, 0x8A4FD6u, 0x431774u, 0x0E0318u },
    [HS_DMG_PAL_GREEN]  = { 0xB8FFC8u, 0x3FD46Au, 0x0E6B2Au, 0x021A08u },
};

static const char *const k_dmg_names[HS_DMG_PAL_COUNT] = {
    "CLASSIC OLIVE", "POCKET B&W", "DEEP PURPLE", "MATRIX GREEN"
};

static const char *const k_mode_names[HS_GBSCALE_COUNT] = {
    "UPSAMPLED", "SHARP NEAREST"
};

const char *hs_gbscale_mode_name(hs_gbscale_mode_t m)
{
    return (unsigned)m < (unsigned)HS_GBSCALE_COUNT ? k_mode_names[m] : "?";
}

const char *hs_dmg_pal_name(hs_dmg_pal_t p)
{
    return (unsigned)p < (unsigned)HS_DMG_PAL_COUNT ? k_dmg_names[p] : "?";
}

void hs_dmg_pal_get(hs_dmg_pal_t p, uint32_t out[4])
{
    int i;

    if ((unsigned)p >= (unsigned)HS_DMG_PAL_COUNT) {
        p = HS_DMG_PAL_OLIVE;
    }
    for (i = 0; i < 4; i++) {
        out[i] = k_dmg_pals[p][i];
    }
}

void hs_gbscale_init(struct hs_gbscale *s)
{
    int y;

    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->mode = HS_GBSCALE_UPSAMPLED;
    for (y = 0; y < OUT_H; y++) {
        /* Work in units of 1/480 of a source row: output row y spans
           [y*144, y*144 + 144); source row k spans [k*480, k*480 + 480). */
        uint32_t start = (uint32_t)y * (uint32_t)HS_GB_H;
        uint32_t end = start + (uint32_t)HS_GB_H;
        uint32_t y0 = start / (uint32_t)OUT_H;
        uint32_t bnd = (y0 + 1u) * (uint32_t)OUT_H;
        struct hs_gbscale_row *u = &s->lut[HS_GBSCALE_UPSAMPLED][y];
        struct hs_gbscale_row *n = &s->lut[HS_GBSCALE_NEAREST][y];

        u->y0 = (uint8_t)y0;
        if (end <= bnd || y0 + 1u >= (uint32_t)HS_GB_H) {
            u->y1 = (uint8_t)y0;
            u->w0 = 256u;
            u->w1 = 0u;
        } else {
            uint32_t w0 = ((bnd - start) * 256u + (uint32_t)HS_GB_H / 2u) / (uint32_t)HS_GB_H;
            u->y1 = (uint8_t)(y0 + 1u);
            u->w0 = (uint16_t)w0;
            u->w1 = (uint16_t)(256u - w0);
        }
        /* Nearest: sample the source row under the output row centre. */
        n->y0 = (uint8_t)((start + (uint32_t)HS_GB_H / 2u) / (uint32_t)OUT_H);
        n->y1 = n->y0;
        n->w0 = 256u;
        n->w1 = 0u;
    }
}

static inline uint32_t blend(uint32_t a, uint32_t b, uint32_t wa, uint32_t wb)
{
    uint32_t rb = (((a & 0xFF00FFu) * wa + (b & 0xFF00FFu) * wb) >> 8) & 0xFF00FFu;
    uint32_t g = (((a & 0x00FF00u) * wa + (b & 0x00FF00u) * wb) >> 8) & 0x00FF00u;

    return rb | g;
}

void hs_gbscale_blit(const struct hs_gbscale *s, struct hs_fb *fb,
                     const uint8_t *fb160, const uint32_t pal[4])
{
    const struct hs_gbscale_row *lut;
    uint32_t line[HS_GB_W];
    int y, x;
    int prev_y0 = -1, prev_y1 = -1, prev_w = -1;
    uint32_t *prev_row = NULL;

    if (s == NULL || fb == NULL || fb->pix == NULL || fb160 == NULL || pal == NULL ||
        fb->w != OUT_W || fb->h != OUT_H) {
        return;
    }
    lut = s->lut[(unsigned)s->mode < (unsigned)HS_GBSCALE_COUNT ? s->mode : 0];
    for (y = 0; y < OUT_H; y++) {
        const struct hs_gbscale_row *r = &lut[y];
        uint32_t *row = (uint32_t *)((uint8_t *)fb->pix + (size_t)y * (size_t)fb->stride_bytes);

        if (prev_row != NULL && r->y0 == prev_y0 && r->y1 == prev_y1 && r->w0 == prev_w) {
            memcpy(row, prev_row, (size_t)OUT_W * 4u);
            prev_row = row;
            continue;
        }
        {
            const uint8_t *a = fb160 + (size_t)r->y0 * HS_GB_W;
            if (r->w1 == 0u) {
                for (x = 0; x < HS_GB_W; x++) {
                    line[x] = pal[a[x] & 3u];
                }
            } else {
                const uint8_t *b = fb160 + (size_t)r->y1 * HS_GB_W;
                for (x = 0; x < HS_GB_W; x++) {
                    line[x] = blend(pal[a[x] & 3u], pal[b[x] & 3u], r->w0, r->w1);
                }
            }
        }
        for (x = 0; x < HS_GB_W; x++) {
            uint32_t c = line[x];
            row[4 * x + 0] = c;
            row[4 * x + 1] = c;
            row[4 * x + 2] = c;
            row[4 * x + 3] = c;
        }
        prev_y0 = r->y0;
        prev_y1 = r->y1;
        prev_w = r->w0;
        prev_row = row;
    }
}

void hs_gbscale_toast(struct hs_fb *fb, const char *text, int alpha)
{
    int y, x, band_y, band_h = 44, tw, tx;
    uint32_t a, ia, fg;

    if (fb == NULL || fb->pix == NULL || text == NULL || alpha <= 0) {
        return;
    }
    if (alpha > 256) {
        alpha = 256;
    }
    band_y = fb->h - band_h - 12;
    /* Darken the band to ~25% at full alpha. */
    a = (uint32_t)alpha * 3u / 4u;
    ia = 256u - a;
    for (y = band_y; y < band_y + band_h && y < fb->h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb->pix + (size_t)y * (size_t)fb->stride_bytes);
        for (x = 24; x < fb->w - 24; x++) {
            row[x] = blend(row[x], 0x000000u, ia, a);
        }
    }
    tw = (int)strlen(text) * 16;
    tx = (fb->w - tw) / 2;
    if (tx < 28) {
        tx = 28;
    }
    if (alpha >= 48) {
        fg = blend(0xFFFFFFu, 0x202020u, (uint32_t)alpha, 256u - (uint32_t)alpha);
        (void)hs_fb_text_ex(fb, tx, band_y + 14, 2, fg, 0x000000u, HS_TEXT_NICE, text);
    }
}

void hs_gbscale_toast_bar(struct hs_fb *fb, const char *text, int pct, int alpha)
{
    int band_y, x0, x1, xf, y, x;
    uint32_t a, ia;

    if (fb == NULL || fb->pix == NULL || alpha <= 0) {
        return;
    }
    if (alpha > 256) {
        alpha = 256;
    }
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    hs_gbscale_toast(fb, text, alpha);
    band_y = fb->h - 44 - 12;
    /* bar under the text, inside the band: 400 px wide, 6 px tall */
    x0 = (fb->w - 400) / 2;
    x1 = x0 + 400;
    xf = x0 + pct * 4;
    a = (uint32_t)alpha;
    ia = 256u - a;
    for (y = band_y + 34; y < band_y + 40 && y < fb->h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb->pix + (size_t)y * (size_t)fb->stride_bytes);
        for (x = x0; x < x1; x++) {
            uint32_t c = x < xf ? 0xFFFFFFu : 0x505050u;
            row[x] = blend(row[x], c, ia, a);
        }
    }
}
