/*
 * Vector table and reset for the DM40 native image (link.ld).
 *
 * The FS-loader starts the app with msp = [0x08004000], pc = [0x08004004].
 * Word 0 keeps the stock value (0x2002F7F8) as opendm40 does; reset moves
 * the stack to the TCM top at once.
 */
#include <stdint.h>
#include <string.h>
#include "dm40.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _stcm_bss, _etcm_bss, _stack_top;
extern int main(void);
void SysTick_Handler(void);
void reset_handler(void);
void fault_handler(void);

__attribute__((section(".vectors"), used))
const void *vectors[16 + 91] = {
    (void *)0x2002F7F8,
    reset_handler,
    fault_handler,          // NMI
    fault_handler,          // HardFault
    fault_handler,          // MemManage
    fault_handler,          // BusFault
    fault_handler,          // UsageFault
    0, 0, 0, 0,
    fault_handler,          // SVCall
    fault_handler,          // DebugMon
    0,
    fault_handler,          // PendSV
    SysTick_Handler,
    // External interrupts: never enabled; zero entries are not taken.
};

__attribute__((naked, section(".text.reset"))) void reset_handler(void)
{
    __asm volatile(
        "cpsid i\n"
        "movs r0, #0\n"
        "msr control, r0\n"
        "isb\n"
        // TCM SRAM clock: RCU_AHB1EN.TCMSRAMEN (bit 20)
        "ldr r0, =0x40023830\n"
        "ldr r1, [r0]\n"
        "orr r1, r1, #0x100000\n"
        "str r1, [r0]\n"
        // FPU: CPACR CP10/CP11 full access
        "ldr r0, =0xE000ED88\n"
        "ldr r1, [r0]\n"
        "orr r1, r1, #0xF00000\n"
        "str r1, [r0]\n"
        "dsb\n"
        "isb\n"
        "ldr sp, =_stack_top\n"
        // VTOR = our table (the FS-loader leaves it at 0x080a0000)
        "ldr r0, =0xE000ED08\n"
        "ldr r1, =vectors\n"
        "str r1, [r0]\n"
        "dsb\n"
        "isb\n"
        "b reset_c\n");
}

__attribute__((used, section(".text.reset"))) void reset_c(void)
{
    memcpy(&_sdata, &_sidata, (uint32_t)&_edata - (uint32_t)&_sdata);
    memset(&_sbss, 0, (uint32_t)&_ebss - (uint32_t)&_sbss);
    memset(&_stcm_bss, 0, (uint32_t)&_etcm_bss - (uint32_t)&_stcm_bss);
    hal_init();
    main();
    for (;;)
        ;
}

// A fault: red bar on the panel and a hang; the watchdog then resets.
__attribute__((used)) void fault_handler(void)
{
    __disable_irq();
    lcd_fill(0, 310, 480, 10, 0xF800);
    for (;;)
        ;
}
