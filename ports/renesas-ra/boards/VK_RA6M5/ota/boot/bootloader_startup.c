/*
 * Minimal bare-metal startup for the OTA bootloader.
 *
 * No FSP, no CMSIS_Init, no SystemInit.  We rely on the chip's
 * reset-default clock (HOCO ~48 MHz) which is plenty for a few hundred
 * memory reads + a vector-table jump.  The chosen app's reset handler
 * will set up its own clocks, FPU, and peripherals.
 *
 * .data and .bss are not used by the bootloader (everything is local
 * stack / .rodata) but we initialise them anyway to be safe.
 */

#include <stdint.h>

extern int bootloader_main(void);

/* Symbols populated by the linker script (bootloader.ld). */
extern uint32_t __etext;             /* end of .text in flash */
extern uint32_t __data_start__;
extern uint32_t __data_end__;
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;
extern uint32_t __stack_top__;

void Reset_Handler(void);
void Default_Handler(void);

void __attribute__((naked, noreturn)) Reset_Handler(void) {
    /* Copy .data from flash load addr (__etext) to RAM. */
    uint32_t *src = &__etext;
    uint32_t *dst = &__data_start__;
    while (dst < &__data_end__) {
        *dst++ = *src++;
    }
    /* Zero .bss. */
    for (uint32_t *p = &__bss_start__; p < &__bss_end__; ++p) {
        *p = 0;
    }
    /* Hand off — bootloader_main never returns. */
    bootloader_main();
    while (1) { }
}

void __attribute__((noreturn)) Default_Handler(void) {
    /* Any unexpected exception -> spin so JLink can attach. */
    while (1) { }
}

/* Tiny libc-style helpers — uECC and SHA-256 want them, but we link
 * with -nostdlib.  These are the standard small implementations. */
void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int c, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
int memcmp(const void *a, const void *b, unsigned long n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        ++p; ++q;
    }
    return 0;
}

/* Minimal Cortex-M33 vector table — first 16 system vectors only.
 * Application IRQs are NOT vectored here; if the bootloader ever takes
 * an interrupt before jumping to the app, we hit Default_Handler. */
__attribute__((section(".vectors"), used))
const uint32_t vectors[] = {
    (uint32_t)&__stack_top__,        /*  0 Initial MSP                  */
    (uint32_t)&Reset_Handler,        /*  1 Reset                        */
    (uint32_t)&Default_Handler,      /*  2 NMI                          */
    (uint32_t)&Default_Handler,      /*  3 HardFault                    */
    (uint32_t)&Default_Handler,      /*  4 MemManage                    */
    (uint32_t)&Default_Handler,      /*  5 BusFault                     */
    (uint32_t)&Default_Handler,      /*  6 UsageFault                   */
    (uint32_t)&Default_Handler,      /*  7 SecureFault (M33)            */
    0, 0, 0,                          /*  8-10 Reserved                  */
    (uint32_t)&Default_Handler,      /* 11 SVCall                       */
    (uint32_t)&Default_Handler,      /* 12 DebugMon                     */
    0,                                /* 13 Reserved                     */
    (uint32_t)&Default_Handler,      /* 14 PendSV                       */
    (uint32_t)&Default_Handler,      /* 15 SysTick                      */
};
