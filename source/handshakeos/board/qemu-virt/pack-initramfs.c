/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * pack-initramfs - newc initramfs with handshake-init as /init, /dev/console, /dev/null and
 * roms/handshake-demo.gb (pure C11). Writes the uncompressed archive to stdout:
 *   pack-initramfs INIT ROM | gzip -9n > OUT.cpio.gz
 * mtime is SOURCE_DATE_EPOCH (0 if unset), never the wall clock, so the output is reproducible. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long ino, mtime;

static void add(const char *name, const unsigned char *data, unsigned long size, unsigned long mode, unsigned long maj, unsigned long mino) {
    static const char z[4];
    unsigned long typ = mode & 0170000, nl = (unsigned long)strlen(name) + 1, nlink = typ == 0040000 ? 2 : 1;
    char hdr[160];
    if (typ == 0040000 || typ == 0020000 || typ == 0060000) size = 0;
    snprintf(hdr, sizeof hdr, "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
             (unsigned)++ino, (unsigned)mode, 0u, 0u, (unsigned)nlink, (unsigned)mtime, (unsigned)size, 0u, 0u,
             (unsigned)maj, (unsigned)mino, (unsigned)nl, 0u);
    fwrite(hdr, 1, 110, stdout);
    fwrite(name, 1, nl, stdout);
    fwrite(z, 1, (4 - (110 + nl) % 4) % 4, stdout);
    if (size) fwrite(data, 1, size, stdout);
    fwrite(z, 1, (4 - size % 4) % 4, stdout);
}

static unsigned char *slurp(const char *p, unsigned long *n) {
    FILE *f = fopen(p, "rb");
    unsigned char *b = NULL;
    size_t cap = 0, len = 0, r;
    if (!f) { fprintf(stderr, "pack-initramfs: cannot open %s\n", p); exit(1); }
    do {
        if (len == cap) { cap = cap ? cap * 2 : 1 << 20; b = (unsigned char *)realloc(b, cap); if (!b) exit(1); }
        r = fread(b + len, 1, cap - len, f);
        len += r;
    } while (r);
    fclose(f);
    *n = (unsigned long)len;
    return b;
}

int main(int argc, char **argv) {
    const char *sde = getenv("SOURCE_DATE_EPOCH");
    unsigned long ni, nr;
    unsigned char *init, *rom;
    if (argc != 3) { fputs("usage: pack-initramfs INIT ROM | gzip -9n > OUT.cpio.gz\n", stderr); return 2; }
    mtime = sde ? strtoul(sde, NULL, 10) : 0;
    init = slurp(argv[1], &ni);
    rom = slurp(argv[2], &nr);
    add(".", NULL, 0, 0040755, 0, 0);
    add("dev", NULL, 0, 0040755, 0, 0);
    add("proc", NULL, 0, 0040755, 0, 0);
    add("sys", NULL, 0, 0040755, 0, 0);
    add("roms", NULL, 0, 0040755, 0, 0);
    add("dev/console", NULL, 0, 0020622, 5, 1);
    add("dev/null", NULL, 0, 0020666, 1, 3);
    add("init", init, ni, 0100755, 0, 0);
    add("roms/handshake-demo.gb", rom, nr, 0100644, 0, 0);
    add("TRAILER!!!", NULL, 0, 0, 0, 0);
    free(init); free(rom);
    if (fflush(stdout)) return 1;
    fprintf(stderr, "initramfs: init %lu bytes, rom %lu bytes, mtime %lu\n", ni, nr, mtime);
    return 0;
}
