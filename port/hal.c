/*
 * DM40 hardware layer for the Doom port.
 *
 * Sequences and values are those of opendm40 (firmware/src in the opendm40
 * repository): clock.rs init_core, boot.rs latch_on, wdt.rs start::fwdgt,
 * board.rs init (FSMC + ILI9488 + backlight), spiflash.rs (SPI1 + W25Q128),
 * boot.rs zero_marker (FMC programming).
 */
#include <string.h>
#include "dm40.h"

// ---------------------------------------------------------------- registers
#define RCU_CTL         0x40023800u
#define RCU_PLL         0x40023804u
#define RCU_CFG0        0x40023808u
#define RCU_INT         0x4002380Cu
#define RCU_AHB1EN      0x40023830u
#define RCU_AHB3EN      0x40023838u
#define RCU_APB1EN      0x40023840u
#define RCU_CFG1        0x4002388Cu
#define PMU_CTL         0x40007000u
#define PMU_CS          0x40007004u

#define GPIOA           0x40020000u
#define GPIOC           0x40020800u
#define GPIOD           0x40020C00u
#define GPIOE           0x40021000u
#define G_MODER         0x00u
#define G_OTYPER        0x04u
#define G_OSPEED        0x08u
#define G_PUPD          0x0Cu
#define G_BOP           0x18u
#define G_AFRL          0x20u

#define FSMC_BCR1       0xA0000000u
#define FSMC_BTR1       0xA0000004u
#define FSMC_BWTR1      0xA0000104u
#define TIM13           0x40001C00u

#define FWDGT_CTL       0x40003000u
#define FWDGT_PSC       0x40003004u
#define FWDGT_RLD       0x40003008u
#define FWDGT_STAT      0x4000300Cu

#define SPI1            0x40003800u
#define SPI_CTL0        (SPI1 + 0x00u)
#define SPI_STAT        (SPI1 + 0x08u)
#define SPI_DATA        (SPI1 + 0x0Cu)
#define SPI_I2SCTL      (SPI1 + 0x1Cu)

#define FMC_KEY         0x40023C04u
#define FMC_STAT        0x40023C0Cu
#define FMC_CTL         0x40023C10u

#define SYST_CSR        0xE000E010u
#define SYST_RVR        0xE000E014u
#define SYST_CVR        0xE000E018u
#define SCB_AIRCR       0xE000ED0Cu

static inline void modify32(uint32_t a, uint32_t clr, uint32_t set)
{
    REG32(a) = (REG32(a) & ~clr) | set;
}

static bool wait_bits(uint32_t reg, uint32_t mask, uint32_t want)
{
    for (uint32_t n = 0; n < 0x40000u; n++)
        if ((REG32(reg) & mask) == want)
            return true;
    return false;
}

// ---------------------------------------------------------------- clock
// clock.rs init_core: 200 MHz from HXTAL (25 MHz) or IRC16M, AHB /1,
// APB2 /2, APB1 /4, PMU over-drive, TIMPRE. FLASH_ACR is left alone.
#define PLL_HXTAL       0x29406419u
#define PLL_IRC16M      ((PLL_HXTAL & ~(0x3Fu | (1u << 22))) | 16u)
#define PLL_RESET       0x24003010u

static bool clock_init(bool hxtal)
{
    modify32(RCU_CTL, 0, 1);
    if (!wait_bits(RCU_CTL, 2, 2))
        return false;
    modify32(RCU_CFG0, 0, 0x80);
    for (volatile int i = 0; i < 0x14; i++)
        ;
    REG32(RCU_CFG0) = 0;
    if (!wait_bits(RCU_CFG0, 0xC, 0))
        return false;
    modify32(RCU_CTL, 0x01090000u, 0);
    REG32(RCU_PLL) = PLL_RESET;
    modify32(RCU_CTL, 0x00040000u, 0);
    REG32(RCU_INT) = 0;
    if (!wait_bits(RCU_CTL, 0x02000000u, 0))
        return false;
    if (hxtal)
    {
        modify32(RCU_CTL, 0, 0x00010000u);
        if (!wait_bits(RCU_CTL, 0x00020000u, 0x00020000u))
        {
            modify32(RCU_CTL, 0x00010000u, 0);
            return false;
        }
    }
    modify32(RCU_APB1EN, 0, 0x10000000u);
    modify32(PMU_CTL, 0, 0xC000);
    modify32(RCU_CFG0, 0, 0x8000);
    modify32(RCU_CFG0, 0, 0x1400);
    REG32(RCU_PLL) = hxtal ? PLL_HXTAL : PLL_IRC16M;
    modify32(RCU_CTL, 0, 0x01000000u);
    if (!wait_bits(RCU_CTL, 0x02000000u, 0x02000000u))
    {
        modify32(RCU_CTL, 0x01000000u, 0);
        return false;
    }
    modify32(PMU_CTL, 0, 0x00010000u);
    if (!wait_bits(PMU_CS, 0x00010000u, 0x00010000u))
        return false;
    modify32(PMU_CTL, 0, 0x00020000u);
    if (!wait_bits(PMU_CS, 0x00020000u, 0x00020000u))
        return false;
    modify32(RCU_CFG0, 0x3, 0x2);
    if (!wait_bits(RCU_CFG0, 0xC, 0x8))
    {
        modify32(RCU_CFG0, 0x3, 0);
        return false;
    }
    modify32(RCU_CFG1, 0, 0x01000000u);
    return true;
}

// ---------------------------------------------------------------- watchdog
// The stock configuration (/256, reload 2500: ~19 s). The SysTick handler
// reloads it while the main loop has called wdt_kick() recently, so a hang
// in the game still resets the unit (and the FS-loader starts the app again).
#define WDT_ALIVE_MS    5000u

static volatile uint32_t ms_count;
static volatile uint32_t alive_ms;

static void fwdgt_start(void)
{
    REG32(FWDGT_CTL) = 0xCCCC;
    REG32(FWDGT_CTL) = 0x5555;
    for (uint32_t n = 0; (REG32(FWDGT_STAT) & 3) && n < 0x100000u; n++)
        ;
    REG32(FWDGT_PSC) = 6;
    REG32(FWDGT_RLD) = 2500;
    REG32(FWDGT_CTL) = 0xAAAA;
}

void wdt_kick(void)
{
    alive_ms = ms_count;
}

void SysTick_Handler(void)
{
    uint32_t now = ++ms_count;
    if (now - alive_ms < WDT_ALIVE_MS)
        REG32(FWDGT_CTL) = 0xAAAA;
}

uint32_t time_ms(void)
{
    return ms_count;
}

uint32_t time_us(void)
{
    uint32_t ms, v;
    do
    {
        ms = ms_count;
        v = REG32(SYST_CVR);
    } while (ms != ms_count);
    return ms * 1000u + (HCLK_HZ / 1000u - 1u - v) / (HCLK_HZ / 1000000u);
}

void delay_ms(uint32_t ms)
{
    uint32_t t = ms_count;
    while (ms_count - t < ms)
        wdt_kick();
}

void NVIC_SystemReset(void)
{
    __DSB();
    REG32(SCB_AIRCR) = 0x05FA0004u;
    __DSB();
    for (;;)
        ;
}

// ---------------------------------------------------------------- GPIO
// mode 0 in, 1 out, 2 AF; pull 0 none, 1 up, 2 down; push-pull, very high speed.
static void pins(uint32_t port, uint32_t mask, uint32_t mode, uint32_t pull, uint32_t af)
{
    for (uint32_t p = 0; p < 16; p++)
    {
        if (!(mask & (1u << p)))
            continue;
        if (mode == 2)
        {
            uint32_t reg = p < 8 ? G_AFRL : G_AFRL + 4, sh = 4 * (p & 7);
            modify32(port + reg, 0xFu << sh, af << sh);
        }
        modify32(port + G_OTYPER, 1u << p, 0);
        modify32(port + G_OSPEED, 3u << (2 * p), 3u << (2 * p));
        modify32(port + G_PUPD, 3u << (2 * p), pull << (2 * p));
        modify32(port + G_MODER, 3u << (2 * p), mode << (2 * p));
    }
}

static inline void gpio_set(uint32_t port, uint32_t pin, bool high)
{
    REG32(port + G_BOP) = high ? 1u << pin : 1u << (pin + 16);
}

// ---------------------------------------------------------------- LCD
static void lcd_cmd(uint16_t c) { LCD_CMD = c; }
static void lcd_dat(uint16_t d) { LCD_DATA = d; }

void lcd_window(int x, int y, int w, int h)
{
    int x1 = x + w - 1, y1 = y + h - 1;
    lcd_cmd(0x2A);
    lcd_dat(x >> 8); lcd_dat(x & 0xFF); lcd_dat(x1 >> 8); lcd_dat(x1 & 0xFF);
    lcd_cmd(0x2B);
    lcd_dat(y >> 8); lcd_dat(y & 0xFF); lcd_dat(y1 >> 8); lcd_dat(y1 & 0xFF);
    lcd_cmd(0x2C);
}

void lcd_fill(int x, int y, int w, int h, uint16_t c)
{
    lcd_window(x, y, w, h);
    for (int i = 0; i < w * h; i++)
        LCD_DATA = c;
}

void lcd_backlight(int percent)
{
    REG32(TIM13 + 0x34) = percent;
}

static void board_init(void)
{
    modify32(RCU_AHB1EN, 0, 0x1F);
    // Rails as the stock boot: PC0 = 1 (BT module), PE2 = 1 (power hold /
    // DMM analog), PC4 = 0 (scope supply, unused here).
    gpio_set(GPIOC, 0, true);
    gpio_set(GPIOE, 2, true);
    pins(GPIOC, 1u << 0, 1, 0, 0);
    pins(GPIOE, 1u << 2, 1, 0, 0);
    delay_ms(10);
    gpio_set(GPIOC, 4, false);
    pins(GPIOC, 1u << 4, 1, 0, 0);
    // PD6 power key: input, pull-down, pressed = 1.
    modify32(GPIOD + G_MODER, 3u << 12, 0);
    modify32(GPIOD + G_PUPD, 3u << 12, 2u << 12);

    // FSMC: AF12, pull-up.
    pins(GPIOD, 0xC703u | 0x0800u | 0x0030u | 0x0080u, 2, 1, 12);
    pins(GPIOE, 0xFF80u, 2, 1, 12);
    modify32(RCU_AHB3EN, 0, 1);
    REG32(FSMC_BCR1) = (REG32(FSMC_BCR1) & 0xFFF70081u & ~1u) | 0x5011u;
    REG32(FSMC_BTR1) = 0x00000F04u;
    REG32(FSMC_BWTR1) = 0x0FF00201u;
    modify32(FSMC_BCR1, 0, 1);
    delay_ms(20);

    static const uint8_t seq[] = {
        0xC0, 2, 0x0F, 0x0F,
        0xC1, 1, 0x41,
        0xC5, 3, 0x00, 0x57, 0x80,
        0xB1, 1, 0xA0,
        0xB4, 1, 0x02,
        0x36, 1, 0x48,
        0x3A, 1, 0x55,
        0x21, 0,
        0xE9, 1, 0x00,
        0xF7, 4, 0xA9, 0x51, 0x2C, 0x82,
        0xE0, 15, 0x00, 0x09, 0x0C, 0x02, 0x0E, 0x05, 0x2F, 0x46, 0x47, 0x04, 0x0E, 0x0B, 0x2D, 0x36, 0x0F,
        0xE1, 15, 0x00, 0x10, 0x13, 0x03, 0x10, 0x06, 0x3A, 0x46, 0x51, 0x06, 0x10, 0x0D, 0x33, 0x37, 0x0F,
    };
    for (unsigned i = 0; i + 1 < sizeof(seq);)
    {
        lcd_cmd(seq[i]);
        unsigned n = seq[i + 1];
        for (unsigned k = 0; k < n; k++)
            lcd_dat(seq[i + 2 + k]);
        i += 2 + n;
    }
    lcd_cmd(0x11);
    delay_ms(120);
    lcd_cmd(0x29);
    lcd_cmd(0x36);
    lcd_dat(0xE8);  // landscape 480x320, (0,0) top left as held
    lcd_fill(0, 0, LCD_W, LCD_H, 0);

    // Backlight: PA6 AF9 = TIM13_CH1, 20 kHz PWM, CCR1 = percent.
    modify32(RCU_APB1EN, 0, 1u << 7);
    REG32(TIM13 + 0x00) = 0;
    REG32(TIM13 + 0x28) = 99;
    REG32(TIM13 + 0x2C) = 99;
    REG32(TIM13 + 0x18) = 0x68;
    REG32(TIM13 + 0x34) = 0;
    REG32(TIM13 + 0x20) = 1;
    REG32(TIM13 + 0x14) = 1;
    REG32(TIM13 + 0x00) = 0x81;
    pins(GPIOA, 1u << 6, 2, 0, 9);
}

void hal_init(void)
{
    // Power hold first (the ROM raised PE2 already; keep it).
    modify32(RCU_AHB1EN, 0, 1u << 4);
    REG32(GPIOE + G_BOP) = 1u << 2;
    modify32(GPIOE + G_MODER, 3u << 4, 1u << 4);
    fwdgt_start();
    if (!clock_init(true))
        clock_init(false);
    REG32(SYST_RVR) = HCLK_HZ / 1000u - 1u;
    REG32(SYST_CVR) = 0;
    REG32(SYST_CSR) = 7;
    wdt_kick();
    __enable_irq();
    board_init();
    lcd_backlight(80);
    sf_init();
    input_init();
}

// ---------------------------------------------------------------- SPI flash
// SPI1 mode 3, master, 8 bit, /4 = 12.5 MHz; CS = PE4.
#define SF_CS           4u

static inline void sf_cs(bool active)
{
    REG32(GPIOE + G_BOP) = active ? 1u << (SF_CS + 16) : 1u << SF_CS;
}

static inline uint8_t sf_xfer(uint8_t b)
{
    while (!(REG32(SPI_STAT) & 2))
        ;
    REG16(SPI_DATA) = b;
    while (!(REG32(SPI_STAT) & 1))
        ;
    return (uint8_t)REG32(SPI_DATA);
}

static void sf_af(uint32_t port, uint32_t pin)
{
    modify32(port + G_AFRL, 0xFu << (4 * pin), 5u << (4 * pin));
    modify32(port + G_OTYPER, 1u << pin, 0);
    modify32(port + G_OSPEED, 0, 3u << (2 * pin));
    modify32(port + G_PUPD, 3u << (2 * pin), 0);
    modify32(port + G_MODER, 3u << (2 * pin), 2u << (2 * pin));
}

void sf_init(void)
{
    modify32(RCU_AHB1EN, 0, (1u << 2) | (1u << 3) | (1u << 4));
    modify32(RCU_APB1EN, 0, 1u << 14);
    sf_cs(false);
    modify32(GPIOE + G_MODER, 3u << (2 * SF_CS), 1u << (2 * SF_CS));
    modify32(GPIOE + G_OTYPER, 1u << SF_CS, 0);
    modify32(GPIOE + G_OSPEED, 0, 3u << (2 * SF_CS));
    sf_af(GPIOD, 3);
    sf_af(GPIOC, 2);
    sf_af(GPIOC, 3);
    REG32(SPI_CTL0) = 0;
    modify32(SPI_I2SCTL, 1u << 11, 0);
    REG32(SPI_CTL0) = 0x030F;
    REG32(SPI_CTL0) = 0x030F | (1u << 6);
    (void)REG32(SPI_DATA);
    // An erase left running by a reset: wait for WIP to clear (<= 500 ms).
    uint32_t t = time_ms();
    for (;;)
    {
        sf_cs(true);
        sf_xfer(0x05);
        uint8_t sr = sf_xfer(0xFF);
        sf_cs(false);
        if (!(sr & 1) || time_ms() - t > 500)
            break;
    }
}

uint32_t sf_jedec(void)
{
    sf_cs(true);
    sf_xfer(0x9F);
    uint32_t id = (uint32_t)sf_xfer(0xFF) << 16;
    id |= (uint32_t)sf_xfer(0xFF) << 8;
    id |= sf_xfer(0xFF);
    sf_cs(false);
    return id;
}

void sf_read(uint32_t addr, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    sf_cs(true);
    sf_xfer(0x03);
    sf_xfer(addr >> 16);
    sf_xfer(addr >> 8);
    sf_xfer(addr);
    // Keep one byte in flight: the next TX is queued while the previous
    // one shifts, then its RX is collected.
    if (n)
    {
        while (!(REG32(SPI_STAT) & 2))
            ;
        REG16(SPI_DATA) = 0xFF;
        for (uint32_t i = 0; i + 1 < n; i++)
        {
            while (!(REG32(SPI_STAT) & 2))
                ;
            REG16(SPI_DATA) = 0xFF;
            while (!(REG32(SPI_STAT) & 1))
                ;
            d[i] = (uint8_t)REG32(SPI_DATA);
        }
        while (!(REG32(SPI_STAT) & 1))
            ;
        d[n - 1] = (uint8_t)REG32(SPI_DATA);
    }
    sf_cs(false);
}

// ---------------------------------------------------------------- WAD on FAT
// The whole W25Q128 is one FAT volume at offset 0 (4096-byte sectors on the
// DM40). The file's cluster chain is turned into runs once.
#define WAD_MAX_RUNS    64

static struct
{
    uint32_t off, phys, len;
} runs[WAD_MAX_RUNS];
static int nruns;
static uint32_t wsize;

static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int wad_open(const char *name83)
{
    uint8_t b[64];
    sf_read(0, b, sizeof(b));
    uint32_t bps = le16(b + 0x0B), spc = b[0x0D], rsvd = le16(b + 0x0E), nfat = b[0x10];
    uint32_t nroot = le16(b + 0x11), spf = le16(b + 0x16);
    uint32_t total = le16(b + 0x13) ? le16(b + 0x13) : le32(b + 0x20);
    if (bps < 512 || bps > 4096 || !spc || !nfat || !spf || !nroot)
        return -1;
    uint32_t fat = rsvd * bps, root = (rsvd + nfat * spf) * bps;
    uint32_t data = root + ((nroot * 32 + bps - 1) / bps) * bps;
    uint32_t csize = spc * bps;
    uint32_t nclus = (total * bps - data) / csize;
    bool fat12 = nclus < 4085;
    uint32_t first = 0;
    for (uint32_t i = 0; i < nroot && !first; i++)
    {
        uint8_t e[32];
        sf_read(root + i * 32, e, 32);
        if (e[0] == 0)
            break;
        if (e[0] == 0xE5 || (e[11] & 0x18) || e[11] == 0x0F)
            continue;
        if (!memcmp(e, name83, 11))
        {
            first = le16(e + 26);
            wsize = le32(e + 28);
        }
    }
    if (!first)
        return -2;
    nruns = 0;
    uint32_t off = 0, c = first;
    while (off < wsize)
    {
        if (c < 2 || c >= nclus + 2)
            return -3;
        uint32_t phys = data + (c - 2) * csize;
        if (nruns && runs[nruns - 1].phys + runs[nruns - 1].len == phys)
            runs[nruns - 1].len += csize;
        else
        {
            if (nruns == WAD_MAX_RUNS)
                return -4;
            runs[nruns].off = off;
            runs[nruns].phys = phys;
            runs[nruns].len = csize;
            nruns++;
        }
        off += csize;
        uint8_t v[2];
        if (fat12)
        {
            sf_read(fat + c + c / 2, v, 2);
            uint32_t x = le16(v);
            c = (c & 1) ? x >> 4 : x & 0xFFF;
            if (c >= 0xFF8)
                break;
        }
        else
        {
            sf_read(fat + c * 2, v, 2);
            c = le16(v);
            if (c >= 0xFFF8)
                break;
        }
    }
    if (off < wsize)
        return -5;
    return 0;
}

uint32_t wad_size(void)
{
    return wsize;
}

void wad_read(uint32_t off, void *dst, uint32_t n)
{
    static int last;
    uint8_t *d = dst;
    while (n)
    {
        if (off >= wsize)
        {
            memset(d, 0, n);
            return;
        }
        int r = last;
        if (off < runs[r].off || off >= runs[r].off + runs[r].len)
            for (r = 0; r < nruns - 1 && off >= runs[r + 1].off; r++)
                ;
        last = r;
        uint32_t in = off - runs[r].off, k = runs[r].len - in;
        if (k > n)
            k = n;
        sf_read(runs[r].phys + in, d, k);
        d += k;
        off += k;
        n -= k;
    }
}

// ---------------------------------------------------------------- flash cache
// GD32F427 1 MiB: sectors 6, 7, 8 = 0x08040000, 0x08060000, 0x08080000
// (128 KiB each). Sector 9 is the vendor FS-loader, 10 the calibration:
// never touched. Erase takes a sector NUMBER and refuses anything but 6, 7
// and 8; program refuses any address outside 0x08040000..0x0809FFFF. The
// image itself ends below 0x08040000 (link.ld asserts it), so the loader's
// body CRC does not cover the cache.
static bool fmc_wait(void)
{
    for (uint32_t n = 0; REG32(FMC_STAT) & (1u << 16); n++)
    {
        if (n >= 0x4000000u)
            return false;
        if (!(n & 0xFFFF))
            REG32(FWDGT_CTL) = 0xAAAA;
    }
    return true;
}

static void fmc_unlock(void)
{
    if (REG32(FMC_CTL) & (1u << 31))
    {
        REG32(FMC_KEY) = 0x45670123u;
        REG32(FMC_KEY) = 0xCDEF89ABu;
    }
}

bool fcache_erase(int sector)
{
    if (sector != 6 && sector != 7 && sector != 8)
        return false;
    bool ok;
    __disable_irq();
    fmc_unlock();
    ok = fmc_wait();
    if (ok)
    {
        REG32(FMC_STAT) = 0x1F3;
        // SER + SN first, then the same value with START (no read-back:
        // the emulator's controller model does not keep SER).
        uint32_t ctl = 0x200u | ((uint32_t)sector << 3) | 2u;  // PSZ x32, SN, SER
        REG32(FMC_CTL) = ctl;
        REG32(FMC_CTL) = ctl | (1u << 16);                     // START
        ok = fmc_wait() && !(REG32(FMC_STAT) & 0x1F2);
        REG32(FMC_CTL) = 0;
    }
    REG32(FMC_CTL) = 1u << 31;
    __enable_irq();
    wdt_kick();
    return ok;
}

bool fcache_program(uint32_t addr, uint32_t word)
{
    if (addr < FCACHE_IMM_BASE || addr >= FCACHE_LVL_END || (addr & 3))
        return false;
    if (REG32(addr) == word)
        return true;
    bool ok;
    __disable_irq();
    fmc_unlock();
    ok = fmc_wait();
    if (ok)
    {
        REG32(FMC_STAT) = 0x1F3;
        REG32(FMC_CTL) = 0x200u | 1u;  // PSZ x32, PG
        REG32(addr) = word;
        __DSB();
        ok = fmc_wait() && !(REG32(FMC_STAT) & 0x1F2);
        REG32(FMC_CTL) = 0;
    }
    REG32(FMC_CTL) = 1u << 31;
    __enable_irq();
    return ok && REG32(addr) == word;
}

// ---------------------------------------------------------------- loader marker
// The 12-byte ROM / FS-loader record at 0x08008000. Programming it to zero
// (1 -> 0 bits only, no erase; opendm40 boot.rs zero_marker) makes the vendor
// FS-loader show its update screen with its own USB disk at the next reset.
// Touches exactly these three words.
#define MARKER          0x08008000u

bool marker_zero(void)
{
    __disable_irq();
    fmc_unlock();
    if (fmc_wait())
    {
        REG32(FMC_STAT) = 0x1F3;
        REG32(FMC_CTL) = 0x200u | 1u;  // PSZ x32, PG
        for (uint32_t i = 0; i < 3; i++)
        {
            uint32_t a = MARKER + 4 * i;
            if (REG32(a) != 0)
            {
                REG32(a) = 0;
                __DSB();
                if (!fmc_wait())
                    break;
            }
        }
        REG32(FMC_CTL) = 0;
    }
    REG32(FMC_CTL) = 1u << 31;
    __enable_irq();
    return REG32(MARKER) == 0 && REG32(MARKER + 4) == 0 && REG32(MARKER + 8) == 0;
}

// ---------------------------------------------------------------- debug log
// A RAM ring the emulator monitor / GDB can read (symbol dbg_log).
char dbg_log[4096];
uint32_t dbg_log_pos;

void _putchar(char c)
{
    dbg_log[dbg_log_pos++ & (sizeof(dbg_log) - 1)] = c;
}

void dbg_puts(const char *s)
{
    while (*s)
        _putchar(*s++);
}
