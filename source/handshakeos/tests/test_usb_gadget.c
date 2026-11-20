/* SPDX-License-Identifier: MIT */
/* v5.3 USB TRANSFER state machine test on a simulated sysfs / configfs / mounts tree (argv[1], a
   directory inside the build tree). Runs the shipped src/usb.c with fake mount / umount / sync ops
   that check the safety rules at the moment they are called:
     - the FAT partition is never exported (lun.0/file set or UDC bound) while mounted
     - sync happens before unmount; remount happens only after unbind + LUN clear
     - every failure rolls back to "mounted, unbound" and names the failed step
   The real MUSB controller cannot be exercised here (no H700 in QEMU): this proves the PID 1 logic. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hs_usb.h"
#include "hs_pid1.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define P 512
static char R[P];          /* sim root */
static int g_fail = 0, g_checks = 0;

struct sim {
    char mounts[P], gadget[P];
    int syncs, umounts, mounts_n, mounted;
    int umount_errno;        /* !=0: umount fails with this errno */
    int mount_fail;          /* remount attempts that fail */
    int violations;          /* safety rule broken at op time */
    int sync_since_umount;
};
static struct sim S;

static void check(int ok, const char *what)
{
    g_checks++;
    if (!ok) {
        g_fail++;
        printf("USB_SIM CHECK FAIL: %s\n", what);
    }
}

static void path2(char *out, const char *a, const char *b) { if (snprintf(out, P, "%s/%s", a, b) >= P) out[0] = 0; }

static void mkdirs(const char *rel)
{
    char p[P], *s;
    path2(p, R, rel);
    for (s = p + strlen(R) + 1; *s; s++) {
        if (*s == '/') { *s = '\0'; (void)mkdir(p, 0755); *s = '/'; }
    }
    (void)mkdir(p, 0755);
}

static void put(const char *abs, const char *val)
{
    int fd = open(abs, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { (void)!write(fd, val, strlen(val)); close(fd); }
}

static void putr(const char *rel, const char *val) { char p[P]; path2(p, R, rel); put(p, val); }

static void get(const char *abs, char *buf, size_t n)
{
    long r;
    int fd = open(abs, O_RDONLY);
    buf[0] = '\0';
    if (fd < 0) return;
    r = read(fd, buf, n - 1);
    close(fd);
    if (r < 0) r = 0;
    buf[r] = '\0';
    while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == ' ')) buf[--r] = '\0';
}

static void getg(const char *rel, char *buf, size_t n) { char p[P]; path2(p, S.gadget, rel); get(p, buf, n); }

static int exported(void)
{
    char a[128], b[128];
    getg("UDC", a, sizeof(a));
    getg("functions/mass_storage.usb0/lun.0/file", b, sizeof(b));
    return a[0] != '\0' || b[0] != '\0';
}

static void f_sync(void *ctx) { (void)ctx; S.syncs++; S.sync_since_umount++; }

static int f_umount(void *ctx, const char *t)
{
    (void)ctx; (void)t;
    if (exported()) S.violations++;           /* exported while still mounted */
    if (S.sync_since_umount == 0) S.violations++; /* no sync before unmount */
    if (S.umount_errno) { errno = S.umount_errno; return -1; }
    S.umounts++;
    S.mounted = 0;
    put(S.mounts, "proc /proc proc rw 0 0\n");
    return 0;
}

static int f_mount(void *ctx, const char *src, const char *t, const char *type, unsigned long fl,
                   const char *data)
{
    char line[256];
    (void)ctx; (void)fl; (void)data;
    if (strcmp(type, "vfat") != 0) { errno = EINVAL; return -1; }
    if (exported()) S.violations++;           /* remounted while still exported */
    if (S.mount_fail > 0) { S.mount_fail--; errno = EIO; return -1; }
    S.mounts_n++;
    S.mounted = 1;
    S.sync_since_umount = 0;
    snprintf(line, sizeof(line), "proc /proc proc rw 0 0\n%s %s vfat rw,noatime 0 0\n", src, t);
    put(S.mounts, line);
    return 0;
}

static void f_sleep(void *ctx, unsigned ms) { (void)ctx; (void)ms; }

static void reset_sim(void)
{
    char p[P];
    memset(&S, 0, sizeof(S));
    path2(S.mounts, R, "proc_mounts");
    path2(p, R, "config/usb_gadget/hs");
    snprintf(S.gadget, sizeof(S.gadget), "%s", p);
    put(S.mounts, "proc /proc proc rw 0 0\n/dev/mmcblk0p1 /mnt/sd vfat rw,noatime 0 0\n");
    S.mounted = 1;
    putr("sys/class/udc/musb-hdrc.2.auto/state", "not attached\n");
    putr("sys/bus/platform/devices/musb-hdrc.2.auto/mode", "otg\n");
}

static void env_for(struct hs_usb_env *e, char *cfg, char *udc, char *musb)
{
    memset(e, 0, sizeof(*e));
    path2(cfg, R, "config");
    path2(udc, R, "sys/class/udc");
    path2(musb, R, "sys/bus/platform/devices");
    e->configfs = cfg;
    e->mount_configfs = 0;
    e->udc_class = udc;
    e->musb_devs = musb;
    e->proc_mounts = S.mounts;
    e->mountpoint = "/mnt/sd";
    e->devs = NULL;
    e->ops.sync_fs = f_sync;
    e->ops.umount_fs = f_umount;
    e->ops.mount_fs = f_mount;
    e->ops.sleep_ms = f_sleep;
}

int main(int argc, char **argv)
{
    struct hs_usb u;
    struct hs_usb_env env;
    char cfg[P], udc[P], musb[P], b[256], p[P];
    int rc, scen = 0, scen_pass = 0, before;

    if (argc < 2) {
        fprintf(stderr, "usage: %s SIMDIR\n", argv[0]);
        return 2;
    }
    snprintf(R, sizeof(R), "%s", argv[1]);
    (void)mkdir(R, 0755);
    mkdirs("config/usb_gadget");                 /* configfs already mounted */
    mkdirs("sys/class/udc/musb-hdrc.2.auto");
    mkdirs("sys/bus/platform/devices/musb-hdrc.2.auto");
    mkdirs("sys/bus/platform/devices/5100000.usb"); /* decoy: not a musb-hdrc entry */
    mkdirs("empty_udc");

    /* 1. enable: gadget built, SD synced + unmounted, LUN = partition, bound, port peripheral */
    reset_sim(); env_for(&env, cfg, udc, musb); hs_usb_init(&u);
    before = g_fail; scen++;
    rc = hs_usb_enable(&u, &env);
    check(rc == 0 && u.state == HS_USB_CONNECTED, "enable rc=0 CONNECTED");
    getg("idVendor", b, sizeof(b)); check(strcmp(b, "0x1d6b") == 0, "idVendor 0x1d6b");
    getg("idProduct", b, sizeof(b)); check(strcmp(b, "0x0104") == 0, "idProduct 0x0104");
    getg("strings/0x409/product", b, sizeof(b)); check(strcmp(b, "HandshakeOS RG35XX SD") == 0, "product string");
    getg("functions/mass_storage.usb0/lun.0/file", b, sizeof(b)); check(strcmp(b, "/dev/mmcblk0p1") == 0, "lun.0/file = /dev/mmcblk0p1");
    getg("functions/mass_storage.usb0/lun.0/removable", b, sizeof(b)); check(strcmp(b, "1") == 0, "lun removable=1");
    getg("functions/mass_storage.usb0/lun.0/ro", b, sizeof(b)); check(strcmp(b, "0") == 0, "lun ro=0");
    getg("configs/c.1/strings/0x409/configuration", b, sizeof(b)); check(b[0] != '\0', "config string");
    path2(p, S.gadget, "configs/c.1/mass_storage.usb0");
    { char t[P]; long n = readlink(p, t, sizeof(t) - 1); if (n < 0) n = 0; t[n] = '\0';
      check(n > 0 && strstr(t, "functions/mass_storage.usb0") != NULL, "config -> function symlink"); }
    getg("UDC", b, sizeof(b)); check(strcmp(b, "musb-hdrc.2.auto") == 0, "UDC bound musb-hdrc.2.auto");
    path2(p, musb, "musb-hdrc.2.auto/mode"); get(p, b, sizeof(b)); check(strcmp(b, "peripheral") == 0, "musb mode=peripheral");
    check(S.umounts == 1 && S.mounted == 0 && S.syncs >= 1, "synced + unmounted once");
    check(S.violations == 0, "no export while mounted, sync before umount");
    check(strcmp(u.blockdev, "/dev/mmcblk0p1") == 0, "blockdev from mounts");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=1 enable %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 2. host enumerates, then the cable is pulled */
    before = g_fail; scen++;
    check(hs_usb_poll(&u, &env) == 0 && !u.host_seen, "poll: not attached yet");
    putr("sys/class/udc/musb-hdrc.2.auto/state", "configured\n");
    check(hs_usb_poll(&u, &env) == 1 && u.host_seen, "poll: configured");
    putr("sys/class/udc/musb-hdrc.2.auto/state", "not attached\n");
    check(hs_usb_poll(&u, &env) == 0 && u.unplugged, "poll: unplugged after configured");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=2 poll %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 3. disable: unbind, LUN cleared, port otg, remounted */
    before = g_fail; scen++;
    rc = hs_usb_disable(&u, &env);
    check(rc == 0 && u.state == HS_USB_IDLE, "disable rc=0 IDLE");
    check(!exported(), "UDC + lun.0/file cleared");
    path2(p, musb, "musb-hdrc.2.auto/mode"); get(p, b, sizeof(b)); check(strcmp(b, "otg") == 0, "musb mode=otg");
    check(S.mounted == 1 && S.mounts_n == 1, "remounted once");
    check(S.violations == 0, "no remount while exported");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=3 disable %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 4. second session on the existing gadget (EEXIST everywhere) */
    before = g_fail; scen++;
    S.violations = 0;
    rc = hs_usb_enable(&u, &env);
    check(rc == 0 && u.state == HS_USB_CONNECTED, "re-enable on existing gadget");
    rc = hs_usb_disable(&u, &env);
    check(rc == 0 && u.state == HS_USB_IDLE && S.mounted == 1 && S.violations == 0, "re-disable");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=4 reuse %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 5. unmount refused (a file still open): nothing exported, still mounted, step UMOUNT */
    reset_sim(); env_for(&env, cfg, udc, musb); hs_usb_init(&u);
    before = g_fail; scen++;
    S.umount_errno = EBUSY;
    rc = hs_usb_enable(&u, &env);
    check(rc != 0 && u.state == HS_USB_ERROR && u.fail_step == HS_USB_STEP_UMOUNT && u.fail_errno == EBUSY, "umount EBUSY -> ERROR step UMOUNT");
    check(!exported() && S.mounted == 1 && S.violations == 0, "EBUSY: not exported, still mounted");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=5 umount_busy %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 6. no device controller: the card is never touched */
    reset_sim(); env_for(&env, cfg, udc, musb); hs_usb_init(&u);
    path2(udc, R, "empty_udc"); env.udc_class = udc;
    before = g_fail; scen++;
    rc = hs_usb_enable(&u, &env);
    check(rc != 0 && u.fail_step == HS_USB_STEP_UDC && S.umounts == 0 && S.syncs == 0 && S.mounted == 1, "no UDC -> step UDC, SD untouched");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=6 no_udc %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 7. bind refused: roll back (LUN cleared, port otg, remounted) */
    reset_sim(); env_for(&env, cfg, udc, musb); hs_usb_init(&u);
    path2(p, S.gadget, "UDC"); (void)unlink(p); (void)mkdir(p, 0755); /* open(O_WRONLY) -> EISDIR */
    before = g_fail; scen++;
    rc = hs_usb_enable(&u, &env);
    check(rc != 0 && u.state == HS_USB_ERROR && u.fail_step == HS_USB_STEP_BIND, "bind fails -> step BIND");
    getg("functions/mass_storage.usb0/lun.0/file", b, sizeof(b)); check(b[0] == '\0', "bind fail: LUN cleared");
    path2(p, musb, "musb-hdrc.2.auto/mode"); get(p, b, sizeof(b)); check(strcmp(b, "otg") == 0, "bind fail: musb otg");
    check(S.mounted == 1 && S.mounts_n == 1 && S.violations == 0, "bind fail: remounted, no violation");
    path2(p, S.gadget, "UDC"); (void)rmdir(p);
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=7 bind_fail %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 8. remount fails twice then works (retry), 9. remount never works -> step REMOUNT */
    reset_sim(); env_for(&env, cfg, udc, musb); hs_usb_init(&u);
    before = g_fail; scen++;
    rc = hs_usb_enable(&u, &env);
    S.mount_fail = 2;
    rc |= hs_usb_disable(&u, &env);
    check(rc == 0 && S.mounted == 1 && u.state == HS_USB_IDLE, "remount retried (2 fails) -> mounted");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=8 remount_retry %s\n", g_fail == before ? "PASS" : "FAIL");
    before = g_fail; scen++;
    rc = hs_usb_enable(&u, &env);
    S.mount_fail = 100;
    rc = hs_usb_disable(&u, &env);
    check(rc != 0 && u.state == HS_USB_ERROR && u.fail_step == HS_USB_STEP_REMOUNT && !exported(), "remount fails -> step REMOUNT, unbound");
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=9 remount_fail %s\n", g_fail == before ? "PASS" : "FAIL");

    /* 10. a USB failure is a PID 1 fault record: ledger + readable log line, back to GAMES */
    before = g_fail; scen++;
    {
        char lp[P], lb[400];
        uint64_t h0 = g_hs_faults.hash;
        path2(lp, R, "HANDSHAKE-FAULTS.log");
        (void)unlink(lp);
        hs_fault_record(HS_FAULT_USB, (uint32_t)HS_USB_STEP_UMOUNT, EBUSY, lp);
        get(lp, lb, sizeof(lb));
        check(g_hs_faults.n == 1 && g_hs_faults.hash != h0, "fault ledger advanced");
        check(strstr(lb, "USB_TRANSFER_FAILED code=5 usb_step=3 errno=16") != NULL && strstr(lb, "-> GAMES") != NULL, "HANDSHAKE-FAULTS.log USB line");
        printf("USB_SIM fault_log: %s\n", lb);
    }
    if (g_fail == before) scen_pass++;
    printf("USB_SIM scenario=10 fault_record %s\n", g_fail == before ? "PASS" : "FAIL");

    printf("USB_SIM scenarios=%d/%d checks=%d failed=%d %s\n", scen_pass, scen, g_checks, g_fail,
           (g_fail == 0 && scen_pass == scen) ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
