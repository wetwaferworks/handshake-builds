/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 HandshakeOS contributors
 *
 * hs_usb.h - v5.3 USB TRANSFER: the microSD FAT partition (boot files + ROMS) becomes a USB drive on
 * a PC through the Linux USB gadget mass-storage function (configfs, MUSB OTG port on the H700).
 *
 * The FAT partition is never exported while Linux has it mounted:
 *   ENABLE   find UDC -> find the SD block device -> sync -> unmount -> MUSB mode=peripheral ->
 *            configfs gadget (1d6b:0104, mass_storage.usb0, lun.0 removable) -> write UDC (bind)
 *   CONNECTED  poll /sys/class/udc/<udc>/state ("configured" = PC has the drive)
 *   DISABLE  UDC="" (unbind) -> lun.0/file="" -> sync -> MUSB mode=otg -> mount vfat again
 *            -> caller rescans ROMS
 * Any failed step rolls back to "mounted, gadget unbound" and is reported (never powers off).
 * All paths come from struct hs_usb_env so tests run the same code on a simulated tree.
 * Shell thread only. No locks, no threads. */
#ifndef HS_USB_H
#define HS_USB_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HS_USB_IDLE = 0,      /* SD mounted, gadget unbound */
    HS_USB_CONNECTED,     /* SD unmounted, exported over USB */
    HS_USB_ERROR          /* last enable failed; SD mounted again (rolled back) */
} hs_usb_state_t;

/* Step ids, reported on failure (also the fault ledger "line" field for USB faults). */
typedef enum {
    HS_USB_STEP_NONE = 0,
    HS_USB_STEP_UDC = 1,       /* no USB device controller in udc_class */
    HS_USB_STEP_BLOCKDEV = 2,  /* SD block device not found */
    HS_USB_STEP_UMOUNT = 3,    /* unmount refused (busy) */
    HS_USB_STEP_CONFIGFS = 4,  /* configfs mount / gadget directory */
    HS_USB_STEP_GADGET = 5,    /* gadget attribute / function / config write */
    HS_USB_STEP_LUN = 6,       /* lun.0/file */
    HS_USB_STEP_BIND = 7,      /* write UDC */
    HS_USB_STEP_UNBIND = 8,    /* clear UDC */
    HS_USB_STEP_REMOUNT = 9    /* mount vfat after USB */
} hs_usb_step_t;

struct hs_usb_ops {
    void *ctx;
    void (*sync_fs)(void *ctx);
    int  (*umount_fs)(void *ctx, const char *target);                       /* 0 / -1 + errno */
    int  (*mount_fs)(void *ctx, const char *src, const char *target, const char *type,
                     unsigned long flags, const char *data);               /* 0 / -1 + errno */
    void (*sleep_ms)(void *ctx, unsigned ms);
};

struct hs_usb_env {
    const char *configfs;     /* /sys/kernel/config */
    int         mount_configfs; /* 1: mount configfs there if usb_gadget/ is missing */
    const char *udc_class;    /* /sys/class/udc */
    const char *musb_devs;    /* /sys/bus/platform/devices (entries musb-hdrc.*) */
    const char *proc_mounts;  /* /proc/mounts */
    const char *mountpoint;   /* /mnt/sd */
    const char *const *devs;  /* fallback block devices, NULL terminated */
    struct hs_usb_ops ops;
};

struct hs_usb {
    hs_usb_state_t state;
    hs_usb_step_t  fail_step;
    int            fail_errno;
    int            host_seen;     /* UDC state reached "configured" since enable */
    int            unplugged;     /* host_seen, then "not attached": cable out */
    char udc[64];
    char musb[64];                /* musb-hdrc.N.auto, or "" */
    char blockdev[64];            /* /dev/mmcblk0p1 */
    char udc_state[32];           /* last read of udc/<name>/state */
    char msg[96];                 /* human readable last result */
};

/* Real device environment (PID 1): /sys, /proc, /mnt/sd, mount(2) / umount2(2) / sync(2). */
void hs_usb_env_device(struct hs_usb_env *env, const char *mountpoint);
void hs_usb_init(struct hs_usb *u);
int  hs_usb_enable(struct hs_usb *u, const struct hs_usb_env *env);   /* 0 = CONNECTED */
int  hs_usb_disable(struct hs_usb *u, const struct hs_usb_env *env);  /* 0 = IDLE, SD mounted */
/* Refresh udc_state / host_seen / unplugged. Returns 1 when the host has the drive. */
int  hs_usb_poll(struct hs_usb *u, const struct hs_usb_env *env);
const char *hs_usb_step_name(hs_usb_step_t s);

#endif
