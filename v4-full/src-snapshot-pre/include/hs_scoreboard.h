/* SPDX-License-Identifier: MIT */
#ifndef HS_SCOREBOARD_H
#define HS_SCOREBOARD_H

#include "hs_handshake.h"

#define HS_SCORE_PHASES 128

int      hs_scoreboard_setup(struct hs_hs *hs, int workers);
uint64_t hs_scoreboard_run(struct hs_hs *hs);

#endif
