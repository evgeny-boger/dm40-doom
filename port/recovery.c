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

// After a watchdog reset: where the last session was (dm40_diag) and the
// end of its log, until the power key is pressed (or 5 min).
static void show_diag(void)
{
    const struct dm40_diag *g = &dm40_diag;
    // At most 59 characters per line (480 / 8 pixels).
    displayPrintln(1, "Watchdog reset. Last tick: PC %08x LR %08x", (unsigned) g->prev_pc, (unsigned) g->prev_lr);
    if (g->fault)
    {
        displayPrintln(1, "Fault %u: PC %08x LR %08x", (unsigned) g->fault, (unsigned) g->fault_pc, (unsigned) g->fault_lr);
        displayPrintln(1, "CFSR %08x HFSR %08x", (unsigned) g->cfsr, (unsigned) g->hfsr);
        displayPrintln(1, "MMFAR %08x BFAR %08x", (unsigned) g->mmfar, (unsigned) g->bfar);
    }
    displayPrintln(1, "Log before the reset:");
    // The last LOG_LINES lines before this boot's output.
    enum { LOG_LINES = 16, LOG_COLS = 59 };
    uint32_t end = g->boot_pos, start = end, n = 0;
    while (start > 0 && end - start < sizeof(dbg_log) - 1)
    {
        if (dbg_log[(start - 1) & (sizeof(dbg_log) - 1)] == '\n' && start != end && ++n > LOG_LINES)
            break;
        start--;
    }
    char line[LOG_COLS + 1];
    uint32_t k = 0;
    for (uint32_t i = start; i < end; i++)
    {
        char c = dbg_log[i & (sizeof(dbg_log) - 1)];
        if (c == '\r')
            continue;
        if (c == '\n' || k == LOG_COLS)
        {
            line[k] = 0;
            displayPrintln(1, "%s", line);
            k = 0;
            if (c == '\n')
                continue;
        }
        line[k++] = (c >= 32 && c < 127) ? c : '.';
    }
    if (k)
    {
        line[k] = 0;
        displayPrintln(1, "%s", line);
    }
    displayPrintln(1, "Press POWER to go to the updater.");
    uint32_t t = time_ms();
    while (!power_down() && time_ms() - t < 300000u)
        wdt_kick();
}

void recovery_check(void)
{
    rstsck = REG32(RCU_RSTSCK);
    REG32(RCU_RSTSCK) |= RSTFC;
    if ((rstsck & FWDGTRSTF) && !(rstsck & (SWRSTF | PORRSTF)))
    {
        show_diag();
        enter_updater("Watchdog reset");
    }
    dm40_diag.fault = 0;

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
