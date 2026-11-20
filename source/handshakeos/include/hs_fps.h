/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hs_fps.h - v5.3 live frame-rate meter for the in-game HUD and the per-session timing log.
 * Pure component: never reads a clock; the caller passes CLOCK_MONOTONIC ns stamps.
 *   GAME fps = emulated frames per second (game speed; 59.7 on GB = full speed)
 *   DRAW fps = frames actually presented (flips) per second
 *   frame ms = work per presented frame: emulate + audio + blit + flip
 * A frame that runs more than one emulated frame (catch-up) or whose work exceeds the target
 * frame time (late) is what makes play look laggy; both are counted. */
#ifndef HS_FPS_H
#define HS_FPS_H

#include <stddef.h>
#include <stdint.h>

#define HS_FPS_BUCKETS 8   /* work ms: <4 <8 <12 <16.7 <25 <33.3 <50 >=50 */

struct hs_fps {
    /* current 1 s window */
    int64_t  win_t0;
    uint32_t win_draw, win_game;
    int64_t  win_work_sum, win_work_max;
    /* shown on the HUD (refreshed once per second) */
    int      game_fps10, draw_fps10;  /* fps x10 */
    int      work_us_avg, work_us_max;
    int      have;                    /* at least one full window */
    /* whole session */
    int64_t  s_t0, s_t_last, target_ns; /* s_t0 = end of the first frame (rates count intervals) */
    uint32_t s_first_game;
    uint64_t s_draw, s_game, s_catchup, s_late, s_windows;
    int64_t  s_work_sum, s_work_max, s_emu_sum, s_emu_max, s_flip_sum, s_flip_max;
    int      s_worst_game_fps10;      /* slowest full window */
    uint32_t hist[HS_FPS_BUCKETS];
};

void hs_fps_start(struct hs_fps *f, int64_t now_ns, int64_t target_frame_ns);
/* One presented frame: t0 = loop start (after the pacing wait), t_emu = cores + audio done,
   t_end = after the flip; game_frames = emulated frames run this present (0 allowed). */
void hs_fps_frame(struct hs_fps *f, int64_t t0, int64_t t_emu, int64_t t_end, uint32_t game_frames);
/* HUD lines "FPS 59.7" (game speed), "DRAW 59.7" (presented), "MS 4.2" (avg work per frame).
   Before the first full 1 s window: "FPS --", "DRAW --", "MS --". */
void hs_fps_hud(const struct hs_fps *f, char *l1, size_t n1, char *l2, size_t n2, char *l3, size_t n3);
/* One readable log line (ends in '\n'). Returns its length. */
int  hs_fps_report(const struct hs_fps *f, const char *tag, const char *core, const char *game,
                   char *out, size_t n);

#endif
