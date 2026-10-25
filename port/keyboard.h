/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Modified for the Alientek DM40 (dm40-doom) by Evgeny Boger, 2026; from
 * next-hack/MG24_Doom_BLE fc02a55, changes in the git history.
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>
#include "main.h"
void initKeyboard(void);
void getKeys(uint16_t *keys);
void getAnalogInput(int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
#endif
