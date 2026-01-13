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

#include <string.h>
#include "ra_ble_events.h"

// Event queue structure (lock-free ring buffer)
typedef struct {
    ble_event_t events[MICROPY_BLE_EVENT_QUEUE_SIZE];
    volatile uint16_t head;  // Write index (IRQ context)
    volatile uint16_t tail;  // Read index (main loop)
    ble_event_stats_t stats;
} ble_event_queue_t;

static ble_event_queue_t g_ble_event_queue;

// Initialize event queue
void ra_ble_event_queue_init(void) {
    memset(&g_ble_event_queue, 0, sizeof(ble_event_queue_t));
}

// Push event from BLE callback (IRQ context safe)
void ra_ble_event_push(ble_event_type_t type, uint16_t conn_hdl, 
                       uint16_t attr_hdl, const uint8_t *data, uint16_t len) {
    
    uint16_t next_head = (g_ble_event_queue.head + 1) % MICROPY_BLE_EVENT_QUEUE_SIZE;
    
    // Check for overflow
    if (next_head == g_ble_event_queue.tail) {
        g_ble_event_queue.stats.dropped++;
        return; // Queue full, drop event
    }
    
    // Get event slot
    ble_event_t *evt = &g_ble_event_queue.events[g_ble_event_queue.head];
    
    // Fill event data
    evt->type = type;
    evt->conn_handle = conn_hdl;
    evt->attr_handle = attr_hdl;
    evt->data_len = (len > BLE_EVENT_MAX_PAYLOAD) ? BLE_EVENT_MAX_PAYLOAD : len;
    
    // Copy payload if present
    if (data && evt->data_len > 0) {
        memcpy(evt->data, data, evt->data_len);
    }
    
    // Commit write (atomic on Cortex-M)
    g_ble_event_queue.head = next_head;
    g_ble_event_queue.stats.pushed++;
}

// Pop event from main loop (Python context)
bool ra_ble_event_pop(ble_event_t *evt) {
    // Check if queue is empty
    if (g_ble_event_queue.tail == g_ble_event_queue.head) {
        return false;
    }
    
    // Copy event
    memcpy(evt, &g_ble_event_queue.events[g_ble_event_queue.tail], sizeof(ble_event_t));
    
    // Advance tail
    g_ble_event_queue.tail = (g_ble_event_queue.tail + 1) % MICROPY_BLE_EVENT_QUEUE_SIZE;
    g_ble_event_queue.stats.popped++;
    
    return true;
}

// Get queue statistics
void ra_ble_event_get_stats(ble_event_stats_t *stats) {
    if (stats) {
        memcpy(stats, &g_ble_event_queue.stats, sizeof(ble_event_stats_t));
    }
}

// Check if queue is empty
bool ra_ble_event_queue_empty(void) {
    return (g_ble_event_queue.tail == g_ble_event_queue.head);
}

