/* SPDX-License-Identifier: MIT */
#ifndef HS_UI_H
#define HS_UI_H

#include <stdint.h>

#include "hs_theme.h"
#include "hs_handshake.h"

#include "hs_battery.h"

struct hs_fb;

/* v3: 3 x 2 tile grid, GAMES first (battery lives in the top bar). */
typedef enum {
    HS_TILE_GAMES = 0,
    HS_TILE_THEME,
    HS_TILE_DIAL,
    HS_TILE_PACING,
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
    HS_ACTION_QUIT,
    HS_ACTION_GAMES
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

/* v3 GAMES picker. names[i] are display names (extension stripped). */
struct hs_games_view {
    const char *const *names;
    int         n;
    int         selected;
    const char *sd_status;    /* "SD: 12 ROMS" / "SD: DEMO ONLY" */
    const char *audio_status; /* "SOUND: SYNTH ONLY" etc. */
    const char *message;      /* transient error, or NULL */
};

void hs_ui_draw_games(struct hs_fb *fb, hs_theme_id_t theme,
                      const struct hs_games_view *v);

/* v4-multi: 2x bold multi-system GAMES picker. */
struct hs_games_multi_view {
    const char *const *names;   /* display names */
    const char *const *tags;    /* "GB", "GBC", "GBA", "SNES", "GEN", "PSX", "N64" */
    const uint32_t    *tag_rgb; /* per-entry tag colour */
    const int         *linked;  /* per-entry: 1 = core linked in this build */
    int         n;
    int         selected;
    const char *sd_status;      /* "SD: 12 ROMS" / "SD: DEMO ONLY" */
    const char *systems_status; /* "5 SYSTEMS LIVE" */
    const char *battery_status; /* "BAT 87%" / "BAT --" */
    const char *audio_status;   /* "VOL 80%" / "SYNTH" */
    const char *message;        /* transient line, or NULL */
    const char *notice;         /* centred 2x panel (unlinked core), or NULL */
    const char *notice_sub;     /* second line of the panel */
};

void hs_ui_draw_games_multi(struct hs_fb *fb, hs_theme_id_t theme,
                            const struct hs_games_multi_view *v);

#endif /* HS_UI_H */
