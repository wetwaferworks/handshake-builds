/* SPDX-License-Identifier: MIT */
/* HandshakeOS v5.3 USB TRANSFER: export the microSD FAT partition as a USB mass-storage drive
   through the Linux configfs gadget API (see include/hs_usb.h for the state machine).
   Pure C, shell thread only, zero locks. Every path comes from struct hs_usb_env. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_usb.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define USB_PATH 384
#define GADGET   "hs"
#define FUNC     "mass_storage.usb0"
#define VFAT_FLAGS (MS_NOATIME | MS_NODIRATIME)
#define VFAT_DATA  "shortname=mixed,errors=remount-ro"

static const char *const k_step_names[] = {
    "NONE", "UDC", "BLOCKDEV", "UMOUNT", "CONFIGFS", "GADGET", "LUN", "BIND", "UNBIND", "REMOUNT"
};

const char *hs_usb_step_name(hs_usb_step_t s)
{
    if ((unsigned)s < sizeof(k_step_names) / sizeof(k_step_names[0])) {
        return k_step_names[s];
    }
    return "?";
}

/* ---- real device ops ---- */
static void dev_sync(void *ctx)
{
    (void)ctx;
    sync();
}

static int dev_umount(void *ctx, const char *target)
{
    (void)ctx;
    return umount2(target, 0);
}

static int dev_mount(void *ctx, const char *src, const char *target, const char *type,
                     unsigned long flags, const char *data)
{
    (void)ctx;
    return mount(src, target, type, flags, data);
}

static void dev_sleep(void *ctx, unsigned ms)
{
    struct timespec ts;
    (void)ctx;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000l;
    (void)clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

static const char *const k_dev_blockdevs[] = { "/dev/mmcblk0p1", "/dev/mmcblk1p1", "/dev/vda1", NULL };

void hs_usb_env_device(struct hs_usb_env *env, const char *mountpoint)
{
    memset(env, 0, sizeof(*env));
    env->configfs = "/sys/kernel/config";
    env->mount_configfs = 1;
    env->udc_class = "/sys/class/udc";
    env->musb_devs = "/sys/bus/platform/devices";
    env->proc_mounts = "/proc/mounts";
    env->mountpoint = mountpoint ? mountpoint : "/mnt/sd";
    env->devs = k_dev_blockdevs;
    env->ops.ctx = NULL;
    env->ops.sync_fs = dev_sync;
    env->ops.umount_fs = dev_umount;
    env->ops.mount_fs = dev_mount;
    env->ops.sleep_ms = dev_sleep;
}

void hs_usb_init(struct hs_usb *u)
{
    memset(u, 0, sizeof(*u));
    u->state = HS_USB_IDLE;
    snprintf(u->msg, sizeof(u->msg), "USB OFF");
}

/* ---- small file helpers ---- */
static int join(char *out, size_t n, const char *a, const char *b)
{
    int r = snprintf(out, n, "%s/%s", a, b);
    if (r < 0 || (size_t)r >= n) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int write_attr(const char *path, const char *val)
{
    size_t len = strlen(val);
    long w;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    int e;

    if (fd < 0) {
        return -1;
    }
    w = write(fd, val, len);
    e = errno;
    close(fd);
    if (w != (long)len) {
        errno = w < 0 ? e : EIO;
        return -1;
    }
    return 0;
}

static int write_attr2(const char *dir, const char *name, const char *val)
{
    char p[USB_PATH];
    if (join(p, sizeof(p), dir, name) != 0) {
        return -1;
    }
    return write_attr(p, val);
}

static int read_line(const char *path, char *buf, size_t n)
{
    long r;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (n == 0) {
        return -1;
    }
    buf[0] = '\0';
    if (fd < 0) {
        return -1;
    }
    r = read(fd, buf, n - 1);
    close(fd);
    if (r < 0) {
        buf[0] = '\0';
        return -1;
    }
    buf[r] = '\0';
    while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == '\r' || buf[r - 1] == ' ')) {
        buf[--r] = '\0';
    }
    return 0;
}

static int mkdir_ok(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static int mkdir2(const char *dir, const char *name, char *out, size_t n)
{
    if (join(out, n, dir, name) != 0) {
        return -1;
    }
    return mkdir_ok(out);
}

/* Lexicographically first entry of dir (optionally with a prefix): deterministic on any fs. */
static int find_first(const char *dir, const char *prefix, char *out, size_t n)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    size_t pl = prefix ? strlen(prefix) : 0;

    out[0] = '\0';
    if (d == NULL) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || l >= n) {
            continue;
        }
        if (pl > 0 && strncmp(e->d_name, prefix, pl) != 0) {
            continue;
        }
        if (out[0] == '\0' || strcmp(e->d_name, out) < 0) {
            memcpy(out, e->d_name, l + 1);
        }
    }
    closedir(d);
    return out[0] != '\0' ? 0 : -1;
}

/* Device mounted at mountpoint, from a /proc/mounts style file. 1 = found. */
static int mounted_dev(const char *mounts, const char *mountpoint, char *dev, size_t n)
{
    char line[512];
    FILE *fp = fopen(mounts, "r");
    size_t ml = strlen(mountpoint);
    int hit = 0;

    if (fp == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t i = 0, dl;
        while (line[i] != '\0' && line[i] != ' ') {
            i++;
        }
        if (line[i] != ' ') {
            continue;
        }
        dl = i;
        if (strncmp(line + i + 1, mountpoint, ml) == 0 && line[i + 1 + ml] == ' ') {
            if (dl < n) {
                memcpy(dev, line, dl);
                dev[dl] = '\0';
                hit = 1;
            }
            break;
        }
    }
    fclose(fp);
    return hit;
}

static void musb_mode(struct hs_usb *u, const struct hs_usb_env *env, const char *mode)
{
    char p[USB_PATH];
    if (u->musb[0] == '\0' && env->musb_devs != NULL) {
        (void)find_first(env->musb_devs, "musb-hdrc", u->musb, sizeof(u->musb));
    }
    if (u->musb[0] == '\0') {
        return; /* single-role controller: nothing to switch */
    }
    if (snprintf(p, sizeof(p), "%s/%s/mode", env->musb_devs, u->musb) < (int)sizeof(p)) {
        (void)write_attr(p, mode);
    }
}

static void gadget_dir(const struct hs_usb_env *env, char *out, size_t n)
{
    snprintf(out, n, "%s/usb_gadget/" GADGET, env->configfs);
}

static int remount(struct hs_usb *u, const struct hs_usb_env *env)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (env->ops.mount_fs(env->ops.ctx, u->blockdev, env->mountpoint, "vfat", VFAT_FLAGS,
                              VFAT_DATA) == 0) {
            return 0;
        }
        if (env->ops.sleep_ms) {
            env->ops.sleep_ms(env->ops.ctx, 200);
        }
    }
    return -1;
}

/* Back to "mounted, unbound". unmounted: 1 if we unmounted the SD and must mount it again. */
static int fail(struct hs_usb *u, const struct hs_usb_env *env, hs_usb_step_t step, int err,
                int bound, int lun_set, int unmounted)
{
    char g[USB_PATH], p[USB_PATH];

    gadget_dir(env, g, sizeof(g));
    if (bound) {
        (void)write_attr2(g, "UDC", "\n");
    }
    if (lun_set && snprintf(p, sizeof(p), "%s/functions/" FUNC "/lun.0", g) < (int)sizeof(p)) {
        (void)write_attr2(p, "file", "\n");
    }
    if (unmounted) {
        env->ops.sync_fs(env->ops.ctx);
        musb_mode(u, env, "otg");
        (void)remount(u, env);
    }
    u->state = HS_USB_ERROR;
    u->fail_step = step;
    u->fail_errno = err;
    snprintf(u->msg, sizeof(u->msg), "USB FAILED: %s (errno %d)", hs_usb_step_name(step), err);
    return -1;
}

static int gadget_setup(const struct hs_usb_env *env)
{
    char g[USB_PATH], d[USB_PATH], d2[USB_PATH], f[USB_PATH], l[USB_PATH];

    if (join(d, sizeof(d), env->configfs, "usb_gadget") != 0 || mkdir_ok(d) != 0) {
        return -1;
    }
    gadget_dir(env, g, sizeof(g));
    if (mkdir_ok(g) != 0) {
        return -1;
    }
    if (write_attr2(g, "idVendor", "0x1d6b") != 0 ||  /* Linux Foundation */
        write_attr2(g, "idProduct", "0x0104") != 0 || /* Multifunction Composite Gadget */
        write_attr2(g, "bcdDevice", "0x0503") != 0 ||
        write_attr2(g, "bcdUSB", "0x0200") != 0) {
        return -1;
    }
    if (mkdir2(g, "strings", d, sizeof(d)) != 0 || mkdir2(d, "0x409", d2, sizeof(d2)) != 0 ||
        write_attr2(d2, "manufacturer", "HandshakeOS") != 0 ||
        write_attr2(d2, "product", "HandshakeOS RG35XX SD") != 0 ||
        write_attr2(d2, "serialnumber", "HSRG35XX2024") != 0) {
        return -1;
    }
    if (mkdir2(g, "functions", d, sizeof(d)) != 0 || mkdir2(d, FUNC, f, sizeof(f)) != 0 ||
        mkdir2(f, "lun.0", l, sizeof(l)) != 0) {
        return -1;
    }
    if (write_attr2(l, "removable", "1") != 0 || write_attr2(l, "ro", "0") != 0 ||
        write_attr2(l, "nofua", "0") != 0) {
        return -1;
    }
    (void)write_attr2(l, "inquiry_string", "HANDSHK RG35XX SD CARD  0503"); /* cosmetic */
    if (mkdir2(g, "configs", d, sizeof(d)) != 0 || mkdir2(d, "c.1", d2, sizeof(d2)) != 0 ||
        write_attr2(d2, "MaxPower", "500") != 0 || mkdir2(d2, "strings", d, sizeof(d)) != 0 ||
        mkdir2(d, "0x409", l, sizeof(l)) != 0 ||
        write_attr2(l, "configuration", "HandshakeOS SD") != 0) {
        return -1;
    }
    if (join(l, sizeof(l), d2, FUNC) != 0) {
        return -1;
    }
    if (symlink(f, l) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int hs_usb_enable(struct hs_usb *u, const struct hs_usb_env *env)
{
    char g[USB_PATH], p[USB_PATH];
    int was_mounted, i;

    if (u->state == HS_USB_CONNECTED) {
        return 0;
    }
    u->fail_step = HS_USB_STEP_NONE;
    u->fail_errno = 0;
    u->host_seen = 0;
    u->unplugged = 0;
    u->udc_state[0] = '\0';
    /* 1. a device controller must exist before the card is touched */
    if (find_first(env->udc_class, NULL, u->udc, sizeof(u->udc)) != 0) {
        return fail(u, env, HS_USB_STEP_UDC, ENODEV, 0, 0, 0);
    }
    /* 2. the block device behind the mount point (or the first that exists) */
    was_mounted = mounted_dev(env->proc_mounts, env->mountpoint, u->blockdev, sizeof(u->blockdev));
    if (!was_mounted) {
        u->blockdev[0] = '\0';
        for (i = 0; env->devs != NULL && env->devs[i] != NULL; i++) {
            if (access(env->devs[i], F_OK) == 0) {
                snprintf(u->blockdev, sizeof(u->blockdev), "%s", env->devs[i]);
                break;
            }
        }
        if (u->blockdev[0] == '\0') {
            return fail(u, env, HS_USB_STEP_BLOCKDEV, ENOENT, 0, 0, 0);
        }
    }
    /* 3. configfs gadget (does not touch the card) */
    if (join(p, sizeof(p), env->configfs, "usb_gadget") != 0) {
        return fail(u, env, HS_USB_STEP_CONFIGFS, errno, 0, 0, 0);
    }
    if (access(p, F_OK) != 0 && env->mount_configfs) {
        (void)env->ops.mount_fs(env->ops.ctx, "configfs", env->configfs, "configfs", 0, NULL);
    }
    if (access(p, F_OK) != 0) {
        return fail(u, env, HS_USB_STEP_CONFIGFS, ENOENT, 0, 0, 0);
    }
    if (gadget_setup(env) != 0) {
        return fail(u, env, HS_USB_STEP_GADGET, errno, 0, 0, 0);
    }
    /* 4. flush and release the FAT partition: never export a mounted filesystem */
    env->ops.sync_fs(env->ops.ctx);
    if (was_mounted && env->ops.umount_fs(env->ops.ctx, env->mountpoint) != 0) {
        return fail(u, env, HS_USB_STEP_UMOUNT, errno, 0, 0, 0);
    }
    env->ops.sync_fs(env->ops.ctx);
    /* 5. OTG port to peripheral (dr_mode stays "otg" in the device tree) */
    musb_mode(u, env, "peripheral");
    if (env->ops.sleep_ms) {
        env->ops.sleep_ms(env->ops.ctx, 200);
    }
    /* 6. back the LUN with the partition, 7. bind to the controller */
    gadget_dir(env, g, sizeof(g));
    if (snprintf(p, sizeof(p), "%s/functions/" FUNC "/lun.0", g) >= (int)sizeof(p) ||
        write_attr2(p, "file", u->blockdev) != 0) {
        return fail(u, env, HS_USB_STEP_LUN, errno, 0, 1, 1);
    }
    if (write_attr2(g, "UDC", u->udc) != 0) {
        return fail(u, env, HS_USB_STEP_BIND, errno, 0, 1, 1);
    }
    u->state = HS_USB_CONNECTED;
    snprintf(u->msg, sizeof(u->msg), "USB ON: %.40s via %.40s", u->blockdev, u->udc);
    return 0;
}

int hs_usb_disable(struct hs_usb *u, const struct hs_usb_env *env)
{
    char g[USB_PATH], p[USB_PATH];
    int unbind_ok;

    if (u->state != HS_USB_CONNECTED) {
        return 0;
    }
    gadget_dir(env, g, sizeof(g));
    unbind_ok = write_attr2(g, "UDC", "\n") == 0;
    if (!unbind_ok) {
        u->fail_step = HS_USB_STEP_UNBIND;
        u->fail_errno = errno;
    }
    if (snprintf(p, sizeof(p), "%s/functions/" FUNC "/lun.0", g) < (int)sizeof(p)) {
        (void)write_attr2(p, "file", "\n");
    }
    env->ops.sync_fs(env->ops.ctx);
    musb_mode(u, env, "otg");
    if (remount(u, env) != 0) {
        u->state = HS_USB_ERROR;
        u->fail_step = HS_USB_STEP_REMOUNT;
        u->fail_errno = errno;
        snprintf(u->msg, sizeof(u->msg), "SD REMOUNT FAILED (errno %d)", u->fail_errno);
        return -1;
    }
    u->state = unbind_ok ? HS_USB_IDLE : HS_USB_ERROR;
    snprintf(u->msg, sizeof(u->msg), unbind_ok ? "USB OFF: SD MOUNTED" : "USB UNBIND FAILED");
    return unbind_ok ? 0 : -1;
}

int hs_usb_poll(struct hs_usb *u, const struct hs_usb_env *env)
{
    char p[USB_PATH];

    if (u->state != HS_USB_CONNECTED || u->udc[0] == '\0') {
        return 0;
    }
    if (snprintf(p, sizeof(p), "%s/%s/state", env->udc_class, u->udc) >= (int)sizeof(p) ||
        read_line(p, u->udc_state, sizeof(u->udc_state)) != 0) {
        snprintf(u->udc_state, sizeof(u->udc_state), "unknown");
        return 0;
    }
    if (strcmp(u->udc_state, "configured") == 0) {
        u->host_seen = 1;
        return 1;
    }
    if (u->host_seen && strcmp(u->udc_state, "not attached") == 0) {
        u->unplugged = 1;
    }
    return 0;
}
