/* SPDX-License-Identifier: MIT */
/* v5.3 FPS meter test: synthetic frame stamps -> exact HUD strings and session log line. */
#include "hs_fps.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0, g_n = 0;
static void eq(const char *got, const char *want, const char *what)
{
    g_n++;
    if (strcmp(got, want) != 0) { g_fail++; printf("FPS CHECK FAIL %s: got [%s] want [%s]\n", what, got, want); }
}

int main(void)
{
    struct hs_fps f;
    char a[32], b[32], c[32], line[640];
    const long long DMG = 16742706ll;
    long long t = 1000000000ll;
    int i, len;

    /* full speed: 120 presents, 1 emulated frame each, 4.2 ms work */
    hs_fps_start(&f, t, DMG);
    hs_fps_hud(&f, a, sizeof(a), b, sizeof(b), c, sizeof(c));
    eq(a, "FPS --", "hud before window");
    for (i = 0; i < 120; i++) {
        hs_fps_frame(&f, t, t + 3000000ll, t + 4200000ll, 1);
        t += DMG;
    }
    hs_fps_hud(&f, a, sizeof(a), b, sizeof(b), c, sizeof(c));
    eq(a, "FPS 59.7", "fps full speed");
    eq(b, "DRAW 59.7", "draw full speed");
    eq(c, "MS 4.2", "ms full speed");
    /* slow device: 25 ms work, 2 emulated frames per present (catch-up) at 33.3 ms presents */
    for (i = 0; i < 60; i++) {
        hs_fps_frame(&f, t, t + 20000000ll, t + 25000000ll, 2);
        t += 33333333ll;
    }
    hs_fps_hud(&f, a, sizeof(a), b, sizeof(b), c, sizeof(c));
    eq(a, "FPS 60.0", "fps catch-up");
    eq(b, "DRAW 30.0", "draw halved");
    eq(c, "MS 25.0", "ms slow");
    len = hs_fps_report(&f, "GB", "hs-gbc", "handshake-demo", line, sizeof(line));
    printf("%s", line);
    g_n++;
    if (len <= 0 || strstr(line, "late=60 catchup=60 draws=180 game_frames=240") == NULL ||
        strstr(line, "hist_ms<4,8,12,16.7,25,33,50,inf=0,120,0,0,0,60,0,0") == NULL ||
        strstr(line, "worst_1s_game_fps=59.5") == NULL || line[len - 1] != '\n') {
        g_fail++;
        printf("FPS CHECK FAIL report\n");
    }
    printf("FPS_TEST checks=%d failed=%d %s\n", g_n, g_fail, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
