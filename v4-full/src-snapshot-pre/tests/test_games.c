/* SPDX-License-Identifier: MIT */
/* v3 smoke test: ROM scanner and .sav round trip in a synthetic temp dir. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_games.h"
#include "hs_gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef ROM_PATH
#define ROM_PATH "roms/handshake-demo.gb"
#endif

static int fail_count;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail_count++; \
    } \
} while (0)

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *b;
    long len;

    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    b = (uint8_t *)malloc((size_t)len);
    if (b == NULL || fread(b, 1, (size_t)len, fp) != (size_t)len) {
        free(b);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *n = (size_t)len;
    return b;
}

static int put(const char *path, const uint8_t *b, size_t n)
{
    FILE *fp = fopen(path, "wb");
    int ok;

    if (fp == NULL) {
        return -1;
    }
    ok = fwrite(b, 1, n, fp) == n;
    fclose(fp);
    return ok ? 0 : -1;
}

/* MBC1+RAM+BATTERY, 8 KiB RAM, fixed header checksum. */
static void make_battery_cart(uint8_t *rom)
{
    uint8_t x = 0;
    int i;

    rom[0x147] = 0x03;
    rom[0x149] = 0x02;
    for (i = 0x134; i <= 0x14C; i++) {
        x = (uint8_t)(x - rom[i] - 1);
    }
    rom[0x14D] = x;
}

int main(void)
{
    char root[] = "/tmp/hs-games-test-XXXXXX";
    char p[1024], sav[1024];
    struct hs_gamelist *list = calloc(1, sizeof(*list));
    uint8_t *rom;
    size_t rom_n = 0, ram_n = 0;
    struct hs_gb *g;
    uint8_t *ram;
    int i;

    CHECK(list != NULL);
    CHECK(mkdtemp(root) != NULL);
    rom = slurp(ROM_PATH, &rom_n);
    CHECK(rom != NULL && rom_n >= 0x8000);
    if (rom == NULL || list == NULL) {
        return 1;
    }
    make_battery_cart(rom);

    snprintf(p, sizeof(p), "%s/ROMS", root);          mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/ROMS/GB", root);       mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/ROMS/GB/zebra quest.gb", root); CHECK(put(p, rom, rom_n) == 0);
    snprintf(p, sizeof(p), "%s/ROMS/GB/Alpha.GB", root);       CHECK(put(p, rom, rom_n) == 0);
    snprintf(p, sizeof(p), "%s/ROMS/GB/._Alpha.GB", root);     CHECK(put(p, rom, 4096) == 0);
    snprintf(p, sizeof(p), "%s/ROMS/GB/colour.gbc", root);     CHECK(put(p, rom, rom_n) == 0);
    snprintf(p, sizeof(p), "%s/ROMS/GB/tiny.gb", root);        CHECK(put(p, rom, 16) == 0);
    snprintf(p, sizeof(p), "%s/ROMS/middle.gb", root);         CHECK(put(p, rom, rom_n) == 0);
    snprintf(p, sizeof(p), "%s/ROMS/notes.txt", root);         CHECK(put(p, rom, 8) == 0);

    /* card + built-in */
    CHECK(hs_games_scan(list, root, 1, ROM_PATH) == 4);
    CHECK(list->n_sd == 3);
    CHECK(strcmp(list->g[0].name, "Alpha") == 0);
    CHECK(strcmp(list->g[1].name, "middle") == 0);
    CHECK(strcmp(list->g[2].name, "zebra quest") == 0);
    CHECK(strcmp(list->g[3].name, HS_BUILTIN_NAME) == 0 && list->g[3].builtin);
    for (i = 0; i < list->n; i++) {
        printf("  rom[%d] %-28s %s\n", i, list->g[i].name, list->g[i].path);
    }
    /* no card: demo only */
    CHECK(hs_games_scan(list, root, 0, ROM_PATH) == 1);
    CHECK(list->n_sd == 0 && list->g[0].builtin);

    /* .sav path */
    CHECK(hs_games_sav_path("/mnt/sd/ROMS/GB/Alpha.GB", sav, sizeof(sav)) == 0);
    CHECK(strcmp(sav, "/mnt/sd/ROMS/GB/Alpha.sav") == 0);
    CHECK(hs_games_sav_path("/mnt/sd.x/ROMS/noext", sav, sizeof(sav)) == 0);
    CHECK(strcmp(sav, "/mnt/sd.x/ROMS/noext.sav") == 0);

    /* .sav round trip through two separate cores */
    snprintf(p, sizeof(p), "%s/ROMS/GB/Alpha.GB", root);
    CHECK(hs_games_sav_path(p, sav, sizeof(sav)) == 0);
    g = hs_gb_create();
    CHECK(g != NULL && hs_gb_load_rom(g, p) == 0);
    ram = hs_gb_cart_ram(g, &ram_n);
    CHECK(ram != NULL && ram_n == 8192);
    CHECK(hs_games_load_sav(g, sav) == 0); /* no file yet */
    if (ram != NULL) {
        for (i = 0; i < (int)ram_n; i++) {
            ram[i] = (uint8_t)((i * 37 + 11) & 0xFF);
        }
    }
    CHECK(hs_games_save_sav(g, sav) == 0);
    hs_gb_destroy(g);
    {
        struct stat st;
        char tmp[1100];
        CHECK(stat(sav, &st) == 0 && st.st_size == 8192);
        snprintf(tmp, sizeof(tmp), "%s.tmp", sav);
        CHECK(access(tmp, F_OK) != 0);
    }
    g = hs_gb_create();
    CHECK(g != NULL && hs_gb_load_rom(g, p) == 0);
    CHECK(hs_games_load_sav(g, sav) == 8192);
    ram = hs_gb_cart_ram(g, &ram_n);
    {
        int same = ram != NULL;
        for (i = 0; same && i < (int)ram_n; i++) {
            same = ram[i] == (uint8_t)((i * 37 + 11) & 0xFF);
        }
        CHECK(same);
        printf("sav round trip %s (%zu B) %s\n", sav, ram_n, same ? "MATCH" : "MISMATCH");
    }
    CHECK(!hs_gb_sram_dirty(g));
    /* a running core marks RAM dirty only through cart writes */
    hs_gb_destroy(g);

    /* live joypad reaches the core: libbet title -> START -> gameplay */
    {
        struct hs_gb *a = hs_gb_create(), *b = hs_gb_create();
        struct hs_gb_stats sa, sb;
        CHECK(a != NULL && b != NULL);
        CHECK(hs_gb_load_rom(a, "roms/libbet.gb") == 0);
        CHECK(hs_gb_load_rom(b, "roms/libbet.gb") == 0);
        hs_gb_set_joypad(a, 0);
        hs_gb_set_joypad(b, 0);
        CHECK(hs_gb_start(a, 3) == 0);
        CHECK(hs_gb_start(b, 3) == 0);
        CHECK(hs_gb_run_frames(a, 1000) == 0);
        CHECK(hs_gb_run_frames(b, 700) == 0);
        hs_gb_set_joypad(b, 0x08); /* START */
        CHECK(hs_gb_run_frames(b, 10) == 0);
        hs_gb_set_joypad(b, 0);
        CHECK(hs_gb_run_frames(b, 290) == 0);
        hs_gb_stats(a, &sa);
        hs_gb_stats(b, &sb);
        CHECK(sa.frames == 1000 && sb.frames == 1000);
        CHECK(sa.frame_crc != sb.frame_crc);
        CHECK(sa.overruns == 0 && sb.overruns == 0);
        printf("live joypad: idle=%016llx start=%016llx %s\n",
               (unsigned long long)sa.frame_crc, (unsigned long long)sb.frame_crc,
               sa.frame_crc != sb.frame_crc ? "RESPONDS" : "NO EFFECT");
        hs_gb_destroy(a);
        hs_gb_destroy(b);
    }

    /* cleanup */
    {
        static const char *const names[] = {
            "ROMS/GB/zebra quest.gb", "ROMS/GB/Alpha.GB", "ROMS/GB/._Alpha.GB",
            "ROMS/GB/colour.gbc", "ROMS/GB/tiny.gb", "ROMS/GB/Alpha.sav",
            "ROMS/middle.gb", "ROMS/notes.txt"
        };
        size_t k;
        for (k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
            snprintf(p, sizeof(p), "%s/%s", root, names[k]);
            (void)unlink(p);
        }
        snprintf(p, sizeof(p), "%s/ROMS/GB", root); (void)rmdir(p);
        snprintf(p, sizeof(p), "%s/ROMS", root);    (void)rmdir(p);
        (void)rmdir(root);
    }
    free(rom);
    free(list);
    if (fail_count == 0) {
        printf("ALL GAMES TESTS PASSED\n");
        return 0;
    }
    printf("%d checks failed\n", fail_count);
    return 1;
}
