/**
 *  Display utility functions for Doom Port to EFR32xG2x MCUs.
 *  It also contains the interrupt-assisted DMA update of the display
 *  and double buffer display data structure.
 *
 *  Copyright (C) 2021-2023 by Nicola Wrachien (next-hack in the comments)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 *  DESCRIPTION:
 *  Interrupt assisted DMA-based display update engine, with double buffering 
 *  256-color functions to print general purpose texts.
 *  
 */


#include "graphics.h"
#include "font8x8.h"
#include "printf.h"
#include <string.h>
#include <stdarg.h>
#include "dm40.h"
#define MAX_STRING_SIZE 64
#define FONT_WIDTH 8
#define FONT_HEIGHT 8
#define LE2BE16(x) ((uint16_t)(((x >> 8) & 0xFF) | (x << 8)))
#define RGB(r, g, b) (((r >> 3) << (6 + 5)) | ((g >> 2) << 5) | ((b >> 3) << (0)))
// Palette entries are byte-swapped (the MG24 SPI panel order); the blit
// swaps them back for the 16-bit FSMC bus.
const uint16_t palette16[] =
{
    LE2BE16(RGB(170, 170, 170)),
    LE2BE16(RGB(0, 0, 0)),
    LE2BE16(RGB(255, 255, 255)),
    LE2BE16(RGB(86, 119, 170)),
};
typedef enum
{
    dm_4bpp = 4, dm_8bpp = 8
} displayMode_t;
displayData_t displayData;

// The 320x200 frame is shown 1.5x (480x300) at OUT_Y on the 480x320 panel:
// source pixels a b -> a a b, source rows 2k -> two lines, 2k+1 -> one.
#define OUT_Y   ((LCD_H - SCREEN_HEIGHT * 3 / 2) / 2)

void initGraphics()
{
    memset(&displayData, 0, sizeof(displayData));
    displayData.pPalette = (uint16_t *) palette16;
    displayData.displayMode = dm_8bpp;
    setDisplayPen(1, 2);
    lcd_fill(0, 0, LCD_W, OUT_Y, 0);
    lcd_fill(0, OUT_Y + SCREEN_HEIGHT * 3 / 2, LCD_W, LCD_H - OUT_Y - SCREEN_HEIGHT * 3 / 2, 0);
}

#pragma GCC push_options
#pragma GCC optimize ("O3")
void startDisplayRefresh(uint8_t bufferNumber, int rowsToUpdate)
{
    uint32_t perf_t0 = DWT_CYCCNT_REG;
    static uint16_t pal[256];
    static uint32_t line[LCD_W / 2];
    const volatile uint16_t *pp = displayData.pPalette;
    int n = pp == palette16 ? 4 : 256;
    for (int i = 0; i < n; i++)
        pal[i] = __builtin_bswap16(pp[i]);
    if (rowsToUpdate > SCREEN_HEIGHT)
        rowsToUpdate = SCREEN_HEIGHT;
    int outRows = (rowsToUpdate * 3 + 1) / 2;
    lcd_window(0, OUT_Y, LCD_W, outRows);
    const uint8_t *src = displayData.displayFrameBuffer[bufferNumber];
    int out = 0;
    for (int r = 0; r < rowsToUpdate; r++)
    {
        const uint8_t *s = src + r * SCREEN_WIDTH;
        uint16_t *l = (uint16_t *) line;
        for (int x = 0; x < SCREEN_WIDTH; x += 2)
        {
            uint16_t a = pal[s[x]], b = pal[s[x + 1]];
            l[0] = a;
            l[1] = a;
            l[2] = b;
            l += 3;
        }
        int reps = (r & 1) ? 1 : 2;
        for (int k = 0; k < reps && out < outRows; k++, out++)
        {
            const uint16_t *p = (const uint16_t *) line;
            for (int x = 0; x < LCD_W; x += 8)
            {
                LCD_DATA = p[0]; LCD_DATA = p[1]; LCD_DATA = p[2]; LCD_DATA = p[3];
                LCD_DATA = p[4]; LCD_DATA = p[5]; LCD_DATA = p[6]; LCD_DATA = p[7];
                p += 8;
            }
        }
    }
    displayData.dmaBusy = 0;
    wdt_kick();
    perf_cycles[PERF_BLIT] += DWT_CYCCNT_REG - perf_t0;
}
#pragma GCC pop_options

uint32_t perf_cycles[PERF_N];

// 8x8 text straight to the panel (RGB565), outside the game picture.
static void lcdText(int x, int y, const char *str, uint16_t fg, uint16_t bg)
{
    int n = strlen(str);
    if (x + n * 8 > LCD_W)
        n = (LCD_W - x) / 8;
    lcd_window(x, y, n * 8, 8);
    for (int row = 0; row < 8; row++)
        for (int i = 0; i < n; i++)
        {
            uint8_t fb = font8x8_basic[0x7F & str[i]][row];
            for (int cx = 0; cx < 8; cx++, fb >>= 1)
                LCD_DATA = (fb & 1) ? fg : bg;
        }
}

// Once per frame: every second, the average ms per frame of the whole frame,
// the game tics, the 3D view, the panel blit and the SPI flash reads (these
// happen inside the others, mostly the 3D view), in the strip above the
// picture.
void perf_frame(void)
{
    static uint32_t t0, frames;
    uint32_t now = DWT_CYCCNT_REG;
    frames++;
    if (now - t0 < HCLK_HZ)
        return;
    uint32_t k = HCLK_HZ / 10000u;  // cycles per 0.1 ms
    uint32_t total = (now - t0) / k / frames;
    uint32_t v[PERF_N];
    for (int i = 0; i < PERF_N; i++)
    {
        v[i] = perf_cycles[i] / k / frames;
        perf_cycles[i] = 0;
    }
    uint32_t fps10 = (uint32_t) (frames * 10ull * HCLK_HZ / (now - t0));
    char line[64];
    snprintf(line, sizeof(line), "%u.%u fps  frame %u.%u tic %u.%u 3d %u.%u spi %u.%u lcd %u.%u ms  /%d",
             (unsigned) fps10 / 10, (unsigned) fps10 % 10,
             (unsigned) total / 10, (unsigned) total % 10,
             (unsigned) v[PERF_TIC] / 10, (unsigned) v[PERF_TIC] % 10,
             (unsigned) v[PERF_RENDER] / 10, (unsigned) v[PERF_RENDER] % 10,
             (unsigned) v[PERF_SPI] / 10, (unsigned) v[PERF_SPI] % 10,
             (unsigned) v[PERF_BLIT] / 10, (unsigned) v[PERF_BLIT] % 10, sf_clock_div());
    lcdText(0, 1, line, 0xFFE0, 0x0000);
    t0 = now;
    frames = 0;
}

static uint8_t penColor = 1;
static uint8_t penBackground = 0;
static uint8_t line = 0;
//
void setDisplayMode(displayMode_t dm)
{
    displayData.displayMode = dm;
}
void setPixel(unsigned int x, unsigned int y, int c)
{
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
    {
        return;
    }
    switch (displayData.displayMode)
    {
        case dm_8bpp:
            displayData.displayFrameBuffer[displayData.workingBuffer][x  + SCREEN_WIDTH * y] = c;
            break;
    }
}
void displayPutChar(char c, int x, int y)
{
    for (int cy = 0; cy < FONT_HEIGHT; cy++)
    {
        uint8_t fb = font8x8_basic[0x7F & c][cy];
        for (int cx = 0; cx < FONT_WIDTH; cx++)
        {
            setPixel(x + cx, y + cy, (fb & 1) ? penColor : penBackground);
            fb >>= 1;
        }
    }
}
void setDisplayPen(int color, int background)
{
    penColor = color;
    penBackground = background;
}
void displayVPrintf(int x, int y, const char *format, va_list va)
{
    char outString[MAX_STRING_SIZE];
    vsnprintf(outString, MAX_STRING_SIZE, format, va);
    for (int i = 0;
            i < MAX_STRING_SIZE && x < SCREEN_WIDTH && outString[i] <= 0x7F && outString[i] > 0;
            i++)
    {
        displayPutChar(outString[i], x, y);
        x += FONT_WIDTH;
    }
}
void displayPrintf(int x, int y, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    displayVPrintf(x, y, format, va);
    va_end(va);
}


void displayPrintln(bool update, const char *format, ...)
{
    int y;
    uint32_t *d, *s;
    // check if we have space to print.
    //
    if (line < SCREEN_HEIGHT / FONT_HEIGHT)
    {
        y = line * FONT_HEIGHT;
        line++;
    }
    else
    {
        y = SCREEN_HEIGHT - FONT_HEIGHT;
        // printing at the bottom of the screen. move everything up.
        d = (uint32_t*) displayData.displayFrameBuffer[displayData.workingBuffer];
        s = (uint32_t*) (&displayData.displayFrameBuffer[displayData.workingBuffer][SCREEN_WIDTH * FONT_HEIGHT]);
#if 0        
        for (int i = 0; i < SCREEN_WIDTH * (SCREEN_HEIGHT - FONT_HEIGHT) / 4;  i++)
        {
            *d++ = *s++;
        }
#else  
        memcpy(d, s, SCREEN_WIDTH * (SCREEN_HEIGHT - FONT_HEIGHT));
        d += SCREEN_WIDTH * (SCREEN_HEIGHT - FONT_HEIGHT) / 4;
#endif
        // clear last row
        uint32_t color = penBackground | (penBackground << 8) | (penBackground << 16) | (penBackground << 24);
        for (int i = 0; i < SCREEN_WIDTH * FONT_HEIGHT / 4; i++)
        {
            *d++ = color;
        }
    }
    // printf will not wrap around
    va_list va;
    va_start(va, format);
    displayVPrintf(0, y, format, va);
    va_end(va);
    if (update)
    {
        startDisplayRefresh(displayData.workingBuffer, SCREEN_HEIGHT);
        while (displayData.dmaBusy);
    }
}

// Fatal error: the message on the console screen, then wait (the watchdog is
// kept fed so the message stays readable; power off to leave).
void dm40_fatal(const char *format, ...)
{
    char msg[MAX_STRING_SIZE];
    va_list va;
    va_start(va, format);
    vsnprintf(msg, sizeof(msg), format, va);
    va_end(va);
    dbg_puts("FATAL: ");
    dbg_puts(msg);
    dbg_puts("\r\n");
    displayData.pPalette = (uint16_t *) palette16;
    setDisplayPen(2, 1);
    displayPrintln(1, "FATAL: %s", msg);
    for (;;)
        wdt_kick();
}
