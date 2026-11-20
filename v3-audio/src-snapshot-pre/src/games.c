/* SPDX-License-Identifier: MIT */
/* HandshakeOS v3: microSD mount, ROM scanner, and .sav battery RAM.
   Runs on the shell thread only (never inside a phase kernel). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_games.h"
#include "hs_gb.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_mounted(const char *mountpoint)
{
    char line[1024];
    char needle[HS_GAMES_PATH_MAX + 4];
    FILE *fp = fopen("/proc/mounts", "r");
    int hit = 0;

    if (fp == NULL) {
        return 0;
    }
    snprintf(needle, sizeof(needle), " %s ", mountpoint);
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, needle) != NULL) {
            hit = 1;
            break;
        }
    }
    fclose(fp);
    return hit;
}

int hs_sd_mount(const char *mountpoint)
{
    static const char *const k_devs[] = {
        "/dev/mmcblk0p1", "/dev/mmcblk1p1"
    };
    size_t i;

    if (mountpoint == NULL) {
        mountpoint = HS_SD_MOUNTPOINT;
    }
    if (is_mounted(mountpoint)) {
        return 1;
    }
    (void)mkdir("/mnt", 0755);
    (void)mkdir(mountpoint, 0755);
    for (i = 0; i < sizeof(k_devs) / sizeof(k_devs[0]); i++) {
        if (access(k_devs[i], F_OK) != 0) {
            continue;
        }
        if (mount(k_devs[i], mountpoint, "vfat", MS_NOATIME | MS_NODIRATIME,
                  "shortname=mixed,errors=remount-ro") == 0) {
            return 1;
        }
    }
    return 0;
}

void hs_sd_sync(void)
{
    sync();
}

static int has_gb_ext(const char *name)
{
    size_t n = strlen(name);

    return n > 3 && strcasecmp(name + n - 3, ".gb") == 0;
}

static int cmp_game(const void *a, const void *b)
{
    const struct hs_game *x = (const struct hs_game *)a;
    const struct hs_game *y = (const struct hs_game *)b;
    int c = strcasecmp(x->name, y->name);

    return c != 0 ? c : strcmp(x->path, y->path);
}

static void scan_dir(struct hs_gamelist *list, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL && list->n < HS_GAMES_MAX) {
        struct hs_game *g;
        struct stat st;
        size_t n;

        if (e->d_name[0] == '.' || !has_gb_ext(e->d_name)) {
            continue; /* also skips macOS "._x.gb" resource forks */
        }
        g = &list->g[list->n];
        if (snprintf(g->path, sizeof(g->path), "%s/%s", dir, e->d_name) >=
            (int)sizeof(g->path)) {
            continue;
        }
        if (stat(g->path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0x150) {
            continue;
        }
        n = strlen(e->d_name) - 3;
        if (n >= sizeof(g->name)) {
            n = sizeof(g->name) - 1;
        }
        memcpy(g->name, e->d_name, n);
        g->name[n] = '\0';
        g->builtin = 0;
        list->n++;
    }
    closedir(d);
}

int hs_games_scan(struct hs_gamelist *list, const char *root, int sd_mounted,
                  const char *builtin_path)
{
    char dir[HS_GAMES_PATH_MAX];

    if (list == NULL) {
        return -EINVAL;
    }
    memset(list, 0, sizeof(*list));
    list->sd_mounted = sd_mounted;
    if (root != NULL && sd_mounted) {
        snprintf(dir, sizeof(dir), "%s/ROMS/GB", root);
        scan_dir(list, dir);
        snprintf(dir, sizeof(dir), "%s/ROMS", root);
        scan_dir(list, dir);
        qsort(list->g, (size_t)list->n, sizeof(list->g[0]), cmp_game);
    }
    list->n_sd = list->n;
    if (builtin_path != NULL && access(builtin_path, R_OK) == 0) {
        struct hs_game *g = &list->g[list->n];
        snprintf(g->path, sizeof(g->path), "%s", builtin_path);
        snprintf(g->name, sizeof(g->name), "%s", HS_BUILTIN_NAME);
        g->builtin = 1;
        list->n++;
    }
    return list->n;
}

int hs_games_sav_path(const char *rom_path, char *out, size_t n)
{
    const char *dot, *slash;
    size_t base;

    if (rom_path == NULL || out == NULL || n == 0) {
        return -EINVAL;
    }
    dot = strrchr(rom_path, '.');
    slash = strrchr(rom_path, '/');
    base = (dot != NULL && (slash == NULL || dot > slash)) ? (size_t)(dot - rom_path)
                                                             : strlen(rom_path);
    if (base + 5 > n) {
        return -ENAMETOOLONG;
    }
    memcpy(out, rom_path, base);
    memcpy(out + base, ".sav", 5);
    return 0;
}

long hs_games_read_file(const char *path, uint8_t *buf, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    size_t got = 0;

    if (fd < 0) {
        return errno == ENOENT ? 0 : -errno;
    }
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -EIO;
        }
        if (r == 0) {
            break;
        }
        got += (size_t)r;
    }
    close(fd);
    return (long)got;
}

int hs_games_write_file(const char *path, const uint8_t *buf, size_t n)
{
    char tmp[HS_GAMES_PATH_MAX + 8];
    char dir[HS_GAMES_PATH_MAX];
    const char *slash;
    size_t off = 0;
    int fd;

    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return -ENAMETOOLONG;
    }
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return -errno;
    }
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            (void)unlink(tmp);
            return -EIO;
        }
        off += (size_t)w;
    }
    if (fsync(fd) != 0) {
        close(fd);
        (void)unlink(tmp);
        return -EIO;
    }
    close(fd);
    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return -errno;
    }
    slash = strrchr(path, '/');
    if (slash != NULL && (size_t)(slash - path) < sizeof(dir)) {
        size_t dl = slash == path ? 1u : (size_t)(slash - path);
        memcpy(dir, path, dl);
        dir[dl] = '\0';
        fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0) {
            (void)fsync(fd);
            close(fd);
        }
    }
    return 0;
}

long hs_games_load_sav(struct hs_gb *g, const char *sav_path)
{
    size_t size = 0;
    uint8_t *ram = hs_gb_cart_ram(g, &size);
    long r;

    if (ram == NULL || size == 0 || sav_path == NULL) {
        return 0;
    }
    r = hs_games_read_file(sav_path, ram, size);
    hs_gb_sram_mark_clean(g);
    return r;
}

int hs_games_save_sav(struct hs_gb *g, const char *sav_path)
{
    size_t size = 0;
    uint8_t *ram = hs_gb_cart_ram(g, &size);
    int rc;

    if (ram == NULL || size == 0 || sav_path == NULL) {
        return 0;
    }
    rc = hs_games_write_file(sav_path, ram, size);
    if (rc == 0) {
        hs_gb_sram_mark_clean(g);
    }
    return rc;
}

const char *hs_audio_probe(void)
{
    static const char *const k_pcm = "/dev/snd/pcmC0D0p";

    return access(k_pcm, W_OK) == 0 ? k_pcm : NULL;
}
