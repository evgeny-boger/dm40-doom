/*
 * Doom on the Alientek DM40: start-up (from the MG24 port's main.c).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <string.h>
#include "main.h"
#include "graphics.h"
#include "printf.h"
#include "z_zone.h"
#include "global_data.h"
#include "d_main.h"

// 8.3 directory name of the converted WAD (MCUDoomWadUtil output) in the
// root of the meter's disk.
#define WAD_NAME83  "DOOMMCU WAD"

uint8_t *currentSpiAddress;

static void halt(const char *msg)
{
    displayPrintln(1, "%s", msg);
    for (;;)
        wdt_kick();
}

int main(void)
{
    Z_Init();
    InitGlobals();
    initGraphics();
    displayPrintln(0, "Doom on Alientek DM40");
    displayPrintln(0, "MG24 port by Nicola Wrachien (next-hack)");
    displayPrintln(1, "Build %s %s", __DATE__, __TIME__);
    displayPrintln(1, "Memzone size %d bytes", (int) getStaticZoneSize());
    displayPrintln(1, "SPI flash JEDEC %06x", (unsigned) sf_jedec());
    extern bool touchPresent(void);
    displayPrintln(1, "Touch panel: %s", touchPresent() ? "ok" : "not found");
    int r = wad_open(WAD_NAME83);
    if (r)
    {
        displayPrintln(1, "DOOMMCU.WAD not found on the disk (%d)", r);
        halt("Convert DOOM1.WAD with MCUDoomWadUtil.");
    }
    displayPrintln(1, "DOOMMCU.WAD: %u bytes", (unsigned) wad_size());
    char id[5] = {0};
    wad_read(0, id, 4);
    if (memcmp(id, "IWAD", 4))
        halt("DOOMMCU.WAD: not an IWAD");
    D_DoomMain();
    return 0;
}
