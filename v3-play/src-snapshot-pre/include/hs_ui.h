/* SPDX-License-Identifier: MIT */
#ifndef HS_UI_H
#define HS_UI_H

#include <stdint.h>

#include "hs_theme.h"
#include "hs_handshake.h"

#include "hs_battery.h"

struct hs_fb;

/* v2: 3 x 2 tile grid. */
typedef enum {
    HS_TILE_THEME = 0,
    HS_TILE_DIAL,
    HS_TILE_PACING,
    HS_TILE_BATTERY,
    HS_TILE_POWEROFF,
    HS_TILE_REBOOT,
    HS_TILE_COUNT
} hs_tile_t;

#define HS_TILE_COLS 3

typedef enum {
    HS_CONFIRM_NONE = 0,
    HS_CONFIRM_POWEROFF,
    HS_CONFIRM_REBOOT
} hs_confirm_t;

typedef enum {
    HS_ACTION_NONE = 0,
    HS_ACTION_POWEROFF,
    HS_ACTION_REBOOT,
    HS_ACTION_QUIT
} hs_action_t;

/* Display divider for the Gemstone dial and live telemetry. The 3+1
   Timing Handshake itself always runs at full native speed. */
typedef enum {
    HS_DIAL_SLOW = 0,   /* 1 beat / 500 ms, telemetry 2x/s (default) */
    HS_DIAL_MED,        /* 4 beats/s */
    HS_DIAL_FAST,       /* 10 beats/s */
    HS_DIAL_FULL,       /* every presented frame */
    HS_DIAL_COUNT
} hs_dial_speed_t;

/* Presented frame rate. */
typedef enum {
    HS_PACE_60 = 0,     /* 59.73 Hz DMG native, 16742706 ns (default) */
    HS_PACE_30,         /* 33333333 ns */
    HS_PACE_24,         /* 41666667 ns, cinema */
    HS_PACE_16,         /* 62500000 ns, silent film */
    HS_PACE_COUNT
} hs_pace_t;

const char *hs_dial_name(hs_dial_speed_t d);
int64_t     hs_dial_period_ns(hs_dial_speed_t d); /* 0 = every frame */
const char *hs_pace_name(hs_pace_t p);            /* "60Hz" .. "16Hz" */
const char *hs_pace_tag(hs_pace_t p);             /* "DMG", "", "FILM", "SILENT" */
int64_t     hs_pace_ns(hs_pace_t p);

/* Telemetry snapshot, refreshed at the dial divider rate. */
struct hs_ui_live {
    int            have_gb;
    const uint8_t *gb_fb;        /* 160x144 shades; drawn every frame */
    uint64_t       gb_frames;
    uint64_t       gb_xruns;
    uint64_t       gb_overruns;
    int            have_timing;
    int64_t        p50_ns;       /* presented-frame period */
    int64_t        p99_ns;
    int64_t        max_ns;
};

struct hs_ui {
    hs_theme_id_t   theme;
    hs_tile_t       selected;
    hs_confirm_t    confirm;
    const char     *status_line;
    int             reserve_gb; /* right-hand GB panel in the hero row */
    hs_beat_t       live_beat;  /* nonzero: animate the dial with this beat */
    hs_dial_speed_t dial;
    hs_pace_t       pace;
    struct hs_ui_live live;
};

void hs_ui_init(struct hs_ui *ui, hs_theme_id_t theme);

void hs_ui_draw(struct hs_fb *fb,
                const struct hs_ui *ui,
                const struct hs_hs *hs,
                const struct hs_jitter_stats *stats,
                const struct hs_battery *bat);

/* Move the selection and handle A/B/L1/R1/X/Y. Returns an action if any. */
hs_action_t hs_ui_move(struct hs_ui *ui, int dx, int dy);
hs_action_t hs_ui_activate(struct hs_ui *ui);
hs_action_t hs_ui_cancel(struct hs_ui *ui);
void        hs_ui_cycle_theme(struct hs_ui *ui, int dir);
void        hs_ui_cycle_dial(struct hs_ui *ui, int dir);
void        hs_ui_cycle_pace(struct hs_ui *ui, int dir);

void hs_ui_draw_gemstone(struct hs_fb *fb, int cx, int cy, int radius,
                         const struct hs_palette *pal,
                         hs_beat_t beat, int frozen);

#endif /* HS_UI_H */
