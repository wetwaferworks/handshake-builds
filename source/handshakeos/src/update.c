/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_update.h"
#include "hs_sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_all(int fd, uint8_t *buf, size_t n)
{
    size_t off = 0;
    while (off < n) {
        long r = read(fd, buf + off, n - off);
        if (r <= 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

static int write_all(int fd, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < n) {
        long r = write(fd, p + off, n - off);
        if (r <= 0) return -1;
        off += (size_t)r;
    }
    return 0;
}

static int slurp(const char *path, uint8_t **out, size_t *n)
{
    int fd;
    off_t sz;
    uint8_t *buf;
    *out = NULL; *n = 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    sz = lseek(fd, 0, SEEK_END);
    if (sz < 0 || lseek(fd, 0, SEEK_SET) < 0) { close(fd); return -1; }
    buf = (uint8_t *)malloc((size_t)sz + 1);
    if (buf == NULL) { close(fd); return -1; }
    if (sz > 0 && read_all(fd, buf, (size_t)sz) != 0) { free(buf); close(fd); return -1; }
    close(fd);
    buf[sz] = 0;
    *out = buf;
    *n = (size_t)sz;
    return 0;
}

static int atomic_write(const char *path, const void *buf, size_t n)
{
    char tmp[256];
    int fd, elen;
    size_t i;
    elen = (int)strlen(path);
    if (elen + 5 >= (int)sizeof(tmp)) { errno = ENAMETOOLONG; return -1; }
    memcpy(tmp, path, (size_t)elen);
    memcpy(tmp + elen, ".tmp", 5);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    if (write_all(fd, buf, n) != 0) {
        int e = errno; close(fd); unlink(tmp); errno = e; return -1;
    }
    (void)fsync(fd);
    close(fd);
    if (rename(tmp, path) != 0) {
        int e = errno; unlink(tmp); errno = e; return -1;
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

int hs_hsu_create(const char *out,
                  const char *const *src_paths,
                  const char *const *dest_names,
                  int n)
{
    uint8_t *body = NULL, *p;
    size_t cap = 64, used = 16;
    int i;
    uint8_t pkg[HS_SHA256_LEN];
    char manifest[1024];
    int mlen;

    if (out == NULL || src_paths == NULL || dest_names == NULL || n <= 0 || n > HS_UPDATE_MAX_FILES) {
        errno = EINVAL;
        return -1;
    }
    mlen = snprintf(manifest, sizeof(manifest), "version=1\nfiles=%d\n", n);
    for (i = 0; i < n; i++) {
        int k = snprintf(manifest + mlen, sizeof(manifest) - (size_t)mlen,
                         "file=%s\n", dest_names[i]);
        if (k < 0 || mlen + k >= (int)sizeof(manifest)) { errno = EOVERFLOW; return -1; }
        mlen += k;
    }
    cap = 16 + (size_t)mlen + 8 * 1024 * 1024;
    body = (uint8_t *)malloc(cap);
    if (body == NULL) return -1;
    memcpy(body, HS_HSU_MAGIC, 4);
    wr_le32(body + 4, 1);
    wr_le32(body + 8, (uint32_t)n);
    wr_le32(body + 12, (uint32_t)mlen);
    memcpy(body + 16, manifest, (size_t)mlen);
    used = 16 + (size_t)mlen;
    for (i = 0; i < n; i++) {
        uint8_t *data = NULL;
        size_t dlen = 0, nlen;
        uint8_t dig[HS_SHA256_LEN];
        nlen = strlen(dest_names[i]);
        if (slurp(src_paths[i], &data, &dlen) != 0) { free(body); return -1; }
        hs_sha256(data, dlen, dig);
        if (used + 8 + nlen + 32 + dlen + 32 > cap) {
            uint8_t *nb = (uint8_t *)realloc(body, used + 8 + nlen + 32 + dlen + 32 + 4096);
            if (!nb) { free(data); free(body); return -1; }
            body = nb;
            cap = used + 8 + nlen + 32 + dlen + 32 + 4096;
        }
        p = body + used;
        wr_le32(p, (uint32_t)nlen);
        memcpy(p + 4, dest_names[i], nlen);
        wr_le32(p + 4 + nlen, (uint32_t)dlen);
        memcpy(p + 8 + nlen, dig, 32);
        memcpy(p + 8 + nlen + 32, data, dlen);
        used += 8 + nlen + 32 + dlen;
        free(data);
    }
    hs_sha256(body, used, pkg);
    if (used + 32 > cap) {
        uint8_t *nb = (uint8_t *)realloc(body, used + 32);
        if (!nb) { free(body); return -1; }
        body = nb;
    }
    memcpy(body + used, pkg, 32);
    used += 32;
    if (atomic_write(out, body, used) != 0) { free(body); return -1; }
    free(body);
    return 0;
}

static int parse_and_verify(const uint8_t *buf, size_t n, struct hs_update_report *rep,
                            const uint8_t **payloads, size_t *psize)
{
    uint32_t nfiles, mlen, i, pos;
    const uint8_t *pkg;
    uint8_t got[HS_SHA256_LEN];

    memset(rep, 0, sizeof(*rep));
    if (n < 16 + 32 || memcmp(buf, HS_HSU_MAGIC, 4) != 0) {
        snprintf(rep->msg, sizeof(rep->msg), "bad magic");
        return -1;
    }
    if (rd_le32(buf + 4) != 1) {
        snprintf(rep->msg, sizeof(rep->msg), "bad version");
        return -1;
    }
    nfiles = rd_le32(buf + 8);
    mlen = rd_le32(buf + 12);
    if (nfiles == 0 || nfiles > HS_UPDATE_MAX_FILES || 16u + mlen + 32u > n) {
        snprintf(rep->msg, sizeof(rep->msg), "bad header");
        return -1;
    }
    /* package hash is the last 32 bytes; covers everything before it */
    pkg = buf + (n - 32);
    hs_sha256(buf, n - 32, got);
    if (memcmp(got, pkg, 32) != 0) {
        snprintf(rep->msg, sizeof(rep->msg), "package sha256 mismatch");
        return -1;
    }
    pos = 16 + mlen;
    for (i = 0; i < nfiles; i++) {
        uint32_t nlen, sz;
        uint8_t file_got[HS_SHA256_LEN];
        char hex[65];
        if (pos + 8 > n - 32) {
            snprintf(rep->msg, sizeof(rep->msg), "truncated file %u", i);
            return -1;
        }
        nlen = rd_le32(buf + pos); pos += 4;
        if (nlen == 0 || nlen >= 64 || pos + nlen + 4 + 32 > n - 32) {
            snprintf(rep->msg, sizeof(rep->msg), "bad name %u", i);
            return -1;
        }
        memcpy(rep->file[i].name, buf + pos, nlen);
        rep->file[i].name[nlen] = '\0';
        pos += nlen;
        sz = rd_le32(buf + pos); pos += 4;
        if (pos + 32 + sz > n - 32) {
            snprintf(rep->msg, sizeof(rep->msg), "truncated payload %u", i);
            return -1;
        }
        hs_sha256(buf + pos + 32, sz, file_got);
        if (memcmp(file_got, buf + pos, 32) != 0) {
            snprintf(rep->msg, sizeof(rep->msg), "file sha256 mismatch %s", rep->file[i].name);
            return -1;
        }
        hs_sha256_hex(file_got, hex);
        memcpy(rep->file[i].sha256_hex, hex, 65);
        rep->file[i].size = sz;
        rep->file[i].verified = 1;
        if (payloads) payloads[i] = buf + pos + 32;
        if (psize) psize[i] = sz;
        pos += 32 + sz;
        rep->nfiles++;
    }
    if (pos != n - 32) {
        snprintf(rep->msg, sizeof(rep->msg), "trailing bytes");
        return -1;
    }
    rep->verified = 1;
    snprintf(rep->msg, sizeof(rep->msg), "OK files=%u", nfiles);
    return 0;
}

int hs_hsu_verify(const char *path, struct hs_update_report *rep)
{
    uint8_t *buf = NULL;
    size_t n = 0;
    int rc;
    struct hs_update_report local;
    if (rep == NULL) rep = &local;
    if (slurp(path, &buf, &n) != 0) {
        memset(rep, 0, sizeof(*rep));
        snprintf(rep->msg, sizeof(rep->msg), "open (%d)", errno);
        return -1;
    }
    rc = parse_and_verify(buf, n, rep, NULL, NULL);
    free(buf);
    return rc;
}

static int slot_marker(const char *mount, hs_slot_t s)
{
    char path[256], empty = 0;
    int fd;
    snprintf(path, sizeof(path), "%s/SLOT-B", mount ? mount : "/mnt/sd");
    if (s == HS_SLOT_A) {
        (void)unlink(path);
        return 0;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    {
        long wr = write(fd, &empty, 0);
        (void)wr;
    }
    (void)fsync(fd);
    close(fd);
    return 0;
}

int hs_update_apply(const char *hsu, const char *mount, struct hs_cfg *cfg,
                    struct hs_update_report *rep)
{
    uint8_t *buf = NULL;
    size_t n = 0, psz[HS_UPDATE_MAX_FILES];
    const uint8_t *payloads[HS_UPDATE_MAX_FILES];
    struct hs_update_report local;
    hs_slot_t dest;
    const char *m = mount && mount[0] ? mount : "/mnt/sd";
    int i, wrote = 0;
    char destpath[256];
    char letter;

    if (rep == NULL) rep = &local;
    memset(rep, 0, sizeof(*rep));
    if (cfg == NULL) {
        snprintf(rep->msg, sizeof(rep->msg), "no cfg");
        return -1;
    }
    dest = hs_slot_other(cfg->slot);
    rep->dest = dest;
    letter = dest == HS_SLOT_B ? 'B' : 'A';
    if (slurp(hsu, &buf, &n) != 0) {
        snprintf(rep->msg, sizeof(rep->msg), "open hsu (%d)", errno);
        return -1;
    }
    if (parse_and_verify(buf, n, rep, payloads, psz) != 0) {
        free(buf);
        return -1;
    }
    rep->dest = dest;
    for (i = 0; i < rep->nfiles; i++) {
        /* Map well-known names onto the destination slot. */
        const char *nm = rep->file[i].name;
        if (strcmp(nm, "Image.gz") == 0 || strcmp(nm, "Image-A.gz") == 0 || strcmp(nm, "Image-B.gz") == 0)
            snprintf(destpath, sizeof(destpath), "%s/Image-%c.gz", m, letter);
        else if (strstr(nm, "initramfs") != NULL)
            snprintf(destpath, sizeof(destpath), "%s/handshakeos-initramfs-%c.uimg", m, letter);
        else
            snprintf(destpath, sizeof(destpath), "%s/%s", m, nm);
        if (atomic_write(destpath, payloads[i], psz[i]) != 0) {
            snprintf(rep->msg, sizeof(rep->msg), "write failed errno=%d slot %s unchanged",
                     errno, hs_slot_name(cfg->slot));
            free(buf);
            return -1;
        }
        wrote++;
    }
    if (slot_marker(m, dest) != 0) {
        snprintf(rep->msg, sizeof(rep->msg), "marker failed; files written but slot %s still active",
                 hs_slot_name(cfg->slot));
        free(buf);
        return -1;
    }
    cfg->fallback = cfg->slot;
    cfg->slot = dest;
    {
        char cfgpath[256];
        snprintf(cfgpath, sizeof(cfgpath), "%s/%s", m, HS_CFG_NAME);
        if (hs_cfg_save(cfg, cfgpath) != 0) {
            /* revert marker so U-Boot still boots the previous slot */
            (void)slot_marker(m, cfg->fallback);
            cfg->slot = cfg->fallback;
            snprintf(rep->msg, sizeof(rep->msg), "cfg save failed; fallback to %s",
                     hs_slot_name(cfg->slot));
            free(buf);
            return -1;
        }
    }
    free(buf);
    rep->ok = 1;
    snprintf(rep->msg, sizeof(rep->msg), "applied slot %c files=%d", letter, wrote);
    return 0;
}

int hs_update_find(const char *dir, char *out, size_t n)
{
    static const char *const names[] = {
        "handshake.hsu", "update.hsu", "handshakeos.hsu", "os.hsu", NULL
    };
    const char *d = (dir && dir[0]) ? dir : "/mnt/sd";
    char path[256];
    int i;
    if (out == NULL || n == 0) return -1;
    out[0] = '\0';
    for (i = 0; names[i] != NULL; i++) {
        int r = snprintf(path, sizeof(path), "%s/%s", d, names[i]);
        if (r < 0 || (size_t)r >= sizeof(path)) continue;
        if (access(path, R_OK) == 0) {
            snprintf(out, n, "%s", path);
            return 0;
        }
    }
    return -1;
}
