/* SPDX-License-Identifier: MIT
 * HandshakeOS build-time splash packer: decode CloudAtlas o.O PNGs (pure C,
 * hs_pngread.h, no Python) into a single RGB565 pack plus a built-in 38x9
 * "HandshakeOS" / o.O ASCII animation. Public branding is HandshakeOS only. */
#include "hs_pngread.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define ASCII_C 38
#define ASCII_R 9
#define ASCII_FRAMES 8

struct ent {
    char id[16];
    char title[48];
    char artist[32];
    uint8_t type;  /* 0 graphic, 1 ascii */
    uint8_t theme; /* 0 purple, 1 green */
    uint16_t nframes;
    uint32_t off, size;
    uint8_t *data;
};

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void wr_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static void put_str(char *dst, size_t n, const char *s)
{
    memset(dst, 0, n);
    if (s) {
        size_t L = strlen(s);
        if (L >= n) L = n - 1;
        memcpy(dst, s, L);
    }
}

/* 8 frames, 38x9, public text "HandshakeOS" only. Eyes of o.O blink. */
static const char *ascii_frames[ASCII_FRAMES] = {
/*0*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "               o.O                    "
      "                                      "
      "                                      "
      "                                      "
      "                                      ",
/*1*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "               o.o                    "
      "                                      "
      "                                      "
      "                                      "
      "                                      ",
/*2*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "               O.o                    "
      "                                      "
      "                                      "
      "                                      "
      "                                      ",
/*3*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "               O.O                    "
      "                                      "
      "                                      "
      "                                      "
      "                                      ",
/*4*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "              (o.O)                   "
      "                                      "
      "                                      "
      "                                      "
      "                                      ",
/*5*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "               o.O                    "
      "                                      "
      "                                      "
      "                                      "
      "                                      ",
/*6*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "              *o.O*                   "
      "                                      "
      "                                      "
      "                                      "
      "                                      ",
/*7*/ "                                      "
      "                                      "
      "           HandshakeOS                "
      "                                      "
      "               o.O                    "
      "                                      "
      "                                      "
      "                                      "
      "                                      "
};

static int pack_ascii(struct ent *e)
{
    size_t n = (size_t)ASCII_FRAMES * ASCII_C * ASCII_R;
    uint8_t *d = (uint8_t *)calloc(1, n);
    int f;
    if (!d) return -1;
    for (f = 0; f < ASCII_FRAMES; f++) {
        size_t L = strlen(ascii_frames[f]);
        if (L != (size_t)ASCII_C * ASCII_R) {
            fprintf(stderr, "ascii frame %d length %zu want %d\n", f, L, ASCII_C * ASCII_R);
            free(d);
            return -1;
        }
        memcpy(d + (size_t)f * ASCII_C * ASCII_R, ascii_frames[f], L);
    }
    put_str(e->id, sizeof(e->id), "ascii-oo");
    put_str(e->title, sizeof(e->title), "HandshakeOS o.O");
    put_str(e->artist, sizeof(e->artist), "HandshakeOS");
    e->type = 1;
    e->theme = 0;
    e->nframes = ASCII_FRAMES;
    e->size = (uint32_t)n;
    e->data = d;
    return 0;
}

static int pack_png(struct ent *e, const char *id, const char *title,
                    const char *artist, int green, const char *path)
{
    int w = 0, h = 0, x, y;
    uint8_t *rgb = NULL;
    uint16_t *px;
    if (hs_png_load_rgb(path, &w, &h, &rgb) != 0 || !rgb) {
        fprintf(stderr, "png decode failed: %s\n", path);
        return -1;
    }
    if (w != W || h != H) {
        fprintf(stderr, "png %s is %dx%d, want %dx%d\n", path, w, h, W, H);
        free(rgb);
        return -1;
    }
    px = (uint16_t *)malloc((size_t)W * H * 2);
    if (!px) { free(rgb); return -1; }
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            const uint8_t *s = rgb + ((size_t)y * W + (size_t)x) * 3;
            px[(size_t)y * W + (size_t)x] = rgb565(s[0], s[1], s[2]);
        }
    }
    free(rgb);
    put_str(e->id, sizeof(e->id), id);
    put_str(e->title, sizeof(e->title), title);
    put_str(e->artist, sizeof(e->artist), artist);
    e->type = 0;
    e->theme = green ? 1 : 0;
    e->nframes = 1;
    e->size = (uint32_t)W * H * 2;
    e->data = (uint8_t *)px;
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir, *out;
    struct ent e[9];
    int n = 0, i;
    uint32_t dir_sz, off, total;
    uint8_t *blob;
    FILE *fp;
    static const struct { const char *id, *title, *file; int green; } gfx[] = {
        { "01", "Silicon Wafer Pulse",    "01-silicon-wafer-pulse-purple.png", 0 },
        { "02", "Field of Handshakes",    "02-field-of-handshakes-purple.png", 0 },
        { "03", "Handheld Phosphor",      "03-handheld-phosphor-purple.png", 0 },
        { "04", "Timing Loop Gears",      "04-timing-loop-gears-purple.png", 0 },
        { "05", "Minimal Swiss Signal",   "05-minimal-swiss-signal-purple.png", 0 },
        { "06", "Cyber Owl Sprite",       "06-cyber-owl-sprite-purple.png", 0 },
        { "07", "Oscilloscope Dual-Wave", "07-oscilloscope-dual-wave-green.png", 1 },
        { "08", "Liquid Glass Orb",       "08-liquid-glass-orb-purple.png", 0 },
    };
    memset(e, 0, sizeof(e));
    if (argc != 3) {
        fprintf(stderr, "usage: hs-pack-splash <splash-dir> <out-pack.bin>\n");
        return 2;
    }
    dir = argv[1];
    out = argv[2];
    for (i = 0; i < 8; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, gfx[i].file);
        if (pack_png(&e[n], gfx[i].id, gfx[i].title, "CloudAtlas", gfx[i].green, path) != 0)
            return 1;
        n++;
    }
    if (pack_ascii(&e[n]) != 0) return 1;
    n++;

    dir_sz = 32 + (uint32_t)n * 112;
    off = dir_sz;
    for (i = 0; i < n; i++) {
        e[i].off = off;
        off += e[i].size;
    }
    total = off;
    blob = (uint8_t *)calloc(1, total);
    if (!blob) return 1;
    memcpy(blob, "HSPLASH1", 8);
    wr_le32(blob + 8, (uint32_t)n);
    wr_le32(blob + 12, W);
    wr_le32(blob + 16, H);
    wr_le32(blob + 20, 1); /* format version */
    for (i = 0; i < n; i++) {
        uint8_t *d = blob + 32 + (size_t)i * 112;
        memcpy(d + 0, e[i].id, 16);
        memcpy(d + 16, e[i].title, 48);
        memcpy(d + 64, e[i].artist, 32);
        d[96] = e[i].type;
        d[97] = e[i].theme;
        wr_le16(d + 98, e[i].nframes);
        wr_le32(d + 100, e[i].off);
        wr_le32(d + 104, e[i].size);
        memcpy(blob + e[i].off, e[i].data, e[i].size);
    }
    fp = fopen(out, "wb");
    if (!fp) { perror(out); return 1; }
    if (fwrite(blob, 1, total, fp) != total) { fclose(fp); return 1; }
    fclose(fp);
    printf("PACKED entries=%d bytes=%u out=%s\n", n, total, out);
    for (i = 0; i < n; i++) {
        printf("  %s type=%s theme=%s artist=%s title=%s size=%u frames=%u\n",
               e[i].id, e[i].type ? "ascii" : "graphic",
               e[i].theme ? "green" : "purple", e[i].artist, e[i].title,
               e[i].size, e[i].nframes);
        free(e[i].data);
    }
    free(blob);
    return 0;
}
