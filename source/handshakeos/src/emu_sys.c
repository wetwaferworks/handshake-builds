/* SPDX-License-Identifier: MIT */
/* HandshakeOS v4-full: 7-system registry (all seven live) + zero-mutex Timing Handshake
   phase loop (see emu_sys.h). No mutex, no condvar, no lock: the only
   cross-thread state is the handshake's own publish/barrier words and a
   single-producer / single-consumer PCM ring (acquire/release indices). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "emu_sys.h"
#include "emu_core.h"
#include "hs_handshake.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FNV_OFF 0xcbf29ce484222325ull
#define EMU_PPC 4
#define EMU_GROUP_CPU 1u
#define EMU_GROUP_AV  2u

static const struct emu_sys_info k_sys[EMU_SYS_COUNT] = {
    { EMU_SYS_GB,      "GB",   "Game Boy",         "GB",      ".gb",
      "hs-gbc",     1, 0, 0x9BBC0Fu, 4194304u, 70224u },
    { EMU_SYS_GBC,     "GBC",  "Game Boy Color",   "GBC",     ".gbc .cgb",
      "hs-gbc",     1, 0, 0xFF8C1Au, 4194304u, 70224u },
    { EMU_SYS_GBA,     "GBA",  "Game Boy Advance", "GBA",     ".gba .agb",
      "hs-gba",     1, 0, 0x4F8DFFu, 16777216u, 280896u },
    { EMU_SYS_SNES,    "SNES", "Super Nintendo",   "SNES",    ".sfc .smc",
      "hs-snes",    1, 0, 0xB57CFFu, 21477272u, 357366u },
    { EMU_SYS_GENESIS, "GEN",  "Sega Genesis",     "GENESIS", ".md .gen .bin",
      "hs-genesis", 1, 0, 0xFF4D4Du, 53693175u, 896040u },
    /* PS1: PS-X EXE programs (.exe .psexe, or .bin inside ROMS/PSX). */
    { EMU_SYS_PSX,     "PS1",  "PlayStation",      "PSX",     ".exe .psexe .psx .bin",
      "hs-psx",     1, 1, 0x36D6F0u, 60u, 1u },
    { EMU_SYS_N64,     "N64",  "Nintendo 64",      "N64",     ".z64 .n64 .v64",
      "hs-n64",     1, 1, 0x3DDC84u, 60u, 1u },
};

const struct emu_sys_info *emu_sys_info(emu_sys_t sys)
{
    if ((int)sys < 0 || sys >= EMU_SYS_COUNT) {
        return NULL;
    }
    return &k_sys[sys];
}

int emu_sys_linked(emu_sys_t sys)
{
    const struct emu_sys_info *i = emu_sys_info(sys);
    return i != NULL && i->linked;
}

int emu_sys_count_linked(void)
{
    int n = 0, i;
    for (i = 0; i < EMU_SYS_COUNT; i++) {
        n += k_sys[i].linked != 0;
    }
    return n;
}

const char *emu_sys_unlinked_notice(void)
{
    return "Core not linked in this build";
}

const char *emu_aa_name(emu_aa_t aa)
{
    static const char *const k[EMU_AA_COUNT] = { "1X", "FXAA", "2X SSAA", "4X SSAA", "8X SSAA" };
    return ((int)aa >= 0 && aa < EMU_AA_COUNT) ? k[aa] : "1X";
}

emu_aa_t emu_aa_next(emu_aa_t aa)
{
    return ((int)aa < 0 || aa + 1 >= EMU_AA_COUNT) ? EMU_AA_1X : (emu_aa_t)(aa + 1);
}

int emu_sys_ext_match(emu_sys_t sys, const char *name)
{
    const struct emu_sys_info *in = emu_sys_info(sys);
    const char *dot, *p;
    size_t el;

    if (in == NULL || name == NULL) {
        return 0;
    }
    dot = strrchr(name, '.');
    if (dot == NULL || dot[1] == '\0') {
        return 0;
    }
    el = strlen(dot);
    p = in->exts;
    while (*p != '\0') {
        size_t k = 0;
        while (p[k] != '\0' && p[k] != ' ') {
            k++;
        }
        if (k == el && strncasecmp(p, dot, k) == 0) {
            return 1;
        }
        p += k;
        while (*p == ' ') {
            p++;
        }
    }
    return 0;
}

emu_sys_t emu_sys_from_folder(const char *folder)
{
    int i;
    if (folder == NULL) {
        return EMU_SYS_NONE;
    }
    if (strcasecmp(folder, "PS1") == 0) {
        return EMU_SYS_PSX;
    }
    for (i = 0; i < EMU_SYS_COUNT; i++) {
        if (strcasecmp(folder, k_sys[i].folder) == 0) {
            return (emu_sys_t)i;
        }
    }
    return EMU_SYS_NONE;
}

emu_sys_t emu_sys_from_path(const char *path, const char *folder_hint)
{
    emu_sys_t hint = emu_sys_from_folder(folder_hint);
    int i;

    if (path == NULL) {
        return EMU_SYS_NONE;
    }
    if (hint != EMU_SYS_NONE && emu_sys_ext_match(hint, path)) {
        return hint;
    }
    /* A Game Boy cart in ROMS/GBC runs on Game Boy Color hardware. */
    if (hint == EMU_SYS_GBC && emu_sys_ext_match(EMU_SYS_GB, path)) {
        return EMU_SYS_GBC;
    }
    for (i = 0; i < EMU_SYS_COUNT; i++) {
        if (emu_sys_ext_match((emu_sys_t)i, path)) {
            return (emu_sys_t)i;
        }
    }
    return EMU_SYS_NONE;
}

const char *emu_dmg_palette_name(unsigned pal)
{
    static const char *const k[5] = {
        "CLASSIC OLIVE", "POCKET B&W", "DEEP PURPLE", "MATRIX GREEN",
        "CGB BOOT PALETTE"
    };
    return k[pal < 5u ? pal : 4u];
}

struct emu {
    emu_sys_t        sys;
    struct emu_core *core;
    uint8_t         *rom;
    size_t           rom_len;
    struct hs_hs     hs;
    int              started;
    uint64_t         next_phase;

    /* PRE-side inputs: written by the shell thread before a phase is
       published, read by the phase kernels after they pull it. */
    uint32_t pad;
    int64_t  rtc;
    int      nearest;
    int      aa;          /* emu_aa_t, 3D cores */
    int      render_this;
    unsigned palette;

    /* Core 1 (video) private */
    uint64_t video_hash;
    uint32_t fb[EMU_FB_W * EMU_FB_H];

    /* Core 2 (audio) private */
    uint64_t audio_hash;
    uint64_t pcm_frames;
    uint64_t pcm_nonzero;
    uint64_t pcm_dropped;
    int16_t  pcm[2048 * 2];

    /* SPSC ring: Core 2 produces, shell thread consumes. */
    _Atomic uint32_t ring_head;
    _Atomic uint32_t ring_tail;
    int16_t  ring[EMU_PCM_RING * 2];

    uint64_t frames;
    uint64_t sram_hash;
};

static uint64_t work_cpu(uint64_t phase, uint64_t cycle, void *user)
{
    struct emu *e = (struct emu *)user;
    (void)cycle;
    emu_core_step(e->core, e->pad, e->rtc);
    return hs_fnv1a64(hs_fnv1a64(FNV_OFF, phase), e->pad);
}

static uint64_t fnv_words(uint64_t h, const uint32_t *p, size_t n)
{
    size_t i;
    for (i = 0; i + 1 < n; i += 2) {
        h = hs_fnv1a64(h, (uint64_t)p[i] | ((uint64_t)p[i + 1] << 32));
    }
    if (i < n) {
        h = hs_fnv1a64(h, p[i]);
    }
    return h;
}

static uint64_t work_video(uint64_t phase, uint64_t cycle, void *user)
{
    struct emu *e = (struct emu *)user;
    (void)phase;
    (void)cycle;
    if (!e->render_this) {
        return 0;
    }
    emu_core_render(e->core, e->fb, e->nearest, e->aa);
    e->video_hash = fnv_words(FNV_OFF, e->fb, (size_t)EMU_FB_W * EMU_FB_H);
    return e->video_hash;
}

static void ring_push(struct emu *e, const int16_t *pcm, uint32_t n)
{
    uint32_t head = atomic_load_explicit(&e->ring_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&e->ring_tail, memory_order_acquire);
    uint32_t space = EMU_PCM_RING - (head - tail);
    uint32_t i;

    if (n > space) {
        e->pcm_dropped += n - space;
        n = space;
    }
    for (i = 0; i < n; i++) {
        uint32_t s = (head + i) & (EMU_PCM_RING - 1u);
        e->ring[s * 2u] = pcm[i * 2u];
        e->ring[s * 2u + 1u] = pcm[i * 2u + 1u];
    }
    atomic_store_explicit(&e->ring_head, head + n, memory_order_release);
}

static uint64_t work_audio(uint64_t phase, uint64_t cycle, void *user)
{
    struct emu *e = (struct emu *)user;
    uint32_t n, i;
    uint64_t h = FNV_OFF;

    (void)phase;
    (void)cycle;
    n = emu_core_synth(e->core, e->pcm, 2048u);
    for (i = 0; i < n * 2u; i++) {
        e->pcm_nonzero += e->pcm[i] != 0;
    }
    for (i = 0; i < n; i++) {
        uint32_t w = (uint32_t)(uint16_t)e->pcm[i * 2u] | ((uint32_t)(uint16_t)e->pcm[i * 2u + 1u] << 16);
        h = hs_fnv1a64(h, w);
        e->audio_hash = hs_fnv1a64(e->audio_hash, w);
    }
    e->pcm_frames += n;
    ring_push(e, e->pcm, n);
    return hs_fnv1a64(h, n);
}

static int reg(struct emu *e, const char *name, uint32_t group, hs_hs_work_fn fn)
{
    struct hs_hs_comp_desc d;
    memset(&d, 0, sizeof(d));
    d.name = name;
    d.cadence = HS_CADENCE_EVERY_PHASE;
    d.tick_critical = true;
    d.group = group;
    return hs_hs_register(&e->hs, &d, fn, e) == UINT32_MAX ? -1 : 0;
}

struct emu *emu_create(emu_sys_t sys, const uint8_t *rom, size_t len, int workers, int *err)
{
    struct emu *e;
    struct hs_hs_config cfg;
    int rc = 0;

    if (err != NULL) {
        *err = 0;
    }
    if (!emu_sys_linked(sys)) {
        if (err != NULL) {
            *err = emu_sys_info(sys) != NULL ? -ENOSYS : -EINVAL;
        }
        return NULL;
    }
    if (rom == NULL || len == 0) {
        if (err != NULL) {
            *err = -EINVAL;
        }
        return NULL;
    }
    e = (struct emu *)calloc(1, sizeof(*e));
    if (e == NULL) {
        if (err != NULL) {
            *err = -ENOMEM;
        }
        return NULL;
    }
    e->sys = sys;
    atomic_init(&e->ring_head, 0u);
    atomic_init(&e->ring_tail, 0u);
    e->audio_hash = FNV_OFF;
    e->render_this = 1;
    e->rom = (uint8_t *)malloc(len + 4u);
    if (e->rom == NULL) {
        free(e);
        if (err != NULL) {
            *err = -ENOMEM;
        }
        return NULL;
    }
    memcpy(e->rom, rom, len);
    memset(e->rom + len, 0, 4u);
    e->rom_len = len;
    e->core = emu_core_create(sys, e->rom, len, &rc);
    if (e->core == NULL) {
        emu_destroy(e);
        if (err != NULL) {
            *err = rc ? rc : -EIO;
        }
        return NULL;
    }

    hs_hs_default_config(&cfg);
    cfg.worker_threads = workers > 0 ? workers : 3;
    cfg.phases_per_cycle = EMU_PPC;
    /* Determinism, not wall-clock pacing, is judged here; give
       qemu-aarch64-static room so WAIT / FREEZE never trips. */
    cfg.phase_budget_ns = 2000000000ull;
    cfg.watchdog_ceiling_ns = 5000000000ull;
    if (emu_sys_info(sys)->aa) {
        /* v4-full: a pure-software 8X SSAA frame (5120x3840 virtual) can
           take several seconds on the H700 (and far longer under TCG), so
           the 3D cores get a 120 s freeze ceiling instead of 5 s. */
        cfg.watchdog_ceiling_ns = 120000000000ull;
    }
    cfg.ledger = true;
    if (hs_hs_init(&e->hs, &cfg) != 0 ||
        reg(e, "emu-cpu", EMU_GROUP_CPU, work_cpu) != 0 ||     /* shard 0: Core 0 */
        reg(e, "emu-video", EMU_GROUP_AV, work_video) != 0 ||  /* shard 1: Core 1 */
        reg(e, "emu-audio", EMU_GROUP_AV, work_audio) != 0 ||  /* shard 2: Core 2 */
        hs_hs_start(&e->hs) != 0) {
        emu_destroy(e);
        if (err != NULL) {
            *err = -EIO;
        }
        return NULL;
    }
    e->started = 1;
    emu_sram_mark_clean(e);
    return e;
}

void emu_destroy(struct emu *e)
{
    if (e == NULL) {
        return;
    }
    if (e->started) {
        hs_hs_stop(&e->hs);
        e->started = 0;
    }
    emu_core_destroy(e->core);
    free(e->rom);
    free(e);
}

emu_sys_t emu_system(const struct emu *e)
{
    return e != NULL ? e->sys : EMU_SYS_NONE;
}

int emu_run_frames(struct emu *e, uint32_t n, uint32_t pad, int render_last_only)
{
    uint32_t i;

    if (e == NULL || !e->started) {
        return -EINVAL;
    }
    for (i = 0; i < n; i++) {
        uint64_t ph = e->next_phase;
        /* PRE: inputs for this frame, before the phase is published. */
        e->pad = pad;
        e->render_this = !render_last_only || i + 1u == n;
        /* phase A: Core 0 CPU/bus -> BACK */
        if (!hs_hs_run_phase(&e->hs, ph, ph / EMU_PPC, EMU_GROUP_CPU)) {
            return -EIO;
        }
        /* barrier passed, every core quiescent: BACK -> FRONT */
        emu_core_commit(e->core);
        /* phase B: Core 1 video + Core 2 audio over FRONT */
        if (!hs_hs_run_phase(&e->hs, ph + 1u, (ph + 1u) / EMU_PPC, EMU_GROUP_AV)) {
            return -EIO;
        }
        e->next_phase = ph + 2u;
        e->frames++;
    }
    return 0;
}

const uint32_t *emu_framebuffer(const struct emu *e)
{
    return e != NULL ? e->fb : NULL;
}

void emu_set_nearest(struct emu *e, int nearest)
{
    if (e != NULL) {
        e->nearest = nearest != 0;
    }
}

int emu_nearest(const struct emu *e)
{
    return e != NULL && e->nearest;
}

void emu_set_aa(struct emu *e, emu_aa_t aa)
{
    if (e != NULL) {
        e->aa = ((int)aa >= 0 && aa < EMU_AA_COUNT) ? (int)aa : (int)EMU_AA_1X;
    }
}

emu_aa_t emu_aa(const struct emu *e)
{
    return e != NULL ? (emu_aa_t)e->aa : EMU_AA_1X;
}

size_t emu_core_bytes_live(void)
{
    return emu_core_live_bytes();
}

void emu_set_dmg_palette(struct emu *e, unsigned pal)
{
    if (e != NULL) {
        e->palette = pal; /* 0..3 generic, 4 = CGB boot-ROM auto */
        emu_core_set_palette(e->core, e->palette);
    }
}

void emu_set_rtc(struct emu *e, int64_t epoch_sec)
{
    if (e != NULL) {
        e->rtc = epoch_sec;
    }
}

size_t emu_pcm_take(struct emu *e, int16_t *out, size_t max_frames)
{
    uint32_t tail, head, n, i;

    if (e == NULL) {
        return 0;
    }
    tail = atomic_load_explicit(&e->ring_tail, memory_order_relaxed);
    head = atomic_load_explicit(&e->ring_head, memory_order_acquire);
    n = head - tail;
    if (n > max_frames) {
        n = (uint32_t)max_frames;
    }
    for (i = 0; i < n && out != NULL; i++) {
        uint32_t s = (tail + i) & (EMU_PCM_RING - 1u);
        out[i * 2u] = e->ring[s * 2u];
        out[i * 2u + 1u] = e->ring[s * 2u + 1u];
    }
    atomic_store_explicit(&e->ring_tail, tail + n, memory_order_release);
    return n;
}

size_t emu_sram_size(struct emu *e)
{
    return e != NULL ? emu_core_sram_size(e->core) : 0;
}

size_t emu_sram_get(struct emu *e, uint8_t *buf, size_t cap)
{
    return (e != NULL && buf != NULL) ? emu_core_sram_get(e->core, buf, cap) : 0;
}

int emu_sram_put(struct emu *e, const uint8_t *buf, size_t len)
{
    int rc;
    if (e == NULL || buf == NULL || len == 0) {
        return -EINVAL;
    }
    rc = emu_core_sram_put(e->core, buf, len);
    if (rc == 0) {
        emu_sram_mark_clean(e);
    }
    return rc;
}

static uint64_t sram_hash(struct emu *e)
{
    size_t n = emu_sram_size(e), got, i;
    uint8_t *b;
    uint64_t h = FNV_OFF;

    if (n == 0) {
        return 0;
    }
    b = (uint8_t *)malloc(n);
    if (b == NULL) {
        return 0;
    }
    got = emu_sram_get(e, b, n);
    for (i = 0; i < got; i++) {
        h = hs_fnv1a64(h, b[i]);
    }
    free(b);
    return h;
}

int emu_sram_dirty(struct emu *e)
{
    return e != NULL && emu_sram_size(e) > 0 && sram_hash(e) != e->sram_hash;
}

void emu_sram_mark_clean(struct emu *e)
{
    if (e != NULL) {
        e->sram_hash = sram_hash(e);
    }
}

void emu_stats(const struct emu *e, struct emu_stats *out)
{
    static struct hs_hs_stats st; /* shell thread only; keeps 4 KB off the stack */

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (e == NULL) {
        return;
    }
    hs_hs_stats(&e->hs, &st);
    out->frames = e->frames;
    out->phases = st.phases;
    out->freezes = st.freezes;
    out->ledger = hs_hs_ledger_hash(&e->hs);
    out->video_hash = e->video_hash;
    out->audio_hash = e->audio_hash;
    out->pcm_frames = e->pcm_frames;
    out->pcm_nonzero = e->pcm_nonzero;
    out->pcm_dropped = e->pcm_dropped;
}
