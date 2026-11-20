/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * Load kernel modules with finit_module(2) (falls back to init_module(2)).
 * Used by v5.4 USB TRANSFER: libcomposite / usb_f_mass_storage and deps are
 * shipped in the initramfs and loaded only when START is pressed in GAMES.
 * Shell thread only. Zero locks. A load failure is reported, never a power-off. */
#ifndef HS_MOD_H
#define HS_MOD_H
#include <stddef.h>

#ifndef HS_MOD_DIR
#define HS_MOD_DIR "/modules"
#endif

#define HS_MOD_MAX        16
#define HS_MOD_NAME_MAX   64
#define HS_MOD_MSG_MAX    192

struct hs_mod_one {
    char name[HS_MOD_NAME_MAX];  /* module filename, e.g. libcomposite.ko */
    int  loaded;                 /* 1 = this call loaded it (or it was already live) */
    int  already;                /* 1 = already present (EEXIST) */
    int  err;                    /* errno on failure, 0 on success */
};

struct hs_mod_report {
    int  n;
    int  failed;
    char summary[HS_MOD_MSG_MAX];
    struct hs_mod_one one[HS_MOD_MAX];
};

/* Load one .ko from `dir`/`name` (name may be a bare filename). 0 / -1. */
int hs_mod_finit(const char *path, const char *params, struct hs_mod_one *out);

/* Load the USB gadget stack in dependency order from `dir` (default HS_MOD_DIR).
 * Missing dir / files: failed set, summary filled, return -1. Partial loads stay. */
int hs_mod_load_usb(const char *dir, struct hs_mod_report *rep);

/* Load a single extra module (QEMU test-only dummy_hcd, never shipped). */
int hs_mod_load_one(const char *dir, const char *name, struct hs_mod_report *rep);

#endif
