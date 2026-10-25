/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Modified for the Alientek DM40 (dm40-doom) by Evgeny Boger, 2026; from
 * next-hack/MG24_Doom_BLE fc02a55, changes in the git history.
 */
/*
 * Frame buffers and the text console (from the MG24 port's graphics.h; the
 * DMA line buffers are gone, the DM40 blits over FSMC).
 */
#ifndef SRC_GRAPHICS_H_
#define SRC_GRAPHICS_H_
#include <stdbool.h>
#include <stdint.h>
#include "boards.h"

typedef struct
{
    volatile uint16_t * pPalette;
    uint8_t displayFrameBuffer[2][SCREEN_WIDTH * SCREEN_HEIGHT];
    uint8_t workingBuffer;
    uint8_t displayMode;
    uint8_t rowsToUpdate;
    volatile uint8_t dmaBusy;
} displayData_t;
extern displayData_t displayData;
void displayPrintf(int x, int y, const char * format, ...);
void startDisplayRefresh(uint8_t bufferNumber, int rowsToUpdate);
void setDisplayPen(int color, int background);
void displayPrintln(bool update, const char * format, ...);
void initGraphics();
#endif
