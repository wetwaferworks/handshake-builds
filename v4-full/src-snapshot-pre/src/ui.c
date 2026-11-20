/* SPDX-License-Identifier: MIT */
/* HandshakeOS v2 shell: every glyph is scale 2 (16x16) bold with a 2 px
   drop shadow, laid out for the RG35XX 2024 640x480 3.5" IPS. Layout grid:

     y   0..48   top bar      HandshakeOS | pacing chip | dial chip | battery
     y  56..300  hero card    handshake telemetry + Gemstone dial
                              (GB mode: hero 12..420, GB panel 432..628)
     y 308..366  tile row 1   GAMES | THEME | DIAL
     y 374..432  tile row 2   PACING | POWER OFF | REBOOT
     y 440..480  footer       button legend (or confirm legend)
*/
#include "hs_ui.h"
#include "hs_fb.h"
#include "hs_font.h"
#include "hs_gb.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define S2        2                   /* every UI string */
#define CH        (HS_FONT_W * S2)    /* 16 px per character */
#define LINE      24                  /* 16 px glyph + 8 px leading */
#define MARGIN    12
#define BAR_H     48
#define FOOT_H    40
#define HERO_Y    56
#define HERO_H    244
#define TILE_Y    308
#define TILE_H    58
#define TILE_GAP  8
#define GB_PANEL_W 196

static const char *const k_dial_names[HS_DIAL_COUNT] = { "SLOW", "MED", "FAST", "FULL" };
static const int64_t k_dial_ns[HS_DIAL_COUNT] = { 500000000ll, 250000000ll, 100000000ll, 0 };
static const char *const k_pace_names[HS_PACE_COUNT] = { "60Hz", "30Hz", "24Hz", "16Hz" };
static const char *const k_pace_tags[HS_PACE_COUNT] = { "DMG", "", "FILM", "SILENT" };
static const int64_t k_pace_ns[HS_PACE_COUNT] = {
    16742706ll,   /* 1 / 59.7275 s, DMG LCD refresh */
    33333333ll,
    41666667ll,   /* 24 fps cinema */
    62500000ll    /* 16 fps silent film */
};

const char *hs_dial_name(hs_dial_speed_t d)
{
    return (unsigned)d < HS_DIAL_COUNT ? k_dial_names[d] : "?";
}

int64_t hs_dial_period_ns(hs_dial_speed_t d)
{
    return (unsigned)d < HS_DIAL_COUNT ? k_dial_ns[d] : 0;
}

const char *hs_pace_name(hs_pace_t p)
{
    return (unsigned)p < HS_PACE_COUNT ? k_pace_names[p] : "?";
}

const char *hs_pace_tag(hs_pace_t p)
{
    return (unsigned)p < HS_PACE_COUNT ? k_pace_tags[p] : "";
}

int64_t hs_pace_ns(hs_pace_t p)
{
    return (unsigned)p < HS_PACE_COUNT ? k_pace_ns[p] : k_pace_ns[0];
}

void hs_ui_init(struct hs_ui *ui, hs_theme_id_t theme)
{
    if (ui == NULL) {
        return;
    }
    memset(ui, 0, sizeof(*ui));
    ui->theme = theme;
    ui->selected = HS_TILE_GAMES;
    ui->confirm = HS_CONFIRM_NONE;
    ui->status_line = NULL;
    ui->dial = HS_DIAL_SLOW;
    ui->pace = HS_PACE_60;
}

/* All shell text goes through here: scale 2, bold, theme drop shadow. */
static int txt(struct hs_fb *fb, const struct hs_palette *pal, int x, int y,
               uint32_t color, const char *s)
{
    return hs_fb_text_ex(fb, x, y, S2, color, pal->text_shadow, HS_TEXT_NICE, s);
}

static int txtf(struct hs_fb *fb, const struct hs_palette *pal, int x, int y,
                uint32_t color, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

static int txtf(struct hs_fb *fb, const struct hs_palette *pal, int x, int y,
                uint32_t color, const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return txt(fb, pal, x, y, color, buf);
}

static int text_w(const char *s)
{
    return hs_font_text_width(s, S2);
}

/* Compact durations so telemetry lines stay inside their columns:
   "850ns", "25us", "16.7ms", "1.25s". */
static void format_ns(char *buf, size_t n, int64_t ns)
{
    if (ns < 0) {
        ns = -ns;
    }
    if (ns < 1000) {
        snprintf(buf, n, "%lldns", (long long)ns);
    } else if (ns < 1000000) {
        snprintf(buf, n, "%lldus", (long long)((ns + 500) / 1000));
    } else if (ns < 100000000) {
        long long t = (long long)((ns + 50000) / 100000); /* tenths of ms */
        snprintf(buf, n, "%lld.%lldms", t / 10, t % 10);
    } else if (ns < 1000000000) {
        snprintf(buf, n, "%lldms", (long long)((ns + 500000) / 1000000));
    } else {
        long long t = (long long)((ns + 5000000) / 10000000);
        snprintf(buf, n, "%lld.%02llds", t / 100, t % 100);
    }
}

static const char *beat_short(hs_beat_t b)
{
    switch (b) {
    case HS_BEAT_PUBLISH: return "PUB";
    case HS_BEAT_PULL:    return "PULL";
    case HS_BEAT_BARRIER: return "BAR";
    case HS_BEAT_COMMIT:  return "CMIT";
    default:              return "IDLE";
    }
}

/* Diamond: top=publish, right=pull, bottom=barrier, left=commit.
   labels: 0 none, 1 top/bottom only, 2 all four (scale 2, bold). */
static void draw_gemstone(struct hs_fb *fb, int cx, int cy, int r,
                          const struct hs_palette *pal, hs_beat_t beat, int frozen,
                          int labels)
{
    const int verts[4][2] = {
        { 0, -r },          /* publish  (top)    */
        { r,  0 },          /* pull     (right)  */
        { 0,  r },          /* barrier  (bottom) */
        { -r, 0 }           /* commit   (left)   */
    };
    const char *labs[4] = { "PUB", "PULL", "BAR", "CMIT" };
    int i, x0, y0, x1, y1;
    hs_beat_t lit = beat;

    if (lit == HS_BEAT_IDLE) {
        lit = HS_BEAT_COMMIT; /* settled after a successful phase */
    }

    for (i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        int dx, dy, steps, s;
        uint32_t col = pal->border;

        if ((hs_beat_t)(i + 1) == lit || (hs_beat_t)(j + 1) == lit) {
            col = frozen ? pal->warn : pal->accent;
        }
        x0 = cx + verts[i][0];
        y0 = cy + verts[i][1];
        x1 = cx + verts[j][0];
        y1 = cy + verts[j][1];
        dx = x1 - x0;
        dy = y1 - y0;
        steps = (dx < 0 ? -dx : dx);
        if ((dy < 0 ? -dy : dy) > steps) {
            steps = dy < 0 ? -dy : dy;
        }
        if (steps < 1) {
            steps = 1;
        }
        for (s = 0; s <= steps; s++) {
            int x = x0 + dx * s / steps;
            int y = y0 + dy * s / steps;
            hs_fb_fill(fb, x - 2, y - 2, 4, 4, col);
        }
    }

    for (i = 0; i < 4; i++) {
        int vx = cx + verts[i][0];
        int vy = cy + verts[i][1];
        int on = ((hs_beat_t)(i + 1) == lit);
        uint32_t col = on ? (frozen ? pal->warn : pal->accent2) : pal->border;
        int rad = on ? 9 : 6;

        hs_fb_fill_circle(fb, vx + 2, vy + 2, rad, pal->shadow);
        hs_fb_fill_circle(fb, vx, vy, rad, col);
        if (labels == 0 || (labels == 1 && (i == 1 || i == 3))) {
            continue;
        }
        {
            int tw = text_w(labs[i]);
            int tx = vx, ty = vy;
            if (i == 0) { tx -= tw / 2; ty -= 12 + CH; }
            if (i == 1) { tx += 12;     ty -= CH / 2; }
            if (i == 2) { tx -= tw / 2; ty += 12; }
            if (i == 3) { tx -= tw + 12; ty -= CH / 2; }
            if (tx + tw > fb->w - 4) {
                tx = fb->w - 4 - tw;
            }
            if (tx < 0) {
                tx = 0;
            }
            txt(fb, pal, tx, ty, on ? pal->text : pal->dim, labs[i]);
        }
    }
}

void hs_ui_draw_gemstone(struct hs_fb *fb, int cx, int cy, int radius,
                         const struct hs_palette *pal,
                         hs_beat_t beat, int frozen)
{
    draw_gemstone(fb, cx, cy, radius, pal, beat, frozen, 2);
}

static void card(struct hs_fb *fb, const struct hs_palette *pal, int x, int y,
                 int w, int h, int sel)
{
    hs_fb_fill_round(fb, x + 3, y + 3, w, h, 8, pal->shadow);
    hs_fb_fill_round(fb, x, y, w, h, 8, sel ? pal->tile_sel : pal->tile);
    hs_fb_rect(fb, x, y, w, h, sel ? 3 : 1, sel ? pal->border_sel : pal->border);
}

static void chip(struct hs_fb *fb, const struct hs_palette *pal, int x, int y,
                 const char *s, uint32_t fg)
{
    int w = text_w(s) + 16;

    hs_fb_fill_round(fb, x, y, w, 28, 6, pal->tile_sel);
    hs_fb_rect(fb, x, y, w, 28, 1, pal->border_sel);
    txt(fb, pal, x + 8, y + 6, fg, s);
}

static void draw_battery_icon(struct hs_fb *fb, int x, int y, int w, int h,
                              const struct hs_battery *bat, const struct hs_palette *pal)
{
    int percent = (bat && bat->present) ? bat->percent : -1;
    int inner, fillw;
    uint32_t col = pal->value;

    hs_fb_rect(fb, x, y, w - 4, h, 2, pal->text);
    hs_fb_fill(fb, x + w - 4, y + h / 4, 4, h / 2, pal->text);
    if (percent < 0) {
        if (bat && bat->charger_online == 1) {
            hs_fb_fill(fb, x + 6, y + h / 2 - 2, w - 16, 4, pal->good);
        }
        return;
    }
    if (percent <= 20) {
        col = pal->warn;
    } else if (percent >= 80 || bat->charging == 1) {
        col = pal->good;
    }
    inner = w - 12;
    fillw = inner * percent / 100;
    hs_fb_fill(fb, x + 4, y + 4, fillw, h - 8, col);
}

static void draw_confirm(struct hs_fb *fb, const struct hs_palette *pal, hs_confirm_t c)
{
    const int bw = 560, bh = 248;
    const int bx = (HS_FB_W - bw) / 2, by = (HS_FB_H - bh) / 2;
    const char *head = c == HS_CONFIRM_POWEROFF ? "POWER OFF?" : "REBOOT?";
    const char *sub = c == HS_CONFIRM_POWEROFF ? "CONFIRM POWER OFF" : "CONFIRM REBOOT";
    int hw = hs_font_text_width(head, 4);

    /* Darken everything behind the dialog (50% checker) so the box is the
       only readable thing on screen. */
    hs_fb_checker(fb, pal->shadow);
    hs_fb_fill_round(fb, bx + 6, by + 6, bw, bh, 12, 0x000000u);
    hs_fb_fill_round(fb, bx, by, bw, bh, 12, pal->bg);
    hs_fb_rect(fb, bx, by, bw, bh, 6, pal->warn);
    hs_fb_rect(fb, bx + 10, by + 10, bw - 20, bh - 20, 1, pal->warn);

    hs_fb_text_ex(fb, bx + (bw - hw) / 2, by + 28, 4, pal->warn, pal->text_shadow,
                  HS_TEXT_NICE, head);
    txt(fb, pal, bx + (bw - text_w(sub)) / 2, by + 28 + 32 + 18, pal->text, sub);

    /* Two big buttons */
    hs_fb_fill_round(fb, bx + 40, by + 128, 220, 56, 10, pal->warn);
    hs_fb_text_ex(fb, bx + 40 + (220 - hs_font_text_width("A = YES", S2)) / 2, by + 148,
                  S2, 0x000000u, pal->warn, HS_TEXT_BOLD, "A = YES");
    hs_fb_fill_round(fb, bx + bw - 260, by + 128, 220, 56, 10, pal->tile_sel);
    hs_fb_rect(fb, bx + bw - 260, by + 128, 220, 56, 2, pal->border_sel);
    txt(fb, pal, bx + bw - 260 + (220 - text_w("B = CANCEL")) / 2, by + 148, pal->text,
        "B = CANCEL");

    if (c == HS_CONFIRM_POWEROFF) {
        const char *wake = "Wake: hold POWER 3s";
        txt(fb, pal, bx + (bw - text_w(wake)) / 2, by + bh - 44, pal->dim, wake);
    } else {
        const char *note = "Back to stock SD boot";
        txt(fb, pal, bx + (bw - text_w(note)) / 2, by + bh - 44, pal->dim, note);
    }
}

static void draw_gb_panel(struct hs_fb *fb, const struct hs_ui *ui,
                          const struct hs_palette *pal, int px)
{
    const int py = HERO_Y;
    const int gx = px + (GB_PANEL_W - 160) / 2;
    const int gy = py + 38;
    char a[24], b[24];

    card(fb, pal, px, py, GB_PANEL_W, HERO_H, 0);
    txt(fb, pal, px + 10, py + 10, pal->accent2, "GB DEMO");
    hs_fb_fill(fb, px + GB_PANEL_W - 22, py + 14, 10, 10, pal->good); /* LIVE dot */
    hs_fb_fill(fb, gx - 4, gy - 4, 160 + 8, 144 + 8, pal->border_sel);
    if (ui->live.gb_fb != NULL) {
        hs_gb_blit(fb, gx, gy, 1, ui->live.gb_fb, pal->gb);
    }
    if (ui->live.have_timing) {
        format_ns(a, sizeof(a), ui->live.p50_ns);
        txtf(fb, pal, px + 10, gy + 144 + 12, pal->value, "%s %s",
             hs_pace_name(ui->pace), a);
    } else {
        txtf(fb, pal, px + 10, gy + 144 + 12, pal->value, "%s", hs_pace_name(ui->pace));
    }
    snprintf(b, sizeof(b), "xr%llu ov%llu",
             (unsigned long long)ui->live.gb_xruns,
             (unsigned long long)ui->live.gb_overruns);
    txt(fb, pal, px + 10, gy + 144 + 12 + LINE, pal->dim, b);
}

void hs_ui_draw(struct hs_fb *fb,
                const struct hs_ui *ui,
                const struct hs_hs *hs,
                const struct hs_jitter_stats *stats,
                const struct hs_battery *bat)
{
    const struct hs_palette *pal;
    int hero_x = MARGIN, hero_w, tx, ty;
    int tile_w, i, gb = 0;
    char p50[24], p95[24], p99[24], pmax[24], bl[16];
    hs_beat_t beat;
    int frozen;
    uint64_t epoch, hash;
    hs_tile_t t;

    if (fb == NULL || ui == NULL) {
        return;
    }
    pal = hs_theme_get(ui->theme);
    gb = ui->reserve_gb ? 1 : 0;
    hs_fb_clear(fb, pal->bg);

    /* ---- top bar ---- */
    hs_fb_fill(fb, 0, 0, fb->w, BAR_H, pal->bar);
    hs_fb_fill(fb, 0, BAR_H - 3, fb->w, 3, pal->accent);
    txt(fb, pal, MARGIN, 14, pal->accent2, "HandshakeOS");
    chip(fb, pal, 208, 9, hs_pace_name(ui->pace), pal->value);
    chip(fb, pal, 304, 9, hs_dial_name(ui->dial), pal->value);
    hs_battery_label(bat, bl, sizeof(bl));
    {
        int lx = fb->w - MARGIN - text_w(bl);
        int ix = lx - 44;
        const char *tag = hs_pace_tag(ui->pace);
        draw_battery_icon(fb, ix, 14, 36, 20, bat, pal);
        txt(fb, pal, lx, 14, (bat && bat->present) ? pal->text : pal->dim, bl);
        if (tag[0] != '\0' && 400 + text_w(tag) + 8 <= ix) {
            txt(fb, pal, 400, 14, pal->dim, tag);
        }
    }

    /* ---- hero card ---- */
    hero_w = fb->w - 2 * MARGIN - (gb ? GB_PANEL_W + MARGIN : 0);
    card(fb, pal, hero_x, HERO_Y, hero_w, HERO_H, 0);

    beat = hs ? hs_hs_beat(hs) : HS_BEAT_IDLE;
    if (ui->live_beat != HS_BEAT_IDLE) {
        beat = ui->live_beat;
    }
    frozen = hs ? (hs_hs_frozen(hs) ? 1 : 0) : 0;
    epoch = hs ? hs_hs_commit_epoch(hs) : 0;
    hash = hs ? hs_hs_ledger_hash(hs) : 0;
    if (beat == HS_BEAT_IDLE && epoch > 0) {
        beat = HS_BEAT_COMMIT;
    }

    tx = hero_x + 14;
    ty = HERO_Y + 12;
    txt(fb, pal, tx, ty, pal->accent2, gb ? "HANDSHAKE 3+1" : "TIMING HANDSHAKE 3+1");
    ty += LINE;
    txt(fb, pal, tx, ty, pal->dim, "EPOCH");
    txtf(fb, pal, tx + 6 * CH, ty, pal->value, "%llu", (unsigned long long)epoch);
    ty += LINE;
    txt(fb, pal, tx, ty, pal->dim, "LEDGER");
    ty += LINE;
    txtf(fb, pal, tx, ty, pal->accent2, "%016llx", (unsigned long long)hash);
    ty += LINE;
    if (stats) {
        format_ns(p50, sizeof(p50), stats->p50_ns);
        format_ns(p95, sizeof(p95), stats->p95_ns);
        format_ns(p99, sizeof(p99), stats->p99_ns);
        format_ns(pmax, sizeof(pmax), stats->max_ns);
        txtf(fb, pal, tx, ty, pal->text, "p50 %s p95 %s", p50, p95);
        ty += LINE;
        if (gb) {
            /* <= 17 columns so nothing reaches the dial strip at x 315+ */
            txtf(fb, pal, tx, ty, pal->text, "p99 %s", p99);
            ty += LINE;
            txtf(fb, pal, tx, ty, pal->dim, "max %s ovr %llu", pmax,
                 (unsigned long long)stats->overruns);
        } else {
            txtf(fb, pal, tx, ty, pal->text, "p99 %s max %s", p99, pmax);
            ty += LINE;
            txtf(fb, pal, tx, ty, pal->dim, "phases %llu ovr %llu",
                 (unsigned long long)stats->ticks_done,
                 (unsigned long long)stats->overruns);
        }
        ty += LINE;
    } else {
        txt(fb, pal, tx, ty, pal->dim, "no samples yet");
        ty += 3 * LINE;
    }
    if (frozen) {
        struct hs_freeze fr;
        hs_hs_last_freeze(hs, &fr);
        txtf(fb, pal, tx, ty, pal->warn, "FREEZE %.10s", fr.component_name);
    } else {
        txt(fb, pal, tx, ty, pal->good, gb ? "WATCHDOG OK" : "WATCHDOG OK 250ms");
    }
    ty += LINE;
    txtf(fb, pal, tx, ty, pal->text, "BEAT %s", beat_short(beat));
    txtf(fb, pal, tx + 10 * CH, ty, pal->dim, "%s", hs_dial_name(ui->dial));

    if (gb) {
        /* Narrow hero: dial in the right strip, PUB/BAR labels only
           (the BEAT line names the lit vertex). */
        draw_gemstone(fb, hero_x + hero_w - 56, HERO_Y + 122, 40, pal, beat, frozen, 1);
        draw_gb_panel(fb, ui, pal, fb->w - MARGIN - GB_PANEL_W);
    } else {
        draw_gemstone(fb, hero_x + 476, HERO_Y + 122, 56, pal, beat, frozen, 2);
    }

    /* ---- 3 x 2 tiles ---- */
    tile_w = (fb->w - 2 * MARGIN - (HS_TILE_COLS - 1) * TILE_GAP) / HS_TILE_COLS;
    for (i = 0; i < HS_TILE_COUNT; i++) {
        int sel = (ui->selected == (hs_tile_t)i) && ui->confirm == HS_CONFIRM_NONE;
        int col = i % HS_TILE_COLS, row = i / HS_TILE_COLS;
        int x = MARGIN + col * (tile_w + TILE_GAP);
        int y = TILE_Y + row * (TILE_H + TILE_GAP);
        const char *title = "", *key = "";
        char val[24];

        val[0] = '\0';
        t = (hs_tile_t)i;
        card(fb, pal, x, y, tile_w, TILE_H, sel);
        if (sel) {
            hs_fb_fill(fb, x + 3, y + 8, 5, TILE_H - 16, pal->accent);
        }
        switch (t) {
        case HS_TILE_GAMES:
            title = "GAMES"; key = "A";
            snprintf(val, sizeof(val), "Game Boy");
            break;
        case HS_TILE_THEME:
            title = "THEME"; key = "L1R1";
            snprintf(val, sizeof(val), "%s", pal->short_name);
            break;
        case HS_TILE_DIAL:
            title = "DIAL"; key = "Y";
            snprintf(val, sizeof(val), "%s", hs_dial_name(ui->dial));
            break;
        case HS_TILE_PACING:
            title = "PACING"; key = "X";
            snprintf(val, sizeof(val), "%s %s", hs_pace_name(ui->pace),
                     hs_pace_tag(ui->pace));
            break;
        case HS_TILE_POWEROFF:
            title = "POWER OFF"; key = "";
            snprintf(val, sizeof(val), "Press A");
            break;
        case HS_TILE_REBOOT:
            title = "REBOOT"; key = "";
            snprintf(val, sizeof(val), "Press A");
            break;
        default:
            break;
        }
        txt(fb, pal, x + 14, y + 7, sel ? pal->text : pal->dim, title);
        if (key[0] != '\0') {
            txt(fb, pal, x + tile_w - 12 - text_w(key), y + 7, pal->accent, key);
        }
        txt(fb, pal, x + 14, y + 7 + LINE, (t == HS_TILE_POWEROFF || t == HS_TILE_REBOOT)
                                               ? pal->warn : pal->value, val);
    }

    /* ---- footer ---- */
    hs_fb_fill(fb, 0, fb->h - FOOT_H, fb->w, FOOT_H, pal->bar);
    hs_fb_fill(fb, 0, fb->h - FOOT_H, fb->w, 2, pal->border);
    if (ui->confirm != HS_CONFIRM_NONE) {
        txt(fb, pal, MARGIN, fb->h - FOOT_H + 12, pal->warn, "A = YES      B = CANCEL");
    } else if (ui->status_line) {
        txt(fb, pal, MARGIN, fb->h - FOOT_H + 12, pal->text, ui->status_line);
    } else {
        /* 38 columns max at scale 2 */
        int x = MARGIN;
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "L1R1");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Theme ");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "X");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Pace ");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "Y");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Dial ");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "A");
        (void)txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Select");
    }

    if (ui->confirm != HS_CONFIRM_NONE) {
        draw_confirm(fb, pal, ui->confirm);
    }
}

hs_action_t hs_ui_move(struct hs_ui *ui, int dx, int dy)
{
    int s;

    if (ui == NULL) {
        return HS_ACTION_NONE;
    }
    if (ui->confirm != HS_CONFIRM_NONE) {
        return HS_ACTION_NONE;
    }
    s = (int)ui->selected;
    if (dx != 0) {
        int col = s % HS_TILE_COLS + dx;
        if (col < 0) {
            col = 0;
        }
        if (col >= HS_TILE_COLS) {
            col = HS_TILE_COLS - 1;
        }
        s = (s / HS_TILE_COLS) * HS_TILE_COLS + col;
    }
    if (dy != 0) {
        s += dy * HS_TILE_COLS;
        if (s < 0) {
            s += HS_TILE_COLS;
        }
        if (s >= HS_TILE_COUNT) {
            s -= HS_TILE_COLS;
        }
    }
    ui->selected = (hs_tile_t)s;
    return HS_ACTION_NONE;
}

hs_action_t hs_ui_activate(struct hs_ui *ui)
{
    if (ui == NULL) {
        return HS_ACTION_NONE;
    }
    if (ui->confirm == HS_CONFIRM_POWEROFF) {
        ui->confirm = HS_CONFIRM_NONE;
        return HS_ACTION_POWEROFF;
    }
    if (ui->confirm == HS_CONFIRM_REBOOT) {
        ui->confirm = HS_CONFIRM_NONE;
        return HS_ACTION_REBOOT;
    }
    switch (ui->selected) {
    case HS_TILE_GAMES:
        return HS_ACTION_GAMES;
    case HS_TILE_THEME:
        hs_ui_cycle_theme(ui, 1);
        break;
    case HS_TILE_DIAL:
        hs_ui_cycle_dial(ui, 1);
        break;
    case HS_TILE_PACING:
        hs_ui_cycle_pace(ui, 1);
        break;
    case HS_TILE_POWEROFF:
        ui->confirm = HS_CONFIRM_POWEROFF;
        break;
    case HS_TILE_REBOOT:
        ui->confirm = HS_CONFIRM_REBOOT;
        break;
    default:
        break;
    }
    return HS_ACTION_NONE;
}

hs_action_t hs_ui_cancel(struct hs_ui *ui)
{
    if (ui == NULL) {
        return HS_ACTION_NONE;
    }
    ui->confirm = HS_CONFIRM_NONE;
    return HS_ACTION_NONE;
}

void hs_ui_cycle_theme(struct hs_ui *ui, int dir)
{
    if (ui == NULL) {
        return;
    }
    if (dir < 0) {
        ui->theme = hs_theme_prev(ui->theme);
    } else {
        ui->theme = hs_theme_next(ui->theme);
    }
}

void hs_ui_cycle_dial(struct hs_ui *ui, int dir)
{
    if (ui == NULL) {
        return;
    }
    ui->dial = (hs_dial_speed_t)(((int)ui->dial + (dir < 0 ? HS_DIAL_COUNT - 1 : 1))
                                 % HS_DIAL_COUNT);
}

void hs_ui_cycle_pace(struct hs_ui *ui, int dir)
{
    if (ui == NULL) {
        return;
    }
    ui->pace = (hs_pace_t)(((int)ui->pace + (dir < 0 ? HS_PACE_COUNT - 1 : 1))
                           % HS_PACE_COUNT);
}

/* ---- v3 GAMES picker: same 2x bold shadowed text as the shell ---- */
#define GL_TOP    60
#define GL_ROW    36
#define GL_ROWS   9

void hs_ui_draw_games(struct hs_fb *fb, hs_theme_id_t theme,
                      const struct hs_games_view *v)
{
    const struct hs_palette *pal;
    int first = 0, i, y;
    char line[64];

    if (fb == NULL || v == NULL) {
        return;
    }
    pal = hs_theme_get(theme);
    hs_fb_clear(fb, pal->bg);

    hs_fb_fill(fb, 0, 0, fb->w, BAR_H, pal->bar);
    hs_fb_fill(fb, 0, BAR_H - 3, fb->w, 3, pal->accent);
    txt(fb, pal, MARGIN, 14, pal->accent2, "HandshakeOS");
    txt(fb, pal, 208, 14, pal->text, "GAMES");
    snprintf(line, sizeof(line), "%d", v->n);
    chip(fb, pal, fb->w - MARGIN - text_w("GAME BOY") - 16, 9, "GAME BOY", pal->value);

    if (v->selected >= GL_ROWS) {
        first = v->selected - GL_ROWS + 1;
    }
    card(fb, pal, MARGIN, GL_TOP - 4, fb->w - 2 * MARGIN, GL_ROWS * GL_ROW + 8, 0);
    if (v->n <= 0) {
        txt(fb, pal, MARGIN + 20, GL_TOP + 8, pal->warn, "NO GAMES FOUND");
        txt(fb, pal, MARGIN + 20, GL_TOP + 8 + LINE, pal->dim, "Copy .gb files to ROMS/GB");
    }
    for (i = 0; i < GL_ROWS && first + i < v->n; i++) {
        int idx = first + i;
        int sel = idx == v->selected;
        const char *nm = v->names[idx] ? v->names[idx] : "?";
        size_t k;

        y = GL_TOP + i * GL_ROW;
        if (sel) {
            hs_fb_fill_round(fb, MARGIN + 6, y, fb->w - 2 * MARGIN - 12, GL_ROW - 4, 6,
                             pal->tile_sel);
            hs_fb_rect(fb, MARGIN + 6, y, fb->w - 2 * MARGIN - 12, GL_ROW - 4, 2,
                       pal->border_sel);
            hs_fb_fill(fb, MARGIN + 10, y + 6, 5, GL_ROW - 16, pal->accent);
        }
        /* 34 columns fit between the margins at scale 2. */
        for (k = 0; k < 34 && nm[k] != '\0'; k++) {
            line[k] = nm[k];
        }
        line[k] = '\0';
        if (nm[k] != '\0' && k >= 2) {
            line[k - 1] = '.';
            line[k - 2] = '.';
        }
        txt(fb, pal, MARGIN + 24, y + 8, sel ? pal->text : pal->dim, line);
    }
    if (v->n > GL_ROWS) {
        /* scroll thumb */
        int track = GL_ROWS * GL_ROW;
        int th = track * GL_ROWS / v->n;
        int ty = GL_TOP + (track - th) * first / (v->n - GL_ROWS);
        hs_fb_fill(fb, fb->w - MARGIN - 8, ty, 4, th < 12 ? 12 : th, pal->accent);
    }

    /* status line */
    y = GL_TOP + GL_ROWS * GL_ROW + 10;
    if (v->message != NULL) {
        txt(fb, pal, MARGIN, y, pal->warn, v->message);
    } else {
        int x = MARGIN;
        x += txt(fb, pal, x, y, pal->value, v->sd_status ? v->sd_status : "SD: ?");
        x += txt(fb, pal, x, y, pal->dim, "  ");
        (void)txt(fb, pal, x, y, pal->accent2, pal->name);
    }

    hs_fb_fill(fb, 0, fb->h - FOOT_H, fb->w, FOOT_H, pal->bar);
    hs_fb_fill(fb, 0, fb->h - FOOT_H, fb->w, 2, pal->border);
    {
        int x = MARGIN;
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "A");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Play  ");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "B");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Back  ");
        x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "L1R1");
        (void)txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Theme");
    }
    (void)v->audio_status;
}

/* ---- v4-multi: 2x bold multi-system GAMES picker ---- */
#define GM_TAG_W  (6 * CH + 8)   /* "[SNES]" + gap */

void hs_ui_draw_games_multi(struct hs_fb *fb, hs_theme_id_t theme,
                            const struct hs_games_multi_view *v)
{
    const struct hs_palette *pal;
    int first = 0, i, y, x;
    char line[64];

    if (fb == NULL || v == NULL) {
        return;
    }
    pal = hs_theme_get(theme);
    hs_fb_clear(fb, pal->bg);

    /* header: brand, GAMES, battery + audio chips */
    hs_fb_fill(fb, 0, 0, fb->w, BAR_H, pal->bar);
    hs_fb_fill(fb, 0, BAR_H - 3, fb->w, 3, pal->accent);
    txt(fb, pal, MARGIN, 14, pal->accent2, "HandshakeOS");
    txt(fb, pal, 208, 14, pal->text, "GAMES");
    {
        const char *bs = v->battery_status ? v->battery_status : "BAT --";
        const char *as = v->audio_status ? v->audio_status : "SYNTH";
        int aw = text_w(as) + 16, bw = text_w(bs) + 16;
        int ax = fb->w - MARGIN - aw;
        chip(fb, pal, ax, 9, as, pal->value);
        chip(fb, pal, ax - 8 - bw, 9, bs, pal->value);
    }

    if (v->selected >= GL_ROWS) {
        first = v->selected - GL_ROWS + 1;
    }
    card(fb, pal, MARGIN, GL_TOP - 4, fb->w - 2 * MARGIN, GL_ROWS * GL_ROW + 8, 0);
    if (v->n <= 0) {
        txt(fb, pal, MARGIN + 20, GL_TOP + 8, pal->warn, "NO GAMES FOUND");
        txt(fb, pal, MARGIN + 20, GL_TOP + 8 + LINE, pal->dim, "Copy games to ROMS/<SYSTEM>");
    }
    for (i = 0; i < GL_ROWS && first + i < v->n; i++) {
        int idx = first + i;
        int sel = idx == v->selected;
        const char *nm = v->names[idx] ? v->names[idx] : "?";
        const char *tg = (v->tags && v->tags[idx]) ? v->tags[idx] : "?";
        uint32_t tc = v->tag_rgb ? v->tag_rgb[idx] : pal->accent;
        int live = v->linked ? v->linked[idx] : 1;
        int maxc = (fb->w - 2 * MARGIN - 24 - GM_TAG_W - 20) / CH;
        size_t k;

        y = GL_TOP + i * GL_ROW;
        if (sel) {
            hs_fb_fill_round(fb, MARGIN + 6, y, fb->w - 2 * MARGIN - 12, GL_ROW - 4, 6,
                             pal->tile_sel);
            hs_fb_rect(fb, MARGIN + 6, y, fb->w - 2 * MARGIN - 12, GL_ROW - 4, 2,
                       pal->border_sel);
            hs_fb_fill(fb, MARGIN + 10, y + 6, 5, GL_ROW - 16, tc);
        }
        /* colour-coded system tag, bold (2x + 1 px double strike) */
        snprintf(line, sizeof(line), "[%s]", tg);
        x = MARGIN + 24;
        (void)hs_fb_text_ex(fb, x + 1, y + 8, S2, live ? tc : pal->dim, pal->text_shadow,
                            HS_TEXT_NICE, line);
        (void)hs_fb_text_ex(fb, x, y + 8, S2, live ? tc : pal->dim, pal->text_shadow,
                            HS_TEXT_NICE, line);
        for (k = 0; k < (size_t)maxc && k + 1 < sizeof(line) && nm[k] != '\0'; k++) {
            line[k] = nm[k];
        }
        line[k] = '\0';
        if (nm[k] != '\0' && k >= 2) {
            line[k - 1] = '.';
            line[k - 2] = '.';
        }
        txt(fb, pal, x + GM_TAG_W, y + 8, sel ? pal->text : (live ? pal->dim : pal->border), line);
    }
    if (v->n > GL_ROWS) {
        int track = GL_ROWS * GL_ROW;
        int th = track * GL_ROWS / v->n;
        int ty = GL_TOP + (track - th) * first / (v->n - GL_ROWS);
        hs_fb_fill(fb, fb->w - MARGIN - 8, ty, 4, th < 12 ? 12 : th, pal->accent);
    }

    /* status line */
    y = GL_TOP + GL_ROWS * GL_ROW + 10;
    if (v->message != NULL) {
        txt(fb, pal, MARGIN, y, pal->warn, v->message);
    } else {
        x = MARGIN;
        x += txt(fb, pal, x, y, pal->value, v->sd_status ? v->sd_status : "SD: ?");
        x += txt(fb, pal, x, y, pal->dim, "  ");
        (void)txt(fb, pal, x, y, pal->accent2, v->systems_status ? v->systems_status : "");
    }

    hs_fb_fill(fb, 0, fb->h - FOOT_H, fb->w, FOOT_H, pal->bar);
    hs_fb_fill(fb, 0, fb->h - FOOT_H, fb->w, 2, pal->border);
    x = MARGIN;
    x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "A");
    x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Play ");
    x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "B");
    x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Back ");
    x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "X");
    x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Scan ");
    x += txt(fb, pal, x, fb->h - FOOT_H + 12, pal->accent, "L1R1");
    (void)txt(fb, pal, x, fb->h - FOOT_H + 12, pal->text, " Theme");

    if (v->notice != NULL) {
        int pw = text_w(v->notice) + 64, ph = v->notice_sub ? 112 : 80;
        int px, py = (fb->h - ph) / 2;
        if (pw < 400) {
            pw = 400;
        }
        if (pw > fb->w - 2 * MARGIN) {
            pw = fb->w - 2 * MARGIN;
        }
        px = (fb->w - pw) / 2;
        card(fb, pal, px, py, pw, ph, 1);
        x = (fb->w - text_w(v->notice)) / 2;
        (void)hs_fb_text_ex(fb, x + 1, py + 24, S2, pal->warn, pal->text_shadow, HS_TEXT_NICE,
                            v->notice);
        (void)hs_fb_text_ex(fb, x, py + 24, S2, pal->warn, pal->text_shadow, HS_TEXT_NICE,
                            v->notice);
        if (v->notice_sub != NULL) {
            x = (fb->w - text_w(v->notice_sub)) / 2;
            txt(fb, pal, x, py + 24 + LINE + 12, pal->dim, v->notice_sub);
        }
    }
}
