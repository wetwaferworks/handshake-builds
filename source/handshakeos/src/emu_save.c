/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-multi: atomic battery saves for every registry core.
   Shell thread only, strictly between emu_run_frames() calls (every core
   is quiescent at a phase boundary). Writes go through
   <rom>.sav.tmp -> fsync -> rename -> dir fsync (hs_games_write_file). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_emusave.h"
#include "hs_games.h"
#include "emu_sys.h"

#include <errno.h>
#include <stdlib.h>

long hs_emu_load_sav(struct emu *e, const char *sav_path)
{
    size_t n = emu_sram_size(e);
    uint8_t *b;
    long r;

    if (e == NULL || sav_path == NULL || n == 0) {
        return 0;
    }
    b = (uint8_t *)malloc(n);
    if (b == NULL) {
        return -ENOMEM;
    }
    r = hs_games_read_file(sav_path, b, n);
    if (r > 0 && emu_sram_put(e, b, (size_t)r) != 0) {
        r = -EIO;
    }
    free(b);
    return r;
}

int hs_emu_save_sav(struct emu *e, const char *sav_path)
{
    size_t n = emu_sram_size(e), got;
    uint8_t *b;
    int rc;

    if (e == NULL || sav_path == NULL || n == 0) {
        return 0;
    }
    b = (uint8_t *)malloc(n);
    if (b == NULL) {
        return -ENOMEM;
    }
    got = emu_sram_get(e, b, n);
    rc = got == n ? hs_games_write_file(sav_path, b, n) : -EIO;
    free(b);
    if (rc == 0) {
        emu_sram_mark_clean(e);
    }
    return rc;
}
