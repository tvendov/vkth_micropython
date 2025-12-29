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

#ifndef RA_RA_OPAMP_H_
#define RA_RA_OPAMP_H_

#include <stdint.h>
#include <stdbool.h>

// OPAMP power modes
typedef enum {
    RA_OPAMP_MODE_LOW_POWER = 0,    // Low power mode
    RA_OPAMP_MODE_HIGH_SPEED = 1,   // High speed mode
} ra_opamp_mode_t;

// OPAMP channel mask
#define RA_OPAMP_CH0    (1U << 0)
#define RA_OPAMP_CH1    (1U << 1)
#define RA_OPAMP_CH2    (1U << 2)
#define RA_OPAMP_CH3    (1U << 3)

// Maximum number of OPAMP channels (RA4M1 has 4 channels: 0,1,2,3)
#define RA_OPAMP_MAX_CH     4

// Stabilization wait times in microseconds
#define RA_OPAMP_WAIT_LP_US     650     // Low power mode
#define RA_OPAMP_WAIT_HS_US     13      // High speed mode

// Function prototypes

/**
 * @brief Initialize the OPAMP module
 * @param mode Power mode (low power or high speed)
 * @return true on success, false on failure
 */
bool ra_opamp_init(ra_opamp_mode_t mode);

/**
 * @brief Deinitialize the OPAMP module
 */
void ra_opamp_deinit(void);

/**
 * @brief Start one or more OPAMP channels
 * @param channel_mask Bitmask of channels to start (RA_OPAMP_CH0, etc.)
 * @return true on success, false on failure
 */
bool ra_opamp_start(uint8_t channel_mask);

/**
 * @brief Stop one or more OPAMP channels
 * @param channel_mask Bitmask of channels to stop
 * @return true on success, false on failure
 */
bool ra_opamp_stop(uint8_t channel_mask);

/**
 * @brief Get the operating status of OPAMP channels
 * @return Bitmask of currently running channels
 */
uint8_t ra_opamp_status(void);

/**
 * @brief Check if OPAMP is initialized
 * @return true if initialized, false otherwise
 */
bool ra_opamp_is_init(void);

/**
 * @brief Get the current power mode
 * @return Current power mode
 */
ra_opamp_mode_t ra_opamp_get_mode(void);

/**
 * @brief Get the minimum stabilization wait time for current mode
 * @return Wait time in microseconds
 */
uint32_t ra_opamp_get_wait_time(void);

#endif /* RA_RA_OPAMP_H_ */

