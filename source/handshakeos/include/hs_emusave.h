/* SPDX-License-Identifier: MIT */
#ifndef HS_EMUSAVE_H
#define HS_EMUSAVE_H

struct emu;

/* Bytes loaded (0 = no file / no battery), negative errno on error. */
long hs_emu_load_sav(struct emu *e, const char *sav_path);
/* 0 on success (or nothing to save). Atomic: .sav.tmp + fsync + rename. */
int  hs_emu_save_sav(struct emu *e, const char *sav_path);

#endif
