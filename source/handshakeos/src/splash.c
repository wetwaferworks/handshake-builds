/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_splash.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void hs_splash_init(struct hs_splash *s)
{
    memset(s, 0, sizeof(*s));
}

void hs_splash_free(struct hs_splash *s)
{
    if (s == NULL) return;
    free(s->pack);
    memset(s, 0, sizeof(*s));
}

int hs_splash_load(struct hs_splash *s, const char *path)
{
    int fd;
    off_t sz;
    long r, off;
    uint32_t n, w, h, ver, i;
    const char *p = path && path[0] ? path : HS_SPLASH_PACK;

    hs_splash_init(s);
    fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        snprintf(s->err, sizeof(s->err), "missing (%d)", errno);
        return -1;
    }
    sz = lseek(fd, 0, SEEK_END);
    if (sz < 32 || sz > 16 * 1024 * 1024) {
        close(fd);
        snprintf(s->err, sizeof(s->err), "bad size");
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        snprintf(s->err, sizeof(s->err), "seek");
        return -1;
    }
    s->pack = (uint8_t *)malloc((size_t)sz);
    if (s->pack == NULL) {
        close(fd);
        snprintf(s->err, sizeof(s->err), "oom");
        return -1;
    }
    off = 0;
    while (off < sz) {
        r = read(fd, s->pack + off, (size_t)(sz - off));
        if (r <= 0) break;
        off += r;
    }
    close(fd);
    s->pack_len = (size_t)off;
    if (off != sz || memcmp(s->pack, "HSPLASH1", 8) != 0) {
        snprintf(s->err, sizeof(s->err), "bad magic");
        free(s->pack); s->pack = NULL; s->pack_len = 0;
        return -1;
    }
    n = rd_le32(s->pack + 8);
    w = rd_le32(s->pack + 12);
    h = rd_le32(s->pack + 16);
    ver = rd_le32(s->pack + 20);
    if (n == 0 || n > HS_SPLASH_MAX || w != 640 || h != 480 || ver != 1) {
        snprintf(s->err, sizeof(s->err), "bad header");
        free(s->pack); s->pack = NULL; s->pack_len = 0;
        return -1;
    }
    if (32u + n * 112u > s->pack_len) {
        snprintf(s->err, sizeof(s->err), "truncated dir");
        free(s->pack); s->pack = NULL; s->pack_len = 0;
        return -1;
    }
    for (i = 0; i < n; i++) {
        const uint8_t *d = s->pack + 32 + i * 112;
        struct hs_splash_ent *e = &s->ent[i];
        memset(e, 0, sizeof(*e));
        memcpy(e->id, d + 0, 15);
        memcpy(e->title, d + 16, 47);
        memcpy(e->artist, d + 64, 31);
        e->type = d[96] ? HS_SPLASH_ASCII : HS_SPLASH_GRAPHIC;
        e->theme = d[97] ? HS_THEME_GREEN : HS_THEME_PURPLE;
        e->nframes = rd_le16(d + 98);
        e->off = rd_le32(d + 100);
        e->size = rd_le32(d + 104);
        if (e->nframes == 0 || (uint64_t)e->off + e->size > s->pack_len) {
            snprintf(s->err, sizeof(s->err), "bad entry %u", i);
            free(s->pack); s->pack = NULL; s->pack_len = 0;
            return -1;
        }
    }
    s->n = (int)n;
    s->loaded = 1;
    return 0;
}

int hs_splash_find(const struct hs_splash *s, const char *id)
{
    int i;
    if (s == NULL || id == NULL) return -1;
    for (i = 0; i < s->n; i++) {
        if (strcmp(s->ent[i].id, id) == 0) return i;
    }
    return -1;
}

const struct hs_splash_ent *hs_splash_at(const struct hs_splash *s, int i)
{
    if (s == NULL || i < 0 || i >= s->n) return NULL;
    return &s->ent[i];
}

static uint32_t scale_rgb(uint32_t c, int pulse)
{
    unsigned r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (pulse < 0) pulse = 0;
    if (pulse > 256) pulse = 256;
    r = (r * (unsigned)pulse) >> 8;
    g = (g * (unsigned)pulse) >> 8;
    b = (b * (unsigned)pulse) >> 8;
    return (r << 16) | (g << 8) | b;
}

void hs_splash_blit(struct hs_fb *fb, const struct hs_splash *s, int idx,
                    int frame, int pulse, hs_theme_id_t theme)
{
    const struct hs_palette *pal = hs_theme_get(theme);
    const struct hs_splash_ent *e;
    uint32_t bg;
    int x, y;

    if (fb == NULL || fb->pix == NULL) return;
    bg = pal ? pal->bg : 0x12081Fu;
    if (s == NULL || idx < 0 || idx >= (s ? s->n : 0) || s->pack == NULL) {
        hs_fb_clear(fb, bg);
        hs_fb_text(fb, 160, 220, 2, pal ? pal->text : 0xFFFFFFu, "HandshakeOS");
        return;
    }
    e = &s->ent[idx];
    if (e->type == HS_SPLASH_GRAPHIC) {
        const uint16_t *src = (const uint16_t *)(s->pack + e->off);
        int w = fb->w < 640 ? fb->w : 640;
        int h = fb->h < 480 ? fb->h : 480;
        if (e->size < 640u * 480u * 2u) {
            hs_fb_clear(fb, bg);
            return;
        }
        for (y = 0; y < h; y++) {
            uint32_t *dst = fb->pix + y * (fb->stride_bytes / 4);
            const uint16_t *row = src + y * 640;
            if (y >= 444) { /* reserved status strip: solid OS background */
                for (x = 0; x < w; x++) dst[x] = bg;
                continue;
            }
            for (x = 0; x < w; x++) {
                uint16_t p = row[x];
                uint32_t r = (uint32_t)((p >> 11) & 31) * 255 / 31;
                uint32_t g = (uint32_t)((p >> 5) & 63) * 255 / 63;
                uint32_t b = (uint32_t)(p & 31) * 255 / 31;
                uint32_t c = (r << 16) | (g << 8) | b;
                dst[x] = pulse == 256 ? c : scale_rgb(c, pulse);
            }
        }
        return;
    }
    /* ASCII: 8x8 font at scale 2, 38x9 cells, centred. */
    {
        int scale = 2, cw = 8 * scale, ch = 8 * scale;
        int grid_w = HS_SPLASH_ASCII_C * cw;
        int grid_h = HS_SPLASH_ASCII_R * ch;
        int ox = (fb->w - grid_w) / 2;
        int oy = (fb->h - 36 - grid_h) / 2;
        int nf = e->nframes > 0 ? e->nframes : 1;
        const uint8_t *fr;
        uint32_t fg = pal ? pal->accent2 : 0xE0B0FFu;
        if (frame < 0) frame = 0;
        frame %= nf;
        if ((uint32_t)frame * (HS_SPLASH_ASCII_C * HS_SPLASH_ASCII_R) + (HS_SPLASH_ASCII_C * HS_SPLASH_ASCII_R) > e->size) {
            hs_fb_clear(fb, bg);
            return;
        }
        fr = s->pack + e->off + (size_t)frame * HS_SPLASH_ASCII_C * HS_SPLASH_ASCII_R;
        hs_fb_clear(fb, bg);
        for (y = 0; y < HS_SPLASH_ASCII_R; y++) {
            char line[HS_SPLASH_ASCII_C + 1];
            memcpy(line, fr + y * HS_SPLASH_ASCII_C, HS_SPLASH_ASCII_C);
            line[HS_SPLASH_ASCII_C] = '\0';
            hs_fb_text(fb, ox, oy + y * ch, scale, pulse == 256 ? fg : scale_rgb(fg, pulse), line);
        }
        for (y = 444; y < fb->h && y < 480; y++) {
            uint32_t *dst = fb->pix + y * (fb->stride_bytes / 4);
            for (x = 0; x < fb->w; x++) dst[x] = bg;
        }
        (void)grid_h;
    }
}


#include "hs_cfg.h"
#include "hs_input.h"

int hs_boot_wait_display(struct hs_fb *fb, int once, int max_wait_ms,
                         int *wait_ms, char *mode, size_t mode_n)
{
    int tries, max_tries, rc;

    if (wait_ms) *wait_ms = 0;
    if (mode && mode_n) snprintf(mode, mode_n, "headless");
    if (fb == NULL) return -1;
    if (fb->pix == NULL) {
        rc = hs_fb_open(fb, "headless");
        if (rc != 0) return rc;
    }
    if (hs_fb_try_hw(fb) == 0) {
        if (wait_ms) *wait_ms = 0;
        if (mode && mode_n) snprintf(mode, mode_n, "%s", hs_fb_mode_name(fb));
        return 0;
    }
    if (max_wait_ms < 0) max_wait_ms = 10000;
    if (once && max_wait_ms > 300) max_wait_ms = 300;
    max_tries = max_wait_ms / 100;
    if (max_tries < 1) max_tries = 1;
    for (tries = 1; tries < max_tries; tries++) {
        usleep(100000);
        if (hs_fb_try_hw(fb) == 0) {
            if (wait_ms) *wait_ms = tries * 100;
            if (mode && mode_n) snprintf(mode, mode_n, "%s", hs_fb_mode_name(fb));
            return 0;
        }
    }
    if (wait_ms) *wait_ms = max_tries * 100;
    if (mode && mode_n) snprintf(mode, mode_n, "%s", hs_fb_mode_name(fb));
    return 0;
}

void hs_boot_animate(struct hs_fb *fb, struct hs_input *in,
                     const struct hs_splash *sp, const char *splash_id,
                     int animation, hs_theme_id_t theme,
                     int min_ms, int *open_menu)
{
    int idx = 0, frame = 0, steps, i, pulse;
    const struct hs_palette *pal = hs_theme_get(theme);

    if (open_menu) *open_menu = 0;
    if (fb == NULL || fb->pix == NULL) return;
    if (sp && sp->loaded) {
        int f = hs_splash_find(sp, splash_id);
        if (f >= 0) idx = f;
    }
    if (min_ms < 0) min_ms = 0;
    if (!animation) min_ms = 0;
    steps = min_ms / 100;
    if (steps < 1) steps = 1;
    for (i = 0; i < steps; i++) {
        pulse = 256;
        if (animation) {
            int ph = i % 20;
            pulse = (ph < 10) ? (210 + ph * 4) : (256 - (ph - 10) * 4);
        }
        if (sp && sp->loaded)
            hs_splash_blit(fb, sp, idx, frame, pulse, theme);
        else {
            uint32_t bg = pal ? pal->bg : 0x12081Fu;
            uint32_t fg = pal ? pal->text : 0xFFFFFFu;
            hs_fb_clear(fb, bg);
            hs_fb_text(fb, 160, 220, 2, fg, "HandshakeOS");
        }
        hs_fb_flip(fb);
        if (animation && (i & 1)) frame++;
        if (in) {
            (void)hs_input_poll(in, 0);
            if (hs_input_pressed(in, HS_BTN_A) ||
                hs_input_pressed(in, HS_BTN_START) ||
                hs_input_pressed(in, HS_BTN_MENU)) {
                if (open_menu) *open_menu = 1;
                return;
            }
        }
        if (i + 1 < steps) usleep(100000);
    }
}
