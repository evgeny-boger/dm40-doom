/*
 * Levels baked into the image (flash cache regions after the WAD-wide one).
 *
 * The image has room for about 340 KiB of level data. A level needs its
 * geometry (E1M1 81 KiB, E1M2 167, E1M3 163, E1M4 130, E1M5 131, E1M6 220,
 * E1M7 155, E1M8 75, E1M9 108, plus ~5 KiB of lump table and up to 16 KiB
 * of unchecked buffers); what is left of its region caches flats and wall
 * patches, which are read from the SPI flash otherwise. A size of 0 takes the
 * rest of the flash below FCACHE_END. tools/bake.py fills the regions.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <stdio.h>
#include "dm40.h"
#include "doomdef.h"
#include "g_game.h"

static const struct
{
    uint8_t episode, map;
    uint32_t size;
} levels[] =
{
    { 1, 1, 120 * 1024 },
    { 1, 2, 0 },
};

bool fcache_level_region(int episode, int map, uint32_t *base, uint32_t *end)
{
    uint32_t a = FCACHE_START + FCACHE_IMM_SIZE;
    for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++)
    {
        uint32_t b = levels[i].size ? a + levels[i].size : FCACHE_END;
        if (b > FCACHE_END)
            return false;
        if (levels[i].episode == episode && levels[i].map == map)
        {
            *base = a;
            *end = b;
            return true;
        }
        a = b;
    }
    return false;
}

int fcache_next_level(int episode, int map)
{
    int next = 0;
    for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++)
        if (levels[i].episode == episode && levels[i].map > map && (!next || levels[i].map < next))
            next = levels[i].map;
    return next;
}

void fcache_bake_levels(void)
{
    for (unsigned i = 0; i < sizeof(levels) / sizeof(levels[0]); i++)
    {
        G_InitNew(sk_medium, levels[i].episode, levels[i].map);
        printf("DM40 baked E%dM%d\r\n", levels[i].episode, levels[i].map);
    }
    dm40_fatal("Bake done: %u levels", (unsigned) (sizeof(levels) / sizeof(levels[0])));
}
