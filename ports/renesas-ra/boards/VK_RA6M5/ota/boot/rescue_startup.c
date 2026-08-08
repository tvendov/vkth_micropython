/* Rescue stub startup — initialises C runtime and copies the .ramcode
 * section from flash LMA → RAM VMA.  Functions tagged with the
 * `RAMCODE` attribute (in rescue_fcu.c) execute from RAM so the FCU
 * can erase/program code-flash while we're "running" without stalling
 * on instruction fetches.
 */

#include <stdint.h>

extern int rescue_main(void);

extern uint32_t __etext;
extern uint32_t __ramcode_load__, __ramcode_start__, __ramcode_end__;
extern uint32_t __data_start__, __data_end__;
extern uint32_t __bss_start__,  __bss_end__;
extern uint32_t __stack_top__;

void Reset_Handler(void);
void Default_Handler(void);

void __attribute__((naked, noreturn)) Reset_Handler(void) {
    /* Copy .ramcode (FCU CF-write code) from flash LMA → RAM VMA. */
    {
        uint32_t *src = &__ramcode_load__;
        uint32_t *dst = &__ramcode_start__;
        while (dst < &__ramcode_end__) *dst++ = *src++;
    }
    /* Copy .data from flash → RAM. */
    {
        /* .data immediately follows the .ramcode payload in flash. */
        uint32_t *src = &__etext;
        uint32_t *dst = &__data_start__;
        while (dst < &__data_end__) *dst++ = *src++;
    }
    /* Zero .bss. */
    for (uint32_t *p = &__bss_start__; p < &__bss_end__; ++p) *p = 0;

    rescue_main();
    while (1) { }
}

void __attribute__((noreturn)) Default_Handler(void) {
    while (1) { }
}

/* Tiny libc helpers (we link with -nostdlib).  Same as in the older
 * bootloader_startup.c — kept here so the rescue stub is self-contained. */
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

__attribute__((section(".vectors"), used))
const uint32_t vectors[] = {
    (uint32_t)&__stack_top__,
    (uint32_t)&Reset_Handler,
    (uint32_t)&Default_Handler,        /* NMI */
    (uint32_t)&Default_Handler,        /* HardFault */
    (uint32_t)&Default_Handler,        /* MemManage */
    (uint32_t)&Default_Handler,        /* BusFault */
    (uint32_t)&Default_Handler,        /* UsageFault */
    (uint32_t)&Default_Handler,        /* SecureFault (M33) */
    0, 0, 0,
    (uint32_t)&Default_Handler,        /* SVCall */
    (uint32_t)&Default_Handler,        /* DebugMon */
    0,
    (uint32_t)&Default_Handler,        /* PendSV */
    (uint32_t)&Default_Handler,        /* SysTick */
};
