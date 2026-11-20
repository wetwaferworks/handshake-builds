/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_battery.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void hs_battery_clear(struct hs_battery *bat)
{
    if (bat == NULL) {
        return;
    }
    memset(bat, 0, sizeof(*bat));
    bat->percent = -1;
    bat->charging = -1;
    bat->voltage_uv = -1;
    bat->charger_online = -1;
}

static int read_file(const char *path, char *buf, size_t buflen)
{
    FILE *fp;
    size_t n;

    fp = fopen(path, "r");
    if (fp == NULL) {
        return -errno;
    }
    if (fgets(buf, (int)buflen, fp) == NULL) {
        fclose(fp);
        return -EIO;
    }
    fclose(fp);
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
        buf[--n] = '\0';
    }
    return 0;
}

static int read_attr(const char *root, const char *ent, const char *attr,
                     char *buf, size_t n)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s/%s", root, ent, attr);
    return read_file(path, buf, n);
}

static void copy_name(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);

    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Single-cell Li-ion rough curve (3.40 V empty .. 4.20 V full), used only
   when the driver exposes voltage_now but not capacity. */
static int percent_from_uv(int uv)
{
    static const int mv_tab[11] = { 3400, 3590, 3660, 3710, 3750, 3790,
                                    3840, 3900, 3970, 4080, 4200 };
    int mv = uv / 1000;
    int i;

    if (mv <= mv_tab[0]) {
        return 0;
    }
    if (mv >= mv_tab[10]) {
        return 100;
    }
    for (i = 1; i < 11; i++) {
        if (mv < mv_tab[i]) {
            int lo = mv_tab[i - 1];
            int hi = mv_tab[i];
            return (i - 1) * 10 + ((mv - lo) * 10) / (hi - lo);
        }
    }
    return 100;
}

int hs_battery_read_root(struct hs_battery *bat, const char *root)
{
    DIR *dir;
    struct dirent *ent;
    char type[64];
    char val[64];

    if (bat == NULL) {
        return -EINVAL;
    }
    hs_battery_clear(bat);
    if (root == NULL) {
        root = "/sys/class/power_supply";
    }
    dir = opendir(root);
    if (dir == NULL) {
        return 0;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (read_attr(root, ent->d_name, "type", type, sizeof(type)) != 0) {
            continue;
        }
        bat->n_supplies++;

        if (strcmp(type, "Battery") == 0 || strcmp(type, "battery") == 0) {
            if (bat->present) {
                continue; /* first battery wins */
            }
            if (read_attr(root, ent->d_name, "present", val, sizeof(val)) == 0
                && atoi(val) == 0) {
                continue;
            }
            bat->present = 1;
            copy_name(bat->name, sizeof(bat->name), ent->d_name);

            if (read_attr(root, ent->d_name, "voltage_now", val, sizeof(val)) == 0) {
                bat->voltage_uv = atoi(val);
            }
            if (read_attr(root, ent->d_name, "capacity", val, sizeof(val)) == 0) {
                bat->percent = atoi(val);
            } else if (bat->voltage_uv > 0) {
                bat->percent = percent_from_uv(bat->voltage_uv);
                bat->percent_est = 1;
            }
            if (bat->percent > 100) {
                bat->percent = 100;
            }
            if (bat->percent < -1) {
                bat->percent = 0;
            }
            if (read_attr(root, ent->d_name, "status", val, sizeof(val)) == 0) {
                copy_name(bat->status, sizeof(bat->status), val);
                if (strcmp(val, "Charging") == 0) {
                    bat->charging = 1;
                } else if (strcmp(val, "Full") == 0 || strcmp(val, "Discharging") == 0
                           || strcmp(val, "Not charging") == 0) {
                    bat->charging = 0;
                }
            }
        } else if (strcmp(type, "USB") == 0 || strcmp(type, "Mains") == 0
                   || strcmp(type, "Wireless") == 0) {
            if (read_attr(root, ent->d_name, "online", val, sizeof(val)) == 0) {
                int on = atoi(val) != 0;
                if (on || bat->charger_online < 0) {
                    bat->charger_online = on;
                    copy_name(bat->charger, sizeof(bat->charger), ent->d_name);
                }
            }
        }
    }
    closedir(dir);
    if (bat->charging < 0 && bat->present && bat->charger_online >= 0) {
        bat->charging = bat->charger_online;
    }
    return 0;
}

int hs_battery_read(struct hs_battery *bat)
{
    return hs_battery_read_root(bat, getenv("HANDSHAKEOS_POWER_SUPPLY_ROOT"));
}

const char *hs_battery_label(const struct hs_battery *bat, char *buf, unsigned n)
{
    if (buf == NULL || n == 0) {
        return "";
    }
    if (bat != NULL && bat->present && bat->percent >= 0) {
        snprintf(buf, n, "%s%d%%", bat->percent_est ? "~" : "", bat->percent);
    } else if (bat != NULL && bat->charger_online == 1) {
        snprintf(buf, n, "USB");
    } else if (bat != NULL && bat->n_supplies > 0) {
        snprintf(buf, n, "--");
    } else {
        snprintf(buf, n, "NO DRV");
    }
    return buf;
}
