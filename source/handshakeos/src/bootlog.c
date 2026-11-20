/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_bootlog.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const char *hs_bootlog_path(const char *mountpoint)
{
    static char p[256];
    const char *m = (mountpoint && mountpoint[0]) ? mountpoint : "/mnt/sd";
    snprintf(p, sizeof(p), "%s/%s", m, HS_BOOTLOG_NAME);
    return p;
}

static void console(const char *s)
{
    int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd >= 0) {
        (void)!write(fd, s, strlen(s));
        close(fd);
    }
}

int hs_bootlog_append(const char *path, const char *line)
{
    char buf[512];
    int fd, len, n;
    if (path == NULL || line == NULL) {
        errno = EINVAL;
        return -1;
    }
    n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n < 0) return -1;
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        char msg[320];
        snprintf(msg, sizeof(msg), "HANDSHAKE-BOOT.log append failed path=%s errno=%d\n", path, errno);
        console(msg);
        return -1;
    }
    len = (int)write(fd, buf, (size_t)n);
    (void)fsync(fd);
    close(fd);
    if (len != n) {
        console("HANDSHAKE-BOOT.log short write\n");
        errno = EIO;
        return -1;
    }
    return 0;
}

int hs_bootlog_last(const char *path, char *out, size_t n)
{
    char buf[4096];
    long r, i, start, end;
    int fd;
    if (out == NULL || n == 0) return -1;
    out[0] = '\0';
    if (path == NULL) return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    r = lseek(fd, 0, SEEK_END);
    if (r < 0) { close(fd); return -1; }
    if (r > (long)sizeof(buf)) {
        if (lseek(fd, r - (long)sizeof(buf), SEEK_SET) < 0) { close(fd); return -1; }
        r = read(fd, buf, sizeof(buf));
    } else {
        if (lseek(fd, 0, SEEK_SET) < 0) { close(fd); return -1; }
        r = read(fd, buf, sizeof(buf));
    }
    close(fd);
    if (r <= 0) return -1;
    end = r;
    while (end > 0 && (buf[end - 1] == '\n' || buf[end - 1] == '\r')) end--;
    if (end == 0) return -1;
    start = end;
    while (start > 0 && buf[start - 1] != '\n') start--;
    i = end - start;
    if ((size_t)i >= n) i = (long)n - 1;
    memcpy(out, buf + start, (size_t)i);
    out[i] = '\0';
    return 0;
}

int hs_bootlog_boot_line(char *out, size_t n, const char *mode, int wait_ms,
                         const char *kernel, const char *modules)
{
    if (out == NULL || n == 0) return -1;
    return snprintf(out, n, "boot mode=%s wait_ms=%d kernel=%s modules=%s",
                    mode ? mode : "headless",
                    wait_ms,
                    kernel ? kernel : "unknown",
                    modules ? modules : "idle");
}
