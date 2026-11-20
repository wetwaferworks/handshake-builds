/* SPDX-License-Identifier: MIT */
#include "hs_theme.h"

/* v2 palettes, tuned from the first real-H700 photos. v1 Deep Purple was
   blue-dominant (tile 0x2A1848, border 0x5E35B1: B > R by 0x30..0x7C) and
   read as cobalt on the RG35XX 2024 IPS. v2 lifts red so every mid tone
   is unmistakably violet, and makes "dim" bright enough to read. */
static const struct hs_palette k_themes[HS_THEME_COUNT] = {
    [HS_THEME_PURPLE] = {
        .name = "Deep Purple",
        .short_name = "Purple",
        .bg          = 0x1A082Eu,
        .bar         = 0x24103Du,
        .surface     = 0x220C3Au,
        .tile        = 0x2D124Bu,
        .tile_sel    = 0x4A1C78u,
        .border      = 0x7E3FC8u,
        .border_sel  = 0xC97BFFu,
        .accent      = 0xC97BFFu,
        .accent2     = 0xE8C2FFu,
        .text        = 0xFAF4FFu,
        .dim         = 0xD2B8F0u,
        .value       = 0xF0D6FFu,
        .warn        = 0xFFC24Du,
        .shadow      = 0x07020Fu,
        .good        = 0x7CFFB2u,
        .text_shadow = 0x07020Fu,
        .gb          = { 0xBE9FEAu, 0x8A4FD6u, 0x431774u, 0x0E0318u },
    },
    [HS_THEME_GREEN] = {
        .name = "Matrix Green",
        .short_name = "Green",
        .bg          = 0x020A02u,
        .bar         = 0x041204u,
        .surface     = 0x061606u,
        .tile        = 0x081E08u,
        .tile_sel    = 0x0F3A12u,
        .border      = 0x1F7A2Au,
        .border_sel  = 0x39FF5Au,
        .accent      = 0x39FF5Au,
        .accent2     = 0x9CFFAEu,
        .text        = 0xE6FFE6u,
        .dim         = 0xA8E6B0u,
        .value       = 0x39FF5Au,
        .warn        = 0xE6FF4Du,
        .shadow      = 0x010401u,
        .good        = 0x39FF5Au,
        .text_shadow = 0x010401u,
        .gb          = { 0x9EE89Au, 0x4FB84Cu, 0x1A5E24u, 0x021404u },
    },
};

const struct hs_palette *hs_theme_get(hs_theme_id_t id)
{
    if ((unsigned)id >= (unsigned)HS_THEME_COUNT) {
        id = HS_THEME_PURPLE;
    }
    return &k_themes[id];
}

hs_theme_id_t hs_theme_next(hs_theme_id_t id)
{
    return (hs_theme_id_t)(((unsigned)id + 1u) % (unsigned)HS_THEME_COUNT);
}

hs_theme_id_t hs_theme_prev(hs_theme_id_t id)
{
    return (hs_theme_id_t)(((unsigned)id + (unsigned)HS_THEME_COUNT - 1u)
                              % (unsigned)HS_THEME_COUNT);
}
