/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-multi: multi-system GAMES picker and full-screen 640x480
   play for every registry core (GB, GBC, GBA, SNES, Genesis; PSX / N64
   slots show "Core coming in v4-full"). Shell-thread only. Each emulated
   frame is two Timing Handshake phases (CPU, then video + audio); this
   file only touches a core between emu_run_frames() calls (phase
   boundaries), so .sav writes never race a core. Zero locks. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_play.h"
#include "hs_audio.h"
#include "hs_fb.h"
#include "hs_games.h"
#include "hs_gb.h"
#include "hs_gbscale.h"
#include "hs_input.h"
#include "hs_ui.h"
#include "hs_battery.h"
#include "hs_emusave.h"
#include "emu_sys.h"

#include <errno.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DMG_FRAME_NS   16742706ll
#define TOAST_NS       1500000000ll
#define TOAST_FADE_NS   500000000ll
#define SAVE_EVERY_NS 10000000000ll
#define COMBO_NS       1000000000ll /* v4-multi: hold SELECT+START 1 s */

/* JOYPAD_* (Peanut-GB) */
#define J_A      0x01u
#define J_B      0x02u
#define J_SELECT 0x04u
#define J_START  0x08u
#define J_RIGHT  0x10u
#define J_LEFT   0x20u
#define J_UP     0x40u
#define J_DOWN   0x80u

static unsigned g_palette = 0;       /* DMG palette (GB mode) */
static int g_nearest = 0;            /* 0 SHARP UPSAMPLED, 1 NEAREST */
static int g_hud = 0;                /* L1 stats overlay (non-GB systems) */
static int16_t g_pcm[16384 * 2];     /* legacy GB audio test */
static int16_t g_epcm[EMU_PCM_RING * 2]; /* registry PCM drained per frame */

int hs_play_volume_keys(struct hs_audio *audio, const struct hs_input *in)
{
    int v;

    if (audio == NULL || in == NULL) {
        return 0;
    }
    v = hs_audio_volume(audio);
    if (hs_input_pressed(in, HS_BTN_VOLUP)) {
        v += 10;
    } else if (hs_input_pressed(in, HS_BTN_VOLDOWN)) {
        v -= 10;
    } else {
        return 0;
    }
    (void)hs_audio_set_volume(audio, v);
    return 1;
}

static int64_t now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
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


/* ---------------- v4-multi: registry play ---------------- */

static uint32_t pad_from_input(const struct hs_input *in, emu_sys_t sys)
{
    uint32_t m = 0;
    int shoulders = sys == EMU_SYS_GBA || sys == EMU_SYS_SNES;

    if (hs_input_down(in, HS_BTN_UP))     { m |= EMU_PAD_UP; }
    if (hs_input_down(in, HS_BTN_DOWN))   { m |= EMU_PAD_DOWN; }
    if (hs_input_down(in, HS_BTN_LEFT))   { m |= EMU_PAD_LEFT; }
    if (hs_input_down(in, HS_BTN_RIGHT))  { m |= EMU_PAD_RIGHT; }
    if (hs_input_down(in, HS_BTN_A))      { m |= EMU_PAD_A; }
    if (hs_input_down(in, HS_BTN_B))      { m |= EMU_PAD_B; }
    if (hs_input_down(in, HS_BTN_X))      { m |= EMU_PAD_X; }
    if (hs_input_down(in, HS_BTN_START))  { m |= EMU_PAD_START; }
    if (hs_input_down(in, HS_BTN_SELECT)) { m |= EMU_PAD_SELECT; }
    /* Y is fast-forward on GB/GBC; a real face button elsewhere. */
    if (sys != EMU_SYS_GB && sys != EMU_SYS_GBC && hs_input_down(in, HS_BTN_Y)) {
        m |= EMU_PAD_Y;
    }
    /* GBA / SNES games need their shoulder buttons. */
    if (shoulders && hs_input_down(in, HS_BTN_L1)) { m |= EMU_PAD_L; }
    if (shoulders && hs_input_down(in, HS_BTN_R1)) { m |= EMU_PAD_R; }
    if ((m & (EMU_PAD_LEFT | EMU_PAD_RIGHT)) == (EMU_PAD_LEFT | EMU_PAD_RIGHT)) {
        m &= ~(uint32_t)(EMU_PAD_LEFT | EMU_PAD_RIGHT);
    }
    if ((m & (EMU_PAD_UP | EMU_PAD_DOWN)) == (EMU_PAD_UP | EMU_PAD_DOWN)) {
        m &= ~(uint32_t)(EMU_PAD_UP | EMU_PAD_DOWN);
    }
    return m;
}

#define ROM_MAX_BYTES (64u * 1024u * 1024u)

static uint8_t *read_rom(const char *path, size_t *len)
{
    struct stat st;
    uint8_t *b;
    long r;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (unsigned long long)st.st_size > ROM_MAX_BYTES) {
        return NULL;
    }
    b = (uint8_t *)malloc((size_t)st.st_size);
    if (b == NULL) {
        return NULL;
    }
    r = hs_games_read_file(path, b, (size_t)st.st_size);
    if (r != (long)st.st_size) {
        free(b);
        return NULL;
    }
    *len = (size_t)r;
    return b;
}

static void blit_full(struct hs_fb *fb, const uint32_t *src)
{
    int y;

    if (fb == NULL || fb->pix == NULL || src == NULL || fb->w < EMU_FB_W || fb->h < EMU_FB_H) {
        return;
    }
    for (y = 0; y < EMU_FB_H; y++) {
        memcpy((uint8_t *)fb->pix + (size_t)y * (size_t)fb->stride_bytes,
               src + (size_t)y * EMU_FB_W, (size_t)EMU_FB_W * 4u);
    }
}

static void draw_hud(struct hs_fb *fb, const struct emu_sys_info *in, int fps10, int nearest,
                     int turbo, const struct hs_audio *audio)
{
    char l1[48], l2[48], l3[48];

    snprintf(l1, sizeof(l1), "[%s] %s", in->tag, in->name);
    snprintf(l2, sizeof(l2), "%d.%d FPS %s%s", fps10 / 10, fps10 % 10,
             nearest ? "NEAREST" : "SHARP", turbo ? " x3" : "");
    if (audio != NULL && audio->live) {
        snprintf(l3, sizeof(l3), "VOL %d%%  %s", hs_audio_volume(audio), in->core);
    } else {
        snprintf(l3, sizeof(l3), "SYNTH  %s", in->core);
    }
    hs_fb_fill_round(fb, 10, 10, 388, 92, 8, 0x101418u);
    hs_fb_rect(fb, 10, 10, 388, 92, 2, in->rgb);
    (void)hs_fb_text_ex(fb, 23, 20, 2, in->rgb, 0x000000u, HS_TEXT_NICE, l1);
    (void)hs_fb_text_ex(fb, 22, 20, 2, in->rgb, 0x000000u, HS_TEXT_NICE, l1);
    (void)hs_fb_text_ex(fb, 22, 46, 2, 0xFFFFFFu, 0x000000u, HS_TEXT_NICE, l2);
    (void)hs_fb_text_ex(fb, 22, 72, 2, 0xB0B8C0u, 0x000000u, HS_TEXT_NICE, l3);
}

/* Returns 0 after a normal exit back to the picker, <0 if the ROM failed,
   -ENOSYS if the system's core is not linked in this build. */
static int play_emu(struct hs_play_ctx *c, const struct hs_game *game)
{
    emu_sys_t sys = (emu_sys_t)game->sys;
    const struct emu_sys_info *info = emu_sys_info(sys);
    struct emu *e;
    char sav[HS_GAMES_PATH_MAX + 8];
    char toast[96], vol_txt[32];
    int64_t toast_until = 0, t0, deadline, next_save, combo_t = 0, vol_until = 0;
    int64_t frame_ns, fps_t0;
    uint64_t done = 0, fps_frames = 0;
    unsigned prev_rate = 0;
    int have_sav = 0, err = 0, fps10 = 0, shoulders;
    uint8_t *rom;
    size_t len = 0;

    if (info == NULL || !info->linked) {
        return -ENOSYS;
    }
    rom = read_rom(game->path, &len);
    if (rom == NULL) {
        fprintf(stderr, "play: read %s failed\n", game->path);
        return -2;
    }
    e = emu_create(sys, rom, len, c->workers, &err);
    free(rom);
    if (e == NULL) {
        fprintf(stderr, "play: %s core rejected %s (%d)\n", info->core, game->path, err);
        return -3;
    }
    if (!game->builtin && emu_sram_size(e) > 0 &&
        hs_games_sav_path(game->path, sav, sizeof(sav)) == 0) {
        long r;
        have_sav = 1;
        r = hs_emu_load_sav(e, sav);
        if (r > 0) {
            fprintf(stderr, "play: loaded %s (%ld B)\n", sav, r);
        }
    }
    if (sys == EMU_SYS_GB) {
        emu_set_dmg_palette(e, g_palette);
    }
    emu_set_nearest(e, g_nearest);
    if (c->audio != NULL) {
        prev_rate = c->audio->src_rate;
        hs_audio_set_src_rate(c->audio, EMU_PCM_RATE);
    }
    shoulders = sys == EMU_SYS_GBA || sys == EMU_SYS_SNES;
    frame_ns = (int64_t)(1000000000ull * info->fps_den / info->fps_num);
    snprintf(toast, sizeof(toast), "[%s] MENU = BACK TO GAMES", info->tag);
    toast_until = now_ns() + TOAST_NS;

    t0 = now_ns();
    fps_t0 = t0;
    deadline = t0 + frame_ns;
    next_save = t0 + SAVE_EVERY_NS;
    for (;;) {
        int64_t now;
        uint64_t due, n;
        int quit = 0, turbo, hot;
        size_t k;

        wait_until(c->in, deadline);
        now = now_ns();
        deadline += frame_ns;
        if (now > deadline) {
            deadline = now + frame_ns;
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
        /* L1 / R1 hotkeys (GBA / SNES: hold SELECT, the shoulders are game buttons). */
        hot = !shoulders || hs_input_down(c->in, HS_BTN_SELECT);
        if (hot && hs_input_pressed(c->in, HS_BTN_L1)) {
            if (sys == EMU_SYS_GB) {
                g_palette = (g_palette + 1u) & 3u;
                emu_set_dmg_palette(e, g_palette);
                set_toast(toast, sizeof(toast), &toast_until, emu_dmg_palette_name(g_palette), NULL);
            } else {
                g_hud = !g_hud;
                set_toast(toast, sizeof(toast), &toast_until, g_hud ? "HUD ON" : "HUD OFF", NULL);
            }
        }
        if (hot && hs_input_pressed(c->in, HS_BTN_R1)) {
            g_nearest = !g_nearest;
            emu_set_nearest(e, g_nearest);
            set_toast(toast, sizeof(toast), &toast_until,
                      g_nearest ? "NEAREST 640x480" : "SHARP UPSAMPLED 640x480", NULL);
        }
        if (hs_play_volume_keys(c->audio, c->in)) {
            snprintf(vol_txt, sizeof(vol_txt), "VOLUME %d%%", hs_audio_volume(c->audio));
            vol_until = now + TOAST_NS;
            toast_until = 0;
        }

        /* PRE: this frame's buttons, then release the phases. */
        due = (uint64_t)((now - t0) / frame_ns) + 1u;
        n = due > done ? due - done : 0;
        if (n > 4) {
            n = 4;
        }
        done = due;
        turbo = hs_input_down(c->in, HS_BTN_R2) ||
                ((sys == EMU_SYS_GB || sys == EMU_SYS_GBC) && hs_input_down(c->in, HS_BTN_Y));
        if (turbo) {
            n *= 3u; /* 3x fast-forward, muted */
        }
        /* Cartridge RTC (MBC3) follows the monotonic clock while playing;
           cores never read time themselves. */
        emu_set_rtc(e, now / 1000000000ll);
        if (n > 0 && emu_run_frames(e, (uint32_t)n, pad_from_input(c->in, sys), 1) != 0) {
            fprintf(stderr, "play: run_frames failed\n");
            break;
        }
        fps_frames += n;
        /* Phase boundary: drain the lock-free PCM ring into the speaker. */
        k = emu_pcm_take(e, g_epcm, EMU_PCM_RING);
        if (c->audio != NULL && !turbo && k > 0) {
            (void)hs_audio_write(c->audio, g_epcm, k);
        }
        /* Phase boundary: every core is idle, battery RAM is stable. */
        if (have_sav && now >= next_save) {
            if (emu_sram_dirty(e)) {
                (void)hs_emu_save_sav(e, sav);
            }
            next_save = now + SAVE_EVERY_NS;
        }
        if (now - fps_t0 >= 1000000000ll) {
            fps10 = (int)((fps_frames * 10000000000ull) / (uint64_t)(now - fps_t0));
            fps_frames = 0;
            fps_t0 = now;
        }

        blit_full(c->fb, emu_framebuffer(e));
        if (g_hud && sys != EMU_SYS_GB) {
            draw_hud(c->fb, info, fps10, g_nearest, turbo, c->audio);
        }
        if (vol_until > now) {
            hs_gbscale_toast_bar(c->fb, vol_txt, hs_audio_volume(c->audio),
                                 toast_alpha(vol_until, now));
        } else {
            hs_gbscale_toast(c->fb, toast, toast_alpha(toast_until, now));
        }
        hs_fb_flip(c->fb);
    }

    if (have_sav) {
        int rc = hs_emu_save_sav(e, sav); /* atomic .sav.tmp -> fsync -> rename */
        if (rc != 0) {
            fprintf(stderr, "play: save %s failed (%d)\n", sav, rc);
        }
        hs_sd_sync();
    }
    emu_destroy(e);
    if (c->audio != NULL && prev_rate != 0) {
        hs_audio_set_src_rate(c->audio, prev_rate);
    }
    return 0;
}

struct picker {
    struct hs_gamelist  list;
    const char         *names[HS_GAMES_MAX + HS_GAMES_BUILTIN_MAX];
    const char         *tags[HS_GAMES_MAX + HS_GAMES_BUILTIN_MAX];
    uint32_t            rgb[HS_GAMES_MAX + HS_GAMES_BUILTIN_MAX];
    int                 linked[HS_GAMES_MAX + HS_GAMES_BUILTIN_MAX];
    char                sd_status[32];
    char                systems[32];
    char                battery[24];
    char                builtin_dir[HS_GAMES_PATH_MAX];
};

static void builtin_dir_of(const struct hs_play_ctx *c, char *out, size_t n)
{
    const char *b = c->builtin ? c->builtin : HS_BUILTIN_ROM;
    const char *slash = strrchr(b, '/');

    if (slash == NULL) {
        snprintf(out, n, ".");
    } else if (slash == b) {
        snprintf(out, n, "/");
    } else {
        snprintf(out, n, "%.*s", (int)(slash - b), b);
    }
}

static void picker_battery(struct picker *p)
{
    struct hs_battery bat;

    hs_battery_clear(&bat);
    (void)hs_battery_read(&bat);
    if (bat.present && bat.percent >= 0) {
        snprintf(p->battery, sizeof(p->battery), "BAT %d%%%s", bat.percent,
                 bat.charging == 1 ? "+" : "");
    } else {
        snprintf(p->battery, sizeof(p->battery), "BAT --");
    }
}

static void picker_load(struct hs_play_ctx *c, struct picker *p)
{
    const char *root = c->sd_root ? c->sd_root : HS_SD_MOUNTPOINT;
    int mounted = 1;
    int i;

    if (c->try_mount) {
        mounted = hs_sd_mount(root);
    }
    builtin_dir_of(c, p->builtin_dir, sizeof(p->builtin_dir));
    (void)hs_games_scan_multi(&p->list, root, mounted, p->builtin_dir);
    for (i = 0; i < p->list.n; i++) {
        const struct emu_sys_info *in = emu_sys_info((emu_sys_t)p->list.g[i].sys);
        p->names[i] = p->list.g[i].name;
        p->tags[i] = in ? in->tag : "?";
        p->rgb[i] = in ? in->rgb : 0xFFFFFFu;
        p->linked[i] = in ? in->linked : 0;
    }
    if (!mounted) {
        snprintf(p->sd_status, sizeof(p->sd_status), "SD: DEMO ONLY");
    } else if (p->list.n_sd == 0) {
        snprintf(p->sd_status, sizeof(p->sd_status), "SD: 0 ROMS");
    } else {
        snprintf(p->sd_status, sizeof(p->sd_status), "SD: %d ROM%s", p->list.n_sd,
                 p->list.n_sd == 1 ? "" : "S");
    }
    snprintf(p->systems, sizeof(p->systems), "%d SYSTEMS LIVE", emu_sys_count_linked());
    picker_battery(p);
}

static void picker_draw(struct hs_play_ctx *c, struct picker *p, int sel, const char *msg,
                        const char *notice, const char *notice_sub)
{
    struct hs_games_multi_view v;
    char vol[24];

    if (c->audio != NULL && c->audio->live) {
        snprintf(vol, sizeof(vol), "VOL %d%%", hs_audio_volume(c->audio));
    } else {
        snprintf(vol, sizeof(vol), "%s", hs_audio_probe() ? "PCM" : "SYNTH");
    }
    memset(&v, 0, sizeof(v));
    v.names = p->names;
    v.tags = p->tags;
    v.tag_rgb = p->rgb;
    v.linked = p->linked;
    v.n = p->list.n;
    v.selected = sel;
    v.sd_status = p->sd_status;
    v.systems_status = p->systems;
    v.battery_status = p->battery;
    v.audio_status = vol;
    v.message = msg;
    v.notice = notice;
    v.notice_sub = notice_sub;
    hs_ui_draw_games_multi(c->fb, *c->theme, &v);
}

int hs_play_run(struct hs_play_ctx *c)
{
    static struct picker p; /* keep it off the stack */
    static char notice_sub[96];
    int sel = 0;
    int64_t deadline, msg_until = 0, hold_t = 0, notice_until = 0, bat_t;
    const char *msg = NULL, *notice = NULL;

    if (c == NULL || c->fb == NULL || c->in == NULL || c->theme == NULL) {
        return -1;
    }
    picker_load(c, &p);
    deadline = now_ns();
    bat_t = deadline + 5000000000ll;
    for (;;) {
        int64_t now;
        int dy = 0;

        deadline += DMG_FRAME_NS;
        wait_until(c->in, deadline);
        now = now_ns();
        if (now > deadline + DMG_FRAME_NS) {
            deadline = now;
        }
        if (now >= bat_t) {
            picker_battery(&p);
            bat_t = now + 5000000000ll;
        }

        if (hs_input_pressed(c->in, HS_BTN_B) || hs_input_pressed(c->in, HS_BTN_MENU)) {
            if (notice != NULL) {
                notice = NULL;
            } else {
                return 0;
            }
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
            notice = NULL;
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
        if (hs_play_volume_keys(c->audio, c->in)) {
            static char vmsg[32];
            snprintf(vmsg, sizeof(vmsg), "VOLUME %d%%", hs_audio_volume(c->audio));
            msg = vmsg;
            msg_until = now + TOAST_NS;
        }
        if (hs_input_pressed(c->in, HS_BTN_X)) {
            picker_load(c, &p); /* card inserted late: rescan */
            if (sel >= p.list.n) {
                sel = p.list.n > 0 ? p.list.n - 1 : 0;
            }
        }
        if (hs_input_pressed(c->in, HS_BTN_A) && p.list.n > 0) {
            const struct hs_game *g = &p.list.g[sel];
            if (!emu_sys_linked((emu_sys_t)g->sys)) {
                const struct emu_sys_info *in = emu_sys_info((emu_sys_t)g->sys);
                notice = emu_sys_unlinked_notice();
                snprintf(notice_sub, sizeof(notice_sub), "%s (%s)", in ? in->name : "?",
                         in ? in->core : "?");
                notice_until = now + 2500000000ll;
            } else {
                int rc = play_emu(c, g);
                if (rc != 0) {
                    msg = "COULD NOT LOAD THAT ROM";
                    msg_until = now_ns() + 2500000000ll;
                }
                (void)hs_input_poll_ns(c->in, 0);
                c->in->pressed = 0;
                deadline = now_ns();
            }
        }
        if (msg != NULL && now_ns() >= msg_until) {
            msg = NULL;
        }
        if (notice != NULL && now_ns() >= notice_until) {
            notice = NULL;
        }
        picker_draw(c, &p, sel, msg, notice, notice ? notice_sub : NULL);
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
    picker_draw(c, &p, 0, NULL, NULL, NULL);
    hs_fb_flip(c->fb);
    return hs_fb_snapshot_png(c->fb, png);
}

int hs_play_snapshot_rom(struct hs_fb *fb, const char *rom, unsigned frames,
                         int nearest, int palette, const char *toast, const char *png)
{
    emu_sys_t sys = emu_sys_from_path(rom, NULL);
    struct emu *e;
    uint8_t *buf;
    size_t len = 0;
    int rc, err = 0;

    if (!emu_sys_linked(sys) || (buf = read_rom(rom, &len)) == NULL) {
        return -1;
    }
    e = emu_create(sys, buf, len, 3, &err);
    free(buf);
    if (e == NULL) {
        return -1;
    }
    emu_set_nearest(e, nearest);
    emu_set_dmg_palette(e, (unsigned)palette);
    if (emu_run_frames(e, frames ? frames : 1u, 0, 1) != 0) {
        emu_destroy(e);
        return -1;
    }
    blit_full(fb, emu_framebuffer(e));
    if (toast != NULL) {
        hs_gbscale_toast(fb, toast, 256);
    }
    hs_fb_flip(fb);
    rc = hs_fb_snapshot_png(fb, png);
    emu_destroy(e);
    return rc;
}


int hs_play_multi_selftest(const char *dir, unsigned frames, int workers, char *out, size_t n)
{
    static const struct { const char *file; emu_sys_t sys; } k[] = {
        { "handshake-demo.gb", EMU_SYS_GB }, { "hs_cgb_test.gbc", EMU_SYS_GBC },
        { "hs_gba_demo.gba", EMU_SYS_GBA }, { "hs_mode1_demo.sfc", EMU_SYS_SNES },
        { "hs_mode7_floor.sfc", EMU_SYS_SNES }, { "hs_genesis_demo.md", EMU_SYS_GENESIS },
    };
    size_t i, used = 0;
    int ok = 0;

    if (out == NULL || n == 0) {
        return -1;
    }
    out[0] = '\0';
    for (i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        char path[HS_GAMES_PATH_MAX];
        struct emu_stats st;
        struct emu *e;
        uint8_t *rom;
        size_t len = 0;
        int err = 0, w;

        snprintf(path, sizeof(path), "%s/%s", dir, k[i].file);
        rom = read_rom(path, &len);
        e = rom ? emu_create(k[i].sys, rom, len, workers, &err) : NULL;
        free(rom);
        if (e == NULL || emu_run_frames(e, frames, 0, 0) != 0) {
            w = snprintf(out + used, n - used, "MULTI %-4s %s FAIL (%d)\n",
                         emu_sys_info(k[i].sys)->tag, k[i].file, err);
        } else {
            emu_stats(e, &st);
            (void)emu_pcm_take(e, NULL, EMU_PCM_RING);
            w = snprintf(out + used, n - used,
                         "MULTI %-4s %-18s frames=%llu ledger=%016llx video=%016llx audio=%016llx "
                         "pcm_nonzero=%llu freezes=%llu\n",
                         emu_sys_info(k[i].sys)->tag, k[i].file, (unsigned long long)st.frames,
                         (unsigned long long)st.ledger, (unsigned long long)st.video_hash,
                         (unsigned long long)st.audio_hash, (unsigned long long)st.pcm_nonzero,
                         (unsigned long long)st.freezes);
            ok++;
        }
        emu_destroy(e);
        if (w > 0 && (size_t)w < n - used) {
            used += (size_t)w;
        }
    }
    return ok;
}

int hs_play_audio_test(struct hs_audio *audio, const char *rom, double seconds)
{
    struct hs_gb *gb = hs_gb_create();
    int64_t t0, deadline, stop;
    uint64_t done = 0, frames = 0;
    size_t pcm_samples = 0;
    uint64_t nonzero = 0;

    if (gb == NULL || hs_gb_load_rom(gb, rom) != 0) {
        hs_gb_destroy(gb);
        printf("AUDIOTEST FAIL rom=%s\n", rom);
        return -1;
    }
    hs_gb_set_joypad(gb, 0);
    if (hs_gb_start(gb, 3) != 0) {
        hs_gb_destroy(gb);
        return -1;
    }
    t0 = now_ns();
    deadline = t0;
    stop = t0 + (int64_t)(seconds * 1e9);
    while (now_ns() < stop) {
        int64_t now;
        uint64_t due, n, ph;
        size_t k, got;

        deadline += DMG_FRAME_NS;
        wait_until(NULL, deadline);
        now = now_ns();
        due = (uint64_t)((now - t0) / DMG_FRAME_NS) + 1u;
        n = due > done ? due - done : 0;
        if (n > 4) {
            n = 4;
        }
        done = due;
        /* demo ROM: right, then A (beep) every ~second */
        ph = frames % 60u;
        hs_gb_set_joypad(gb, ph < 20u ? 0x00u : (ph < 40u ? 0x10u : 0x11u));
        if (n > 0 && hs_gb_run_frames(gb, n) != 0) {
            break;
        }
        frames += n;
        got = hs_gb_pcm_take(gb, g_pcm, sizeof(g_pcm) / sizeof(g_pcm[0]));
        pcm_samples += got;
        for (k = 0; k < got; k++) {
            nonzero += g_pcm[k] != 0;
        }
        if (audio != NULL && got >= 2) {
            (void)hs_audio_write(audio, g_pcm, got / 2u);
        }
    }
    printf("AUDIOTEST status=\"%s\" gb_frames=%llu apu_frames=%llu apu_nonzero=%llu "
           "out_frames=%llu xruns=%llu dropped=%llu vol=%d ctls=%d\n",
           hs_audio_status(audio), (unsigned long long)frames,
           (unsigned long long)(pcm_samples / 2u), (unsigned long long)nonzero,
           (unsigned long long)(audio ? audio->frames_out : 0),
           (unsigned long long)(audio ? audio->xruns : 0),
           (unsigned long long)(audio ? audio->dropped : 0),
           hs_audio_volume(audio), audio ? audio->ctl_set : 0);
    fflush(stdout);
    hs_gb_stop(gb);
    hs_gb_destroy(gb);
    return 0;
}
