/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jim Mussared
 * Copyright (c) 2025 Teodor Kostov / Vekatech Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Renesas RA port-specific implementation of machine.bitstream().
// Uses DWT cycle counter for precise timing on Cortex-M33/M4,
// and fast GPIO via POSR/PORR registers for minimal pin-toggle latency.

#include "py/mpconfig.h"
#include "py/mphal.h"
#include "pin.h"
#include "ra/ra_gpio.h"

#if MICROPY_PY_MACHINE_BITSTREAM

// Overhead in CPU cycles for the timing loop (branch, compare, etc.).
#define NS_CYCLES_OVERHEAD (6)

void machine_bitstream_high_low(mp_hal_pin_obj_t pin, uint32_t *timing_ns, const uint8_t *buf, size_t len) {
    // Extract port and bit mask for fast GPIO via POSR/PORR registers.
    uint32_t port = GPIO_PORT(pin->pin);
    uint16_t mask = (uint16_t)GPIO_MASK(pin->pin);

    // POSR = Port Output Set Register (write 1 to set pin HIGH).
    // PORR = Port Output Reset Register (write 1 to set pin LOW).
    // These are single-write atomic operations, much faster than PFS manipulation.
    volatile uint16_t *posr = _PPOSR(port);
    volatile uint16_t *porr = _PPORR(port);

    // Convert nanosecond timing to CPU cycle counts.
    uint32_t fcpu_mhz = SystemCoreClock / 1000000;
    for (size_t i = 0; i < 4; ++i) {
        timing_ns[i] = fcpu_mhz * timing_ns[i] / 1000;
        if (timing_ns[i] > NS_CYCLES_OVERHEAD) {
            timing_ns[i] -= NS_CYCLES_OVERHEAD;
        }
        if (i % 2 == 1) {
            // Convert low_time to total period (high_time + low_time).
            timing_ns[i] += timing_ns[i - 1];
        }
    }

    // Enable DWT cycle counter (available on Cortex-M33 and Cortex-M4).
    #if __CORTEX_M >= 4 && __CORTEX_M != 23
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    #endif

    mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = buf[i];
        for (size_t j = 0; j < 8; ++j) {
            #if __CORTEX_M >= 4 && __CORTEX_M != 23
            // DWT cycle-counter approach: precise to ~1 cycle.
            DWT->CYCCNT = 0;
            *posr = mask;   // pin HIGH
            uint32_t *t = &timing_ns[b >> 6 & 2];
            while (DWT->CYCCNT < t[0]) {
            }
            *porr = mask;   // pin LOW
            b <<= 1;
            while (DWT->CYCCNT < t[1]) {
            }
            #else
            // Fallback loop-based approach for Cortex-M23 (no DWT CYCCNT).
            *posr = mask;   // pin HIGH
            uint32_t *t = &timing_ns[b >> 6 & 2];
            for (volatile uint32_t c = t[0]; c > 0; --c) {
            }
            *porr = mask;   // pin LOW
            b <<= 1;
            for (volatile uint32_t c = t[1] - t[0]; c > 0; --c) {
            }
            #endif
        }
    }

    MICROPY_END_ATOMIC_SECTION(atomic_state);
}

#endif // MICROPY_PY_MACHINE_BITSTREAM

