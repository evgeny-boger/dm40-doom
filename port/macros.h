/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Modified for the Alientek DM40 (dm40-doom) by Evgeny Boger, 2026; from
 * next-hack/MG24_Doom_BLE fc02a55, changes in the git history.
 */
#ifndef SRC_MACROS_H_
#define SRC_MACROS_H_
#define CATEXP(a, b)                    a ## b
#define CAT(a, b)                       CATEXP(a, b)
#define CAT3(a,b,c)                     CAT3EXP(a, b, c)
#define CAT3EXP(a,b,c)                  a ## b ## c
#endif
