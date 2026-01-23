/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Vekatech
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

#include "bsp_api.h"  // fsp_err_t

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of channels that can be configured concurrently (threshold table size).
// This is NOT the maximum TS channel number supported by the MCU.
#define RA_CTSU_MAX_CHANNELS    14

// Maximum CTSU TS channel count supported by this wrapper (TS00..TS35).
// Used for parameter validation in ra_ctsu_read().
#define RA_CTSU_TS_CHANNEL_COUNT 36

// ---------------------------------------------------------------------------
// Public error codes (negative)
// ---------------------------------------------------------------------------
// Keep these stable: they are user-visible via the MicroPython binding.
#define RA_CTSU_ERR_NOT_INITIALIZED      (-1)
#define RA_CTSU_ERR_TS_OUT_OF_RANGE      (-2)
#define RA_CTSU_ERR_TOO_MANY_CHANNELS    (-3)
#define RA_CTSU_ERR_NOT_CONFIGURED       (-4)
#define RA_CTSU_ERR_INVALID_ARG          (-5)
#define RA_CTSU_ERR_TS_NOT_MAPPED        (-20)

// CTSU channel configuration
typedef struct {
    uint8_t  ts_channel;     // TS channel number (0..RA_CTSU_TS_CHANNEL_COUNT-1)
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

// Configure a touch channel by GPIO pin code (Port<<4 | Pin), e.g. P011 = 0x0B.
// Internally resolves to a TS channel using ra_ctsu_pin_to_channel().
// Returns 0 on success, negative on error.
int ra_ctsu_channel_config_pin(uint8_t pin_code, uint16_t threshold);

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

// Read back per-channel CTSU SO offsets currently stored in the FSP control block.
// - ts_channels[i] and so_values[i] correspond to the same configured element index.
// - out_count will be set to the number of configured channels.
// Returns 0 on success, negative wrapper error code on failure.
int ra_ctsu_get_offsets(uint8_t * ts_channels, uint16_t * so_values, uint32_t max_entries, uint32_t * out_count);

// Manually set the per-channel SO offset for a configured TS channel.
// - ts_channel: TS channel number (0..RA_CTSU_TS_CHANNEL_COUNT-1)
// - so_value:   10-bit SO value (0..1023)
// Returns 0 on success, negative wrapper error code on failure.
int ra_ctsu_set_offset(uint8_t ts_channel, uint16_t so_value);

// Debug: get the last FSP error observed by ra_ctsu.c.
// Returns FSP_SUCCESS if no error has been recorded since the last read attempt.
fsp_err_t ra_ctsu_last_fsp_err(void);

// Debug: get the last CTSU event observed by ra_ctsu.c callback.
// This is typically CTSU_EVENT_SCAN_COMPLETE (0) on success, or a bitmask of
// error events (e.g. overflow/ICOMP) when a scan completes with issues.
uint32_t ra_ctsu_last_event(void);

// ---------------------------------------------------------------------------
// Diagnosis test hook (manual / debug)
// ---------------------------------------------------------------------------
// This API is intended for testing the FSP CTSU diagnosis flow from MicroPython.
// It does not make a touch decision; it only runs the FSP diagnosis scan loop.
typedef struct {
    fsp_err_t data_get_err;    // Return value from the last R_CTSU_DataGet() in the diag loop
    fsp_err_t diagnosis_err;   // Return value from R_CTSU_Diagnosis() (valid when data_get_err==FSP_SUCCESS)
    uint32_t  last_event;      // CTSU event bitmask captured by callback (0 means normal completion)
    uint32_t  scans;           // Number of diagnosis scans attempted
} ra_ctsu_diag_result_t;

// Run CTSU diagnosis scan sequence.
// max_scans: max attempts (suggested 32). If 0, an internal default is used.
// Returns 0 if the function executed, negative on wrapper-level failure.
int ra_ctsu_diagnose(uint32_t max_scans, ra_ctsu_diag_result_t * p_result);

// ---------------------------------------------------------------------------
// Offset tuning helper (manual / debug)
// ---------------------------------------------------------------------------
// Implements the recommended flow after R_CTSU_Open(): scan -> wait callback -> R_CTSU_OffsetTuning.
// Repeat until offset tuning completes (FSP_SUCCESS) or the max scan limit is reached.
typedef struct {
    fsp_err_t offset_err;     // Return value from the last R_CTSU_OffsetTuning() call
    uint32_t  last_event;     // CTSU event bitmask captured by callback (0 means normal completion)
    uint32_t  scans;          // Number of scans attempted
} ra_ctsu_offset_result_t;

// Run CTSU offset tuning loop.
// max_scans: max attempts (suggested 32). If 0, an internal default is used.
// Returns 0 if the function executed, negative on wrapper-level failure.
int ra_ctsu_offset_tune(uint32_t max_scans, ra_ctsu_offset_result_t * p_result);

// Map GPIO pin to CTSU TS channel
// Returns TS channel number (0..RA_CTSU_TS_CHANNEL_COUNT-1), or -1 if pin is not a CTSU pin
int8_t ra_ctsu_pin_to_channel(uint16_t pin);

#ifdef __cplusplus
}
#endif

#endif // RA_CTSU_H

