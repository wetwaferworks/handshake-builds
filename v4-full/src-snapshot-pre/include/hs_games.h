/* SPDX-License-Identifier: MIT */
#ifndef HS_GAMES_H
#define HS_GAMES_H

#include <stddef.h>
#include <stdint.h>

struct hs_gb;

#define HS_GAMES_MAX      256
#define HS_GAMES_PATH_MAX 512
#define HS_GAMES_NAME_MAX 64
#define HS_SD_MOUNTPOINT  "/mnt/sd"
#define HS_BUILTIN_ROM    "/roms/handshake-demo.gb"
#define HS_BUILTIN_NAME   "Handshake Demo (built-in)"

struct hs_game {
    char path[HS_GAMES_PATH_MAX];
    char name[HS_GAMES_NAME_MAX]; /* file name without .gb */
    int  builtin;
    int  sys;                     /* v4-multi: emu_sys_t (GB for the v3 scanner) */
};

#define HS_GAMES_BUILTIN_MAX 8

struct hs_gamelist {
    int            n;     /* total entries, built-in included */
    int            n_sd;  /* ROMs found on the card */
    int            sd_mounted;
    struct hs_game g[HS_GAMES_MAX + HS_GAMES_BUILTIN_MAX];
};

/* Mount the FAT32 HANDSHAKEOS partition read-write at `mountpoint`
   (mmcblk0p1, then mmcblk1p1). Returns 1 if mounted (or already mounted),
   0 if no card. Never fatal. */
int  hs_sd_mount(const char *mountpoint);
void hs_sd_sync(void);

/* Scan <root>/ROMS/GB and <root>/ROMS for *.gb (any case), sorted by name,
   then append the built-in demo if `builtin_path` is readable. */
int  hs_games_scan(struct hs_gamelist *list, const char *root, int sd_mounted,
                   const char *builtin_path);

/* v4-multi: scan <root>/ROMS/{GB,GBC,GBA,SNES,GENESIS,PSX,N64} and
   <root>/ROMS for every registry extension (folder decides .gb in GBC and
   .bin in PSX), sort by name, then append the built-in demos found in
   builtin_dir (/roms in the initramfs). Returns entries. */
#define HS_BUILTIN_DIR "/roms"
int  hs_games_scan_multi(struct hs_gamelist *list, const char *root, int sd_mounted,
                         const char *builtin_dir);

/* "<rom without extension>.sav" */
int  hs_games_sav_path(const char *rom_path, char *out, size_t n);

/* Battery RAM: load returns bytes read (0 = no file), save returns 0 on
   success. Save goes through <path>.tmp + fsync + rename + dir fsync. */
long hs_games_load_sav(struct hs_gb *g, const char *sav_path);
int  hs_games_save_sav(struct hs_gb *g, const char *sav_path);

int  hs_games_write_file(const char *path, const uint8_t *buf, size_t n);
long hs_games_read_file(const char *path, uint8_t *buf, size_t n);

/* "/dev/snd/pcmC0D0p" when the kernel exposes a playback PCM, else NULL. */
const char *hs_audio_probe(void);

#endif /* HS_GAMES_H */
