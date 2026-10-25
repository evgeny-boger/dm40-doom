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

// Internal flash cache (sectors 6, 7 and 8 only, see hal.c).
#define FCACHE_IMM_BASE   0x08040000u      // sector 6
#define FCACHE_IMM_END    0x0805FFF0u      // last 16 bytes: signature
#define FCACHE_LVL_BASE   0x08060000u      // sectors 7 + 8
#define FCACHE_LVL_END    0x080A0000u
bool fcache_erase(int sector);             // 6, 7 or 8, anything else refused
bool fcache_program(uint32_t addr, uint32_t word);

// Zero the vendor loader marker (next reset: loader update screen + USB disk).
bool marker_zero(void);
// Way back to the vendor updater (port/recovery.c).
void recovery_check(void);
void enter_updater(const char *why) __attribute__((noreturn));

// Keys / touch as the KEY_* bits of boards.h.
void input_init(void);
uint16_t input_keys(void);

// Frame time breakdown (graphics.c perf_frame): DWT cycles spent per part,
// shown once a second in the black strip above the picture.
enum { PERF_TIC, PERF_RENDER, PERF_BLIT, PERF_SPI, PERF_N };
extern uint32_t perf_cycles[PERF_N];
#define DWT_CYCCNT_REG  REG32(0xE0001004u)
void perf_frame(void);
int sf_clock_div(void);                   // SPI1 prescaler chosen at start (2 or 4)

// Debug text over the console screen (graphics.c) and the emulator log.
void dbg_puts(const char *s);

// Kept across a watchdog reset (TCM .noinit, with dbg_log): the PC / LR the
// last SysTick interrupted, a fault's registers, and where this boot's log
// starts. recovery.c shows them after a watchdog reset.
struct dm40_diag
{
    uint32_t magic, pc, lr;
    uint32_t prev_pc, prev_lr, boot_pos;    // snapshot at boot
    uint32_t fault, cfsr, hfsr, mmfar, bfar, fault_pc, fault_lr;
};
extern struct dm40_diag dm40_diag;
extern char dbg_log[4096];
extern uint32_t dbg_log_pos;
void dm40_fatal(const char *format, ...) __attribute__((noreturn, format(printf, 1, 2)));
#endif
