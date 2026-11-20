/* SPDX-License-Identifier: MIT */
/* v5.5: CGB boot-ROM DMG colorization — Yellow, unknown default, curve. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gbc_compat_pal.h"

#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fails++; \
    } \
} while (0)

static void put_title(uint8_t *rom, const char *title, uint8_t old_lic)
{
    size_t i, n;
    memset(rom, 0, 0x200);
    n = strlen(title);
    if (n > 16) n = 16;
    for (i = 0; i < n; i++) {
        rom[0x134 + i] = (uint8_t)title[i];
    }
    rom[0x14B] = old_lic;
}

static int pal_eq(const uint16_t a[4], uint16_t c0, uint16_t c1, uint16_t c2, uint16_t c3)
{
    return a[0] == c0 && a[1] == c1 && a[2] == c2 && a[3] == c3;
}

int main(void)
{
    uint8_t rom[0x200];
    struct hs_dmg_compat p;
    unsigned sum;
    uint32_t raw, lcd;

    /* --- Pokémon Yellow header -> combination 3, palette 24 x 3. --- */
    put_title(rom, "POKEMON YELLOW", 0x01);
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    sum = 0;
    { unsigned i; for (i = 0x134; i <= 0x143; i++) sum = (sum + rom[i]) & 0xFFu; }
    printf("YELLOW checksum=0x%02X combo=%u default=%d yellow=%d "
           "bg=%04X,%04X,%04X,%04X obp0=%04X,%04X,%04X,%04X obp1=%04X,%04X,%04X,%04X\n",
           p.checksum, p.combo, p.used_default, p.is_yellow,
           p.bg[0], p.bg[1], p.bg[2], p.bg[3],
           p.obp0[0], p.obp0[1], p.obp0[2], p.obp0[3],
           p.obp1[0], p.obp1[1], p.obp1[2], p.obp1[3]);
    CHECK(p.is_yellow == 1);
    CHECK(p.used_default == 0);
    CHECK(p.combo == HS_DMG_COMPAT_COMBO_YELLOW);
    CHECK(p.checksum == sum);
    CHECK(sum == 0x15u); /* "POKEMON YELLOW" + two $00 pads */
    /* Palette 24: white, yellow, red, black. All three layers identical. */
    CHECK(pal_eq(p.bg,   0x7FFF, 0x03FF, 0x001F, 0x0000));
    CHECK(pal_eq(p.obp0, 0x7FFF, 0x03FF, 0x001F, 0x0000));
    CHECK(pal_eq(p.obp1, 0x7FFF, 0x03FF, 0x001F, 0x0000));

    /* CGB flag in the last title byte must not hide Yellow (matched first
       on the 14-character title, not the checksum). */
    rom[0x143] = 0x80;
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    CHECK(p.is_yellow == 1);
    CHECK(p.combo == HS_DMG_COMPAT_COMBO_YELLOW);
    CHECK(pal_eq(p.bg, 0x7FFF, 0x03FF, 0x001F, 0x0000));

    /* --- Unknown Nintendo title -> default combination $00. --- */
    put_title(rom, "UNKNOWN CART!!", 0x01);
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    printf("UNKNOWN combo=%u default=%d yellow=%d bg=%04X,%04X,%04X,%04X\n",
           p.combo, p.used_default, p.is_yellow,
           p.bg[0], p.bg[1], p.bg[2], p.bg[3]);
    CHECK(p.is_yellow == 0);
    CHECK(p.used_default == 1);
    CHECK(p.combo == HS_DMG_COMPAT_COMBO_DEFAULT);
    /* Combination 0 = OBJ0/OBJ1 palette 4, BG palette 29. */
    CHECK(pal_eq(p.obp0, 0x7FFF, 0x421F, 0x1CF2, 0x0000));
    CHECK(pal_eq(p.obp1, 0x7FFF, 0x421F, 0x1CF2, 0x0000));
    CHECK(pal_eq(p.bg,   0x7FFF, 0x1BEF, 0x6180, 0x0000));

    /* Non-Nintendo licensee is also the default, even with a known title
       checksum (e.g. a homebrew that spells TETRIS). */
    put_title(rom, "TETRIS", 0x00);
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    CHECK(p.used_default == 1);
    CHECK(p.combo == 0);

    /* Tetris, Nintendo, unique checksum 0xDB -> combination 3 (same
       colors as Yellow, but is_yellow is 0). */
    put_title(rom, "TETRIS", 0x01);
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    CHECK(p.is_yellow == 0);
    CHECK(p.used_default == 0);
    CHECK(p.combo == 3);
    CHECK(pal_eq(p.bg, 0x7FFF, 0x03FF, 0x001F, 0x0000));

    /* Pokémon Red: checksum 0x14, combo 13 = OBJ0 pal 3, OBJ1/BG pal 4. */
    put_title(rom, "POKEMON RED", 0x01);
    /* "POKEMON RED" is 11 letters; confirm checksum 0x14. */
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    printf("RED checksum=0x%02X combo=%u\n", p.checksum, p.combo);
    CHECK(p.checksum == 0x14u);
    CHECK(p.combo == 13);
    CHECK(pal_eq(p.obp0, 0x7FFF, 0x1BEF, 0x0200, 0x0000));
    CHECK(pal_eq(p.obp1, 0x7FFF, 0x421F, 0x1CF2, 0x0000));
    CHECK(pal_eq(p.bg,   0x7FFF, 0x421F, 0x1CF2, 0x0000));

    /* Pokémon Blue vs Vegas Stakes: same checksum 0x61, 4th letter. */
    put_title(rom, "POKEMON BLUE", 0x01);
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    printf("BLUE checksum=0x%02X fourth=%c combo=%u\n",
           p.checksum, rom[0x137], p.combo);
    CHECK(p.checksum == 0x61u);
    CHECK(rom[0x137] == (uint8_t)'E');
    CHECK(p.combo == 11); /* palettes 4, 28, 28 */
    CHECK(pal_eq(p.obp0, 0x7FFF, 0x421F, 0x1CF2, 0x0000));
    CHECK(pal_eq(p.obp1, 0x7FFF, 0x7E8C, 0x7C00, 0x0000));
    CHECK(pal_eq(p.bg,   0x7FFF, 0x7E8C, 0x7C00, 0x0000));

    put_title(rom, "VEGAS STAKES", 0x01);
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    printf("VEGAS checksum=0x%02X fourth=%c combo=%u\n",
           p.checksum, rom[0x137], p.combo);
    CHECK(p.checksum == 0x61u);
    CHECK(rom[0x137] == (uint8_t)'A');
    CHECK(p.combo == 41);

    /* New licensee "01" is Nintendo. */
    put_title(rom, "TETRIS", 0x33);
    rom[0x144] = '0';
    rom[0x145] = '1';
    hs_dmg_compat_from_rom(rom, sizeof(rom), &p);
    CHECK(p.combo == 3);
    CHECK(p.used_default == 0);

    /* --- Color curve vs raw expand. --- */
    /* $03EF = R=0x0F, G=0x1F, B=0x00. Raw 5-to-8: R=0x7B, G=0xFF, B=0x00
       (neon green). The LCD curve must mix red+green into a washed yellow:
       blue is no longer 0, green is no longer 255, red is lifted. */
    raw = hs_rgb555_raw888(0x03EF);
    lcd = hs_cgb_lcd_rgb888(0x03EF);
    printf("CURVE $03EF raw=%06X lcd=%06X (R=%u G=%u B=%u)\n",
           raw, lcd, (lcd >> 16) & 255u, (lcd >> 8) & 255u, lcd & 255u);
    CHECK(raw == 0x007BFF00u);
    CHECK(((lcd >> 16) & 255u) > 80u);            /* red is clearly present */
    CHECK(((lcd >> 8) & 255u) > ((lcd >> 16) & 255u)); /* still green-ish */
    CHECK((lcd & 255u) > 20u && (lcd & 255u) < 80u);   /* mixed-in blue */
    CHECK(((lcd >> 8) & 255u) <= 240u);           /* peak is not 255 */

    /* White $7FFF: peak is 240, not 255 (light gray, not sRGB white). */
    lcd = hs_cgb_lcd_rgb888(0x7FFF);
    printf("CURVE $7FFF lcd=%06X\n", lcd);
    CHECK(lcd == 0x00F0F0F0u);

    /* Black stays black. */
    CHECK(hs_cgb_lcd_rgb888(0x0000) == 0);

    /* Yellow mid color 0x03FF (R=31,G=31,B=0) must not clip, and must
       pick up a little blue from the mixing so it is less "laser yellow"
       than the raw expand. */
    raw = hs_rgb555_raw888(0x03FF);
    lcd = hs_cgb_lcd_rgb888(0x03FF);
    printf("CURVE $03FF raw=%06X lcd=%06X\n", raw, lcd);
    CHECK(raw == 0x00FFFF00u);
    CHECK((lcd & 255u) > 0);                      /* some blue mixed in */
    CHECK(((lcd >> 16) & 255u) <= 240u);

    if (fails == 0) {
        printf("DMG_COMPAT checks passed yellow=combo3 unknown=default curve=cgb-lcd\n");
        printf("ALL DMG COMPAT TESTS PASSED\n");
        return 0;
    }
    printf("%d checks failed\n", fails);
    return 1;
}
