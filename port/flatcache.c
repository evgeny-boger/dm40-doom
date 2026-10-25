/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Evgeny Boger
 */
/*
 * Flats (64x64 floor / ceiling pictures) that did not fit in the internal
 * flash cache: an LRU of FLAT_SLOTS pictures in TCM, loaded from the WAD on
 * the SPI flash on a miss (~2.7 ms per flat at 12.5 MHz). The span drawer
 * reads a flat directly, so it cannot use the external-memory reads the wall
 * and sprite columns use. The MG24 port, with ~1 MiB of flash cache, never
 * needed this.
 */
#include <stdint.h>
#include "extMemory.h"

#define FLAT_SLOTS  10
#define FLAT_SIZE   4096

static uint8_t slots[FLAT_SLOTS][FLAT_SIZE] __attribute__((section(".tcm_bss.flatcache"), aligned(4)));
static uint32_t tag[FLAT_SLOTS];
static uint32_t used[FLAT_SLOTS];
static uint32_t tick;
uint32_t flatCacheMisses;

const void *flatCacheGet(const void *extAddress)
{
    uint32_t a = (uint32_t) extAddress;
    int victim = 0;
    tick++;
    for (int i = 0; i < FLAT_SLOTS; i++)
    {
        if (tag[i] == a)
        {
            used[i] = tick;
            return slots[i];
        }
        if (used[i] < used[victim])
            victim = i;
    }
    flatCacheMisses++;
    extMemSetCurrentAddress(a);
    extMemGetDataFromCurrentAddress(slots[victim], FLAT_SIZE);
    tag[victim] = a;
    used[victim] = tick;
    return slots[victim];
}
