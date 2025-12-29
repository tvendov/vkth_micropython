/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 MicroPython contributors
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

#include "hal_data.h"
#include "ra_opamp.h"

#if defined(RA4M1)

// Module state
static bool opamp_initialized = false;
static ra_opamp_mode_t current_mode = RA_OPAMP_MODE_LOW_POWER;

// OPAMP register base address for RA4M1
// Reference: RA4M1 Hardware Manual, Chapter 37 - Operational Amplifier
#define OPAMP_BASE  (0x40086000UL)

// Register offsets (match RA4M1 CMSIS header R7FA4M1AB.h, R_OPAMP structure)
//   RESERVED[8] : 0x00-0x07
//   AMPMC       : 0x08
//   AMPTRM      : 0x09
//   AMPTRS      : 0x0A
//   AMPC        : 0x0B
//   AMPMON      : 0x0C
#define AMPMC_OFFSET    0x08    // OPAMP Mode Control Register
#define AMPTRM_OFFSET   0x09    // OPAMP Trigger Mode Register
#define AMPTRS_OFFSET   0x0A    // OPAMP Trigger Select Register
#define AMPC_OFFSET     0x0B    // OPAMP Control Register
#define AMPMON_OFFSET   0x0C    // OPAMP Monitor Register

// Register access macros
#define OPAMP_AMPMC     (*((volatile uint8_t *)(OPAMP_BASE + AMPMC_OFFSET)))
#define OPAMP_AMPC      (*((volatile uint8_t *)(OPAMP_BASE + AMPC_OFFSET)))
#define OPAMP_AMPMON    (*((volatile uint8_t *)(OPAMP_BASE + AMPMON_OFFSET)))
#define OPAMP_AMPTRS    (*((volatile uint8_t *)(OPAMP_BASE + AMPTRS_OFFSET)))
#define OPAMP_AMPTRM    (*((volatile uint8_t *)(OPAMP_BASE + AMPTRM_OFFSET)))

// AMPMC register bits
#define AMPMC_AMPSP_Pos     7       // Power mode bit position
#define AMPMC_AMPSP_Msk     (1 << AMPMC_AMPSP_Pos)

// Module start/stop control
// According to bsp_module_stop.h:
//   BSP_MSTP_REG_FSP_IP_OPAMP(channel) = R_MSTP->MSTPCRD
//   BSP_MSTP_BIT_FSP_IP_OPAMP(channel) = (1U << (31U - channel));
// For OPAMP channel 0 this is bit 31.
#define MSTP_OPAMP_Pos      31      // OPAMP module stop bit in MSTPCRD (channel 0)
#define SYSTEM_MSTPCRD      (*((volatile uint32_t *)0x4001E00C))

// Simple delay function using busy loop
static void opamp_delay_us(uint32_t us) {
    // Approximate delay - assumes ~48MHz clock
    volatile uint32_t count = us * 12;
    while (count--) {
        __asm__ volatile ("nop");
    }
}

bool ra_opamp_init(ra_opamp_mode_t mode) {
    if (opamp_initialized) {
        return true;  // Already initialized
    }

    // Enable OPAMP module (clear module stop bit)
    SYSTEM_MSTPCRD &= ~(1 << MSTP_OPAMP_Pos);

    // Small delay for module startup
    opamp_delay_us(10);

    // Stop all OPAMP channels first
    OPAMP_AMPC = 0x00;

    // Set power mode
    if (mode == RA_OPAMP_MODE_HIGH_SPEED) {
        OPAMP_AMPMC = AMPMC_AMPSP_Msk;  // High speed mode
    } else {
        OPAMP_AMPMC = 0x00;  // Low power mode
    }

    // Software trigger mode for all channels
    OPAMP_AMPTRS = 0x00;
    OPAMP_AMPTRM = 0x00;

    current_mode = mode;
    opamp_initialized = true;

    return true;
}

void ra_opamp_deinit(void) {
    if (!opamp_initialized) {
        return;
    }

    // Stop all OPAMP channels
    OPAMP_AMPC = 0x00;

    // Disable OPAMP module (set module stop bit)
    SYSTEM_MSTPCRD |= (1 << MSTP_OPAMP_Pos);

    opamp_initialized = false;
}

bool ra_opamp_start(uint8_t channel_mask) {
    if (!opamp_initialized) {
        return false;
    }

    // Validate channel mask (only CH0-CH3 valid for RA4M1)
    if (channel_mask & ~0x0F) {
        return false;
    }

    // Start the specified channels
    OPAMP_AMPC |= channel_mask;

    // Wait for stabilization
    opamp_delay_us(ra_opamp_get_wait_time());

    return true;
}

bool ra_opamp_stop(uint8_t channel_mask) {
    if (!opamp_initialized) {
        return false;
    }

    // Stop the specified channels
    OPAMP_AMPC &= ~channel_mask;

    return true;
}

uint8_t ra_opamp_status(void) {
    if (!opamp_initialized) {
        return 0;
    }
    return OPAMP_AMPMON;
}

bool ra_opamp_is_init(void) {
    return opamp_initialized;
}

ra_opamp_mode_t ra_opamp_get_mode(void) {
    return current_mode;
}

uint32_t ra_opamp_get_wait_time(void) {
    return (current_mode == RA_OPAMP_MODE_HIGH_SPEED) ?
           RA_OPAMP_WAIT_HS_US : RA_OPAMP_WAIT_LP_US;
}

#endif // defined(RA4M1)

