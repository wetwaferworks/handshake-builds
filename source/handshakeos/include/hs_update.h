/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * A/B over-the-cable updater. A .hsu package is SHA-256 verified, then written
 * to the inactive slot. On any fault the previous slot is left intact (fallback).
 * Never powers off. */
#ifndef HS_UPDATE_H
#define HS_UPDATE_H

#include "hs_cfg.h"
#include <stddef.h>

#define HS_HSU_MAGIC "HSU1"
#define HS_UPDATE_MSG_MAX 192
#define HS_UPDATE_MAX_FILES 8

struct hs_update_file {
    char     name[64];
    uint32_t size;
    char     sha256_hex[65];
    int      verified;
};

struct hs_update_report {
    int  ok;                 /* 1 = applied to dest slot */
    int  verified;           /* 1 = package and file hashes matched */
    hs_slot_t dest;
    int  nfiles;
    char msg[HS_UPDATE_MSG_MAX];
    struct hs_update_file file[HS_UPDATE_MAX_FILES];
};

/* Build a .hsu at `out` from `n` (path, dest-name) pairs. 0 / -1. */
int hs_hsu_create(const char *out,
                  const char *const *src_paths,
                  const char *const *dest_names,
                  int n);

/* Verify a .hsu (hashes only). 0 / -1. */
int hs_hsu_verify(const char *path, struct hs_update_report *rep);

/* Apply `hsu` onto the FAT at `mount`, targeting the inactive slot in `cfg`.
 * On success cfg->fallback is the previous slot and cfg is saved. 0 / -1. */
int hs_update_apply(const char *hsu, const char *mount, struct hs_cfg *cfg,
                    struct hs_update_report *rep);

/* First regular file matching *.hsu in `dir` (not recursive). 0 if found. */
int hs_update_find(const char *dir, char *out, size_t n);

#endif
