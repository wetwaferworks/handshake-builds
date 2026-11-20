/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_cfg.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void hs_cfg_defaults(struct hs_cfg *c)
{
    memset(c, 0, sizeof(*c));
    c->version = 1;
    snprintf(c->splash_id, sizeof(c->splash_id), "01");
    c->animation = 1;
    c->theme = HS_THEME_PURPLE;
    c->slot = HS_SLOT_A;
    c->fallback = HS_SLOT_A;
    c->loaded = 0;
    c->fallback_used = 0;
}

const char *hs_cfg_path(const char *mountpoint)
{
    static char p[256];
    const char *m = (mountpoint && mountpoint[0]) ? mountpoint : "/mnt/sd";
    snprintf(p, sizeof(p), "%s/%s", m, HS_CFG_NAME);
    return p;
}

const char *hs_slot_name(hs_slot_t s)
{
    return s == HS_SLOT_B ? "B" : "A";
}

hs_slot_t hs_slot_from_name(const char *s)
{
    if (s != NULL && (s[0] == 'B' || s[0] == 'b')) return HS_SLOT_B;
    return HS_SLOT_A;
}

hs_slot_t hs_slot_other(hs_slot_t s)
{
    return s == HS_SLOT_A ? HS_SLOT_B : HS_SLOT_A;
}

static int starts(const char *line, const char *key, const char **val)
{
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0) return 0;
    if (line[n] != '=') return 0;
    *val = line + n + 1;
    return 1;
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

int hs_cfg_load(struct hs_cfg *c, const char *path)
{
    char buf[1024];
    char line[192];
    int fd, got = 0, bad = 0;
    long r, i, start;
    const char *v;

    hs_cfg_defaults(c);
    if (path == NULL) {
        c->fallback_used = 1;
        snprintf(c->err, sizeof(c->err), "no path");
        return 0;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        c->fallback_used = 1;
        snprintf(c->err, sizeof(c->err), "missing (%d)", errno);
        return 0;
    }
    r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r < 0) {
        c->fallback_used = 1;
        snprintf(c->err, sizeof(c->err), "read (%d)", errno);
        return 0;
    }
    buf[r] = '\0';
    start = 0;
    for (i = 0; i <= r; i++) {
        if (i < r && buf[i] != '\n') continue;
        {
            size_t L = (size_t)(i - start);
            if (L >= sizeof(line)) L = sizeof(line) - 1;
            memcpy(line, buf + start, L);
            line[L] = '\0';
            trim(line);
            start = i + 1;
            if (line[0] == '\0' || line[0] == '#') continue;
            if (starts(line, "version", &v)) {
                if (v[0] != '1' || v[1] != '\0') { bad = 1; break; }
                got++;
            } else if (starts(line, "splash", &v)) {
                if (v[0] == '\0' || strlen(v) >= HS_CFG_SPLASH_MAX) { bad = 1; break; }
                snprintf(c->splash_id, sizeof(c->splash_id), "%s", v);
                got++;
            } else if (starts(line, "animation", &v)) {
                if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0) c->animation = 1;
                else if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) c->animation = 0;
                else { bad = 1; break; }
                got++;
            } else if (starts(line, "theme", &v)) {
                if (strcmp(v, "purple") == 0 || strcmp(v, "deep-purple") == 0) c->theme = HS_THEME_PURPLE;
                else if (strcmp(v, "green") == 0 || strcmp(v, "matrix-green") == 0) c->theme = HS_THEME_GREEN;
                else { bad = 1; break; }
                got++;
            } else if (starts(line, "slot", &v)) {
                if (v[0] == 'A' || v[0] == 'a' || v[0] == 'B' || v[0] == 'b') c->slot = hs_slot_from_name(v);
                else { bad = 1; break; }
                got++;
            } else if (starts(line, "fallback", &v)) {
                if (v[0] == 'A' || v[0] == 'a' || v[0] == 'B' || v[0] == 'b') c->fallback = hs_slot_from_name(v);
                else { bad = 1; break; }
                got++;
            } else {
                bad = 1;
                break;
            }
        }
    }
    if (bad || got < 5) {
        hs_cfg_defaults(c);
        c->fallback_used = 1;
        snprintf(c->err, sizeof(c->err), bad ? "corrupt" : "incomplete");
        return 0;
    }
    c->loaded = 1;
    return 0;
}

int hs_cfg_save(const struct hs_cfg *c, const char *path)
{
    char tmp[256], body[512];
    int fd, n, w, elen;
    size_t i;
    if (c == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    elen = (int)strlen(path);
    if (elen + 5 >= (int)sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(tmp, path, (size_t)elen);
    memcpy(tmp + elen, ".tmp", 5);
    n = snprintf(body, sizeof(body),
                 "# HandshakeOS boot config\n"
                 "version=1\n"
                 "splash=%s\n"
                 "animation=%s\n"
                 "theme=%s\n"
                 "slot=%s\n"
                 "fallback=%s\n",
                 c->splash_id[0] ? c->splash_id : "01",
                 c->animation ? "on" : "off",
                 c->theme == HS_THEME_GREEN ? "green" : "purple",
                 hs_slot_name(c->slot),
                 hs_slot_name(c->fallback));
    if (n < 0 || (size_t)n >= sizeof(body)) {
        errno = EOVERFLOW;
        return -1;
    }
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    w = (int)write(fd, body, (size_t)n);
    if (w == n) (void)fsync(fd);
    close(fd);
    if (w != n) {
        (void)unlink(tmp);
        errno = EIO;
        return -1;
    }
    if (rename(tmp, path) != 0) {
        int e = errno;
        (void)unlink(tmp);
        errno = e;
        return -1;
    }
    i = strlen(path);
    while (i > 0 && path[i - 1] != '/') i--;
    if (i > 0) {
        char dir[256];
        if (i < sizeof(dir)) {
            memcpy(dir, path, i);
            dir[i] = '\0';
            fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fd >= 0) { (void)fsync(fd); close(fd); }
        }
    }
    return 0;
}
