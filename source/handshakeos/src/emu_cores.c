/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-full: thin adapters over the seven clean-room cores
   (GB/GBC, GBA, SNES, Genesis, PS1, N64).
   Pure component code: no locks, no threads, no clocks, no entropy.
   Core state is allocated lazily in emu_core_create() (PS1 ~12 MB with
   2 MB RAM + VRAM + SPU RAM + SSAA strip, N64 ~11.8 MB with RDRAM + SSAA
   strip) and freed in emu_core_destroy(), so idle systems hold no RAM. */
#include "emu_core.h"

#include "hs_core.h"
#include "hs_video.h"
#include "gbc_internal.h"
#include "hs_gba.h"
#include "hs_snes.h"
#include "hs_genesis.h"
#include "hs_psx.h"
#include "hs_n64.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define EMU_AUDIO_MAX 2048u

struct emu_core {
    emu_sys_t sys;
    size_t    bytes;    /* state bytes this adapter allocated */
    /* hs_core_ops cores (GB/GBC, Genesis) */
    const hs_core_ops_t *ops;
    void    *mem;
    /* GBA */
    hs_gba  *gba;
    hs_scaler_t gba_near;
    uint32_t gba_native[240 * 160];
    uint32_t row_tmp[2 * 640];
    /* SNES */
    hs_snes      *snes;
    hs_snes_slot *slot[2];
    unsigned      back;
    hs_snes_video *vid_sharp;
    hs_snes_video *vid_near;
    hs_snes_dsp   *dsp;
};

/* Shell thread only (create / destroy never run inside a phase). */
static size_t g_live_bytes;

size_t emu_core_live_bytes(void)
{
    return g_live_bytes;
}

uint32_t emu_core_max_audio(void)
{
    return EMU_AUDIO_MAX;
}

static void snes_make_nearest(hs_snes_video *v)
{
    int i;
    hs_snes_hlut *h[2];
    hs_snes_vlut *vl[2];

    h[0] = &v->h256; h[1] = &v->h512;
    vl[0] = &v->v224; vl[1] = &v->v239;
    for (i = 0; i < 2; i++) {
        int x;
        for (x = 0; x < HS_SNES_OUT_W; x++) {
            if (h[i]->w[x] >= 128u) {
                h[i]->i0[x] = (uint16_t)(h[i]->i0[x] + 1u);
            }
            h[i]->w[x] = 0;
        }
        for (x = 0; x < HS_SNES_OUT_H; x++) {
            if (vl[i]->w[x] >= 128u) {
                vl[i]->r0[x] = (uint16_t)(vl[i]->r0[x] + 1u);
            }
            vl[i]->w[x] = 0;
        }
    }
}

static void *zalloc64(size_t n)
{
    size_t r = (n + 63u) & ~(size_t)63u;
    void *p = aligned_alloc(64, r);
    if (p != NULL) {
        memset(p, 0, r);
    }
    return p;
}

static void *czalloc(struct emu_core *c, size_t n)
{
    void *p = zalloc64(n);
    if (p != NULL) {
        size_t r = (n + 63u) & ~(size_t)63u;
        c->bytes += r;
    }
    return p;
}

static int is_3d(emu_sys_t sys)
{
    return sys == EMU_SYS_PSX || sys == EMU_SYS_N64;
}

struct emu_core *emu_core_create(emu_sys_t sys, const uint8_t *rom, size_t len, int *err)
{
    struct emu_core *c;
    int rc = 0;

    if (err != NULL) {
        *err = 0;
    }
    if (rom == NULL || len == 0) {
        if (err != NULL) {
            *err = -EINVAL;
        }
        return NULL;
    }
    if (sys != EMU_SYS_GB && sys != EMU_SYS_GBC && sys != EMU_SYS_GBA &&
        sys != EMU_SYS_SNES && sys != EMU_SYS_GENESIS && sys != EMU_SYS_PSX &&
        sys != EMU_SYS_N64) {
        if (err != NULL) {
            *err = -ENOSYS;
        }
        return NULL;
    }
    c = (struct emu_core *)zalloc64(sizeof(*c));
    if (c == NULL) {
        if (err != NULL) {
            *err = -ENOMEM;
        }
        return NULL;
    }
    c->sys = sys;
    c->bytes = (sizeof(*c) + 63u) & ~(size_t)63u;
    if (sys == EMU_SYS_GB || sys == EMU_SYS_GBC || sys == EMU_SYS_GENESIS || is_3d(sys)) {
        hs_core_config_t cfg;
        size_t sz;
        memset(&cfg, 0, sizeof(cfg));
        if (sys == EMU_SYS_GENESIS) {
            c->ops = &hs_genesis_core_ops;
            cfg.console_id = HS_SYS_GENESIS;
        } else if (sys == EMU_SYS_PSX) {
            c->ops = &hs_psx_core_ops;
            cfg.console_id = HS_SYS_PS1;
        } else if (sys == EMU_SYS_N64) {
            c->ops = &hs_n64_core_ops; /* 4 MiB RDRAM (no Expansion Pak) */
            cfg.console_id = HS_SYS_N64;
        } else {
            c->ops = &hs_gbc_core_ops;
            cfg.console_id = sys == EMU_SYS_GB ? HS_SYS_GB : HS_SYS_GBC;
        }
        sz = c->ops->state_size();
        c->mem = czalloc(c, sz);
        if (c->mem == NULL) {
            rc = -ENOMEM;
        } else if (c->ops->init(c->mem, sz, &cfg) != 0) {
            rc = -EIO;
        } else if (c->ops->load_rom(c->mem, rom, len) != 0) {
            rc = -EINVAL;
        } else {
            c->ops->reset(c->mem);
        }
    } else if (sys == EMU_SYS_GBA) {
        c->gba = hs_gba_create();
        if (c->gba == NULL) {
            rc = -ENOMEM;
        } else if (hs_gba_load_rom(c->gba, rom, len) != 0) {
            rc = -EINVAL;
        } else if (hs_scaler_init(&c->gba_near, 240, 160, HS_SCALE_STRETCH_NEAREST) != 0) {
            rc = -EIO;
        }
    } else {
        c->snes = (hs_snes *)czalloc(c, hs_snes_sizeof());
        c->slot[0] = (hs_snes_slot *)czalloc(c, sizeof(hs_snes_slot));
        c->slot[1] = (hs_snes_slot *)czalloc(c, sizeof(hs_snes_slot));
        c->vid_sharp = (hs_snes_video *)czalloc(c, sizeof(hs_snes_video));
        c->vid_near = (hs_snes_video *)czalloc(c, sizeof(hs_snes_video));
        c->dsp = (hs_snes_dsp *)czalloc(c, sizeof(hs_snes_dsp));
        if (!c->snes || !c->slot[0] || !c->slot[1] || !c->vid_sharp || !c->vid_near || !c->dsp) {
            rc = -ENOMEM;
        } else if (hs_snes_init(c->snes, rom, len) != 0) {
            rc = -EINVAL;
        } else {
            hs_snes_video_init(c->vid_sharp);
            hs_snes_video_init(c->vid_near);
            snes_make_nearest(c->vid_near);
            hs_snes_dsp_init(c->dsp);
            c->back = 0;
        }
    }
    g_live_bytes += c->bytes;
    if (rc != 0) {
        emu_core_destroy(c);
        if (err != NULL) {
            *err = rc;
        }
        return NULL;
    }
    return c;
}

void emu_core_destroy(struct emu_core *c)
{
    if (c == NULL) {
        return;
    }
    g_live_bytes -= c->bytes;
    free(c->mem);
    if (c->gba != NULL) {
        hs_gba_destroy(c->gba);
    }
    free(c->snes);
    free(c->slot[0]);
    free(c->slot[1]);
    free(c->vid_sharp);
    free(c->vid_near);
    free(c->dsp);
    free(c);
}

static uint16_t gba_keys(uint32_t p)
{
    uint16_t k = 0;
    if (p & EMU_PAD_A)      { k |= HS_GBA_KEY_A; }
    if (p & EMU_PAD_B)      { k |= HS_GBA_KEY_B; }
    if (p & EMU_PAD_SELECT) { k |= HS_GBA_KEY_SELECT; }
    if (p & EMU_PAD_START)  { k |= HS_GBA_KEY_START; }
    if (p & EMU_PAD_RIGHT)  { k |= HS_GBA_KEY_RIGHT; }
    if (p & EMU_PAD_LEFT)   { k |= HS_GBA_KEY_LEFT; }
    if (p & EMU_PAD_UP)     { k |= HS_GBA_KEY_UP; }
    if (p & EMU_PAD_DOWN)   { k |= HS_GBA_KEY_DOWN; }
    if (p & EMU_PAD_R)      { k |= HS_GBA_KEY_R; }
    if (p & EMU_PAD_L)      { k |= HS_GBA_KEY_L; }
    return k;
}

static uint16_t snes_pad(uint32_t p)
{
    uint16_t k = 0;
    if (p & EMU_PAD_B)      { k |= HS_SNES_BTN_B; }
    if (p & EMU_PAD_Y)      { k |= HS_SNES_BTN_Y; }
    if (p & EMU_PAD_SELECT) { k |= HS_SNES_BTN_SELECT; }
    if (p & EMU_PAD_START)  { k |= HS_SNES_BTN_START; }
    if (p & EMU_PAD_UP)     { k |= HS_SNES_BTN_UP; }
    if (p & EMU_PAD_DOWN)   { k |= HS_SNES_BTN_DOWN; }
    if (p & EMU_PAD_LEFT)   { k |= HS_SNES_BTN_LEFT; }
    if (p & EMU_PAD_RIGHT)  { k |= HS_SNES_BTN_RIGHT; }
    if (p & EMU_PAD_A)      { k |= HS_SNES_BTN_A; }
    if (p & EMU_PAD_X)      { k |= HS_SNES_BTN_X; }
    if (p & EMU_PAD_L)      { k |= HS_SNES_BTN_L; }
    if (p & EMU_PAD_R)      { k |= HS_SNES_BTN_R; }
    return k;
}

void emu_core_step(struct emu_core *c, uint32_t pad, int64_t rtc)
{
    if (c->ops != NULL) {
        uint32_t bits = pad & 0xFFFFu;
        if (is_3d(c->sys)) {
            bits = pad & 0x007FFFFFu; /* + N64 Z / C buttons; MENU never reaches a core */
        }
        if (c->sys == EMU_SYS_GENESIS && (pad & EMU_PAD_A)) {
            bits |= HS_BTN_C; /* RG35XX Y/B/A = Genesis A/B/C */
        }
        c->ops->step_cpu_phase(c->mem, bits, rtc);
    } else if (c->gba != NULL) {
        hs_gba_step_cpu_phase(c->gba, gba_keys(pad));
    } else {
        hs_snes_set_input(c->snes, snes_pad(pad), 0);
        hs_snes_step_cpu_phase(c->snes, c->slot[c->back]);
    }
}

void emu_core_commit(struct emu_core *c)
{
    if (c->ops != NULL) {
        c->ops->commit_phase(c->mem);
    } else if (c->snes != NULL) {
        c->back ^= 1u; /* the slot just written becomes FRONT */
    }
    /* hs-gba publishes its snapshot index at the end of its CPU phase;
       running video/audio only in the next handshake phase orders it. */
}

static uint32_t x555(uint16_t c)
{
    uint32_t r = c & 31u, g = (c >> 5) & 31u, b = (c >> 10) & 31u;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

static hs_video_mode_t aa_mode(int aa)
{
    switch (aa) {
    case EMU_AA_FXAA:  return HS_AA_FXAA;
    case EMU_AA_SSAA2: return HS_SSAA_2X;
    case EMU_AA_SSAA4: return HS_SSAA_4X;
    case EMU_AA_SSAA8: return HS_SSAA_8X;
    default:           return HS_SSAA_1X; /* native raster, sharp 640x480 stretch */
    }
}

void emu_core_render(struct emu_core *c, uint32_t *fb, int nearest, int aa)
{
    if (is_3d(c->sys)) {
        c->ops->render_video_phase(c->mem, fb, 640, aa_mode(aa));
    } else if (c->ops != NULL) {
        c->ops->render_video_phase(c->mem, fb, 640,
                                   nearest ? HS_SCALE_STRETCH_NEAREST : HS_SCALE_STRETCH_SHARP);
    } else if (c->gba != NULL) {
        if (!nearest) {
            hs_gba_render_video_phase(c->gba, fb);
        } else {
            const uint16_t *n;
            uint32_t i;
            hs_gba_render_video_phase(c->gba, NULL);
            n = hs_gba_native_frame(c->gba);
            for (i = 0; i < 240u * 160u; i++) {
                c->gba_native[i] = x555(n[i]);
            }
            hs_scaler_run(&c->gba_near, c->gba_native, 240, fb, 640, c->row_tmp);
        }
    } else {
        hs_snes_render_video_phase(c->slot[c->back ^ 1u], nearest ? c->vid_near : c->vid_sharp, fb);
    }
}

uint32_t emu_core_synth(struct emu_core *c, int16_t *pcm, uint32_t max_frames)
{
    if (max_frames > EMU_AUDIO_MAX) {
        max_frames = EMU_AUDIO_MAX;
    }
    if (c->ops != NULL) {
        return c->ops->synth_audio_phase(c->mem, pcm, max_frames);
    }
    if (c->gba != NULL) {
        int n = hs_gba_synth_audio_phase(c->gba, pcm, (int)max_frames);
        return n > 0 ? (uint32_t)n : 0u;
    }
    {
        int n = hs_snes_synth_audio_phase(c->slot[c->back ^ 1u], c->dsp, pcm, (int)max_frames);
        return n > 0 ? (uint32_t)n : 0u;
    }
}

void emu_core_set_palette(struct emu_core *c, unsigned pal)
{
    if (c->ops == &hs_gbc_core_ops) {
        gbc_t *g = (gbc_t *)c->mem;
        if (pal >= 4u) {
            g->dmg_pal_auto = 1;
        } else {
            g->dmg_pal_auto = 0;
            g->dmg_palette = pal & 3u;
        }
        gbc_refresh_dmg_palettes(g); /* sampled into the next CPU-phase snapshot */
    }
}

size_t emu_core_sram_size(struct emu_core *c)
{
    if (c->ops == &hs_gbc_core_ops) {
        const gbc_t *g = (const gbc_t *)c->mem;
        /* hs-gbc appends its 24-byte RTC block (gbc_core.c RTC_BLOCK). */
        return g->has_battery ? (size_t)g->ram_size + (g->has_rtc ? 24u : 0u) : 0u;
    }
    if (c->ops != NULL) {
        return c->ops->save_sram(c->mem, NULL, 0);
    }
    if (c->gba != NULL) {
        size_t n = 0;
        (void)hs_gba_save_data(c->gba, &n);
        return n;
    }
    {
        size_t n = 0;
        (void)hs_snes_sram(c->snes, &n);
        return n;
    }
}

size_t emu_core_sram_get(struct emu_core *c, uint8_t *buf, size_t cap)
{
    if (c->ops != NULL) {
        return c->ops->save_sram(c->mem, buf, cap);
    }
    if (c->gba != NULL) {
        size_t n = 0;
        const uint8_t *p = hs_gba_save_data(c->gba, &n);
        if (p == NULL || n > cap) {
            return 0;
        }
        memcpy(buf, p, n);
        return n;
    }
    {
        size_t n = 0;
        const uint8_t *p = hs_snes_sram(c->snes, &n);
        if (p == NULL || n > cap) {
            return 0;
        }
        memcpy(buf, p, n);
        return n;
    }
}

int emu_core_sram_put(struct emu_core *c, const uint8_t *buf, size_t len)
{
    if (c->ops != NULL) {
        return c->ops->load_sram(c->mem, buf, len);
    }
    if (c->gba != NULL) {
        return hs_gba_load_save(c->gba, buf, len);
    }
    {
        size_t n = 0;
        uint8_t *p = hs_snes_sram(c->snes, &n);
        if (p == NULL || len != n) {
            return -1;
        }
        memcpy(p, buf, n);
        return 0;
    }
}
