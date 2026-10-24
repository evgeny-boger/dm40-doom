/*
 * DM40 input: the capacitive touch panel and the four side keys (both come
 * from the Hynitron controller), as the gamepad bits of boards.h. The power
 * key held 1.5 s switches the meter off.
 *
 * INT pulses low per report: the falling edge is latched by EXTI line 5
 * (pending bit polled, no interrupt), as opendm40 does.
 *
 * Touch controller (opendm40 touch.rs, stock touch_v2_6.h): Hynitron at
 * 7-bit 0x5A on SCL = PA8, SDA = PC9 (hardware I2C2 at 100 kHz, or
 * bit-banged when that does not answer),
 * RST = PE6, INT = PE5 (low while a report is pending).
 *   init:  write D1 01; write D2 04 + read 4; write D1 09
 *   frame: write D0 00, read 7; byte 6 == 0xAB; n = byte5 & 7 records;
 *          records 1..n-1 at D0 07 + 5 * (i - 1) (5 bytes); write D0 00 AB.
 *   record: b0 low nibble 6 = pressed; X = b1 << 4 | b3 >> 4,
 *          Y = b2 << 4 | b3 & 0xF (landscape 480x320).
 *   keys:  b0 bit 7 = key report, b0 & 0xF != 0 while held; key from X:
 *          < 500 menu, < 750 run, < 1000 auto, else mf.
 *
 * Screen layout (480x320, the game picture is 480x300 at y = 10):
 *   x < 180          stick: direction from (STICK_X, STICK_Y), dead zone
 *                    STICK_DEAD (up = forward, left / right = turn)
 *   180 <= x < 300   y < 90 menu, y >= 230 automap, else next weapon
 *   x >= 300         y < 150 use (doors, switches), else fire
 * Side keys: menu = menu, run = fire, auto = use, mf = next weapon.
 */
#include "keyboard.h"

#define GPIOA           0x40020000u
#define GPIOC           0x40020800u
#define GPIOD           0x40020C00u
#define GPIOE           0x40021000u
#define RCU_AHB1EN      0x40023830u
#define RCU_APB2EN      0x40023844u
#define SYSCFG_EXTISS1  0x4001380Cu
#define EXTI_INTEN      0x40013C00u
#define EXTI_FTEN       0x40013C0Cu
#define EXTI_PD         0x40013C14u
#define DEMCR           0xE000EDFCu
#define DWT_CTRL        0xE0001000u
#define DWT_CYCCNT      0xE0001004u

#define SCL             8u   // PA8
#define SDA             9u   // PC9
#define RST             6u   // PE6
#define INT             5u   // PE5
#define ADDR            0x5Au
#define HALF_US         4u

#define STICK_X         90
#define STICK_Y         220
#define STICK_DEAD      22
#define POWER_OFF_MS    1500u

static bool present;
bool touchPresent(void);
static uint16_t touchKeys, sideKeys;
static int powerState;          // 0 wait for release, 1 armed, 2 pressed
static uint32_t powerSince;

static void delay_us(uint32_t us)
{
    uint32_t t = REG32(DWT_CYCCNT), n = us * (HCLK_HZ / 1000000u);
    while (REG32(DWT_CYCCNT) - t < n)
        ;
}

static inline void scl(bool h) { REG32(GPIOA + 0x18) = h ? 1u << SCL : 1u << (SCL + 16); }
static inline void sda(bool h) { REG32(GPIOC + 0x18) = h ? 1u << SDA : 1u << (SDA + 16); }
static inline bool sda_in(void) { return REG32(GPIOC + 0x10) & (1u << SDA); }
static inline bool scl_in(void) { return REG32(GPIOA + 0x10) & (1u << SCL); }

static void scl_release(void)
{
    scl(true);
    for (int n = 0; !scl_in() && n < 1000; n++)
        delay_us(1);
}

static void i2c_start(void)
{
    sda(true);
    scl_release();
    delay_us(HALF_US);
    sda(false);
    delay_us(HALF_US);
    scl(false);
    delay_us(HALF_US);
}

static void i2c_stop(void)
{
    sda(false);
    delay_us(HALF_US);
    scl_release();
    delay_us(HALF_US);
    sda(true);
    delay_us(HALF_US);
}

static bool i2c_tx(uint8_t b)
{
    for (int i = 7; i >= 0; i--)
    {
        sda(b & (1u << i));
        delay_us(HALF_US);
        scl_release();
        delay_us(HALF_US);
        scl(false);
    }
    sda(true);
    delay_us(HALF_US);
    scl_release();
    delay_us(HALF_US);
    bool ack = !sda_in();
    scl(false);
    delay_us(HALF_US);
    return ack;
}

static uint8_t i2c_rx(bool ack)
{
    uint8_t b = 0;
    sda(true);
    for (int i = 0; i < 8; i++)
    {
        delay_us(HALF_US);
        scl_release();
        delay_us(HALF_US);
        b = (b << 1) | sda_in();
        scl(false);
    }
    sda(!ack);
    delay_us(HALF_US);
    scl_release();
    delay_us(HALF_US);
    scl(false);
    sda(true);
    delay_us(HALF_US);
    return b;
}

static bool bb_write(const uint8_t *d, int n)
{
    i2c_start();
    bool ok = i2c_tx(ADDR << 1);
    for (int i = 0; i < n && ok; i++)
        ok = i2c_tx(d[i]);
    i2c_stop();
    return ok;
}

static bool bb_read(uint8_t *buf, int n)
{
    i2c_start();
    if (!i2c_tx((ADDR << 1) | 1))
    {
        i2c_stop();
        return false;
    }
    for (int i = 0; i < n; i++)
        buf[i] = i2c_rx(i + 1 < n);
    i2c_stop();
    return true;
}

// ---- hardware I2C2 (GD32 I2C v1 = STM32F4 style), blocking, polled ----
// Sequences as opendm40 touch.rs hw_*: 100 kHz standard mode.
#define RCU_APB1RST     0x40023820u
#define RCU_APB1EN      0x40023840u
#define I2C2            0x40005C00u
#define I2C_CTL0        (I2C2 + 0x00u)
#define I2C_CTL1        (I2C2 + 0x04u)
#define I2C_DATA        (I2C2 + 0x10u)
#define I2C_STAT0       (I2C2 + 0x14u)
#define I2C_STAT1       (I2C2 + 0x18u)
#define I2C_CKCFG       (I2C2 + 0x1Cu)
#define I2C_RT          (I2C2 + 0x20u)
#define C_START         (1u << 8)
#define C_STOP          (1u << 9)
#define C_ACKEN         (1u << 10)
#define C_POAP          (1u << 11)
#define S_SBSEND        (1u << 0)
#define S_ADDSEND       (1u << 1)
#define S_BTC           (1u << 2)
#define S_RBNE          (1u << 6)
#define S_TBE           (1u << 7)
#define S_ERR           ((1u << 8) | (1u << 9))     // BERR, LOSTARB
#define S_AERR          (1u << 10)
#define HW_TIMEOUT      (HCLK_HZ / 500)             // 2 ms

static bool useHw;

static void af4_od(uint32_t port, uint32_t pin)
{
    uint32_t reg = port + 0x24, sh = 4 * (pin - 8);
    REG32(reg) = (REG32(reg) & ~(0xFu << sh)) | (4u << sh);
    REG32(port + 0x04) |= 1u << pin;
    REG32(port + 0x08) |= 3u << (2 * pin);
    REG32(port + 0x0C) = (REG32(port + 0x0C) & ~(3u << (2 * pin))) | (1u << (2 * pin));
    REG32(port + 0x00) = (REG32(port + 0x00) & ~(3u << (2 * pin))) | (2u << (2 * pin));
}

static void hw_init(void)
{
    uint32_t pclk = HCLK_HZ / 4, mhz = pclk / 1000000u;
    REG32(RCU_APB1EN) |= 1u << 23;
    REG32(RCU_APB1RST) |= 1u << 23;
    REG32(RCU_APB1RST) &= ~(1u << 23);
    REG32(I2C_CTL1) = mhz;
    REG32(I2C_CKCFG) = (pclk + 2 * 100000 - 1) / (2 * 100000);
    REG32(I2C_RT) = mhz + 1;
    REG32(I2C_CTL0) = 1u | C_ACKEN;
    af4_od(GPIOA, SCL);
    af4_od(GPIOC, SDA);
}

// 1 = flag, 0 = NAK, -1 = fault
static int hw_wait(uint32_t mask)
{
    uint32_t t = REG32(DWT_CYCCNT);
    for (;;)
    {
        uint32_t st = REG32(I2C_STAT0);
        if (st & S_ERR)
            return -1;
        if (st & S_AERR)
        {
            REG32(I2C_STAT0) = ~S_AERR & 0xFFFF;
            return 0;
        }
        if (st & mask)
            return 1;
        if (REG32(DWT_CYCCNT) - t > HW_TIMEOUT)
            return -1;
    }
}

static bool hw_stop(void)
{
    REG32(I2C_CTL0) |= C_STOP;
    uint32_t t = REG32(DWT_CYCCNT);
    while (REG32(I2C_CTL0) & C_STOP)
        if (REG32(DWT_CYCCNT) - t > HW_TIMEOUT)
            return false;
    return true;
}

static bool hw_start(uint8_t addrByte)
{
    uint32_t t = REG32(DWT_CYCCNT);
    while (REG32(I2C_STAT1) & 2)
        if (REG32(DWT_CYCCNT) - t > HW_TIMEOUT)
            return false;
    REG32(I2C_CTL0) |= C_START;
    if (hw_wait(S_SBSEND) != 1)
        return false;
    REG32(I2C_DATA) = addrByte;
    int r = hw_wait(S_ADDSEND);
    if (r == 0)
        hw_stop();
    return r == 1;
}

static void hw_fault(void)
{
    hw_init();
}

static bool hw_write(const uint8_t *d, int n)
{
    if (!hw_start(ADDR << 1))
    {
        hw_fault();
        return false;
    }
    (void) REG32(I2C_STAT0);
    (void) REG32(I2C_STAT1);
    for (int i = 0; i < n; i++)
    {
        if (hw_wait(S_TBE) != 1)
        {
            hw_fault();
            return false;
        }
        REG32(I2C_DATA) = d[i];
    }
    if (hw_wait(S_BTC) != 1)
    {
        hw_fault();
        return false;
    }
    return hw_stop();
}

static bool hw_read(uint8_t *buf, int n)
{
    uint32_t c = REG32(I2C_CTL0) & ~(C_ACKEN | C_POAP | C_START | C_STOP);
    REG32(I2C_CTL0) = c | (n >= 2 ? C_ACKEN : 0) | (n == 2 ? C_POAP : 0);
    if (!hw_start((ADDR << 1) | 1))
    {
        REG32(I2C_CTL0) &= ~C_POAP;
        hw_fault();
        return false;
    }
    bool ok = true;
    (void) REG32(I2C_STAT0);
    (void) REG32(I2C_STAT1);
    if (n == 1)
    {
        REG32(I2C_CTL0) |= C_STOP;
        ok = hw_wait(S_RBNE) == 1;
        buf[0] = REG32(I2C_DATA);
    }
    else if (n == 2)
    {
        REG32(I2C_CTL0) &= ~C_ACKEN;
        ok = hw_wait(S_BTC) == 1;
        REG32(I2C_CTL0) |= C_STOP;
        buf[0] = REG32(I2C_DATA);
        buf[1] = REG32(I2C_DATA);
    }
    else
    {
        for (int i = 0; i < n - 3 && ok; i++)
        {
            ok = hw_wait(S_RBNE) == 1;
            buf[i] = REG32(I2C_DATA);
        }
        ok = ok && hw_wait(S_BTC) == 1;
        REG32(I2C_CTL0) &= ~C_ACKEN;
        buf[n - 3] = REG32(I2C_DATA);
        ok = ok && hw_wait(S_BTC) == 1;
        REG32(I2C_CTL0) |= C_STOP;
        buf[n - 2] = REG32(I2C_DATA);
        ok = ok && hw_wait(S_RBNE) == 1;
        buf[n - 1] = REG32(I2C_DATA);
    }
    uint32_t t = REG32(DWT_CYCCNT);
    while (ok && (REG32(I2C_CTL0) & C_STOP))
        if (REG32(DWT_CYCCNT) - t > HW_TIMEOUT)
            ok = false;
    if (!ok)
    {
        hw_fault();
        return false;
    }
    REG32(I2C_CTL0) &= ~C_POAP;
    return true;
}

static bool i2c_write(const uint8_t *d, int n)
{
    return useHw ? hw_write(d, n) : bb_write(d, n);
}

// Register address as its own write + STOP, then a read (as the stock).
static bool read_reg(uint16_t reg, uint8_t *buf, int n)
{
    uint8_t r[2] = {reg >> 8, reg & 0xFF};
    if (!i2c_write(r, 2))
        return false;
    return useHw ? hw_read(buf, n) : bb_read(buf, n);
}

static void od_pin(uint32_t port, uint32_t pin)
{
    REG32(port + 0x18) = 1u << pin;
    REG32(port + 0x04) |= 1u << pin;                               // open drain
    REG32(port + 0x08) |= 3u << (2 * pin);
    REG32(port + 0x0C) = (REG32(port + 0x0C) & ~(3u << (2 * pin))) | (1u << (2 * pin));
    REG32(port + 0x00) = (REG32(port + 0x00) & ~(3u << (2 * pin))) | (1u << (2 * pin));
}

void input_init(void)
{
    REG32(DEMCR) |= 1u << 24;
    REG32(DWT_CTRL) |= 1;
    REG32(RCU_AHB1EN) |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4);
    od_pin(GPIOA, SCL);
    od_pin(GPIOC, SDA);
    // INT: input, pull-up. RST: push-pull, low for 20 ms, then 200 ms.
    REG32(GPIOE + 0x00) &= ~(3u << (2 * INT));
    REG32(GPIOE + 0x0C) = (REG32(GPIOE + 0x0C) & ~(3u << (2 * INT))) | (1u << (2 * INT));
    REG32(GPIOE + 0x04) &= ~(1u << RST);
    REG32(GPIOE + 0x18) = 1u << (RST + 16);
    REG32(GPIOE + 0x00) = (REG32(GPIOE + 0x00) & ~(3u << (2 * RST))) | (1u << (2 * RST));
    // EXTI line 5 <- PE5, falling edge (pending bit only, NVIC line off).
    REG32(RCU_APB2EN) |= 1u << 14;
    REG32(SYSCFG_EXTISS1) = (REG32(SYSCFG_EXTISS1) & ~(0xFu << 4)) | (4u << 4);
    REG32(EXTI_FTEN) |= 1u << 5;
    REG32(EXTI_INTEN) |= 1u << 5;   // the emulator latches PD only when enabled
    REG32(EXTI_PD) = 1u << 5;
    i2c_stop();
    delay_ms(20);
    REG32(GPIOE + 0x18) = 1u << RST;
    delay_ms(200);
    // Hardware I2C2 first (what the emulator models), then the bit-bang.
    static const uint8_t a[] = {0xD1, 0x01}, b[] = {0xD1, 0x09};
    uint8_t chip[4];
    for (int t = 0; t < 2 && !present; t++)
    {
        useHw = t == 0;
        if (useHw)
            hw_init();
        else
        {
            REG32(RCU_APB1EN) &= ~(1u << 23);
            od_pin(GPIOA, SCL);
            od_pin(GPIOC, SDA);
        }
        present = i2c_write(a, 2) && read_reg(0xD204, chip, 4) && i2c_write(b, 2);
    }
}

static uint16_t zone(int x, int y)
{
    if (x < 180)
    {
        int dx = x - STICK_X, dy = y - STICK_Y;
        uint16_t k = 0;
        if (dx < -STICK_DEAD)
            k |= KEY_LEFT;
        if (dx > STICK_DEAD)
            k |= KEY_RIGHT;
        if (dy < -STICK_DEAD)
            k |= KEY_UP;
        if (dy > STICK_DEAD)
            k |= KEY_DOWN;
        return k;
    }
    if (x < 300)
        return y < 90 ? KEY_F1 : y >= 230 ? KEY_F4 : KEY_CHGW;
    return y < 150 ? KEY_USE : KEY_FIRE;
}

static void poll_touch(void)
{
    // Read only when a report is pending (edge latched, or INT still low);
    // the state holds until the next report (the release report clears it).
    bool pending = (REG32(EXTI_PD) & (1u << 5)) || !(REG32(GPIOE + 0x10) & (1u << INT));
    if (!pending)
        return;
    REG32(EXTI_PD) = 1u << 5;
    uint8_t f[7];
    if (!read_reg(0xD000, f, 7) || f[6] != 0xAB)
        return;
    int n = f[5] & 7;
    if (f[0] & 0x80)
    {
        // Side key report: held while the low nibble is non-zero.
        int x = f[1] << 4 | f[3] >> 4;
        static const uint16_t map[4] = {KEY_F1, KEY_FIRE, KEY_USE, KEY_CHGW};
        int code = x < 500 ? 0 : x < 750 ? 1 : x < 1000 ? 2 : 3;
        sideKeys = (f[0] & 0x0F) ? map[code] : 0;
        touchKeys = 0;
    }
    else
    {
        uint16_t k = 0;
        if ((f[0] & 0x0F) == 6)
            k |= zone(f[1] << 4 | f[3] >> 4, f[2] << 4 | (f[3] & 0x0F));
        for (int i = 1; i < n && i < 5; i++)
        {
            uint8_t e[5];
            if (read_reg(0xD000 + 7 + 5 * (i - 1), e, 5) && (e[0] & 0x0F) == 6)
                k |= zone(e[1] << 4 | e[3] >> 4, e[2] << 4 | (e[3] & 0x0F));
        }
        touchKeys = k;
    }
    static const uint8_t ack[] = {0xD0, 0x00, 0xAB};
    i2c_write(ack, 3);
}

// PD6 power key (pressed = 1). At boot it may still be held from the
// power-on press: it counts only after a release.
static void poll_power(void)
{
    bool down = REG32(GPIOD + 0x10) & (1u << 6);
    if (powerState == 0)
    {
        if (!down)
            powerState = 1;
    }
    else if (powerState == 1)
    {
        if (down)
        {
            powerState = 2;
            powerSince = time_ms();
        }
    }
    else if (!down)
        powerState = 1;
    else if (time_ms() - powerSince > POWER_OFF_MS)
    {
        // As opendm40 power_off: PE2 = 0 and PC4 = 0 drop the power hold;
        // on battery the meter switches off when the key is released.
        lcd_backlight(0);
        lcd_fill(0, 0, LCD_W, LCD_H, 0);
        REG32(GPIOE + 0x18) = 1u << (2 + 16);
        REG32(GPIOC + 0x18) = 1u << (4 + 16);
        for (;;)
            wdt_kick();
    }
}

uint16_t input_keys(void)
{
    if (present)
        poll_touch();
    poll_power();
    return touchKeys | sideKeys;
}

void initKeyboard(void) {}
void getKeys(uint16_t *keys) { *keys = input_keys(); }
void getAnalogInput(int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry)
{
    *lx = *ly = *rx = *ry = 0;
}

bool touchPresent(void)
{
    return present;
}
