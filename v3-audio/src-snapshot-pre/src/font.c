/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include "hs_font.h"
#include "hs_font_data.h"

const uint8_t *hs_font_glyph(int c)
{
    if (c < HS_FONT_FIRST || c > HS_FONT_LAST) {
        c = '?';
    }
    return hs_font8[c - HS_FONT_FIRST];
}

int hs_font_text_width(const char *s, int scale)
{
    int n = 0;

    if (s == NULL || scale < 1) {
        return 0;
    }
    while (*s != '\0') {
        n++;
        s++;
    }
    return n * HS_FONT_W * scale;
}
