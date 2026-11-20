/* SPDX-License-Identifier: MIT */
#ifndef HS_FB_INCLUDED
#define HS_FB_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define HS_FB_W 640
#define HS_FB_H 480

typedef enum {
    HS_FB_HEADLESS = 0,
    HS_FB_FBDEV    = 1,
    HS_FB_DRM      = 2
} hs_fb_kind_t;

struct hs_fb {
    int            w;
    int            h;
    int            stride_bytes;
    uint32_t      *pix;
    int            fd;
    hs_fb_kind_t kind;
    int            own_pix;
    /* DRM bookkeeping */
    uint32_t       crtc_id;
    uint32_t       conn_id;
    uint32_t       fb_id;
    uint32_t       handle;
    uint32_t       saved_crtc;
    /* Hardware scanout (may be 16-bit RGB565 or 32-bit XRGB8888).
       Drawing always happens in a 640x480 XRGB8888 shadow; flip() converts. */
    void          *hw_map;
    size_t         hw_map_len;
    int            hw_w;
    int            hw_h;
    int            hw_stride;
    int            hw_bpp;
    unsigned       red_off, green_off, blue_off;
    unsigned       red_len, green_len, blue_len;
    int            drm_addfb2;  /* 1: DRM_FORMAT_XRGB8888 pinned via ADDFB2 */
    int            swapped_rb;  /* 1: handshakeos.bgr override active */
};

enum {
    HS_FB_BPP_16 = 16,
    HS_FB_BPP_32 = 32
};

/* backend: "auto", "drm", "fbdev", or "headless". */
int  hs_fb_open(struct hs_fb *fb, const char *backend);
/* One drm-then-fbdev attempt. On success, adopts the hardware scanout and
   copies the existing shadow so a splash already drawn is preserved. 0 / <0. */
int  hs_fb_try_hw(struct hs_fb *fb);
const char *hs_fb_mode_name(const struct hs_fb *fb);
void hs_fb_close(struct hs_fb *fb);
void hs_fb_flip(struct hs_fb *fb);
/* Escape hatch for a panel that really is BGR: swap R and B at scanout. */
void hs_fb_swap_rb(struct hs_fb *fb);

void hs_fb_clear(struct hs_fb *fb, uint32_t color);
void hs_fb_fill(struct hs_fb *fb, int x, int y, int w, int h, uint32_t color);
void hs_fb_rect(struct hs_fb *fb, int x, int y, int w, int h, int thickness, uint32_t color);
void hs_fb_fill_round(struct hs_fb *fb, int x, int y, int w, int h, int radius, uint32_t color);
/* 50% checkerboard of `color` over the whole frame (modal dim). */
void hs_fb_checker(struct hs_fb *fb, uint32_t color);
void hs_fb_fill_circle(struct hs_fb *fb, int cx, int cy, int radius, uint32_t color);

int  hs_fb_text(struct hs_fb *fb, int x, int y, int scale, uint32_t color, const char *s);
int  hs_fb_printf(struct hs_fb *fb, int x, int y, int scale, uint32_t color, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

/* v2 readable text: HS_TEXT_BOLD thickens strokes (scale/2 px), and
   HS_TEXT_SHADOW draws a dark copy offset by 2 px (1 px at scale 1). */
#define HS_TEXT_BOLD   0x1u
#define HS_TEXT_SHADOW 0x2u
#define HS_TEXT_NICE   (HS_TEXT_BOLD | HS_TEXT_SHADOW)
int  hs_fb_text_ex(struct hs_fb *fb, int x, int y, int scale, uint32_t color,
                   uint32_t shadow, unsigned flags, const char *s);
int  hs_fb_printf_ex(struct hs_fb *fb, int x, int y, int scale, uint32_t color,
                     uint32_t shadow, unsigned flags, const char *fmt, ...)
    __attribute__((format(printf, 8, 9)));

/* Human-readable scanout format, e.g. "DRM XRGB8888 640x480". */
const char *hs_fb_describe(const struct hs_fb *fb);

int  hs_fb_snapshot_ppm(const struct hs_fb *fb, const char *path);
int  hs_fb_snapshot_png(const struct hs_fb *fb, const char *path);

#endif /* HS_FB_INCLUDED */
