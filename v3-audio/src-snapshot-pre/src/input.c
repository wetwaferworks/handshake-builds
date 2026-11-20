/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#include "hs_input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *const k_btn_names[HS_BTN_COUNT] = {
    "Up", "Down", "Left", "Right",
    "A", "B", "X", "Y",
    "L1", "R1", "L2", "R2",
    "Start", "Select", "Menu",
    "Vol+", "Vol-", "Power"
};

const char *hs_btn_name(hs_btn_t btn)
{
    if ((unsigned)btn >= (unsigned)HS_BTN_COUNT) {
        return "?";
    }
    return k_btn_names[btn];
}

static int map_code(uint16_t type, uint16_t code, hs_btn_t *out)
{
    if (type != EV_KEY) {
        return 0;
    }

    switch (code) {
    case BTN_DPAD_UP:
    case KEY_UP:
        *out = HS_BTN_UP;
        return 1;
    case BTN_DPAD_DOWN:
    case KEY_DOWN:
        *out = HS_BTN_DOWN;
        return 1;
    case BTN_DPAD_LEFT:
    case KEY_LEFT:
        *out = HS_BTN_LEFT;
        return 1;
    case BTN_DPAD_RIGHT:
    case KEY_RIGHT:
        *out = HS_BTN_RIGHT;
        return 1;
    case BTN_EAST:      /* physical A */
    case KEY_ENTER:
    case KEY_Z:
        *out = HS_BTN_A;
        return 1;
    case BTN_SOUTH:     /* physical B */
    case KEY_ESC:
    case KEY_X:
        *out = HS_BTN_B;
        return 1;
    case BTN_NORTH:     /* physical X */
    case KEY_A:
        *out = HS_BTN_X;
        return 1;
    case BTN_WEST:      /* physical Y */
    case KEY_S:
        *out = HS_BTN_Y;
        return 1;
    case BTN_TL:
    case KEY_Q:
    case KEY_TAB:
        *out = HS_BTN_L1;
        return 1;
    case BTN_TR:
    case KEY_E:
        *out = HS_BTN_R1;
        return 1;
    case BTN_TL2:
        *out = HS_BTN_L2;
        return 1;
    case BTN_TR2:
        *out = HS_BTN_R2;
        return 1;
    case BTN_START:
    case KEY_RIGHTCTRL:
        *out = HS_BTN_START;
        return 1;
    case BTN_SELECT:
    case KEY_RIGHTSHIFT:
        *out = HS_BTN_SELECT;
        return 1;
    case BTN_MODE:
    case KEY_F1:
        *out = HS_BTN_MENU;
        return 1;
    case KEY_VOLUMEUP:
        *out = HS_BTN_VOLUP;
        return 1;
    case KEY_VOLUMEDOWN:
        *out = HS_BTN_VOLDOWN;
        return 1;
    case KEY_POWER:
    case KEY_P:
        *out = HS_BTN_POWER;
        return 1;
    default:
        return 0;
    }
}

int hs_input_open(struct hs_input *in)
{
    DIR *dir;
    struct dirent *ent;
    char path[320];
    int fd;

    if (in == NULL) {
        return -EINVAL;
    }
    memset(in, 0, sizeof(*in));

    dir = opendir("/dev/input");
    if (dir == NULL) {
        return 0; /* headless is fine */
    }

    while ((ent = readdir(dir)) != NULL && in->n_fds < HS_INPUT_MAX_FDS) {
        if (strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        in->fds[in->n_fds++] = fd;
    }
    closedir(dir);
    return 0;
}

void hs_input_close(struct hs_input *in)
{
    int i;

    if (in == NULL) {
        return;
    }
    for (i = 0; i < in->n_fds; i++) {
        if (in->fds[i] >= 0) {
            close(in->fds[i]);
            in->fds[i] = -1;
        }
    }
    in->n_fds = 0;
}

static void apply_event(struct hs_input *in, const struct input_event *ev)
{
    hs_btn_t btn;
    uint32_t bit;

    if (!map_code(ev->type, ev->code, &btn)) {
        return;
    }
    bit = 1u << (unsigned)btn;

    if (ev->value > 0) {
        if ((in->down & bit) == 0) {
            in->pressed |= bit;
        }
        in->down |= bit;
    } else if (ev->value == 0) {
        if ((in->down & bit) != 0) {
            in->released |= bit;
        }
        in->down &= ~bit;
    }
    /* ev->value == 2 is key repeat; ignore for edge triggers. */
}

int hs_input_poll(struct hs_input *in, int timeout_ms)
{
    return hs_input_poll_ns(in, timeout_ms < 0 ? -1 : (int64_t)timeout_ms * 1000000ll);
}

/* v2: nanosecond timeout via ppoll(), so frame deadlines are not rounded
   down to whole milliseconds as poll() did in v1. */
int hs_input_poll_ns(struct hs_input *in, int64_t timeout_ns)
{
    struct timespec ts;
    struct pollfd pfds[HS_INPUT_MAX_FDS];
    struct input_event ev;
    int i, n, r;
    ssize_t got;

    if (in == NULL) {
        return -EINVAL;
    }
    in->pressed = 0;
    in->released = 0;

    if (in->n_fds <= 0) {
        return 0;
    }

    for (i = 0; i < in->n_fds; i++) {
        pfds[i].fd = in->fds[i];
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }

    if (timeout_ns >= 0) {
        ts.tv_sec = (time_t)(timeout_ns / 1000000000ll);
        ts.tv_nsec = (long)(timeout_ns % 1000000000ll);
    }
    r = ppoll(pfds, (nfds_t)in->n_fds, timeout_ns >= 0 ? &ts : NULL, NULL);
    if (r < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -errno;
    }
    if (r == 0) {
        return 0;
    }

    n = 0;
    for (i = 0; i < in->n_fds; i++) {
        if ((pfds[i].revents & POLLIN) == 0) {
            continue;
        }
        for (;;) {
            got = read(in->fds[i], &ev, sizeof(ev));
            if (got < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                break;
            }
            if (got != (ssize_t)sizeof(ev)) {
                break;
            }
            apply_event(in, &ev);
            n++;
        }
    }
    return n;
}

int hs_input_down(const struct hs_input *in, hs_btn_t btn)
{
    if (in == NULL || (unsigned)btn >= (unsigned)HS_BTN_COUNT) {
        return 0;
    }
    return (in->down & (1u << (unsigned)btn)) != 0;
}

int hs_input_pressed(const struct hs_input *in, hs_btn_t btn)
{
    if (in == NULL || (unsigned)btn >= (unsigned)HS_BTN_COUNT) {
        return 0;
    }
    return (in->pressed & (1u << (unsigned)btn)) != 0;
}
