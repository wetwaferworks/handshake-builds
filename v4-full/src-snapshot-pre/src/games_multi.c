/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-multi: 7-folder microSD ROM scanner + built-in demos.
   Shell thread only. The v3 GB-only hs_games_scan() is left untouched. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_games.h"
#include "emu_sys.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIN_ROM_BYTES 512

static int cmp_multi(const void *a, const void *b)
{
    const struct hs_game *x = (const struct hs_game *)a;
    const struct hs_game *y = (const struct hs_game *)b;
    int c = strcasecmp(x->name, y->name);

    if (c != 0) {
        return c;
    }
    if (x->sys != y->sys) {
        return x->sys < y->sys ? -1 : 1;
    }
    return strcmp(x->path, y->path);
}

static void scan_dir_multi(struct hs_gamelist *list, const char *dir, const char *folder)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL && list->n < HS_GAMES_MAX) {
        struct hs_game *g;
        struct stat st;
        const char *dot;
        size_t n;
        emu_sys_t sys;

        if (e->d_name[0] == '.') {
            continue; /* hidden files and macOS "._x" resource forks */
        }
        sys = emu_sys_from_path(e->d_name, folder);
        if (sys == EMU_SYS_NONE) {
            continue;
        }
        g = &list->g[list->n];
        if (snprintf(g->path, sizeof(g->path), "%s/%s", dir, e->d_name) >= (int)sizeof(g->path)) {
            continue;
        }
        if (stat(g->path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < MIN_ROM_BYTES) {
            continue;
        }
        dot = strrchr(e->d_name, '.');
        n = dot != NULL ? (size_t)(dot - e->d_name) : strlen(e->d_name);
        if (n >= sizeof(g->name)) {
            n = sizeof(g->name) - 1;
        }
        memcpy(g->name, e->d_name, n);
        g->name[n] = '\0';
        g->builtin = 0;
        g->sys = (int)sys;
        list->n++;
    }
    closedir(d);
}

static const struct {
    const char *file;
    const char *name;
    emu_sys_t   sys;
} k_builtin[] = {
    { "handshake-demo.gb",  "Handshake Demo (built-in)",     EMU_SYS_GB },
    { "hs_cgb_test.gbc",    "Handshake CGB Demo (built-in)", EMU_SYS_GBC },
    { "hs_gba_demo.gba",    "Mode 0 + DirectSound (built-in)", EMU_SYS_GBA },
    { "hs_mode1_demo.sfc",  "Mode 1 Demo (built-in)",        EMU_SYS_SNES },
    { "hs_mode7_floor.sfc", "Mode 7 Floor (built-in)",       EMU_SYS_SNES },
    { "hs_genesis_demo.md", "VDP + FM Demo (built-in)",      EMU_SYS_GENESIS },
};

int hs_games_scan_multi(struct hs_gamelist *list, const char *root, int sd_mounted,
                        const char *builtin_dir)
{
    char dir[HS_GAMES_PATH_MAX];
    size_t i;
    int s;

    if (list == NULL) {
        return -EINVAL;
    }
    memset(list, 0, sizeof(*list));
    list->sd_mounted = sd_mounted;
    if (root != NULL && sd_mounted) {
        for (s = 0; s < EMU_SYS_COUNT; s++) {
            const struct emu_sys_info *in = emu_sys_info((emu_sys_t)s);
            snprintf(dir, sizeof(dir), "%s/ROMS/%s", root, in->folder);
            scan_dir_multi(list, dir, in->folder);
        }
        snprintf(dir, sizeof(dir), "%s/ROMS", root);
        scan_dir_multi(list, dir, NULL);
        qsort(list->g, (size_t)list->n, sizeof(list->g[0]), cmp_multi);
    }
    list->n_sd = list->n;
    if (builtin_dir != NULL) {
        for (i = 0; i < sizeof(k_builtin) / sizeof(k_builtin[0]) &&
                    list->n < HS_GAMES_MAX + HS_GAMES_BUILTIN_MAX; i++) {
            struct hs_game *g = &list->g[list->n];
            if (snprintf(g->path, sizeof(g->path), "%s/%s", builtin_dir, k_builtin[i].file) >=
                    (int)sizeof(g->path) ||
                access(g->path, R_OK) != 0) {
                continue;
            }
            snprintf(g->name, sizeof(g->name), "%s", k_builtin[i].name);
            g->builtin = 1;
            g->sys = (int)k_builtin[i].sys;
            list->n++;
        }
    }
    return list->n;
}
