/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hs_mkcpio - deterministic newc initramfs writer (pure C11).
 * usage: hs-mkcpio DIR > out.cpio   (pipe through gzip -9n)
 * The tree lives on a Windows drive (drvfs: every file reads back 0777, inode numbers vary run
 * to run), so modes, owners, inodes and times are fixed here instead of taken from the
 * filesystem: directories 0755, files 0644, /init and /bin/... 0755, uid/gid 0,
 * mtime SOURCE_DATE_EPOCH (0 if unset). Entries: "." then every path, sorted bytewise. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct { char **v; size_t n, cap; } plist;

static void die(const char *m, const char *p) { fprintf(stderr, "hs-mkcpio: %s: %s\n", m, p); exit(1); }

static void push(plist *l, const char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->v = (char **)realloc(l->v, l->cap * sizeof *l->v);
        if (!l->v) die("out of memory", s);
    }
    l->v[l->n] = strdup(s);
    if (!l->v[l->n]) die("out of memory", s);
    l->n++;
}

static void walk(const char *root, const char *rel, plist *l) {
    char full[4096];
    DIR *d;
    struct dirent *e;
    snprintf(full, sizeof full, "%s%s%s", root, rel[0] ? "/" : "", rel);
    d = opendir(full);
    if (!d) die("cannot open directory", full);
    while ((e = readdir(d)) != NULL) {
        char sub[4096], fp[8200];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(fp, sizeof fp, "%s/%s", root, sub);
        if (lstat(fp, &st)) die("cannot stat", fp);
        if (S_ISLNK(st.st_mode)) die("symlink not expected", sub);
        push(l, sub);
        if (S_ISDIR(st.st_mode)) walk(root, sub, l);
    }
    closedir(d);
}

static int cmp_bytes(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);   /* strcmp compares as unsigned char */
}

static unsigned long mtime;

static void pad4(size_t n) { static const char z[4]; fwrite(z, 1, (4 - n % 4) % 4, stdout); }

static void hdr(unsigned long ino, unsigned long mode, unsigned long nlink, unsigned long size, const char *name) {
    char h[160];
    size_t nl = strlen(name) + 1;
    snprintf(h, sizeof h, "070701%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X%08X",
             (unsigned)ino, (unsigned)mode, 0u, 0u, (unsigned)nlink, (unsigned)mtime, (unsigned)size, 0u, 0u, 0u, 0u, (unsigned)nl, 0u);
    fwrite(h, 1, 110, stdout);
    fwrite(name, 1, nl, stdout);
    pad4(110 + nl);
}

int main(int argc, char **argv) {
    plist l = { NULL, 0, 0 };
    const char *sde = getenv("SOURCE_DATE_EPOCH");
    size_t i;
    unsigned long ino = 0;
    if (argc != 2) { fprintf(stderr, "usage: hs-mkcpio DIR > out.cpio\n"); return 2; }
    mtime = sde ? strtoul(sde, NULL, 10) : 0;
    walk(argv[1], "", &l);
    qsort(l.v, l.n, sizeof *l.v, cmp_bytes);
    hdr(++ino, 040755, 2, 0, ".");
    for (i = 0; i < l.n; i++) {
        char fp[8200];
        struct stat st;
        snprintf(fp, sizeof fp, "%s/%s", argv[1], l.v[i]);
        if (stat(fp, &st)) die("cannot stat", fp);
        ino++;
        if (S_ISDIR(st.st_mode)) { hdr(ino, 040755, 2, 0, l.v[i]); continue; }
        {
            FILE *f = fopen(fp, "rb");
            unsigned char *buf;
            size_t n = (size_t)st.st_size, r;
            int exe = !strcmp(l.v[i], "init") || !strncmp(l.v[i], "bin/", 4);
            if (!f) die("cannot open", fp);
            buf = (unsigned char *)malloc(n ? n : 1);
            if (!buf) die("out of memory", fp);
            r = fread(buf, 1, n, f);
            fclose(f);
            if (r != n) die("short read", fp);
            hdr(ino, exe ? 0100755 : 0100644, 1, n, l.v[i]);
            fwrite(buf, 1, n, stdout);
            pad4(n);
            free(buf);
        }
    }
    hdr(0, 0, 1, 0, "TRAILER!!!");
    if (fflush(stdout)) return 1;
    return 0;
}
