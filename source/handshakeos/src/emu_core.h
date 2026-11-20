/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-full: internal per-core adapter (emu_cores.c), used only
   by the registry (emu_sys.c). Kept apart because hs_core.h and
   hs_handshake.h both define hs_fnv1a64 with different signatures. */
#ifndef HS_EMU_CORE_H
#define HS_EMU_CORE_H

#include <stddef.h>
#include <stdint.h>
#include "emu_sys.h"

struct emu_core;

/* rom is borrowed and must outlive the core. NULL + *err on failure. */
struct emu_core *emu_core_create(emu_sys_t sys, const uint8_t *rom, size_t len, int *err);
void     emu_core_destroy(struct emu_core *c);
/* Phase A (Core 0). */
void     emu_core_step(struct emu_core *c, uint32_t pad, int64_t rtc);
/* Barrier (clock thread, all phases quiescent). */
void     emu_core_commit(struct emu_core *c);
/* Phase B (Core 1): full 640x480, stride 640. aa: emu_aa_t (3D cores). */
void     emu_core_render(struct emu_core *c, uint32_t *fb, int nearest, int aa);
/* Phase B (Core 2): 48 kHz stereo int16, returns frames. */
uint32_t emu_core_synth(struct emu_core *c, int16_t *pcm, uint32_t max_frames);
void     emu_core_set_palette(struct emu_core *c, unsigned pal);
size_t   emu_core_sram_size(struct emu_core *c);
size_t   emu_core_sram_get(struct emu_core *c, uint8_t *buf, size_t cap);
int      emu_core_sram_put(struct emu_core *c, const uint8_t *buf, size_t len);
uint32_t emu_core_max_audio(void); /* per-frame upper bound, stereo frames */
size_t   emu_core_live_bytes(void);  /* state bytes held by live cores */

#endif
