/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hs_cfg.h - HandshakeOS boot configuration on the FAT partition (HANDSHAKE.CFG).
 * Written atomically (tmp + fsync + rename). A missing or corrupt file falls back
 * to defaults, is logged, and never powers the device off. */
#ifndef HS_CFG_H
#define HS_CFG_H
#include "hs_theme.h"
#include <stddef.h>

#ifndef HS_CFG_NAME
#define HS_CFG_NAME "HANDSHAKE.CFG"
#endif

#define HS_CFG_SPLASH_MAX 32

typedef enum {
    HS_SLOT_A = 0,
    HS_SLOT_B = 1
} hs_slot_t;

struct hs_cfg {
    int            version;                 /* format version, 1 */
    char           splash_id[HS_CFG_SPLASH_MAX]; /* "01".."08" or "ascii-oo" */
    int            animation;               /* 1 = on, 0 = off */
    hs_theme_id_t  theme;
    hs_slot_t      slot;                    /* selected / confirmed active slot */
    hs_slot_t      fallback;                /* previous confirmed slot */
    int            loaded;                  /* 1 if a valid file was read */
    int            fallback_used;           /* 1 if defaults were used (missing/corrupt) */
    char           err[96];
};

void hs_cfg_defaults(struct hs_cfg *c);
const char *hs_cfg_path(const char *mountpoint);
/* 0 always; sets fallback_used + err on missing/corrupt and fills defaults. */
int  hs_cfg_load(struct hs_cfg *c, const char *path);
/* Atomic write. 0 / -1 (err set). Never powers off. */
int  hs_cfg_save(const struct hs_cfg *c, const char *path);
const char *hs_slot_name(hs_slot_t s);
hs_slot_t   hs_slot_from_name(const char *s);
hs_slot_t   hs_slot_other(hs_slot_t s);

#endif
