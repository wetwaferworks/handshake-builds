/* SPDX-License-Identifier: MIT */
#ifndef HS_THEME_H
#define HS_THEME_H

#include <stdint.h>

typedef enum {
    HS_THEME_PURPLE = 0,
    HS_THEME_GREEN  = 1,
    HS_THEME_COUNT
} hs_theme_id_t;

struct hs_palette {
    const char *name;
    const char *short_name;
    uint32_t bg;
    uint32_t bar;
    uint32_t surface;
    uint32_t tile;
    uint32_t tile_sel;
    uint32_t border;
    uint32_t border_sel;
    uint32_t accent;
    uint32_t accent2;
    uint32_t text;
    uint32_t dim;
    uint32_t value;
    uint32_t warn;
    uint32_t shadow;
    uint32_t good;
    uint32_t text_shadow; /* v2: drop shadow behind every glyph */
    uint32_t gb[4];       /* v2: theme-aware high-contrast DMG shades 0..3 */
};

static inline uint32_t hs_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

const struct hs_palette *hs_theme_get(hs_theme_id_t id);
hs_theme_id_t hs_theme_next(hs_theme_id_t id);
hs_theme_id_t hs_theme_prev(hs_theme_id_t id);

#endif /* HS_THEME_H */
