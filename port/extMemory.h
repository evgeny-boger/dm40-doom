/*
 * extMemory.h for the DM40: the MG24 port's external-memory API on top of
 * the WAD file on the SPI-flash FAT (hal.c wad_read).
 *
 * Addresses are EXT_FLASH_BASE + x: x = 0..3 is the WAD length word (the MG24
 * layout stores it in front of the WAD), x >= 4 is WAD byte x - 4. Reads are
 * synchronous (polled SPI); the "asynchronous" calls finish before they
 * return, with the data at the same buffer offset the MG24 DMA leaves it.
 * Only the renderer's column buffers go through extMemStartAsynchDataRead,
 * which skips repeated reads (below).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#ifndef SRC_EXTMEMORY_H_
#define SRC_EXTMEMORY_H_
#include <string.h>
#include <stdint.h>
#include "dm40.h"

#define SPI_ADDRESS_MASK                        0x1FFFFFF
#define EXT_MEMORY_HEADER_SIZE                  8
#define EXT_MEMORY_READ_ALIGN_SIZE              4

extern uint8_t *currentSpiAddress;

static inline void extMemReadVirt(uint32_t x, void *dest, uint32_t n)
{
    uint8_t *d = dest;
    while (n && x < 4)
    {
        uint32_t len = wad_size();
        *d++ = (uint8_t)(len >> (8 * x));
        x++;
        n--;
    }
    if (n)
        wad_read(x - 4, d, n);
}

static inline void extMemSetCurrentAddress(uint32_t address)
{
    currentSpiAddress = (uint8_t *)(address & SPI_ADDRESS_MASK);
}

static inline void *extMemGetDataFromCurrentAddress(void *dest, unsigned int length)
{
    extMemReadVirt((uint32_t)currentSpiAddress, dest, length);
    currentSpiAddress += length;
    return dest;
}

static inline uint8_t extMemGetByteFromAddress(const void *addr)
{
    uint8_t b;
    extMemSetCurrentAddress((uint32_t)addr);
    extMemGetDataFromCurrentAddress(&b, 1);
    return b;
}

static inline short extMemFlashGetShortFromAddress(const void *addr)
{
    short s;
    extMemSetCurrentAddress((uint32_t)addr);
    extMemGetDataFromCurrentAddress(&s, 2);
    return s;
}

// The renderer often asks for a texture column it read just before (a near
// wall stretches one texture column over several screen columns, and the
// reads alternate between two buffers). The last reads are remembered per
// destination buffer; a repeat is served from the buffer that holds it
// (a copy when that is the other one) instead of the SPI flash.
#define EXT_MEM_MEMO 4
typedef struct
{
    uint8_t *dest;
    uint32_t addr, cnt;
} extMemMemo_t;
extern extMemMemo_t extMemMemo[EXT_MEM_MEMO];

static inline void *extMemStartAsynchDataRead(uint32_t address, void *dest, uint32_t cnt)
{
    static unsigned victim;
    address &= SPI_ADDRESS_MASK;
    uint32_t off = EXT_MEMORY_HEADER_SIZE + (address & 3);
    uint8_t *p = (uint8_t *)dest + off;
    int self = -1, hit = -1;
    for (int i = 0; i < EXT_MEM_MEMO; i++)
    {
        if (extMemMemo[i].dest == dest)
            self = i;
        if (extMemMemo[i].dest && extMemMemo[i].addr == address && extMemMemo[i].cnt >= cnt)
            hit = i;
    }
    if (hit >= 0 && hit == self)
        return p;
    if (hit >= 0)
        memcpy(p, extMemMemo[hit].dest + off, cnt);
    else
        extMemReadVirt(address, p, cnt);
    if (self < 0)
        self = victim++ % EXT_MEM_MEMO;
    extMemMemo[self].dest = dest;
    extMemMemo[self].addr = address;
    extMemMemo[self].cnt = cnt;
    return p;
}

static inline void extMemWaitAsynchDataRead(void)
{
}

static inline int extMemGetSize(void)
{
    return 16 * 1024 * 1024;
}

static inline void extMemWrite(uint32_t address, void *buffer, uint32_t size)
{
    // Save games on the SPI flash are not supported (read-only volume).
    (void)address;
    (void)buffer;
    (void)size;
}
#endif
