/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_bootmenu.h"
#include "hs_bootlog.h"
#include "hs_theme.h"

#include <stdio.h>
#include <string.h>

enum {
    IT_SPLASH = 0,
    IT_ANIM,
    IT_THEME,
    IT_SLOT,
    IT_LOG,
    IT_SAVE,
    IT_CANCEL,
    IT_COUNT
};

static const char *item_name(int i)
{
    switch (i) {
    case IT_SPLASH: return "SPLASH";
    case IT_ANIM:   return "ANIMATION";
    case IT_THEME:  return "THEME";
    case IT_SLOT:   return "OS SLOT";
    case IT_LOG:    return "LAST LOG";
    case IT_SAVE:   return "SAVE AND EXIT";
    case IT_CANCEL: return "EXIT WITHOUT SAVE";
    default:        return "";
    }
}

static void cycle_splash(struct hs_cfg *cfg, const struct hs_splash *sp, int dir)
{
    int i, n, cur;
    if (sp == NULL || sp->n <= 0) return;
    n = sp->n;
    cur = hs_splash_find(sp, cfg->splash_id);
    if (cur < 0) cur = 0;
    i = cur + dir;
    while (i < 0) i += n;
    i %= n;
    snprintf(cfg->splash_id, sizeof(cfg->splash_id), "%s", sp->ent[i].id);
}

static void value_text(char *out, size_t n, int item, const struct hs_cfg *cfg,
                       const struct hs_splash *sp, const char *last_log)
{
    const struct hs_splash_ent *e;
    int i;
    out[0] = '\0';
    if (cfg == NULL) return;
    switch (item) {
    case IT_SPLASH:
        i = sp ? hs_splash_find(sp, cfg->splash_id) : -1;
        e = (i >= 0) ? hs_splash_at(sp, i) : NULL;
        if (e) snprintf(out, n, "%s  %s  (%s)", e->id, e->title, e->artist);
        else snprintf(out, n, "%s", cfg->splash_id[0] ? cfg->splash_id : "01");
        break;
    case IT_ANIM:
        snprintf(out, n, "%s", cfg->animation ? "ON" : "OFF");
        break;
    case IT_THEME:
        snprintf(out, n, "%s", cfg->theme == HS_THEME_GREEN ? "Matrix Green" : "Deep Purple");
        break;
    case IT_SLOT:
        snprintf(out, n, "active %s   fallback %s",
                 hs_slot_name(cfg->slot), hs_slot_name(cfg->fallback));
        break;
    case IT_LOG:
        snprintf(out, n, "%s", (last_log && last_log[0]) ? last_log : "(none)");
        break;
    default:
        break;
    }
}

void hs_bootmenu_draw(struct hs_fb *fb, const struct hs_cfg *cfg,
                      const struct hs_splash *sp, int cursor,
                      const char *last_log)
{
    const struct hs_palette *pal = hs_theme_get(cfg ? cfg->theme : HS_THEME_PURPLE);
    int i, y;
    char val[160];
    uint32_t bg, bd;

    if (fb == NULL || fb->pix == NULL || pal == NULL) return;
    hs_fb_clear(fb, pal->bg);
    hs_fb_fill(fb, 0, 0, fb->w, 48, pal->bar);
    hs_fb_text_ex(fb, 24, 12, 2, pal->text, pal->text_shadow, HS_TEXT_NICE, "BOOT CONFIG");
    hs_fb_text(fb, 360, 18, 1, pal->dim, "HandshakeOS");

    for (i = 0; i < IT_COUNT; i++) {
        y = 64 + i * 48;
        bg = (i == cursor) ? pal->tile_sel : pal->tile;
        bd = (i == cursor) ? pal->border_sel : pal->border;
        hs_fb_fill_round(fb, 24, y, fb->w - 48, 42, 6, bg);
        hs_fb_rect(fb, 24, y, fb->w - 48, 42, 2, bd);
        hs_fb_text_ex(fb, 40, y + 4, 1, pal->accent2, pal->text_shadow, HS_TEXT_NICE, item_name(i));
        value_text(val, sizeof(val), i, cfg, sp, last_log);
        if (val[0]) {
            if (strlen(val) > 52) val[52] = '\0';
            hs_fb_text(fb, 40, y + 22, 1, pal->text, val);
        }
    }
    hs_fb_text(fb, 24, fb->h - 28, 1, pal->dim, "D-PAD MOVE   LEFT/RIGHT CHANGE   A SAVE   B BACK");
}

int hs_bootmenu_run(struct hs_fb *fb, struct hs_input *in,
                    struct hs_cfg *cfg, const struct hs_splash *sp,
                    const char *cfg_path, const char *log_path)
{
    struct hs_cfg edit;
    int cursor = 0, running = 1, rc = HS_BOOTMENU_CANCEL;
    char last[200];

    if (fb == NULL || cfg == NULL) return HS_BOOTMENU_ERR;
    edit = *cfg;
    last[0] = '\0';
    if (log_path) (void)hs_bootlog_last(log_path, last, sizeof(last));

    if (in == NULL) {
        hs_bootmenu_draw(fb, &edit, sp, cursor, last);
        hs_fb_flip(fb);
        return HS_BOOTMENU_CANCEL;
    }

    while (running) {
        hs_bootmenu_draw(fb, &edit, sp, cursor, last);
        hs_fb_flip(fb);
        (void)hs_input_poll(in, 50);
        if (hs_input_pressed(in, HS_BTN_UP))
            cursor = (cursor + IT_COUNT - 1) % IT_COUNT;
        if (hs_input_pressed(in, HS_BTN_DOWN))
            cursor = (cursor + 1) % IT_COUNT;
        if (hs_input_pressed(in, HS_BTN_LEFT) || hs_input_pressed(in, HS_BTN_RIGHT)) {
            int dir = hs_input_pressed(in, HS_BTN_RIGHT) ? 1 : -1;
            if (cursor == IT_SPLASH) cycle_splash(&edit, sp, dir);
            else if (cursor == IT_ANIM) edit.animation = edit.animation ? 0 : 1;
            else if (cursor == IT_THEME)
                edit.theme = (edit.theme == HS_THEME_PURPLE) ? HS_THEME_GREEN : HS_THEME_PURPLE;
            else if (cursor == IT_SLOT)
                edit.slot = hs_slot_other(edit.slot);
        }
        if (hs_input_pressed(in, HS_BTN_A)) {
            if (cursor == IT_SAVE) {
                if (edit.slot != cfg->slot)
                    edit.fallback = cfg->slot;
                if (cfg_path && hs_cfg_save(&edit, cfg_path) == 0) {
                    *cfg = edit;
                    rc = HS_BOOTMENU_SAVED;
                } else {
                    rc = HS_BOOTMENU_ERR;
                }
                running = 0;
            } else if (cursor == IT_CANCEL) {
                rc = HS_BOOTMENU_CANCEL;
                running = 0;
            } else if (cursor == IT_ANIM) {
                edit.animation = edit.animation ? 0 : 1;
            } else if (cursor == IT_THEME) {
                edit.theme = (edit.theme == HS_THEME_PURPLE) ? HS_THEME_GREEN : HS_THEME_PURPLE;
            } else if (cursor == IT_SPLASH) {
                cycle_splash(&edit, sp, 1);
            } else if (cursor == IT_SLOT) {
                edit.slot = hs_slot_other(edit.slot);
            }
        }
        if (hs_input_pressed(in, HS_BTN_B) || hs_input_pressed(in, HS_BTN_MENU)) {
            rc = HS_BOOTMENU_CANCEL;
            running = 0;
        }
    }
    return rc;
}
