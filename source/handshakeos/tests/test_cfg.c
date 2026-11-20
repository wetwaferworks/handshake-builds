#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* SPDX-License-Identifier: MIT */
#include "hs_cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static void die(const char *s) { fprintf(stderr, "FAIL %s\n", s); exit(1); }

int main(void)
{
    char dir[] = "/tmp/hs-cfg-XXXXXX";
    char path[256], line[256];
    struct hs_cfg a, b;
    int fd, n;

    if (!mkdtemp(dir)) die("mkdtemp");
    snprintf(path, sizeof(path), "%s/HANDSHAKE.CFG", dir);

    hs_cfg_load(&a, path);
    if (!a.fallback_used || a.loaded) die("missing should fallback");
    if (strcmp(a.splash_id, "01") != 0 || a.animation != 1 || a.theme != HS_THEME_PURPLE) die("defaults");
    if (a.slot != HS_SLOT_A || a.fallback != HS_SLOT_A) die("default slot");

    snprintf(a.splash_id, sizeof(a.splash_id), "ascii-oo");
    a.animation = 0;
    a.theme = HS_THEME_GREEN;
    a.slot = HS_SLOT_B;
    a.fallback = HS_SLOT_A;
    if (hs_cfg_save(&a, path) != 0) die("save");

    memset(&b, 0, sizeof(b));
    hs_cfg_load(&b, path);
    if (!b.loaded || b.fallback_used) die("roundtrip loaded");
    if (strcmp(b.splash_id, "ascii-oo") != 0) die("splash");
    if (b.animation != 0) die("anim");
    if (b.theme != HS_THEME_GREEN) die("theme");
    if (b.slot != HS_SLOT_B || b.fallback != HS_SLOT_A) die("slots");
    printf("CFG_ROUNDTRIP PASS\n");

    /* corrupt file */
    fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) die("open corrupt");
    n = snprintf(line, sizeof(line), "this is not a config\nslot=QQ\n");
    { long wr = write(fd, line, (size_t)n); (void)wr; };
    close(fd);
    memset(&b, 0, sizeof(b));
    hs_cfg_load(&b, path);
    if (!b.fallback_used || b.loaded) die("corrupt should fallback");
    if (strcmp(b.splash_id, "01") != 0 || b.theme != HS_THEME_PURPLE) die("corrupt defaults");
    if (strcmp(b.err, "corrupt") != 0 && strcmp(b.err, "incomplete") != 0)
        printf("note corrupt err=%s\n", b.err);
    printf("CFG_CORRUPT PASS err=%s\n", b.err);
    printf("ALL CFG TESTS PASSED\n");
    return 0;
}
