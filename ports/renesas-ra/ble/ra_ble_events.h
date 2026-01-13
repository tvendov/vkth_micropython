/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Renesas Electronics Corporation
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

#ifndef MICROPY_INCLUDED_RENESAS_RA_BLE_EVENTS_H
#define MICROPY_INCLUDED_RENESAS_RA_BLE_EVENTS_H

#include <stdint.h>
#include <stdbool.h>

// Event queue configuration
#ifndef MICROPY_BLE_EVENT_QUEUE_SIZE
#define MICROPY_BLE_EVENT_QUEUE_SIZE (32)
#endif

#ifndef BLE_EVENT_MAX_PAYLOAD
#define BLE_EVENT_MAX_PAYLOAD (64)
#endif

// BLE event types
typedef enum {
    BLE_EVT_NONE = 0,
    
    // GAP events
    BLE_EVT_STACK_READY,
    BLE_EVT_GAP_CONNECTED,
    BLE_EVT_GAP_DISCONNECTED,
    BLE_EVT_GAP_ADV_STARTED,
    BLE_EVT_GAP_ADV_STOPPED,
    BLE_EVT_GAP_SCAN_RESULT,
    
    // GATT Server events
    BLE_EVT_GATTS_WRITE,
    BLE_EVT_GATTS_READ,
    BLE_EVT_GATTS_NOTIFY_COMPLETE,
    BLE_EVT_GATTS_INDICATE_COMPLETE,
    BLE_EVT_GATTS_CCCD_CHANGED,
    
    // GATT Client events
    BLE_EVT_GATTC_NOTIFY,
    BLE_EVT_GATTC_INDICATE,
    
    BLE_EVT_MAX
} ble_event_type_t;

// BLE event structure
typedef struct {
    ble_event_type_t type;
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint16_t data_len;
    uint8_t data[BLE_EVENT_MAX_PAYLOAD];
} ble_event_t;

// Event queue statistics
typedef struct {
    uint32_t pushed;
    uint32_t popped;
    uint32_t dropped;
} ble_event_stats_t;

// Initialize event queue
void ra_ble_event_queue_init(void);

// Push event from BLE callback (IRQ context safe)
void ra_ble_event_push(ble_event_type_t type, uint16_t conn_hdl, 
                       uint16_t attr_hdl, const uint8_t *data, uint16_t len);

// Pop event from main loop (Python context)
bool ra_ble_event_pop(ble_event_t *evt);

// Get queue statistics
void ra_ble_event_get_stats(ble_event_stats_t *stats);

// Check if queue is empty
bool ra_ble_event_queue_empty(void);

#endif // MICROPY_INCLUDED_RENESAS_RA_BLE_EVENTS_H

