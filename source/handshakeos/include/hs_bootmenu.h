/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * Boot configuration menu: splash, animation, theme, A/B slot, last
 * HANDSHAKE-BOOT.log line. D-pad / A / B. Persist via hs_cfg. Never powers off. */
#ifndef HS_BOOTMENU_H
#define HS_BOOTMENU_H

#include "hs_cfg.h"
#include "hs_fb.h"
#include "hs_input.h"
#include "hs_splash.h"

enum {
    HS_BOOTMENU_CANCEL = 0,
    HS_BOOTMENU_SAVED  = 1,
    HS_BOOTMENU_ERR    = -1
};

int hs_bootmenu_run(struct hs_fb *fb, struct hs_input *in,
                    struct hs_cfg *cfg, const struct hs_splash *sp,
                    const char *cfg_path, const char *log_path);

void hs_bootmenu_draw(struct hs_fb *fb, const struct hs_cfg *cfg,
                      const struct hs_splash *sp, int cursor,
                      const char *last_log);

#endif
