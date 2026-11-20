#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/* SPDX-License-Identifier: MIT */
#include "hs_bootlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void die(const char *s) { fprintf(stderr, "FAIL %s\n", s); exit(1); }

int main(void)
{
    char dir[] = "/tmp/hs-blog-XXXXXX";
    char path[256], last[256], line[256];
    if (!mkdtemp(dir)) die("mkdtemp");
    snprintf(path, sizeof(path), "%s/HANDSHAKE-BOOT.log", dir);
    if (hs_bootlog_last(path, last, sizeof(last)) == 0) die("empty last");
    if (hs_bootlog_boot_line(line, sizeof(line), "drm", 10000, "7.3.5", "idle") < 0) die("fmt");
    if (hs_bootlog_append(path, line) != 0) die("append1");
    if (hs_bootlog_append(path, "usb modules=OK loaded=2 already=0") != 0) die("append2");
    if (hs_bootlog_last(path, last, sizeof(last)) != 0) die("last");
    if (strcmp(last, "usb modules=OK loaded=2 already=0") != 0) die(last);
    printf("BOOTLOG PASS last=%s first=%s\n", last, line);
    printf("ALL BOOTLOG TESTS PASSED\n");
    return 0;
}
