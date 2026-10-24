/*
 * The way back from Doom to the vendor updater (and from there to opendm40
 * or the stock firmware), in the spirit of opendm40's shim (boot.rs):
 *
 *  - a watchdog reset during a Doom session (FWDGTRSTF without SWRSTF /
 *    PORRSTF: the flags are cleared at every start) means Doom hung: go to
 *    the updater at once instead of looping;
 *  - the power key pressed and held RECOVERY_HOLD_MS within the window after
 *    start-up (after the power-on press was released): go to the updater.
 *
 * "Go to the updater" = zero the 12-byte loader marker + reset: the vendor
 * FS-loader then shows its update screen with its own USB disk until a
 * dm40*.bin is copied there and installed.
 */
#include "dm40.h"
#include "graphics.h"

#define RCU_RSTSCK          0x40023874u
#define RSTFC               (1u << 24)
#define PORRSTF             (1u << 27)
#define SWRSTF              (1u << 28)
#define FWDGTRSTF           (1u << 29)
#define GPIOD_ISTAT         0x40020C10u
#define RECOVERY_WINDOW_MS  3000u
#define RECOVERY_HOLD_MS    3000u

static uint32_t rstsck;

static bool power_down(void)
{
    return REG32(GPIOD_ISTAT) & (1u << 6);
}

void enter_updater(const char *why)
{
    displayPrintln(1, "%s: returning to the updater", why);
    bool ok = marker_zero();
    displayPrintln(1, ok ? "Loader marker cleared, resetting." : "Marker write FAILED.");
    delay_ms(1500);
    if (!ok)
        for (;;)
            wdt_kick();
    NVIC_SystemReset();
}

void recovery_check(void)
{
    rstsck = REG32(RCU_RSTSCK);
    REG32(RCU_RSTSCK) |= RSTFC;
    if ((rstsck & FWDGTRSTF) && !(rstsck & (SWRSTF | PORRSTF)))
        enter_updater("Watchdog reset");

    // The power-on press: wait for its release (at most 2 s).
    uint32_t t = time_ms();
    while (power_down() && time_ms() - t < 2000)
        wdt_kick();
    displayPrintln(1, "Hold POWER %us now: back to the updater", (unsigned) (RECOVERY_HOLD_MS / 1000));
    t = time_ms();
    uint32_t since = 0;
    bool held = false;
    while (time_ms() - t < RECOVERY_WINDOW_MS || held)
    {
        wdt_kick();
        if (!power_down())
        {
            held = false;
            continue;
        }
        if (!held)
        {
            held = true;
            since = time_ms();
        }
        else if (time_ms() - since >= RECOVERY_HOLD_MS)
            enter_updater("Power key held");
    }
}
