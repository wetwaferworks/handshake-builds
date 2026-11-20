/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * fuzz_emu - deterministic mutation fuzzer for every linked core's ROM loader, memory map and
 * first frames, plus the battery-save (SRAM) import path. Pure C11. Built only for host sanitizer
 * runs (tests/fuzz/build-asan.sh: -fsanitize=address,undefined); never shipped.
 * usage: fuzz_emu --roms=DIR [--iters=N] [--frames=F] [--seed=S]
 * Each case: seed ROM -> byte flips / header smashes / truncation / extension / pure noise ->
 * emu_create -> emu_run_frames -> emu_sram_put(random) -> emu_destroy. Any sanitizer report aborts. */
#include "emu_sys.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rs;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static size_t rndn(size_t n) { return n ? (size_t)(rnd() % n) : 0; }

static uint8_t *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    uint8_t *b;
    long sz;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    b = (uint8_t *)malloc((size_t)sz);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f);
    *n = (size_t)sz;
    return b;
}

/* header regions worth smashing per system (ROM header, vectors, PS-X EXE / N64 boot header) */
static void smash_header(emu_sys_t s, uint8_t *b, size_t n) {
    static const struct { emu_sys_t s; size_t off, len; } h[] = {
        { EMU_SYS_GB, 0x100, 0x50 }, { EMU_SYS_GBC, 0x100, 0x50 }, { EMU_SYS_GBA, 0x00, 0xC0 },
        { EMU_SYS_SNES, 0x7FB0, 0x50 }, { EMU_SYS_SNES, 0xFFB0, 0x50 }, { EMU_SYS_GENESIS, 0x000, 0x200 },
        { EMU_SYS_PSX, 0x000, 0x80 }, { EMU_SYS_N64, 0x000, 0x1000 },
    };
    size_t i, k;
    for (i = 0; i < sizeof h / sizeof h[0]; i++) {
        if (h[i].s != s || h[i].off >= n) continue;
        for (k = 0; k < 1 + rndn(16); k++) {
            size_t o = h[i].off + rndn(h[i].len);
            if (o < n) b[o] = (rnd() & 1) ? (uint8_t)rnd() : (uint8_t)((rnd() & 1) ? 0xFF : 0x00);
        }
        /* occasionally plant extreme 32-bit size / address words */
        if ((rnd() & 3) == 0 && h[i].off + h[i].len <= n) {
            size_t o = h[i].off + (rndn(h[i].len / 4) * 4);
            static const uint32_t ext[] = { 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x00000000u, 0x801FFFFCu, 0x10000000u };
            uint32_t v = ext[rndn(sizeof ext / sizeof ext[0])];
            if (o + 4 <= n) memcpy(b + o, &v, 4);
        }
    }
}

int main(int argc, char **argv) {
    static const struct { emu_sys_t s; const char *file; } seeds[] = {
        { EMU_SYS_GB, "handshake-demo.gb" }, { EMU_SYS_GBC, "hs_cgb_test.gbc" }, { EMU_SYS_GBA, "hs_gba_demo.gba" },
        { EMU_SYS_SNES, "hs_mode1_demo.sfc" }, { EMU_SYS_SNES, "hs_mode7_floor.sfc" }, { EMU_SYS_GENESIS, "hs_genesis_demo.md" },
        { EMU_SYS_PSX, "psx/psx_3d_demo.exe" }, { EMU_SYS_N64, "n64/hs_n64_demo.z64" },
    };
    const char *roms = "roms";
    unsigned iters = 100, frames = 2, i;
    size_t si;
    int a;
    rs = 0x48534655ull;
    for (a = 1; a < argc; a++) {
        if (!strncmp(argv[a], "--roms=", 7)) roms = argv[a] + 7;
        else if (!strncmp(argv[a], "--iters=", 8)) iters = (unsigned)strtoul(argv[a] + 8, NULL, 10);
        else if (!strncmp(argv[a], "--frames=", 9)) frames = (unsigned)strtoul(argv[a] + 9, NULL, 10);
        else if (!strncmp(argv[a], "--seed=", 7)) rs = strtoull(argv[a] + 7, NULL, 0) | 1u;
    }
    for (si = 0; si < sizeof seeds / sizeof seeds[0]; si++) {
        char path[4096];
        size_t sn, created = 0, ran = 0;
        uint8_t *seed;
        snprintf(path, sizeof path, "%s/%s", roms, seeds[si].file);
        seed = slurp(path, &sn);
        if (!seed) { fprintf(stderr, "FUZZ missing seed %s\n", path); return 2; }
        for (i = 0; i < iters; i++) {
            size_t n = sn, k, mode = rndn(6);
            uint8_t *b;
            struct emu *e;
            int err = 0;
            if (mode == 3) n = 1 + rndn(sn);                    /* truncate */
            else if (mode == 4) n = sn + 1 + rndn(65536);       /* extend */
            else if (mode == 5) n = 1 + rndn(1u << (8 + rndn(14))); /* pure noise, 1 B .. 2 MiB */
            b = (uint8_t *)malloc(n);
            if (!b) return 3;
            if (mode == 5) { for (k = 0; k < n; k++) b[k] = (uint8_t)rnd(); }
            else {
                memcpy(b, seed, n < sn ? n : sn);
                for (k = sn; k < n; k++) b[k] = (uint8_t)rnd();
                if (mode == 0 || mode == 2) for (k = 0; k < 1 + rndn(64); k++) b[rndn(n)] ^= (uint8_t)(1u << rndn(8));
                if (mode == 1 || mode == 2) smash_header(seeds[si].s, b, n);
            }
            e = emu_create(seeds[si].s, b, n, 3, &err);
            if (e) {
                uint8_t sram[4096];
                size_t sl = rndn(sizeof sram + 1);
                created++;
                for (k = 0; k < sl; k++) sram[k] = (uint8_t)rnd();
                (void)emu_sram_put(e, sram, sl);
                if (emu_run_frames(e, frames, (uint32_t)rnd(), 1) == 0) ran++;
                emu_destroy(e);
            }
            free(b);
        }
        printf("FUZZ sys=%s seed=%s cases=%u created=%zu ran=%zu\n", emu_sys_info(seeds[si].s)->tag, seeds[si].file, iters, created, ran);
        fflush(stdout);
        free(seed);
    }
    {   /* path classifier: hostile names */
        static const char *names[] = { "", ".", "..", "/", "a.", ".gb", "x.GBA", "../../etc/passwd.sfc", "\xff\xfe.md",
                                       "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bin" };
        for (si = 0; si < sizeof names / sizeof names[0]; si++) (void)emu_sys_from_path(names[si], NULL), (void)emu_sys_from_path(names[si], "PSX");
    }
    printf("FUZZ_EMU_DONE\n");
    return 0;
}
