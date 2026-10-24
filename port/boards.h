/*
 * DM40 board configuration for the MG24 Doom port.
 *
 * Alientek DM40: GD32F427VGT6 (Cortex-M4F, 200 MHz), ILI9488 480x320 on
 * FSMC, W25Q128 16 MiB SPI NOR (FAT16 volume) on SPI1, CST touch on I2C.
 * Doom renders 320x200 and the frame is shown scaled 1.5x (480x300).
 */
#ifndef BOARDS_H
#define BOARDS_H
#include "macros.h"
#include "config_enum_values.h"

#define SCREEN_WIDTH                          320
#define SCREEN_HEIGHT                         200

// The touch screen / side keys produce the gamepad layout of the
// two-shift-register boards (KEY_F1 = menu and so on, see i_video.c).
#define KEYBOARD                              SPI2X74165_KEYBOARD
#define KEY_UP                                0x0001
#define KEY_LEFT                              0x0002
#define KEY_RIGHT                             0x0004
#define KEY_DOWN                              0x0008
#define KEY_ALT                               0x0010
#define KEY_FIRE                              0x0020
#define KEY_USE                               0x0040
#define KEY_CHGW                              0x0080
#define KEY_LT_UP                             0x0100
#define KEY_RT_UP                             0x0200
#define KEY_LT_DOWN                           0x0400
#define KEY_RT_DOWN                           0x0800
#define KEY_F1                                0x1000
#define KEY_F2                                0x2000
#define KEY_F3                                0x4000
#define KEY_F4                                0x8000
#define HAS_LEFT_JOYPAD                       0
#define HAS_RIGHT_JOYPAD                      0
#define STEREO_AUDIO                          0
#endif
