/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_mod.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __NR_init_module
#  if defined(__aarch64__)
#    define __NR_init_module 105
#  elif defined(__x86_64__)
#    define __NR_init_module 175
#  else
#    define __NR_init_module 105
#  endif
#endif
#ifndef __NR_finit_module
#  if defined(__aarch64__)
#    define __NR_finit_module 273
#  elif defined(__x86_64__)
#    define __NR_finit_module 313
#  else
#    define __NR_finit_module 273
#  endif
#endif

/* USB gadget load order (v3-audio / v5.2 config, all =m). */
/* v5.2 / v3-audio: libcomposite includes configfs gadget; only these two ship. */
static const char *const k_usb_mods[] = {
    "libcomposite.ko",
    "usb_f_mass_storage.ko",
    NULL
};

static int sys_finit(int fd, const char *params, int flags)
{
    return (int)syscall(__NR_finit_module, fd, params ? params : "", flags);
}

static int sys_init_module(void *image, unsigned long len, const char *params)
{
    return (int)syscall(__NR_init_module, image, len, params ? params : "");
}

int hs_mod_finit(const char *path, const char *params, struct hs_mod_one *out)
{
    int fd, rc, e;
    struct stat st;
    if (out) {
        memset(out, 0, sizeof(*out));
        if (path) {
            const char *slash = strrchr(path, '/');
            snprintf(out->name, sizeof(out->name), "%s", slash ? slash + 1 : path);
        }
    }
    if (path == NULL) {
        errno = EINVAL;
        if (out) out->err = EINVAL;
        return -1;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (out) out->err = errno;
        return -1;
    }
    rc = sys_finit(fd, params, 0);
    e = errno;
    if (rc != 0 && e == ENOSYS) {
        /* fall back to init_module: read the whole file, then the syscall */
        if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size < 8 * 1024 * 1024) {
            unsigned long n = (unsigned long)st.st_size;
            char *buf = (char *)malloc(n);
            long r, off = 0;
            if (buf != NULL) {
                if (lseek(fd, 0, SEEK_SET) == 0) {
                    while ((unsigned long)off < n) {
                        r = read(fd, buf + off, n - (unsigned long)off);
                        if (r <= 0) break;
                        off += r;
                    }
                    if ((unsigned long)off == n) {
                        rc = sys_init_module(buf, n, params);
                        e = errno;
                    }
                }
                free(buf);
            }
        }
    }
    close(fd);
    if (rc == 0) {
        if (out) { out->loaded = 1; out->err = 0; }
        return 0;
    }
    if (e == EEXIST) {
        if (out) { out->loaded = 1; out->already = 1; out->err = 0; }
        return 0;
    }
    if (out) out->err = e ? e : EIO;
    errno = e ? e : EIO;
    return -1;
}

static int join(char *out, size_t n, const char *dir, const char *name)
{
    int r = snprintf(out, n, "%s/%s", dir && dir[0] ? dir : HS_MOD_DIR, name);
    return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

static int load_named(const char *dir, const char *name, struct hs_mod_report *rep)
{
    char path[256];
    struct hs_mod_one one;
    if (rep->n >= HS_MOD_MAX) return -1;
    if (join(path, sizeof(path), dir, name) != 0) {
        memset(&one, 0, sizeof(one));
        snprintf(one.name, sizeof(one.name), "%s", name);
        one.err = ENAMETOOLONG;
        rep->one[rep->n++] = one;
        rep->failed++;
        return -1;
    }
    if (hs_mod_finit(path, "", &one) != 0) {
        rep->one[rep->n++] = one;
        rep->failed++;
        return -1;
    }
    rep->one[rep->n++] = one;
    return 0;
}

static void summarise(struct hs_mod_report *rep)
{
    if (rep->failed) {
        int i;
        for (i = 0; i < rep->n; i++) {
            if (rep->one[i].err) {
                snprintf(rep->summary, sizeof(rep->summary), "FAIL %s errno=%d (%d/%d ok)",
                         rep->one[i].name, rep->one[i].err, rep->n - rep->failed, rep->n);
                return;
            }
        }
    }
    snprintf(rep->summary, sizeof(rep->summary), "OK loaded=%d already=%d",
             rep->n, /* already counted below */ 0);
    {
        int a = 0, i;
        for (i = 0; i < rep->n; i++) if (rep->one[i].already) a++;
        snprintf(rep->summary, sizeof(rep->summary), "OK loaded=%d already=%d", rep->n, a);
    }
}

int hs_mod_load_usb(const char *dir, struct hs_mod_report *rep)
{
    int i;
    if (rep == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(rep, 0, sizeof(*rep));
    for (i = 0; k_usb_mods[i] != NULL; i++) {
        (void)load_named(dir, k_usb_mods[i], rep);
        if (rep->n >= HS_MOD_MAX) break;
    }
    summarise(rep);
    return rep->failed ? -1 : 0;
}

int hs_mod_load_one(const char *dir, const char *name, struct hs_mod_report *rep)
{
    if (rep == NULL || name == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(rep, 0, sizeof(*rep));
    (void)load_named(dir, name, rep);
    summarise(rep);
    return rep->failed ? -1 : 0;
}
