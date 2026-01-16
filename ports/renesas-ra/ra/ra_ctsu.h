/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Augment Agent
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

#ifndef RA_CTSU_H
#define RA_CTSU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum supported touch channels (RA4M2 has up to 14 TS pins)
#define RA_CTSU_MAX_CHANNELS    14

// CTSU channel configuration
typedef struct {
    uint8_t  ts_channel;     // TS channel number (0-13 for RA4M2)
    uint16_t threshold;      // Touch detection threshold
    bool     enabled;        // Channel enabled flag
} ra_ctsu_channel_cfg_t;

// Initialize CTSU hardware
// Returns 0 on success, negative on error
int ra_ctsu_init(void);

// Deinitialize CTSU hardware
void ra_ctsu_deinit(void);

// Configure a touch channel
// ts_channel: CTSU TS pin number (e.g., 0 for TS0)
// Returns 0 on success, negative on error
int ra_ctsu_channel_config(uint8_t ts_channel, uint16_t threshold);

// Perform a single scan and get raw count for a channel
// ts_channel: CTSU TS pin number
// Returns raw count value (0-65535), or negative on error
int32_t ra_ctsu_read(uint8_t ts_channel);

// Check if touch is detected (count > threshold)
// ts_channel: CTSU TS pin number
// Returns 1 if touched, 0 if not, negative on error
int ra_ctsu_is_touched(uint8_t ts_channel);

// Get number of configured channels
uint8_t ra_ctsu_get_channel_count(void);

// Map GPIO pin to CTSU TS channel
// Returns TS channel number (0-13), or -1 if pin is not a CTSU pin
int8_t ra_ctsu_pin_to_channel(uint16_t pin);

#ifdef __cplusplus
}
#endif

#endif // RA_CTSU_H

