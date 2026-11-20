/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hs_pngcmp - pixel-compare two PNGs as 8-bit RGB (pure C11, hs_pngread.h).
 * usage: hs-pngcmp [--size=WxH] A.png B.png
 * Prints PIXELS_IDENTICAL or DIFF (with the reason) and exits 0 / 1 (2 on usage). */
#include "hs_pngread.h"

int main(int argc, char **argv) {
    int ai = 1, ew = -1, eh = -1, aw, ah, bw, bh;
    uint8_t *a, *b;
    if (argc > 1 && strncmp(argv[1], "--size=", 7) == 0) {
        if (sscanf(argv[1] + 7, "%dx%d", &ew, &eh) != 2) return 2;
        ai = 2;
    }
    if (argc != ai + 2) { fprintf(stderr, "usage: hs-pngcmp [--size=WxH] A.png B.png\n"); return 2; }
    if (hs_png_load_rgb(argv[ai], &aw, &ah, &a)) { printf("DIFF (cannot decode %s)\n", argv[ai]); return 1; }
    if (hs_png_load_rgb(argv[ai + 1], &bw, &bh, &b)) { printf("DIFF (cannot decode %s)\n", argv[ai + 1]); free(a); return 1; }
    if ((ew >= 0 && (aw != ew || ah != eh)) || aw != bw || ah != bh) {
        printf("DIFF (size %dx%d vs %dx%d)\n", aw, ah, bw, bh); free(a); free(b); return 1;
    }
    if (memcmp(a, b, (size_t)aw * ah * 3) != 0) { printf("DIFF (pixels)\n"); free(a); free(b); return 1; }
    printf("PIXELS_IDENTICAL\n");
    free(a); free(b);
    return 0;
}
