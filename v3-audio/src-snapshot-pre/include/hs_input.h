/* SPDX-License-Identifier: MIT */
#ifndef HS_INPUT_H
#define HS_INPUT_H

#include <stdint.h>

/*
 * RG35XX 2024 evdev map, from the mainline device tree
 * sun50i-h700-anbernic-rg35xx-2024.dts (Ryan Walklin, 2024):
 *
 *   A BTN_EAST    B BTN_SOUTH    X BTN_NORTH    Y BTN_WEST
 *   L1 BTN_TL     R1 BTN_TR      L2 BTN_TL2     R2 BTN_TR2
 *   Start BTN_START  Select BTN_SELECT  Menu BTN_MODE
 *   D-pad BTN_DPAD_*    Vol± KEY_VOLUMEUP/DOWN
 *
 * Power is not in that gpio-keys block (it typically comes from the
 * AXP717 PMIC as KEY_POWER). We listen for KEY_POWER as well.
 *
 * On a host keyboard we also accept arrows, Enter/Esc, Q/E for L1/R1,
 * Z/X/A/S for A/B/X/Y, P for Power, and Tab for Menu.
 */

typedef enum {
    HS_BTN_UP = 0,
    HS_BTN_DOWN,
    HS_BTN_LEFT,
    HS_BTN_RIGHT,
    HS_BTN_A,
    HS_BTN_B,
    HS_BTN_X,
    HS_BTN_Y,
    HS_BTN_L1,
    HS_BTN_R1,
    HS_BTN_L2,
    HS_BTN_R2,
    HS_BTN_START,
    HS_BTN_SELECT,
    HS_BTN_MENU,
    HS_BTN_VOLUP,
    HS_BTN_VOLDOWN,
    HS_BTN_POWER,
    HS_BTN_COUNT
} hs_btn_t;

#define HS_INPUT_MAX_FDS 16

struct hs_input {
    int      n_fds;
    int      fds[HS_INPUT_MAX_FDS];
    uint32_t down;
    uint32_t pressed;
    uint32_t released;
};

int  hs_input_open(struct hs_input *in);
void hs_input_close(struct hs_input *in);
int  hs_input_poll(struct hs_input *in, int timeout_ms);
int  hs_input_poll_ns(struct hs_input *in, int64_t timeout_ns); /* <0 = block */
int  hs_input_down(const struct hs_input *in, hs_btn_t btn);
int  hs_input_pressed(const struct hs_input *in, hs_btn_t btn);
const char *hs_btn_name(hs_btn_t btn);

#endif /* HS_INPUT_H */
