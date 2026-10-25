/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Evgeny Boger
 */
/*
 * DM40 native image (installed by the vendor FS-loader at 0x08004000).
 *
 *  A 0x08004000..0x08008000  vectors, reset
 *    0x08008000..0x08008010  loader/ROM marker hole (0xFF in the image)
 *  B 0x08008010..0x08040000  code + rodata; must end below sector 6
 *    0x08040000..0x080A0000  Doom flash cache (sectors 6, 7, 8; hal.c)
 *    0x080A0000..            vendor FS-loader, UID data, calibration
 *  SRAM 192 KiB: .data, .bss (zone, frame buffers; the port's 16-bit
 *       pointers address 0x20000000 + 4 * n)
 *  TCM 64 KiB: stack (top 0x10010000) and .tcm_bss
 */
MEMORY
{
    FLASH_A (rx) : ORIGIN = 0x08004000, LENGTH = 0x4000
    FLASH_B (rx) : ORIGIN = 0x08008010, LENGTH = 0x08040000 - 0x08008010
    TCM (rw)     : ORIGIN = 0x10000000, LENGTH = 64K
    SRAM (rw)    : ORIGIN = 0x20000000, LENGTH = 192K
}
ENTRY(reset_handler);
_stack_top = ORIGIN(TCM) + LENGTH(TCM);

SECTIONS
{
    /* Region A: vectors, reset, and cold code (intermission, menus, printf) so the
       rest fits below sector 6. */
    .vectors : {
        KEEP(*(.vectors)) *(.text.reset)
        *wi_stuff.o(.text* .rodata*)
        *m_menu.o(.text* .rodata*)
        *printf.o(.text* .rodata*)
    } > FLASH_A
    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH_B
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH_B
    _sidata = LOADADDR(.data);
    .data : {
        _sdata = .;
        *(.data*)
        *(.ram*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT > FLASH_B
    _image_end = LOADADDR(.data) + SIZEOF(.data);
    /* Kept over a reset (not cleared by reset_c): dbg_log and dm40_diag for
       the fault screen (recovery.c). */
    .noinit (NOLOAD) : { *(.noinit*) } > TCM
    /* Large CPU-only buffers never addressed through the 16-bit pointers:
       TCM, leaving SRAM to the zone. */
    .tcm_bss (NOLOAD) : {
        _stcm_bss = .;
        *(.tcm_bss*)
        *(.bss.columnOffsetData .bss.columnBuffer .bss.columnBotBuffer)
        *(.bss.spanstart .bss.dbg_log .bss.line.* .bss.pal.* .bss.runs .bss.oplChip)
        . = ALIGN(4);
        _etcm_bss = .;
    } > TCM
    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*)
        *(.openings_bss*)   /* r_fast_stuff.c maskedData */
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM
    _stack_size = _stack_top - _etcm_bss;
    ASSERT(_image_end <= 0x08040000, "image reaches the flash cache (sector 6)")
    ASSERT(_stack_size >= 12288, "less than 12 KiB of stack in TCM")
    _szone = ALIGN(_ebss, 8);
    _ezone = ORIGIN(SRAM) + LENGTH(SRAM);
    ASSERT(_ezone - _szone >= 32768, "zone below 32 KiB")
}
