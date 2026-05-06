/*
 * lorawan/glue/utilities.c
 *
 * Portable C utility helpers used by the imported Renesas LoRaMac tree
 * (memcpy1, memcpyr, memset1, srand1) plus board-glue functions
 * (BoardCriticalSectionBegin/End, BoardGetUniqueId, etc.).
 *
 * The memcpy/memset variants exist so LoRaMac code can run on platforms
 * whose libc ones are non-trivial; on RA4M2 we just forward to standard
 * library equivalents.
 */

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "glue/board.h"

/* Minimal PRNG — MicroPython's port is built `-nostdlib` so libc's
   srand/rand are not linked. We provide a simple linear-congruential
   generator (Numerical Recipes constants), which is enough for the
   LoRaMac tree's only use case (channel hopping seed). */
static uint32_t s_lcg_state = 1u;
static int rand_lcg(void) {
    s_lcg_state = s_lcg_state * 1664525u + 1013904223u;
    return (int)(s_lcg_state & 0x7FFFFFFFu);
}

#if defined(MICROPY_HW_LORA_STACK_RENESAS) && MICROPY_HW_LORA_STACK_RENESAS

/* ---- Critical-section nesting -------------------------------------- */

volatile uint8_t g_critical_sectioin_count;

void BoardCriticalSectionBegin(uint32_t *mask) {
    if (mask != NULL) {
        *mask = (uint32_t)disable_irq();
    } else {
        (void)disable_irq();
    }
}

void BoardCriticalSectionEnd(uint32_t *mask) {
    enable_irq(mask != NULL ? (mp_uint_t)*mask : 0);
}

void BoardTimerDisableIrq(void) {
    /* No-op on this port: AGT4 ISR is reentrancy-safe and the
       CRITICAL_SECTION macros in boards/utilities.h already mask
       PRIMASK around protected blocks. */
}

void BoardTimerEnableIrqNoClear(void) {
    /* No-op — see BoardTimerDisableIrq. */
}

/* Global IRQ mask helpers — used by LoRaMacSetLowPower() to bracket
   its sleep-decision section. We toggle PRIMASK directly; nesting is
   not tracked here because LoRaMac wraps the call in a single
   begin/end pair. */
void BoardDisableAllIrq(void) {
    (void)disable_irq();
}

void BoardEnableAllIrq(void) {
    enable_irq(0);
}

/* MCU sleep entry — NOP. MicroPython's `machine.lightsleep()` is the
   user-facing path; LoRaMac calls this only when it has determined the
   MAC is idle, but we let the application layer decide whether to
   actually enter low power. */
void SetLowPower(void) {
    /* deliberate no-op */
}

/* Radio IRQ preprocessor — NOP. Renesas Combo / Private LoRa ports
   multiplex DIO1 here; on plain LoRaWAN no preprocessing is needed. */
void BoardRadioIrqPreprocess(void) {
    /* deliberate no-op */
}

/* Implementation of upstream `system/delay.h`'s `DelayMs()`. The
   imported tree calls this from radio init / wakeup paths only — short
   delays (≤ 25 ms typical), so blocking is acceptable here. Phase 6+
   could route this through the timer service for asyncio-friendliness,
   but it would complicate the radio state machine without a measurable
   benefit. */
void DelayMs(uint32_t ms) {
    mp_hal_delay_ms(ms);
}

/* CCA timer — backed by the AGT4 ms tick. We expose a 16-bit count
   (millisecond resolution wrapping every ~65 s) which is more than
   enough for the CCA listen windows; CCA itself isn't used on
   LoRaWAN, so these only need to be syntactically valid. */
extern uint32_t lorawan_timer_now_ms(void);

uint16_t RpMcuCcaGetCurrentCount(void) {
    return (uint16_t)(lorawan_timer_now_ms() & 0xFFFFu);
}

uint16_t RpMcuCcaDiffTime(uint16_t countStart) {
    return (uint16_t)((lorawan_timer_now_ms() & 0xFFFFu) - countStart);
}

/* ---- Unique ID -------------------------------------------------------
 *
 * RA4M2 silicon unique ID is mapped to 0x01001C00..0x01001C0F (16 bytes).
 * soft-se only reads 8 bytes — we return the low 8.
 */

void BoardGetUniqueId(uint8_t *id) {
    if (id == NULL) {
        return;
    }
    const uint8_t *src = (const uint8_t *)0x01001C00u;
    for (size_t i = 0; i < BOARD_UNIQUE_ID_LEN; ++i) {
        id[i] = src[i];
    }
}

/* Battery level — soft-se uses this purely as a status byte; we return
   "unknown" sentinel (255). */
uint8_t BoardGetBatteryLevel(void) {
    return 255;
}

uint32_t BoardGetRandomSeed(void) {
    /* Mix the silicon unique ID with the AGT4 monotonic ms counter to
       de-correlate across boots without pulling in a hardware TRNG. */
    const uint32_t *uid = (const uint32_t *)0x01001C00u;
    return uid[0] ^ uid[1] ^ (uint32_t)mp_hal_ticks_us();
}

/* ---- LoRaMac-portable buffer helpers --------------------------------
 *
 * Identical to upstream `boards/mcu/utilities.c`. Provided here so we
 * do not need to import that FSP-bound source file.
 */

void srand1(uint32_t seed) {
    s_lcg_state = (seed != 0u) ? seed : 1u;
}

int32_t randr(int32_t min, int32_t max) {
    int32_t span = max - min + 1;
    if (span <= 0) {
        return min;
    }
    return (int32_t)(rand_lcg() % (uint32_t)span) + min;
}

void memcpy1(uint8_t *dst, const uint8_t *src, uint16_t size) {
    while (size--) {
        *dst++ = *src++;
    }
}

void memcpyr(uint8_t *dst, const uint8_t *src, uint16_t size) {
    dst = dst + (size - 1u);
    while (size--) {
        *dst-- = *src++;
    }
}

void memset1(uint8_t *dst, uint8_t value, uint16_t size) {
    while (size--) {
        *dst++ = value;
    }
}

int8_t Nibble2HexChar(uint8_t a) {
    a &= 0x0F;
    return (int8_t)((a < 10) ? ('0' + a) : ('A' + a - 10));
}

#endif /* MICROPY_HW_LORA_STACK_RENESAS */
