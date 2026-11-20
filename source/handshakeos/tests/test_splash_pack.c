#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* SPDX-License-Identifier: MIT */
#include "hs_splash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *s) { fprintf(stderr, "FAIL %s\n", s); exit(1); }

int main(int argc, char **argv)
{
    struct hs_splash s;
    const char *path = argc > 1 ? argv[1] : "/workspace/v55/ship/assets/pack/pack.bin";
    const struct hs_splash_ent *e;
    int i, ascii;
    hs_splash_init(&s);
    if (hs_splash_load(&s, path) != 0) die(s.err);
    if (s.n != 9) die("n != 9");
    for (i = 1; i <= 8; i++) {
        char id[4];
        snprintf(id, sizeof(id), "%02d", i);
        if (hs_splash_find(&s, id) != i - 1) die(id);
        e = hs_splash_at(&s, i - 1);
        if (!e || e->type != HS_SPLASH_GRAPHIC || strcmp(e->artist, "CloudAtlas") != 0) die("cloudatlas");
        if (e->size != 640u * 480u * 2u) die("rgb565 size");
    }
    ascii = hs_splash_find(&s, "ascii-oo");
    if (ascii < 0) die("ascii-oo missing");
    e = hs_splash_at(&s, ascii);
    if (e->type != HS_SPLASH_ASCII || e->nframes != 8) die("ascii frames");
    if (strcmp(e->artist, "HandshakeOS") != 0) die("ascii artist");
    if (strstr(e->title, "HandshakeOS") == NULL) die("title");
    printf("SPLASH_PACK PASS entries=%d cloudatlas=8 ascii-oo frames=8 pack=%s\n", s.n, path);
    hs_splash_free(&s);
    printf("ALL SPLASH PACK TESTS PASSED\n");
    return 0;
}
