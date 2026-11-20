/* SPDX-License-Identifier: MIT
 * Build a HandshakeOS .hsu (HSU1) package from dest=src pairs. */
#include "hs_update.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *src[HS_UPDATE_MAX_FILES];
    const char *dst[HS_UPDATE_MAX_FILES];
    char left[HS_UPDATE_MAX_FILES][64];
    char right[HS_UPDATE_MAX_FILES][512];
    int n = 0, i;
    const char *out;
    if (argc < 4) {
        fprintf(stderr, "usage: hs-mkhsu <out.hsu> <dest=src> [...]\n");
        return 2;
    }
    out = argv[1];
    for (i = 2; i < argc && n < HS_UPDATE_MAX_FILES; i++) {
        const char *eq = strchr(argv[i], '=');
        size_t ln;
        if (!eq || eq == argv[i] || eq[1] == '\0') {
            fprintf(stderr, "bad pair: %s\n", argv[i]);
            return 2;
        }
        ln = (size_t)(eq - argv[i]);
        if (ln >= 64) return 2;
        memcpy(left[n], argv[i], ln);
        left[n][ln] = '\0';
        snprintf(right[n], sizeof(right[n]), "%s", eq + 1);
        dst[n] = left[n];
        src[n] = right[n];
        n++;
    }
    if (hs_hsu_create(out, src, dst, n) != 0) {
        perror("hs_hsu_create");
        return 1;
    }
    printf("HSU created %s files=%d\n", out, n);
    return 0;
}
