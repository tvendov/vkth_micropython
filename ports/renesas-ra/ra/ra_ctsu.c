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

#include "ra_ctsu.h"
#include "bsp_api.h"
#include "r_ctsu.h"
#include "r_ctsu_api.h"
#include "vector_data.h"

#if MICROPY_HW_ENABLE_TOUCHPAD

// Static storage for CTSU driver
static ctsu_instance_ctrl_t g_ctsu_ctrl;
static volatile bool g_ctsu_scan_complete = false;
static volatile ctsu_event_t g_ctsu_last_event = CTSU_EVENT_SCAN_COMPLETE;

// Channel configuration storage
static ra_ctsu_channel_cfg_t g_channel_cfg[RA_CTSU_MAX_CHANNELS];
static uint8_t g_num_channels = 0;
static bool g_ctsu_initialized = false;
static bool g_ctsu_driver_open = false;

// Element configuration for active channel
static ctsu_element_cfg_t g_ctsu_element_cfg;

// CTSU configuration instance
static ctsu_cfg_t g_ctsu_cfg;

// Scan complete callback - called from ISR context
static void ra_ctsu_callback(ctsu_callback_args_t *p_args) {
    g_ctsu_last_event = p_args->event;
    g_ctsu_scan_complete = true;
}

int ra_ctsu_init(void) {
    if (g_ctsu_initialized) {
        return 0;
    }

    // Clear channel configuration
    for (int i = 0; i < RA_CTSU_MAX_CHANNELS; i++) {
        g_channel_cfg[i].enabled = false;
        g_channel_cfg[i].threshold = 0;
        g_channel_cfg[i].ts_channel = 0xFF;
    }
    g_num_channels = 0;
    g_ctsu_initialized = true;

    return 0;
}

void ra_ctsu_deinit(void) {
    if (!g_ctsu_initialized) {
        return;
    }

    // Close CTSU driver if open
    if (g_ctsu_ctrl.open) {
        R_CTSU_Close(&g_ctsu_ctrl);
    }

    g_ctsu_initialized = false;
    g_num_channels = 0;
}

int ra_ctsu_channel_config(uint8_t ts_channel, uint16_t threshold) {
    if (!g_ctsu_initialized) {
        if (ra_ctsu_init() != 0) {
            return -1;
        }
    }

    if (ts_channel >= RA_CTSU_MAX_CHANNELS) {
        return -2;
    }

    // Find or create channel entry
    for (int i = 0; i < RA_CTSU_MAX_CHANNELS; i++) {
        if (g_channel_cfg[i].enabled && g_channel_cfg[i].ts_channel == ts_channel) {
            // Update existing
            g_channel_cfg[i].threshold = threshold;
            return 0;
        }
    }

    // Add new channel
    if (g_num_channels >= RA_CTSU_MAX_CHANNELS) {
        return -3; // No room
    }

    g_channel_cfg[g_num_channels].ts_channel = ts_channel;
    g_channel_cfg[g_num_channels].threshold = threshold;
    g_channel_cfg[g_num_channels].enabled = true;
    g_num_channels++;

    return 0;
}

// Helper to open CTSU driver for a specific channel
static int ra_ctsu_open_channel(uint8_t ts_channel) {
    fsp_err_t err;

    // Close if already open
    if (g_ctsu_driver_open) {
        R_CTSU_Close(&g_ctsu_ctrl);
        g_ctsu_driver_open = false;
    }

    // Setup element configuration (defaults for self-capacitance)
    g_ctsu_element_cfg.ssdiv = CTSU_SSDIV_4000;  // SSDIV for PCLKB >= 4MHz
    g_ctsu_element_cfg.so = 0x100;               // Initial sensor offset
    g_ctsu_element_cfg.snum = 7;                 // Measurement count
    g_ctsu_element_cfg.sdpa = 0x1F;              // Base clock setting

    // Compute channel enable masks
    uint8_t chac0 = 0, chac1 = 0;
    if (ts_channel < 8) {
        chac0 = (1U << ts_channel);
    } else if (ts_channel < 16) {
        chac1 = (1U << (ts_channel - 8));
    }

    // Setup CTSU configuration
    g_ctsu_cfg.cap = CTSU_CAP_SOFTWARE;           // Software trigger
    g_ctsu_cfg.txvsel = CTSU_TXVSEL_VCC;          // VCC for transmission
    g_ctsu_cfg.txvsel2 = CTSU_TXVSEL_MODE;        // Follow TXVSEL
    g_ctsu_cfg.atune1 = CTSU_ATUNE1_NORMAL;       // 40uA (CTSU1)
    g_ctsu_cfg.atune12 = CTSU_ATUNE12_40UA;       // 40uA (CTSU2)
    g_ctsu_cfg.md = CTSU_MODE_SELF_MULTI_SCAN;    // Self-capacitance mode
    g_ctsu_cfg.posel = CTSU_POSEL_HI_Z;           // Hi-Z for non-measured
    g_ctsu_cfg.ctsuchac0 = chac0;                 // TS00-TS07
    g_ctsu_cfg.ctsuchac1 = chac1;                 // TS08-TS15
    g_ctsu_cfg.ctsuchac2 = 0;
    g_ctsu_cfg.ctsuchac3 = 0;
    g_ctsu_cfg.ctsuchac4 = 0;
    g_ctsu_cfg.ctsuchtrc0 = 0;                    // No TX for self mode
    g_ctsu_cfg.ctsuchtrc1 = 0;
    g_ctsu_cfg.ctsuchtrc2 = 0;
    g_ctsu_cfg.ctsuchtrc3 = 0;
    g_ctsu_cfg.ctsuchtrc4 = 0;
    g_ctsu_cfg.p_elements = &g_ctsu_element_cfg;
    g_ctsu_cfg.num_rx = 1;                        // 1 element
    g_ctsu_cfg.num_tx = 0;                        // Self mode
    g_ctsu_cfg.num_moving_average = 1;            // No averaging
    g_ctsu_cfg.tunning_enable = false;            // Manual tuning for now
    g_ctsu_cfg.p_callback = ra_ctsu_callback;
    g_ctsu_cfg.p_transfer_tx = NULL;              // No DTC
    g_ctsu_cfg.p_transfer_rx = NULL;
    g_ctsu_cfg.p_adc_instance = NULL;             // No temp correction
    g_ctsu_cfg.write_irq = VECTOR_NUMBER_CTSU_WRITE;
    g_ctsu_cfg.read_irq = VECTOR_NUMBER_CTSU_READ;
    g_ctsu_cfg.end_irq = VECTOR_NUMBER_CTSU_END;
    g_ctsu_cfg.p_context = NULL;
    g_ctsu_cfg.p_extend = NULL;
    g_ctsu_cfg.tuning_self_target_value = 15360;  // Default target
    g_ctsu_cfg.tuning_mutual_target_value = 10240;

    // Open CTSU driver
    err = R_CTSU_Open(&g_ctsu_ctrl, &g_ctsu_cfg);
    if (err != FSP_SUCCESS) {
        return -1;
    }

    g_ctsu_driver_open = true;
    return 0;
}

int32_t ra_ctsu_read(uint8_t ts_channel) {
    fsp_err_t err;
    uint16_t data = 0;

    if (!g_ctsu_initialized) {
        return -1;
    }

    if (ts_channel >= 16) {
        return -2;  // Channel out of range
    }

    // Open driver for this channel
    if (ra_ctsu_open_channel(ts_channel) != 0) {
        return -3;  // Failed to open
    }

    // Reset scan complete flag
    g_ctsu_scan_complete = false;
    g_ctsu_last_event = CTSU_EVENT_SCAN_COMPLETE;

    // Start scan
    err = R_CTSU_ScanStart(&g_ctsu_ctrl);
    if (err != FSP_SUCCESS) {
        return -4;
    }

    // Wait for scan complete (with timeout)
    uint32_t timeout = 100000;  // ~100ms at typical speeds
    while (!g_ctsu_scan_complete && timeout > 0) {
        timeout--;
    }

    if (!g_ctsu_scan_complete) {
        R_CTSU_ScanStop(&g_ctsu_ctrl);
        return -5;  // Timeout
    }

    // Check for errors
    if (g_ctsu_last_event != CTSU_EVENT_SCAN_COMPLETE) {
        return -6;  // Scan error
    }

    // Get data
    err = R_CTSU_DataGet(&g_ctsu_ctrl, &data);
    if (err != FSP_SUCCESS) {
        return -7;
    }

    return (int32_t)data;
}

int ra_ctsu_is_touched(uint8_t ts_channel) {
    int32_t count = ra_ctsu_read(ts_channel);
    if (count < 0) {
        return count;
    }

    // Find threshold for this channel
    for (int i = 0; i < g_num_channels; i++) {
        if (g_channel_cfg[i].ts_channel == ts_channel) {
            return (count > g_channel_cfg[i].threshold) ? 1 : 0;
        }
    }

    return -4; // Channel not configured
}

uint8_t ra_ctsu_get_channel_count(void) {
    return g_num_channels;
}

int8_t ra_ctsu_pin_to_channel(uint16_t pin) {
    // RA4M2 TS pin mapping (from datasheet pinout table)
    // Pin encoding: P205 = 0x25 (port 2, bit 5)
    switch (pin) {
        case 0x25: return 1;   // P205 -> TS01
        case 0x26: return 2;   // P206 -> TS02
        // P207 (0x27) is TSCAP, not a TS channel
        case 0x47: return 3;   // P407 -> TS03
        case 0x48: return 4;   // P408 -> TS04
        case 0x49: return 5;   // P409 -> TS05
        case 0x4A: return 6;   // P410 -> TS06
        case 0x4B: return 7;   // P411 -> TS07
        case 0x4C: return 8;   // P412 -> TS08
        case 0x4D: return 9;   // P413 -> TS09
        case 0x4E: return 10;  // P414 -> TS10
        case 0x4F: return 11;  // P415 -> TS11
        case 0x78: return 12;  // P708 -> TS12
        default:   return -1;  // Not a TS pin
    }
}

#endif // MICROPY_HW_ENABLE_TOUCHPAD

