/* SPDX-License-Identifier: MIT */
#ifndef HS_FONT_INCLUDED
#define HS_FONT_INCLUDED

#include <stdint.h>

#define HS_FONT_W 8
#define HS_FONT_H 8

/* Returns the 8 row bytes for ASCII c, or the glyph for '?' if out of range. */
const uint8_t *hs_font_glyph(int c);

int hs_font_text_width(const char *s, int scale);

#endif /* HS_FONT_INCLUDED */
