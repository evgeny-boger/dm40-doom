/*
 * DM40 hardware layer for the Doom port: registers and the HAL API.
 * Register values and sequences follow opendm40 (clock.rs, board.rs,
 * spiflash.rs, boot.rs, wdt.rs) and the stock firmware they were read from.
 */
#ifndef DM40_H
#define DM40_H
#include <stdint.h>
#include <stdbool.h>

#define REG32(a)        (*(volatile uint32_t *)(a))
#define REG16(a)        (*(volatile uint16_t *)(a))

static inline void __disable_irq(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void __enable_irq(void) { __asm volatile ("cpsie i" ::: "memory"); }
static inline uint32_t __get_PRIMASK(void) { uint32_t r; __asm volatile ("mrs %0, primask" : "=r"(r)); return r; }
static inline void __set_PRIMASK(uint32_t v) { __asm volatile ("msr primask, %0" :: "r"(v) : "memory"); }
static inline void __DSB(void) { __asm volatile ("dsb" ::: "memory"); }

#define HCLK_HZ         200000000u

// ILI9488 on FSMC bank 1: RS = A16.
#define LCD_CMD         REG16(0x6001FFFE)
#define LCD_DATA        REG16(0x60020000)
#define LCD_W           480
#define LCD_H           320

void hal_init(void);
uint32_t time_us(void);
uint32_t time_ms(void);
void delay_ms(uint32_t ms);
// Main-loop heartbeat: the SysTick handler reloads the watchdog only while
// this was called within the last WDT_ALIVE_MS.
void wdt_kick(void);
void NVIC_SystemReset(void) __attribute__((noreturn));

void lcd_window(int x, int y, int w, int h);
void lcd_fill(int x, int y, int w, int h, uint16_t c);
void lcd_backlight(int percent);

// W25Q128 (SPI1, polled).
void sf_init(void);
uint32_t sf_jedec(void);
void sf_read(uint32_t addr, void *dst, uint32_t n);

// WAD.WAD on the SPI-flash FAT16 volume: located once, read through the
// file's cluster runs (no FAT walk per read).
int wad_open(const char *name83);          // 0 = ok, <0 = error code
uint32_t wad_size(void);
void wad_read(uint32_t off, void *dst, uint32_t n);

// Doom flash cache, part of the image: the WAD-wide ("immutable") region at
// _fcache_start (after the code), then one region per level of
// port/levels.c. Filled by an emulator bake run (tools/bake.py); on a meter
// the firmware only compares against it.
extern const uint8_t _fcache_start[];
#define FCACHE_START      ((uint32_t)_fcache_start)
#define FCACHE_IMM_SIZE   0xF000u
#define FCACHE_IMM_BASE   FCACHE_START
#define FCACHE_IMM_END    (FCACHE_START + FCACHE_IMM_SIZE - 16)  // last 16 bytes: signature
#define FCACHE_END        0x080A0000u      // sector 9: vendor FS-loader
bool fcache_bake_mode(void);               // emulator bake run (key in .noinit)
bool fcache_program(uint32_t addr, uint32_t word);  // bake mode, FCACHE_START..FCACHE_END
// Region of a baked level (false: the level is not in this image).
bool fcache_level_region(int episode, int map, uint32_t *base, uint32_t *end);
// Next baked map of the episode after `map`, 0 if none.
int fcache_next_level(int episode, int map);
// Bake run: set up every level of port/levels.c once, then halt.
void fcache_bake_levels(void) __attribute__((noreturn));

// Zero the vendor loader marker (next reset: loader update screen + USB disk).
bool marker_zero(void);
// Way back to the vendor updater (port/recovery.c).
void recovery_check(void);
void enter_updater(const char *why) __attribute__((noreturn));

// Keys / touch as the KEY_* bits of boards.h.
void input_init(void);
uint16_t input_keys(void);

// Debug text over the console screen (graphics.c) and the emulator log.
void dbg_puts(const char *s);
void dm40_fatal(const char *format, ...) __attribute__((noreturn, format(printf, 1, 2)));
#endif
