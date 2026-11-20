/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * make_handshake_demo_gb - builds roms/handshake-demo.gb (pure C11), an original MIT
 * Game Boy demo for HandshakeOS tests: a HandshakeOS title, a bouncing gem sprite, a D-pad
 * that nudges it and an A-button beep. No third-party assets. Header checksums computed here.
 * usage: make_handshake_demo_gb [OUT.gb]   (default roms/handshake-demo.gb)
 * Byte layout is fixed: tiles at 0x0500, code at 0x0150 (setup "head" + main loop). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE 32768
#define TILE_SRC 0x0500
static unsigned char rom[ROM_SIZE];

/* ---- byte buffer with labels / relative fixups ---- */
typedef struct { unsigned char b[2048]; int n; } buf;
static void B1(buf *o, int v) { if (o->n >= (int)sizeof o->b) { fputs("buffer overflow\n", stderr); exit(1); } o->b[o->n++] = (unsigned char)(v & 0xFF); }
static void B3(buf *o, int op, int imm) { B1(o, op); B1(o, imm & 0xFF); B1(o, (imm >> 8) & 0xFF); }
static void ld_sp(buf *o, int v) { B3(o, 0x31, v); }
static void ld_hl(buf *o, int v) { B3(o, 0x21, v); }
static void ld_de(buf *o, int v) { B3(o, 0x11, v); }
static void ld_bc(buf *o, int v) { B3(o, 0x01, v); }
static void ld_a(buf *o, int v) { B1(o, 0x3E); B1(o, v); }
static void ld_ann(buf *o, int a) { B3(o, 0xFA, a); }    /* LD A,(nn) */
static void ld_nna(buf *o, int a) { B3(o, 0xEA, a); }    /* LD (nn),A */
static int rel(int target, int from_after) {
    int d = target - from_after;
    if (d < -128 || d > 127) { fprintf(stderr, "rel out of range: %d\n", d); exit(1); }
    return d & 0xFF;
}

/* ---- loop body: two-pass labels ---- */
enum { L_LOOP, L_FLIPX, L_XLO, L_XHI, L_DONEY, L_FLIPY, L_YLO, L_YHI, L_DOJOY, L_NRIGHT, L_NLEFT, L_NUP, L_NDOWN, L_NOBEEP, L_N };
static int labels[L_N];
static struct { int idx, lab, after; } fixes[64];
static int nfix;
static void L(buf *o, int l) { labels[l] = o->n; }
static void JR(buf *o, int op, int l) { fixes[nfix].idx = o->n + 1; fixes[nfix].lab = l; fixes[nfix].after = o->n + 2; nfix++; B1(o, op); B1(o, 0); }
#define JRZ(o, l) JR(o, 0x28, l)
#define JR_(o, l) JR(o, 0x18, l)
#define JRC(o, l) JR(o, 0x38, l)
#define JRNC(o, l) JR(o, 0x30, l)

static void glyph(unsigned char *out, const char *const rows[8]) {
    int r, c;
    for (r = 0; r < 8; r++) {
        int bits = 0;
        for (c = 0; rows[r][c]; c++) bits = (bits << 1) | ((rows[r][c] == 'X' || rows[r][c] == '#') ? 1 : 0);
        out[r * 2] = (unsigned char)bits; out[r * 2 + 1] = (unsigned char)bits;   /* both planes: colours 0 or 3 */
    }
}

int main(int argc, char **argv) {
    static const char logo_hex[] = "CEED6666CC0D000B03730083000C000D0008111F8889000EDCCC6EE6DDDDD999BBBB67636E0EECCCDDDC999FBBB9333E";
    static const char *const gem[8] = { "........", "...XX...", "..XXXX..", ".XXXXXX.", ".XXXXXX.", "..XXXX..", "...XX...", "........" };
    static const char *const solid[8] = { "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX" };
    static const struct { char ch; const char *rows[8]; } font[] = {
        { 'A', { ".XXXX..", "X....X.", "X....X.", "XXXXXX.", "X....X.", "X....X.", "X....X.", "........" } },
        { 'D', { "XXXXX..", "X....X.", "X....X.", "X....X.", "X....X.", "X....X.", "XXXXX..", "........" } },
        { 'E', { "XXXXXX.", "X......", "X......", "XXXXX..", "X......", "X......", "XXXXXX.", "........" } },
        { 'H', { "X....X.", "X....X.", "X....X.", "XXXXXX.", "X....X.", "X....X.", "X....X.", "........" } },
        { 'I', { ".XXXX..", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", ".XXXX..", "........" } },
        { 'K', { "X....X.", "X...X..", "X..X...", "XXX....", "X..X...", "X...X..", "X....X.", "........" } },
        { 'L', { "X......", "X......", "X......", "X......", "X......", "X......", "XXXXXX.", "........" } },
        { 'M', { "X....X.", "XX..XX.", "X.XX.X.", "X....X.", "X....X.", "X....X.", "X....X.", "........" } },
        { 'N', { "X....X.", "XX...X.", "X.X..X.", "X..X.X.", "X...XX.", "X....X.", "X....X.", "........" } },
        { 'O', { ".XXXX..", "X....X.", "X....X.", "X....X.", "X....X.", "X....X.", ".XXXX..", "........" } },
        { 'S', { ".XXXX..", "X......", "X......", ".XXXX..", ".....X.", ".....X.", ".XXXX..", "........" } },
        { 'T', { "XXXXXX.", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "........" } },
        { ' ', { "........", "........", "........", "........", "........", "........", "........", "........" } },
    };
    /* glyph_order "ADEHIKLMNOST ": tile index 3 + position */
    const char *out = argc > 1 ? argv[1] : "roms/handshake-demo.gb";
    static buf head, body;
    unsigned char tiles[16 * 16];
    int glyph_index[128], i, ntiles = 0, row, base, x, s;
    const char *m;
    FILE *f;

    memset(tiles, 0, sizeof tiles);
    rom[0x0100] = 0x00; rom[0x0101] = 0xC3; rom[0x0102] = 0x50; rom[0x0103] = 0x01;   /* NOP; JP 0x0150 */
    rom[0x0040] = rom[0x0048] = rom[0x0050] = rom[0x0058] = rom[0x0060] = 0xD9;        /* RETI vectors */
    for (i = 0; i < 48; i++) {
        unsigned v;
        sscanf(logo_hex + i * 2, "%2X", &v);
        rom[0x0104 + i] = (unsigned char)v;
    }
    memcpy(rom + 0x0134, "HandshakeOS", 11);                                           /* padded to 15 with 0 */
    rom[0x014A] = 0x01; rom[0x014B] = 0x33;

    ntiles = 1;                                                                        /* 0: empty */
    glyph(tiles + 16 * ntiles++, gem);                                                 /* 1: gem */
    glyph(tiles + 16 * ntiles++, solid);                                               /* 2: solid */
    for (i = 0; i < 13; i++) { glyph(tiles + 16 * ntiles++, font[i].rows); glyph_index[(int)font[i].ch] = 3 + i; }
    memcpy(rom + TILE_SRC, tiles, (size_t)ntiles * 16);

    /* ---- head: setup, then the first (superseded) loop prologue, kept byte-for-byte ---- */
    B1(&head, 0xF3);
    ld_sp(&head, 0xFFFE);
    ld_hl(&head, TILE_SRC); ld_de(&head, 0x8000); ld_bc(&head, ntiles * 16);
    {
        int copy_start = head.n;
        B1(&head, 0x2A); B1(&head, 0x12); B1(&head, 0x13); B1(&head, 0x0B); B1(&head, 0x78); B1(&head, 0xB1);
        B1(&head, 0x20); B1(&head, rel(copy_start, head.n + 1));
    }
    ld_hl(&head, 0x9800); ld_bc(&head, 0x0400); B1(&head, 0xAF);
    {
        int clear_start = head.n;
        B1(&head, 0x22); B1(&head, 0x0B); B1(&head, 0x5F); B1(&head, 0x78); B1(&head, 0xB1); B1(&head, 0x7B);
        B1(&head, 0x20); B1(&head, rel(clear_start, head.n + 1));
    }
    ld_hl(&head, 0x9884);
    for (m = "HANDSHAKEOS"; *m; m++) { ld_a(&head, glyph_index[(int)*m]); B1(&head, 0x22); }
    ld_hl(&head, 0x98C8);
    for (m = "DEMO"; *m; m++) { ld_a(&head, glyph_index[(int)*m]); B1(&head, 0x22); }
    ld_hl(&head, 0x9800); ld_a(&head, 2); for (i = 0; i < 20; i++) B1(&head, 0x22);
    ld_hl(&head, 0x9800 + 17 * 32); ld_a(&head, 2); for (i = 0; i < 20; i++) B1(&head, 0x22);
    for (row = 1; row < 17; row++) {
        ld_hl(&head, 0x9800 + row * 32); ld_a(&head, 2); B1(&head, 0x22);
        ld_hl(&head, 0x9800 + row * 32 + 19); ld_a(&head, 2); B1(&head, 0x22);
    }
    ld_a(&head, 0xE4); ld_nna(&head, 0xFF47);
    ld_a(&head, 0xE4); ld_nna(&head, 0xFF48);
    ld_a(&head, 80); ld_nna(&head, 0xFE00);
    ld_a(&head, 80 + 16); ld_nna(&head, 0xFE00);
    ld_a(&head, 80 + 8); ld_nna(&head, 0xFE01);
    ld_a(&head, 1); ld_nna(&head, 0xFE02);
    ld_a(&head, 0); ld_nna(&head, 0xFE03);
    ld_a(&head, 80); ld_nna(&head, 0xFF80);
    ld_a(&head, 80); ld_nna(&head, 0xFF81);
    ld_a(&head, 1); ld_nna(&head, 0xFF82);
    ld_a(&head, 1); ld_nna(&head, 0xFF83);
    ld_a(&head, 0x93); ld_nna(&head, 0xFF40);
    ld_a(&head, 0x01); ld_nna(&head, 0xFFFF);
    B1(&head, 0xAF); ld_nna(&head, 0xFF0F);
    B1(&head, 0xFB);
    B1(&head, 0x76);
    ld_a(&head, 0x20); ld_nna(&head, 0xFF00);
    B1(&head, 0x00); B1(&head, 0x00); B1(&head, 0x00);
    ld_ann(&head, 0xFF00); B1(&head, 0x2F); B1(&head, 0xE6); B1(&head, 0x0F); B1(&head, 0x57);
    ld_ann(&head, 0xFF80); B1(&head, 0x5F);
    ld_ann(&head, 0xFF81); B1(&head, 0x6F);
    B1(&head, 0x7A); B1(&head, 0xE6); B1(&head, 0x01); B1(&head, 0x28); B1(&head, 0x06);
    B1(&head, 0x1C);
    B1(&head, 0x7B); B1(&head, 0xFE); B1(&head, 152); B1(&head, 0x38); B1(&head, 0x02); ld_a(&head, 152); B1(&head, 0x5F);

    /* ---- main loop ---- */
    L(&body, L_LOOP);
    B1(&body, 0x76);
    ld_ann(&body, 0xFF80); B1(&body, 0x47);
    ld_ann(&body, 0xFF82); B1(&body, 0x80);
    ld_nna(&body, 0xFF80);
    B1(&body, 0xFE); B1(&body, 8); JRC(&body, L_FLIPX);
    B1(&body, 0xFE); B1(&body, 152); JRNC(&body, L_FLIPX);
    JR_(&body, L_DONEY);
    L(&body, L_FLIPX);
    ld_ann(&body, 0xFF82); B1(&body, 0x2F); B1(&body, 0x3C); ld_nna(&body, 0xFF82);
    ld_ann(&body, 0xFF80); B1(&body, 0xFE); B1(&body, 8); JRNC(&body, L_XLO);
    ld_a(&body, 8); ld_nna(&body, 0xFF80);
    L(&body, L_XLO);
    ld_ann(&body, 0xFF80); B1(&body, 0xFE); B1(&body, 152); JRC(&body, L_XHI);
    ld_a(&body, 152); ld_nna(&body, 0xFF80);
    L(&body, L_XHI);
    L(&body, L_DONEY);
    ld_ann(&body, 0xFF81); B1(&body, 0x47);
    ld_ann(&body, 0xFF83); B1(&body, 0x80);
    ld_nna(&body, 0xFF81);
    B1(&body, 0xFE); B1(&body, 24); JRC(&body, L_FLIPY);
    B1(&body, 0xFE); B1(&body, 120); JRNC(&body, L_FLIPY);
    JR_(&body, L_DOJOY);
    L(&body, L_FLIPY);
    ld_ann(&body, 0xFF83); B1(&body, 0x2F); B1(&body, 0x3C); ld_nna(&body, 0xFF83);
    ld_ann(&body, 0xFF81); B1(&body, 0xFE); B1(&body, 24); JRNC(&body, L_YLO);
    ld_a(&body, 24); ld_nna(&body, 0xFF81);
    L(&body, L_YLO);
    ld_ann(&body, 0xFF81); B1(&body, 0xFE); B1(&body, 120); JRC(&body, L_YHI);
    ld_a(&body, 120); ld_nna(&body, 0xFF81);
    L(&body, L_YHI);
    L(&body, L_DOJOY);
    ld_a(&body, 0x20); ld_nna(&body, 0xFF00);
    B1(&body, 0x00); B1(&body, 0x00);
    ld_ann(&body, 0xFF00); B1(&body, 0x2F); B1(&body, 0xE6); B1(&body, 0x0F); B1(&body, 0x57);
    B1(&body, 0x7A); B1(&body, 0xE6); B1(&body, 0x01); JRZ(&body, L_NRIGHT);
    ld_ann(&body, 0xFF80); B1(&body, 0x3C); ld_nna(&body, 0xFF80);
    L(&body, L_NRIGHT);
    B1(&body, 0x7A); B1(&body, 0xE6); B1(&body, 0x02); JRZ(&body, L_NLEFT);
    ld_ann(&body, 0xFF80); B1(&body, 0x3D); ld_nna(&body, 0xFF80);
    L(&body, L_NLEFT);
    B1(&body, 0x7A); B1(&body, 0xE6); B1(&body, 0x04); JRZ(&body, L_NUP);
    ld_ann(&body, 0xFF81); B1(&body, 0x3D); ld_nna(&body, 0xFF81);
    L(&body, L_NUP);
    B1(&body, 0x7A); B1(&body, 0xE6); B1(&body, 0x08); JRZ(&body, L_NDOWN);
    ld_ann(&body, 0xFF81); B1(&body, 0x3C); ld_nna(&body, 0xFF81);
    L(&body, L_NDOWN);
    ld_ann(&body, 0xFF81); B1(&body, 0xC6); B1(&body, 16); ld_nna(&body, 0xFE00);
    ld_ann(&body, 0xFF80); B1(&body, 0xC6); B1(&body, 8); ld_nna(&body, 0xFE01);
    ld_a(&body, 0x10); ld_nna(&body, 0xFF00);
    B1(&body, 0x00); B1(&body, 0x00);
    ld_ann(&body, 0xFF00); B1(&body, 0x2F); B1(&body, 0xE6); B1(&body, 0x01); JRZ(&body, L_NOBEEP);
    ld_a(&body, 0x80); ld_nna(&body, 0xFF26);
    ld_a(&body, 0x77); ld_nna(&body, 0xFF24);
    ld_a(&body, 0x11); ld_nna(&body, 0xFF25);
    ld_a(&body, 0x00); ld_nna(&body, 0xFF10);
    ld_a(&body, 0x80); ld_nna(&body, 0xFF11);
    ld_a(&body, 0xF3); ld_nna(&body, 0xFF12);
    ld_a(&body, 0xC1); ld_nna(&body, 0xFF13);
    ld_a(&body, 0x87); ld_nna(&body, 0xFF14);
    L(&body, L_NOBEEP);
    B3(&body, 0xC3, 0);                                                                /* JP loop (patched) */
    for (i = 0; i < nfix; i++) body.b[fixes[i].idx] = (unsigned char)rel(labels[fixes[i].lab], fixes[i].after);
    base = 0x0150 + head.n;
    body.b[body.n - 2] = (unsigned char)((base + labels[L_LOOP]) & 0xFF);
    body.b[body.n - 1] = (unsigned char)(((base + labels[L_LOOP]) >> 8) & 0xFF);
    if (0x0150 + head.n + body.n >= TILE_SRC) { fprintf(stderr, "code overflow\n"); return 1; }
    memcpy(rom + 0x0150, head.b, (size_t)head.n);
    memcpy(rom + 0x0150 + head.n, body.b, (size_t)body.n);
    printf("code %d bytes, ends at 0x%x\n", head.n + body.n, 0x0150 + head.n + body.n);

    x = 0;                                                                             /* header checksum */
    for (i = 0x0134; i < 0x014D; i++) x = (x - rom[i] - 1) & 0xFF;
    rom[0x014D] = (unsigned char)x;
    rom[0x014E] = rom[0x014F] = 0;                                                     /* global checksum */
    for (s = 0, i = 0; i < ROM_SIZE; i++) s += rom[i];
    s &= 0xFFFF;
    rom[0x014E] = (unsigned char)(s >> 8); rom[0x014F] = (unsigned char)s;
    f = fopen(out, "wb");
    if (!f || fwrite(rom, 1, ROM_SIZE, f) != ROM_SIZE || fclose(f)) { perror(out); return 1; }
    printf("wrote %s size %d header_ck %02X global %04X\n", out, ROM_SIZE, x, s);
    return 0;
}
