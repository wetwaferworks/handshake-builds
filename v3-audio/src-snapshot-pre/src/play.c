/* SPDX-License-Identifier: MIT */
/* HandshakeOS v3-play: GAMES picker and full-screen 640x480 Game Boy.
   Shell-thread only. Each GB frame is one Timing Handshake phase; this
   file only touches the core between hs_gb_run_frames() calls (phase
   boundaries), so .sav writes never race the CPU core. Zero locks. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_play.h"
#include "hs_fb.h"
#include "hs_games.h"
#include "hs_gb.h"
#include "hs_gbscale.h"
#include "hs_input.h"
#include "hs_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DMG_FRAME_NS   16742706ll
#define TOAST_NS       1500000000ll
#define TOAST_FADE_NS   500000000ll
#define SAVE_EVERY_NS 10000000000ll
#define COMBO_NS        700000000ll

/* JOYPAD_* (Peanut-GB) */
#define J_A      0x01u
#define J_B      0x02u
#define J_SELECT 0x04u
#define J_START  0x08u
#define J_RIGHT  0x10u
#define J_LEFT   0x20u
#define J_UP     0x40u
#define J_DOWN   0x80u

static struct hs_gbscale g_scale; /* 480-row tables, built once */
static int g_scale_ready;
static int g_palette = HS_DMG_PAL_OLIVE;

static int64_t now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
}

static void scale_ready(void)
{
    if (!g_scale_ready) {
        hs_gbscale_init(&g_scale);
        g_scale_ready = 1;
    }
}

/* Gather input until an absolute deadline; OR every edge in the window. */
static void wait_until(struct hs_input *in, int64_t deadline)
{
    uint32_t pressed = 0, released = 0;

    for (;;) {
        int64_t rem = deadline - now_ns();
        if (rem <= 0) {
            break;
        }
        if (in != NULL && in->n_fds > 0) {
            (void)hs_input_poll_ns(in, rem);
            pressed |= in->pressed;
            released |= in->released;
        } else {
            struct timespec ts;
            ts.tv_sec = (time_t)(deadline / 1000000000ll);
            ts.tv_nsec = (long)(deadline % 1000000000ll);
            (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        }
    }
    if (in != NULL && in->n_fds > 0) {
        (void)hs_input_poll_ns(in, 0);
        pressed |= in->pressed;
        released |= in->released;
    }
    if (in != NULL) {
        in->pressed = pressed;
        in->released = released;
    }
}

static uint8_t joy_from_input(const struct hs_input *in)
{
    uint8_t m = 0;

    if (hs_input_down(in, HS_BTN_A))      { m |= J_A; }
    if (hs_input_down(in, HS_BTN_B))      { m |= J_B; }
    if (hs_input_down(in, HS_BTN_SELECT)) { m |= J_SELECT; }
    if (hs_input_down(in, HS_BTN_START))  { m |= J_START; }
    if (hs_input_down(in, HS_BTN_RIGHT))  { m |= J_RIGHT; }
    if (hs_input_down(in, HS_BTN_LEFT))   { m |= J_LEFT; }
    if (hs_input_down(in, HS_BTN_UP))     { m |= J_UP; }
    if (hs_input_down(in, HS_BTN_DOWN))   { m |= J_DOWN; }
    /* A Game Boy D-pad cannot press opposite directions at once. */
    if ((m & (J_LEFT | J_RIGHT)) == (J_LEFT | J_RIGHT)) { m &= (uint8_t)~(J_LEFT | J_RIGHT); }
    if ((m & (J_UP | J_DOWN)) == (J_UP | J_DOWN))       { m &= (uint8_t)~(J_UP | J_DOWN); }
    return m;
}

static int toast_alpha(int64_t until, int64_t now)
{
    int64_t rem = until - now;

    if (rem <= 0) {
        return 0;
    }
    if (rem >= TOAST_FADE_NS) {
        return 256;
    }
    return (int)((rem * 256) / TOAST_FADE_NS);
}

static void set_toast(char *buf, size_t n, int64_t *until, const char *a, const char *b)
{
    snprintf(buf, n, "%s%s%s", a, b ? "  " : "", b ? b : "");
    *until = now_ns() + TOAST_NS;
}

/* Returns 0 after a normal exit back to the picker, <0 if the ROM failed. */
static int play_rom(struct hs_play_ctx *c, const struct hs_game *game)
{
    struct hs_gb *gb;
    char sav[HS_GAMES_PATH_MAX + 8];
    char toast[96];
    int64_t toast_until = 0, t0, deadline, next_save, combo_t = 0;
    uint64_t done = 0;
    uint32_t pal[4];
    int have_sav = 0, rc;

    scale_ready();
    gb = hs_gb_create();
    if (gb == NULL) {
        return -1;
    }
    rc = hs_gb_load_rom(gb, game->path);
    if (rc != 0) {
        fprintf(stderr, "play: load %s failed (%d)\n", game->path, rc);
        hs_gb_destroy(gb);
        return -2;
    }
    if (!game->builtin && hs_games_sav_path(game->path, sav, sizeof(sav)) == 0) {
        size_t sz = 0;
        (void)hs_gb_cart_ram(gb, &sz);
        if (sz > 0) {
            have_sav = 1;
            if (hs_games_load_sav(gb, sav) > 0) {
                fprintf(stderr, "play: loaded %s (%zu B)\n", sav, sz);
            }
        }
    }
    hs_gb_set_joypad(gb, 0);
    if (hs_gb_start(gb, c->workers) != 0) {
        hs_gb_destroy(gb);
        return -3;
    }
    hs_dmg_pal_get((hs_dmg_pal_t)g_palette, pal);
    set_toast(toast, sizeof(toast), &toast_until, "MENU = BACK TO GAMES", NULL);

    t0 = now_ns();
    deadline = t0 + DMG_FRAME_NS;
    next_save = t0 + SAVE_EVERY_NS;
    for (;;) {
        int64_t now;
        uint64_t due, n;
        int quit = 0;

        wait_until(c->in, deadline);
        now = now_ns();
        deadline += DMG_FRAME_NS;
        if (now > deadline) {
            deadline = now + DMG_FRAME_NS;
        }

        if (hs_input_pressed(c->in, HS_BTN_MENU) || hs_input_pressed(c->in, HS_BTN_POWER)) {
            quit = 1;
        }
        if (hs_input_down(c->in, HS_BTN_SELECT) && hs_input_down(c->in, HS_BTN_START)) {
            if (combo_t == 0) {
                combo_t = now;
            } else if (now - combo_t >= COMBO_NS) {
                quit = 1;
            }
        } else {
            combo_t = 0;
        }
        if (quit) {
            break;
        }
        if (hs_input_pressed(c->in, HS_BTN_L1)) {
            g_palette = (g_palette + 1) % HS_DMG_PAL_COUNT;
            hs_dmg_pal_get((hs_dmg_pal_t)g_palette, pal);
            set_toast(toast, sizeof(toast), &toast_until,
                      hs_dmg_pal_name((hs_dmg_pal_t)g_palette), NULL);
        }
        if (hs_input_pressed(c->in, HS_BTN_R1)) {
            g_scale.mode = g_scale.mode == HS_GBSCALE_UPSAMPLED ? HS_GBSCALE_NEAREST
                                                                : HS_GBSCALE_UPSAMPLED;
            set_toast(toast, sizeof(toast), &toast_until,
                      hs_gbscale_mode_name(g_scale.mode), NULL);
        }

        /* PRE: publish this frame's buttons, then release the phases. */
        hs_gb_set_joypad(gb, joy_from_input(c->in));
        due = (uint64_t)((now - t0) / DMG_FRAME_NS) + 1u;
        n = due > done ? due - done : 0;
        if (n > 4) {
            n = 4;
        }
        done = due;
        if (hs_input_down(c->in, HS_BTN_Y) || hs_input_down(c->in, HS_BTN_R2)) {
            n = n * 3u; /* hold Y / R2: 3x fast-forward */
        }
        if (n > 0 && hs_gb_run_frames(gb, n) != 0) {
            fprintf(stderr, "play: run_frames failed\n");
            break;
        }

        /* Phase boundary: the core is idle, battery RAM is stable. */
        if (have_sav && now >= next_save) {
            if (hs_gb_sram_dirty(gb)) {
                (void)hs_games_save_sav(gb, sav);
            }
            next_save = now + SAVE_EVERY_NS;
        }

        hs_gbscale_blit(&g_scale, c->fb, hs_gb_framebuffer(gb), pal);
        hs_gbscale_toast(c->fb, toast, toast_alpha(toast_until, now));
        hs_fb_flip(c->fb);
    }

    hs_gb_stop(gb);
    if (have_sav) {
        rc = hs_games_save_sav(gb, sav);
        if (rc != 0) {
            fprintf(stderr, "play: save %s failed (%d)\n", sav, rc);
        }
        hs_sd_sync();
    }
    hs_gb_destroy(gb);
    return 0;
}

struct picker {
    struct hs_gamelist  list;
    const char         *names[HS_GAMES_MAX + 1];
    char                sd_status[32];
};

static void picker_load(struct hs_play_ctx *c, struct picker *p)
{
    const char *root = c->sd_root ? c->sd_root : HS_SD_MOUNTPOINT;
    int mounted = 1;
    int i;

    if (c->try_mount) {
        mounted = hs_sd_mount(root);
    }
    (void)hs_games_scan(&p->list, root, mounted, c->builtin ? c->builtin : HS_BUILTIN_ROM);
    for (i = 0; i < p->list.n; i++) {
        p->names[i] = p->list.g[i].name;
    }
    if (!mounted) {
        snprintf(p->sd_status, sizeof(p->sd_status), "SD: DEMO ONLY");
    } else if (p->list.n_sd == 0) {
        snprintf(p->sd_status, sizeof(p->sd_status), "SD: 0 ROMS");
    } else {
        snprintf(p->sd_status, sizeof(p->sd_status), "SD: %d ROM%s", p->list.n_sd,
                 p->list.n_sd == 1 ? "" : "S");
    }
}

static void picker_draw(struct hs_play_ctx *c, struct picker *p, int sel, const char *msg)
{
    struct hs_games_view v;

    v.names = p->names;
    v.n = p->list.n;
    v.selected = sel;
    v.sd_status = p->sd_status;
    v.audio_status = hs_audio_probe() ? "SOUND: PCM" : "SOUND: SYNTH ONLY";
    v.message = msg;
    hs_ui_draw_games(c->fb, *c->theme, &v);
}

int hs_play_run(struct hs_play_ctx *c)
{
    static struct picker p; /* 140 KB; keep it off the stack */
    int sel = 0;
    int64_t deadline, msg_until = 0, hold_t = 0;
    const char *msg = NULL;

    if (c == NULL || c->fb == NULL || c->in == NULL || c->theme == NULL) {
        return -1;
    }
    picker_load(c, &p);
    deadline = now_ns();
    for (;;) {
        int64_t now;
        int dy = 0;

        deadline += DMG_FRAME_NS;
        wait_until(c->in, deadline);
        now = now_ns();
        if (now > deadline + DMG_FRAME_NS) {
            deadline = now;
        }

        if (hs_input_pressed(c->in, HS_BTN_B) || hs_input_pressed(c->in, HS_BTN_MENU)) {
            return 0;
        }
        if (hs_input_pressed(c->in, HS_BTN_UP)) {
            dy = -1;
            hold_t = now + 400000000ll;
        } else if (hs_input_pressed(c->in, HS_BTN_DOWN)) {
            dy = 1;
            hold_t = now + 400000000ll;
        } else if ((hs_input_down(c->in, HS_BTN_UP) || hs_input_down(c->in, HS_BTN_DOWN)) &&
                   hold_t != 0 && now >= hold_t) {
            dy = hs_input_down(c->in, HS_BTN_UP) ? -1 : 1;
            hold_t = now + 80000000ll;
        }
        if (hs_input_pressed(c->in, HS_BTN_LEFT)) {
            dy = -10;
        }
        if (hs_input_pressed(c->in, HS_BTN_RIGHT)) {
            dy = 10;
        }
        if (dy != 0 && p.list.n > 0) {
            sel += dy;
            if (sel < 0) {
                sel = (dy == -1) ? p.list.n - 1 : 0;
            }
            if (sel >= p.list.n) {
                sel = (dy == 1) ? 0 : p.list.n - 1;
            }
        }
        if (hs_input_pressed(c->in, HS_BTN_L1)) {
            *c->theme = hs_theme_prev(*c->theme);
        }
        if (hs_input_pressed(c->in, HS_BTN_R1)) {
            *c->theme = hs_theme_next(*c->theme);
        }
        if (hs_input_pressed(c->in, HS_BTN_X)) {
            picker_load(c, &p); /* card inserted late: rescan */
            if (sel >= p.list.n) {
                sel = p.list.n > 0 ? p.list.n - 1 : 0;
            }
        }
        if (hs_input_pressed(c->in, HS_BTN_A) && p.list.n > 0) {
            int rc = play_rom(c, &p.list.g[sel]);
            if (rc != 0) {
                msg = "COULD NOT LOAD THAT ROM";
                msg_until = now_ns() + 2500000000ll;
            }
            (void)hs_input_poll_ns(c->in, 0);
            c->in->pressed = 0;
            deadline = now_ns();
        }
        if (msg != NULL && now_ns() >= msg_until) {
            msg = NULL;
        }
        picker_draw(c, &p, sel, msg);
        hs_fb_flip(c->fb);
    }
}

int hs_play_snapshot_picker(struct hs_play_ctx *c, const char *png)
{
    static struct picker p;

    if (c == NULL || c->fb == NULL || c->theme == NULL || png == NULL) {
        return -1;
    }
    picker_load(c, &p);
    picker_draw(c, &p, 0, NULL);
    hs_fb_flip(c->fb);
    return hs_fb_snapshot_png(c->fb, png);
}

int hs_play_snapshot_rom(struct hs_fb *fb, const char *rom, unsigned frames,
                         int nearest, int palette, const char *toast, const char *png)
{
    struct hs_gb *gb;
    uint32_t pal[4];
    int rc;

    scale_ready();
    gb = hs_gb_create();
    if (gb == NULL || hs_gb_load_rom(gb, rom) != 0) {
        hs_gb_destroy(gb);
        return -1;
    }
    hs_gb_set_joypad(gb, 0);
    if (hs_gb_start(gb, 3) != 0) {
        hs_gb_destroy(gb);
        return -1;
    }
    (void)hs_gb_run_frames(gb, frames);
    g_scale.mode = nearest ? HS_GBSCALE_NEAREST : HS_GBSCALE_UPSAMPLED;
    hs_dmg_pal_get((hs_dmg_pal_t)palette, pal);
    hs_gbscale_blit(&g_scale, fb, hs_gb_framebuffer(gb), pal);
    if (toast != NULL) {
        hs_gbscale_toast(fb, toast, 256);
    }
    hs_fb_flip(fb);
    rc = hs_fb_snapshot_png(fb, png);
    hs_gb_stop(gb);
    hs_gb_destroy(gb);
    g_scale.mode = HS_GBSCALE_UPSAMPLED;
    return rc;
}
