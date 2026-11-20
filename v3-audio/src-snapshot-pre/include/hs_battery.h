/* SPDX-License-Identifier: MIT */
#ifndef HS_BATTERY_H
#define HS_BATTERY_H

struct hs_battery {
    int present;        /* 1 if a Battery-type power-supply device was found */
    int percent;        /* 0-100, or -1 if unknown */
    int percent_est;    /* 1 if percent was estimated from voltage_now */
    int charging;       /* 1 charging, 0 not, -1 unknown */
    int voltage_uv;     /* microvolts, or -1 */
    int charger_online; /* 1 a Mains/USB supply reports online=1, 0 no, -1 unknown */
    int n_supplies;     /* entries seen under /sys/class/power_supply */
    char name[64];      /* battery entry name, e.g. axp20x-battery */
    char charger[64];   /* charger entry name, e.g. axp20x-usb */
    char status[32];
};

void hs_battery_clear(struct hs_battery *bat);
/* Auto-discovers every /sys/class/power_supply/<name> entry (any name):
   type Battery -> capacity / voltage_now / status / present,
   type USB|Mains|Wireless -> online. Root defaults to /sys/class/power_supply;
   HANDSHAKEOS_POWER_SUPPLY_ROOT overrides it (host tests). */
int  hs_battery_read(struct hs_battery *bat);
int  hs_battery_read_root(struct hs_battery *bat, const char *root);
/* Short UI label: "87%", "~62%", "USB", "NO DRV". */
const char *hs_battery_label(const struct hs_battery *bat, char *buf, unsigned n);

#endif /* HS_BATTERY_H */
