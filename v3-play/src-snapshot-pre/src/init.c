/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_battery.h"
#include "hs_fb.h"
#include "hs_handshake.h"
#include "hs_input.h"
#include "hs_scoreboard.h"
#include "hs_ui.h"
#include "hs_gb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  --backend=auto|drm|fbdev|headless\n"
            "  --theme=purple|green\n"
            "  --snapshot=PATH.png\n"
            "  --snapshot-both[=DIR]   write preview-purple.png and preview-green.png\n"
            "  --workers=N             default 3 (the 3+1 seating)\n"
            "  --as-init               mount proc/sys/dev (when pid 1)\n"
            "  --once                  run the scoreboard, draw one frame, exit\n"
            "  --serial-scoreboard     print handshake (and GB) ledger to console\n"
            "  --rom=PATH.gb           optional ROM for a PID-1 / serial GB run\n"
            "  --pace=60|30|24|16      presented frame rate (default 60 = 59.73 Hz DMG)\n"
            "  --dial=slow|med|fast|full  dial/telemetry display divider (default slow)\n"
            "  --confirm=poweroff|reboot  snapshot with the confirm overlay up\n"
            "  --bench-frames=N        headless: time N live frames per stage, exit\n"
            "  --live-seconds=S        run the live paced loop S seconds, print timing\n"
            "  --bgr                   swap R/B at scanout (also handshakeos.bgr)\n",
            argv0);
}

static hs_theme_id_t parse_theme(const char *s)
{
    if (s && (strcmp(s, "green") == 0 || strcmp(s, "matrix") == 0)) {
        return HS_THEME_GREEN;
    }
    return HS_THEME_PURPLE;
}

static int mount_essentials(void)
{
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    (void)mount("proc", "/proc", "proc", 0, NULL);
    (void)mount("sysfs", "/sys", "sysfs", 0, NULL);
    (void)mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    return 0;
}


static void say(const char *msg)
{
    FILE *c = fopen("/dev/console", "w");

    /* One stream only: on virt, stdout, stderr, and /dev/console are the
       same UART, and writing all three triples the banner. */
    if (c != NULL) {
        fputs(msg, c);
        fflush(c);
        fclose(c);
        return;
    }
    fputs(msg, stdout);
    fflush(stdout);
}


/* Live on-glass frame timing (H700 or host): a 128-frame ring of loop
   periods, summarised every 30 frames. Single-threaded, no locks. */
#define LIVE_RING 128
struct live_timing {
    uint64_t frames;
    int64_t  last_ns;
    int64_t  ring[LIVE_RING];
    int      n;
    int      pos;
    int64_t  p50_ns;
    int64_t  p99_ns;
    int64_t  max_ns;
};

static int64_t mono_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;

    return (x > y) - (x < y);
}

/* Record the period between successive frame starts (presentation
   instants), not draw end to draw end. */
static void live_tick(struct live_timing *lt, int64_t now)
{
    if (lt->last_ns != 0) {
        lt->ring[lt->pos] = now - lt->last_ns;
        lt->pos = (lt->pos + 1) % LIVE_RING;
        if (lt->n < LIVE_RING) {
            lt->n++;
        }
    }
    lt->last_ns = now;
    lt->frames++;
}

static void live_summarise(struct live_timing *lt)
{
    int64_t tmp[LIVE_RING];
    int k = lt->n;

    if (k <= 0) {
        return;
    }
    memcpy(tmp, lt->ring, sizeof(int64_t) * (size_t)k);
    qsort(tmp, (size_t)k, sizeof(int64_t), cmp_i64);
    lt->p50_ns = tmp[k / 2];
    lt->p99_ns = tmp[(k * 99) / 100 < k ? (k * 99) / 100 : k - 1];
    lt->max_ns = tmp[k - 1];
}

/* Gather input until an absolute CLOCK_MONOTONIC deadline. Edges from
   every poll in the window are OR-ed so nothing pressed early is lost. */
static void pace_wait(struct hs_input *in, int64_t deadline)
{
    uint32_t pressed = 0, released = 0;

    for (;;) {
        int64_t rem = deadline - mono_ns();
        if (rem <= 0) {
            break;
        }
        if (in->n_fds > 0) {
            (void)hs_input_poll_ns(in, rem);
            pressed |= in->pressed;
            released |= in->released;
        } else {
            struct timespec ts;
            ts.tv_sec = (time_t)(deadline / 1000000000ll);
            ts.tv_nsec = (long)(deadline % 1000000000ll);
            (void)clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        }
    }
    if (in->n_fds > 0) {
        (void)hs_input_poll_ns(in, 0); /* drain anything that just landed */
        pressed |= in->pressed;
        released |= in->released;
    }
    in->pressed = pressed;
    in->released = released;
}

static hs_pace_t parse_pace(const char *s)
{
    int hz = atoi(s);

    if (hz == 30) {
        return HS_PACE_30;
    }
    if (hz == 24) {
        return HS_PACE_24;
    }
    if (hz == 16) {
        return HS_PACE_16;
    }
    return HS_PACE_60;
}

static hs_dial_speed_t parse_dial(const char *s)
{
    if (strcmp(s, "med") == 0 || strcmp(s, "MED") == 0) {
        return HS_DIAL_MED;
    }
    if (strcmp(s, "fast") == 0 || strcmp(s, "FAST") == 0) {
        return HS_DIAL_FAST;
    }
    if (strcmp(s, "full") == 0 || strcmp(s, "FULL") == 0) {
        return HS_DIAL_FULL;
    }
    return HS_DIAL_SLOW;
}

static int cmdline_has(const char *key)
{
    char buf[4096];
    FILE *fp = fopen("/proc/cmdline", "r");
    size_t n;

    if (fp == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (n == 0) {
        return 0;
    }
    buf[n] = '\0';
    return strstr(buf, key) != NULL;
}

static void do_power(hs_action_t act, int as_init)
{
    if (!as_init) {
        fprintf(stderr, "host: %s requested, exiting\n",
                act == HS_ACTION_REBOOT ? "reboot" : "poweroff");
        exit(0);
    }
    sync();
    if (act == HS_ACTION_REBOOT) {
        reboot(RB_AUTOBOOT);
    } else {
        reboot(RB_POWER_OFF);
    }
}

static int render_snapshot(struct hs_fb *fb, struct hs_ui *ui,
                           struct hs_hs *hs, const struct hs_jitter_stats *jit,
                           const struct hs_battery *bat, struct hs_gb *gb,
                           const char *path)
{
    int rc;

    if (gb != NULL) {
        struct hs_gb_stats gs;
        hs_gb_stats(gb, &gs);
        ui->live.have_gb = 1;
        ui->live.gb_fb = hs_gb_framebuffer(gb);
        ui->live.gb_frames = gs.frames;
        ui->live.gb_xruns = gs.xruns;
        ui->live.gb_overruns = gs.overruns;
        if (!ui->live.have_timing) {
            /* Preview: show the nominal slot for the active preset. */
            ui->live.have_timing = 1;
            ui->live.p50_ns = hs_pace_ns(ui->pace);
            ui->live.p99_ns = hs_pace_ns(ui->pace);
        }
    }
    hs_ui_draw(fb, ui, hs, jit, bat);
    hs_fb_flip(fb);
    rc = hs_fb_snapshot_png(fb, path);
    if (rc != 0) {
        fprintf(stderr, "snapshot %s failed: %d\n", path, rc);
        return rc;
    }
    fprintf(stderr, "wrote %s\n", path);
    return 0;
}

/* Headless per-stage frame cost: the same work the live loop does
   (battery read, GB frame, full UI draw, scanout copy), timed separately. */
static int run_bench(struct hs_fb *fb, struct hs_ui *ui, struct hs_hs *hs,
                     const struct hs_jitter_stats *jit, struct hs_battery *bat,
                     struct hs_gb *gb, long frames)
{
    int64_t t_bat = 0, t_gb = 0, t_draw = 0, t_flip = 0, t0, t1;
    long f;
    void *hw = malloc((size_t)fb->w * (size_t)fb->h * 4u);

    if (hw != NULL) {
        /* emulate a same-size XRGB8888 scanout so flip() does its memcpy */
        fb->hw_map = hw;
        fb->hw_map_len = 0; /* not munmap'ed by close */
        fb->hw_w = fb->w;
        fb->hw_h = fb->h;
        fb->hw_stride = fb->stride_bytes;
    }
    for (f = 0; f < frames; f++) {
        t0 = mono_ns();
        hs_battery_read(bat);
        t1 = mono_ns(); t_bat += t1 - t0; t0 = t1;
        if (gb != NULL) {
            (void)hs_gb_run_frames(gb, 1);
            ui->live.have_gb = 1;
            ui->live.gb_fb = hs_gb_framebuffer(gb);
        }
        t1 = mono_ns(); t_gb += t1 - t0; t0 = t1;
        ui->live_beat = (hs_beat_t)(1 + (int)(f & 3));
        hs_ui_draw(fb, ui, hs, jit, bat);
        t1 = mono_ns(); t_draw += t1 - t0; t0 = t1;
        hs_fb_flip(fb);
        t1 = mono_ns(); t_flip += t1 - t0;
    }
    printf("bench frames=%ld gb=%d  battery %lld ns  gb %lld ns  draw %lld ns  flip %lld ns  "
           "total %lld ns/frame\n",
           frames, gb != NULL,
           (long long)(t_bat / frames), (long long)(t_gb / frames),
           (long long)(t_draw / frames), (long long)(t_flip / frames),
           (long long)((t_bat + t_gb + t_draw + t_flip) / frames));
    fb->hw_map = NULL;
    free(hw);
    return 0;
}

int main(int argc, char **argv)
{
    const char *backend = "auto";
    const char *snapshot = NULL;
    const char *snap_dir = NULL;
    int snap_both = 0;
    int as_init = 0;
    int once = 0;
    int serial_sb = 0;
    int workers = 3;
    const char *rom = NULL;
    hs_theme_id_t theme = HS_THEME_PURPLE;
    int i;
    struct hs_fb fb;
    struct hs_ui ui;
    struct hs_hs hs;
    struct hs_input in;
    struct hs_battery bat;
    struct hs_jitter_stats jit;
    uint64_t hash;
    int running = 1;
    struct hs_gb *gb = NULL;
    struct live_timing lt;
    hs_pace_t pace = HS_PACE_60;
    hs_dial_speed_t dial = HS_DIAL_SLOW;
    hs_confirm_t snap_confirm = HS_CONFIRM_NONE;
    long bench_frames = 0;
    double live_seconds = 0.0;
    int force_bgr = 0;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--backend=", 10) == 0) {
            backend = argv[i] + 10;
        } else if (strncmp(argv[i], "--theme=", 8) == 0) {
            theme = parse_theme(argv[i] + 8);
        } else if (strncmp(argv[i], "--snapshot=", 11) == 0) {
            snapshot = argv[i] + 11;
            once = 1;
        } else if (strcmp(argv[i], "--snapshot-both") == 0) {
            snap_both = 1;
            snap_dir = ".";
            once = 1;
        } else if (strncmp(argv[i], "--snapshot-both=", 16) == 0) {
            snap_both = 1;
            snap_dir = argv[i] + 16;
            once = 1;
        } else if (strncmp(argv[i], "--workers=", 10) == 0) {
            workers = atoi(argv[i] + 10);
        } else if (strcmp(argv[i], "--as-init") == 0) {
            as_init = 1;
        } else if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[i], "--serial-scoreboard") == 0) {
            serial_sb = 1;
        } else if (strncmp(argv[i], "--rom=", 6) == 0) {
            rom = argv[i] + 6;
        } else if (strncmp(argv[i], "--pace=", 7) == 0) {
            pace = parse_pace(argv[i] + 7);
        } else if (strncmp(argv[i], "--dial=", 7) == 0) {
            dial = parse_dial(argv[i] + 7);
        } else if (strncmp(argv[i], "--confirm=", 10) == 0) {
            snap_confirm = strcmp(argv[i] + 10, "reboot") == 0 ? HS_CONFIRM_REBOOT
                                                                : HS_CONFIRM_POWEROFF;
        } else if (strncmp(argv[i], "--bench-frames=", 15) == 0) {
            bench_frames = atol(argv[i] + 15);
            backend = "headless";
        } else if (strncmp(argv[i], "--live-seconds=", 15) == 0) {
            live_seconds = atof(argv[i] + 15);
        } else if (strcmp(argv[i], "--bgr") == 0) {
            force_bgr = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (as_init || getpid() == 1) {
        as_init = 1;
        mount_essentials();
        if (cmdline_has("handshakeos.serial") || cmdline_has("hs.serial") ||
            cmdline_has("handshakeos.once") || cmdline_has("hs.once")) {
            serial_sb = 1;
        }
        if (cmdline_has("handshakeos.once") || cmdline_has("hs.once")) {
            once = 1;
        }
        if (cmdline_has("handshakeos.bgr")) {
            force_bgr = 1;
        }
        if (cmdline_has("handshakeos.pace=30")) {
            pace = HS_PACE_30;
        } else if (cmdline_has("handshakeos.pace=24")) {
            pace = HS_PACE_24;
        } else if (cmdline_has("handshakeos.pace=16")) {
            pace = HS_PACE_16;
        }
        if (rom == NULL) {
            /* Prefer a packed legal demo ROM when we are PID 1. */
            if (access("/roms/handshake-demo.gb", R_OK) == 0) {
                rom = "/roms/handshake-demo.gb";
            } else if (access("roms/handshake-demo.gb", R_OK) == 0) {
                rom = "roms/handshake-demo.gb";
            }
        }
        serial_sb = 1;
    }

    /* Host snapshots always go through the headless backend so we get a
       crisp 640x480 frame, even on a machine with a different display. */
    if (snapshot || snap_both) {
        backend = "headless";
    }

    if (hs_fb_open(&fb, backend) != 0) {
        fprintf(stderr, "fb open (%s) failed, trying headless\n", backend);
        if (hs_fb_open(&fb, "headless") != 0) {
            fprintf(stderr, "headless fb open failed\n");
            return 1;
        }
    }

    if (force_bgr) {
        hs_fb_swap_rb(&fb);
    }
    {
        char fl[256];
        snprintf(fl, sizeof(fl), "HandshakeOS v2 scanout: %s\n", hs_fb_describe(&fb));
        if (as_init) {
            say(fl);
        } else {
            fputs(fl, stderr);
        }
    }

    if (hs_scoreboard_setup(&hs, workers) != 0) {
        fprintf(stderr, "handshake setup failed\n");
        hs_fb_close(&fb);
        return 1;
    }
    hash = hs_scoreboard_run(&hs);
    hs_hs_jitter(&hs, &jit);
    fprintf(stderr, "ledger %016llx  epoch %llu  workers %d  p50 %lld ns  overruns %llu\n",
            (unsigned long long)hash,
            (unsigned long long)hs_hs_commit_epoch(&hs),
            hs_hs_worker_count(&hs),
            (long long)jit.p50_ns,
            (unsigned long long)jit.overruns);

    if (serial_sb) {
        char line[512];
        struct hs_gb_stats gs;

        snprintf(line, sizeof(line),
                 "\n======== HandshakeOS PID %d ========\n"
                 "Timing Handshake 3+1\n"
                 "ledger %016llx\n"
                 "epoch  %llu\n"
                 "workers %d\n"
                 "overruns %llu\n",
                 (int)getpid(),
                 (unsigned long long)hash,
                 (unsigned long long)hs_hs_commit_epoch(&hs),
                 hs_hs_worker_count(&hs),
                 (unsigned long long)jit.overruns);
        say(line);

        if (rom != NULL) {
            memset(&gs, 0, sizeof(gs));
            if (hs_gb_scoreboard(rom, workers, 90, &gs) == 0) {
                snprintf(line, sizeof(line),
                         "GB rom %s\n"
                         "GB ledger %016llx\n"
                         "GB frame  %016llx\n"
                         "GB audio  %016llx\n"
                         "GB xruns  %llu\n"
                         "GB overruns %llu\n",
                         rom,
                         (unsigned long long)gs.ledger_hash,
                         (unsigned long long)gs.frame_crc,
                         (unsigned long long)gs.audio_crc,
                         (unsigned long long)gs.xruns,
                         (unsigned long long)gs.overruns);
                say(line);
            } else {
                say("GB scoreboard failed\n");
            }
        }
        say("================================\n");
        if (as_init && once) {
            /* QEMU proof: print, then reboot so -no-reboot exits cleanly. */
            sync();
            reboot(RB_AUTOBOOT);
        }
    }

    hs_ui_init(&ui, theme);
    ui.pace = pace;
    ui.dial = dial;
    hs_battery_clear(&bat);
    hs_battery_read(&bat);
    hs_input_open(&in);

    if (snap_both) {
        char path[512];
        int rc = 0;
        ui.theme = HS_THEME_PURPLE;
        ui.confirm = snap_confirm;
        snprintf(path, sizeof(path), "%s/preview-purple.png", snap_dir[0] ? snap_dir : ".");
        rc |= render_snapshot(&fb, &ui, &hs, &jit, &bat, NULL, path);
        ui.theme = HS_THEME_GREEN;
        snprintf(path, sizeof(path), "%s/preview-green.png", snap_dir[0] ? snap_dir : ".");
        rc |= render_snapshot(&fb, &ui, &hs, &jit, &bat, NULL, path);
        hs_input_close(&in);
        hs_hs_fini(&hs);
        hs_fb_close(&fb);
        return rc == 0 ? 0 : 1;
    }

    /* Live glass: keep the Deep Purple shell up and run the legal demo
       ROM unless handshakeos.once / --once asked us to print-and-exit. A host
       --snapshot with --rom renders the GB column after 90 frames. */
    memset(&lt, 0, sizeof(lt));
    if (rom != NULL && (!once || snapshot != NULL || bench_frames > 0)) {
        gb = hs_gb_create();
        if (gb == NULL || hs_gb_load_rom(gb, rom) != 0 ||
            hs_gb_start(gb, workers) != 0) {
            if (gb != NULL) {
                hs_gb_destroy(gb);
                gb = NULL;
            }
            fprintf(stderr, "GB demo unavailable (%s)\n", rom);
        } else {
            ui.reserve_gb = 1;
            if (snapshot != NULL) {
                (void)hs_gb_run_frames(gb, 90);
            }
        }
    }

    if (bench_frames > 0) {
        int rc = run_bench(&fb, &ui, &hs, &jit, &bat, gb, bench_frames);
        if (gb != NULL) {
            hs_gb_stop(gb);
            hs_gb_destroy(gb);
        }
        hs_input_close(&in);
        hs_hs_fini(&hs);
        hs_fb_close(&fb);
        return rc;
    }

    if (snapshot) {
        ui.confirm = snap_confirm;
        int rc = render_snapshot(&fb, &ui, &hs, &jit, &bat, gb, snapshot);
        if (gb != NULL) {
            hs_gb_stop(gb);
            hs_gb_destroy(gb);
        }
        hs_input_close(&in);
        hs_hs_fini(&hs);
        hs_fb_close(&fb);
        return rc == 0 ? 0 : 1;
    }

    /* v2 live loop. Pacing is deadline based: frame k is presented at
       t0 + k * slot (CLOCK_MONOTONIC), and input is gathered with ppoll()
       until that deadline. v1 waited a full slot measured from the END of
       the previous frame, so every frame cost slot + draw time
       (16.74 ms + ~8 ms = the 24.8 ms p50 seen on the H700). */
    {
        int64_t slot = hs_pace_ns(ui.pace);
        hs_pace_t cur_pace = ui.pace;
        int64_t deadline = mono_ns() + slot;
        int64_t gb_t0 = mono_ns();
        uint64_t gb_due_done = 0;
        int64_t next_beat = 0, next_tel = 0, next_bat = 0;
        int beat_i = 0;
        int64_t live_stop = live_seconds > 0.0
                                ? mono_ns() + (int64_t)(live_seconds * 1e9) : 0;
        uint64_t late = 0;

        while (running) {
            hs_action_t act = HS_ACTION_NONE;
            int64_t now;

            pace_wait(&in, deadline);
            now = mono_ns();
            deadline += slot;
            if (now > deadline) {
                /* Fell more than a slot behind (e.g. a stall): resync
                   instead of bursting frames to catch up. */
                deadline = now + slot;
                late++;
            }
            if (live_stop != 0 && now >= live_stop) {
                struct hs_gb_stats gs;
                memset(&gs, 0, sizeof(gs));
                if (gb != NULL) {
                    hs_gb_stats(gb, &gs);
                }
                live_summarise(&lt);
                printf("live pace=%s slot=%lld ns frames=%llu p50=%lld ns p99=%lld ns "
                       "max=%lld ns late=%llu gb_frames=%llu gb_xruns=%llu gb_overruns=%llu\n",
                       hs_pace_name(ui.pace), (long long)slot,
                       (unsigned long long)lt.frames, (long long)lt.p50_ns,
                       (long long)lt.p99_ns, (long long)lt.max_ns,
                       (unsigned long long)late, (unsigned long long)gs.frames,
                       (unsigned long long)gs.xruns, (unsigned long long)gs.overruns);
                break;
            }

            if (hs_input_pressed(&in, HS_BTN_L1)) {
                hs_ui_cycle_theme(&ui, -1);
            }
            if (hs_input_pressed(&in, HS_BTN_R1)) {
                hs_ui_cycle_theme(&ui, 1);
            }
            if (hs_input_pressed(&in, HS_BTN_Y) && ui.confirm == HS_CONFIRM_NONE) {
                hs_ui_cycle_dial(&ui, 1);
                next_beat = 0;
                next_tel = 0;
            }
            if (hs_input_pressed(&in, HS_BTN_X) && ui.confirm == HS_CONFIRM_NONE) {
                hs_ui_cycle_pace(&ui, 1);
            }
            if (hs_input_pressed(&in, HS_BTN_LEFT)) {
                act = hs_ui_move(&ui, -1, 0);
            }
            if (hs_input_pressed(&in, HS_BTN_RIGHT)) {
                act = hs_ui_move(&ui, 1, 0);
            }
            if (hs_input_pressed(&in, HS_BTN_UP)) {
                act = hs_ui_move(&ui, 0, -1);
            }
            if (hs_input_pressed(&in, HS_BTN_DOWN)) {
                act = hs_ui_move(&ui, 0, 1);
            }
            if (hs_input_pressed(&in, HS_BTN_A)) {
                hs_pace_t before = ui.pace;
                hs_dial_speed_t dbefore = ui.dial;
                act = hs_ui_activate(&ui);
                if (ui.dial != dbefore) {
                    next_beat = 0;
                    next_tel = 0;
                }
                (void)before;
            }
            if (hs_input_pressed(&in, HS_BTN_B) || hs_input_pressed(&in, HS_BTN_MENU)) {
                act = hs_ui_cancel(&ui);
            }
            if (hs_input_pressed(&in, HS_BTN_POWER)) {
                /* First tap asks, second tap (or A) powers off. */
                if (ui.confirm == HS_CONFIRM_POWEROFF) {
                    act = hs_ui_activate(&ui);
                } else {
                    ui.selected = HS_TILE_POWEROFF;
                    act = hs_ui_activate(&ui);
                }
            }
            /* SELECT+START: clean reboot straight away (no confirm). */
            if (hs_input_down(&in, HS_BTN_SELECT) &&
                hs_input_down(&in, HS_BTN_START)) {
                act = HS_ACTION_REBOOT;
            }
            if (act == HS_ACTION_POWEROFF || act == HS_ACTION_REBOOT) {
                if (gb != NULL) {
                    hs_gb_stop(gb);
                    hs_gb_destroy(gb);
                    gb = NULL;
                }
                do_power(act, as_init);
                running = 0;
            }
            if (!running) {
                break;
            }
            if (ui.pace != cur_pace) {
                cur_pace = ui.pace;
                slot = hs_pace_ns(cur_pace);
                deadline = now + slot;
                memset(&lt, 0, sizeof(lt)); /* fresh timing for the new preset */
                ui.live.have_timing = 0;
            }

            /* Battery: sysfs (and, with the AXP717 driver, PMIC I2C/RSB
               reads) every 2 s instead of every frame. */
            if (now >= next_bat) {
                hs_battery_read(&bat);
                next_bat = now + 2000000000ll;
            }

            /* GB keeps native 59.73 Hz game time at every presentation
               rate: run however many DMG frames are due (<= 4 per present). */
            if (gb != NULL) {
                uint64_t due = (uint64_t)((now - gb_t0) / 16742706ll) + 1u;
                uint64_t n = due > gb_due_done ? due - gb_due_done : 0;
                if (n > 4) {
                    n = 4;
                    gb_due_done = due - 4;
                }
                if (n > 0) {
                    (void)hs_gb_run_frames(gb, n);
                    gb_due_done += n;
                }
                ui.live.have_gb = 1;
                ui.live.gb_fb = hs_gb_framebuffer(gb);
            }

            /* Display divider: the handshake runs at full speed; only the
               dial beat and the telemetry numbers are paced for humans. */
            if (gb != NULL) {
                int64_t per = hs_dial_period_ns(ui.dial);
                if (per == 0 || now >= next_beat) {
                    beat_i = (beat_i + 1) & 3;
                    ui.live_beat = (hs_beat_t)(1 + beat_i);
                    next_beat = now + per;
                }
                if (per == 0 || now >= next_tel) {
                    struct hs_gb_stats gs;
                    hs_gb_stats(gb, &gs);
                    ui.live.gb_frames = gs.frames;
                    ui.live.gb_xruns = gs.xruns;
                    ui.live.gb_overruns = gs.overruns;
                    live_summarise(&lt);
                    ui.live.have_timing = lt.n > 0;
                    ui.live.p50_ns = lt.p50_ns;
                    ui.live.p99_ns = lt.p99_ns;
                    ui.live.max_ns = lt.max_ns;
                    next_tel = now + (per > 0 ? per : 0);
                }
            }

            live_tick(&lt, now);
            hs_ui_draw(&fb, &ui, &hs, &jit, &bat);
            hs_fb_flip(&fb);

            if (once) {
                running = 0;
            }
        }
    }

    if (gb != NULL) {
        hs_gb_stop(gb);
        hs_gb_destroy(gb);
    }
    hs_input_close(&in);
    hs_hs_fini(&hs);
    hs_fb_close(&fb);
    return 0;
}
