/* SPDX-License-Identifier: MIT */
/* HandshakeOS v5.3 frame-rate meter (pure component: integer math, no clock, no threads). */
#include "hs_fps.h"

#include <stdio.h>
#include <string.h>

#define NS_S 1000000000ll

static const int64_t k_edges_us[HS_FPS_BUCKETS - 1] = { 4000, 8000, 12000, 16700, 25000, 33300, 50000 };

void hs_fps_start(struct hs_fps *f, int64_t now_ns, int64_t target_frame_ns)
{
    memset(f, 0, sizeof(*f));
    f->win_t0 = now_ns;
    f->s_t0 = now_ns;
    f->s_t_last = now_ns;
    f->target_ns = target_frame_ns;
    f->s_worst_game_fps10 = -1;
}

void hs_fps_frame(struct hs_fps *f, int64_t t0, int64_t t_emu, int64_t t_end, uint32_t game_frames)
{
    int64_t work = t_end - t0, emu = t_emu - t0, flip = t_end - t_emu, us;
    int b;

    if (work < 0) work = 0;
    if (emu < 0) emu = 0;
    if (flip < 0) flip = 0;
    f->s_draw++;
    f->s_game += game_frames;
    if (game_frames > 1) f->s_catchup += game_frames - 1u;
    if (f->target_ns > 0 && work > f->target_ns) f->s_late++;
    f->s_work_sum += work;
    if (work > f->s_work_max) f->s_work_max = work;
    f->s_emu_sum += emu;
    if (emu > f->s_emu_max) f->s_emu_max = emu;
    f->s_flip_sum += flip;
    if (flip > f->s_flip_max) f->s_flip_max = flip;
    us = work / 1000;
    for (b = 0; b < HS_FPS_BUCKETS - 1 && us >= k_edges_us[b]; b++) {
    }
    f->hist[b]++;
    f->s_t_last = t_end;
    if (f->s_draw == 1) {
        /* first present: anchors both clocks; rates are counted over present-to-present intervals */
        f->s_t0 = t_end;
        f->s_first_game = game_frames;
        f->win_t0 = t_end;
        return;
    }

    f->win_draw++;
    f->win_game += game_frames;
    f->win_work_sum += work;
    if (work > f->win_work_max) f->win_work_max = work;
    if (t_end - f->win_t0 >= NS_S) {
        int64_t span = t_end - f->win_t0;
        f->game_fps10 = (int)(((int64_t)f->win_game * 10 * NS_S + span / 2) / span);
        f->draw_fps10 = (int)(((int64_t)f->win_draw * 10 * NS_S + span / 2) / span);
        f->work_us_avg = (int)(f->win_work_sum / (int64_t)f->win_draw / 1000);
        f->work_us_max = (int)(f->win_work_max / 1000);
        f->have = 1;
        f->s_windows++;
        if (f->s_worst_game_fps10 < 0 || f->game_fps10 < f->s_worst_game_fps10) {
            f->s_worst_game_fps10 = f->game_fps10;
        }
        f->win_t0 = t_end;
        f->win_draw = 0;
        f->win_game = 0;
        f->win_work_sum = 0;
        f->win_work_max = 0;
    }
}

void hs_fps_hud(const struct hs_fps *f, char *l1, size_t n1, char *l2, size_t n2, char *l3, size_t n3)
{
    if (!f->have) {
        snprintf(l1, n1, "FPS --");
        snprintf(l2, n2, "DRAW --");
        snprintf(l3, n3, "MS --");
        return;
    }
    snprintf(l1, n1, "FPS %d.%d", f->game_fps10 / 10, f->game_fps10 % 10);
    snprintf(l2, n2, "DRAW %d.%d", f->draw_fps10 / 10, f->draw_fps10 % 10);
    snprintf(l3, n3, "MS %d.%d", f->work_us_avg / 1000, (f->work_us_avg % 1000) / 100);
}

/* x.yz ms from ns */
static void ms2(char *b, size_t n, int64_t ns)
{
    int64_t us = ns / 1000;
    snprintf(b, n, "%lld.%02lld", (long long)(us / 1000), (long long)((us % 1000) / 10));
}

int hs_fps_report(const struct hs_fps *f, const char *tag, const char *core, const char *game,
                  char *out, size_t n)
{
    int64_t span = f->s_t_last - f->s_t0;
    int64_t d = f->s_draw ? (int64_t)f->s_draw : 1;
    int64_t ig = (int64_t)(f->s_game - f->s_first_game), id = f->s_draw > 0 ? (int64_t)f->s_draw - 1 : 0;
    long long game10 = span > 0 ? (long long)((ig * 10 * NS_S + span / 2) / span) : 0;
    long long draw10 = span > 0 ? (long long)((id * 10 * NS_S + span / 2) / span) : 0;
    char wa[24], wm[24], ea[24], em[24], fa[24], fm[24], tg[24];
    int r;

    ms2(wa, sizeof(wa), f->s_work_sum / d);
    ms2(wm, sizeof(wm), f->s_work_max);
    ms2(ea, sizeof(ea), f->s_emu_sum / d);
    ms2(em, sizeof(em), f->s_emu_max);
    ms2(fa, sizeof(fa), f->s_flip_sum / d);
    ms2(fm, sizeof(fm), f->s_flip_max);
    ms2(tg, sizeof(tg), f->target_ns);
    r = snprintf(out, n,
                 "HandshakeOS FPS v5.3 sys=%s core=%s game=%s seconds=%lld.%01lld game_fps=%lld.%lld "
                 "draw_fps=%lld.%lld worst_1s_game_fps=%d.%d target_ms=%s frame_ms_avg=%s frame_ms_max=%s "
                 "emu_ms_avg=%s emu_ms_max=%s blit_flip_ms_avg=%s blit_flip_ms_max=%s late=%llu catchup=%llu "
                 "draws=%llu game_frames=%llu hist_ms<4,8,12,16.7,25,33,50,inf=%u,%u,%u,%u,%u,%u,%u,%u\n",
                 tag ? tag : "?", core ? core : "?", game ? game : "?",
                 (long long)(span / NS_S), (long long)((span % NS_S) / 100000000ll),
                 game10 / 10, game10 % 10, draw10 / 10, draw10 % 10,
                 f->s_worst_game_fps10 < 0 ? 0 : f->s_worst_game_fps10 / 10,
                 f->s_worst_game_fps10 < 0 ? 0 : f->s_worst_game_fps10 % 10,
                 tg, wa, wm, ea, em, fa, fm,
                 (unsigned long long)f->s_late, (unsigned long long)f->s_catchup,
                 (unsigned long long)f->s_draw, (unsigned long long)f->s_game,
                 f->hist[0], f->hist[1], f->hist[2], f->hist[3], f->hist[4], f->hist[5], f->hist[6],
                 f->hist[7]);
    if (r < 0) return 0;
    if ((size_t)r >= n) {
        if (n >= 2) { out[n - 2] = '\n'; out[n - 1] = '\0'; }
        return n > 0 ? (int)n - 1 : 0;
    }
    return r;
}
