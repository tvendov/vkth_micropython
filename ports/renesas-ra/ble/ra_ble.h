/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Renesas Electronics Corporation
 */

#ifndef MICROPY_INCLUDED_RENESAS_RA_BLE_H
#define MICROPY_INCLUDED_RENESAS_RA_BLE_H

#include <stdint.h>
#include <stdbool.h>

// BLE status codes
typedef enum {
    RA_BLE_SUCCESS = 0,
    RA_BLE_ERR_INVALID_ARG,
    RA_BLE_ERR_INVALID_STATE,
    RA_BLE_ERR_NO_MEM,
    RA_BLE_ERR_TIMEOUT,
    RA_BLE_ERR_NOT_SUPPORTED,
} ra_ble_status_t;

// BLE state
typedef enum {
    RA_BLE_STATE_OFF = 0,
    RA_BLE_STATE_IDLE,
    RA_BLE_STATE_ADVERTISING,
    RA_BLE_STATE_CONNECTED,
} ra_ble_state_t;

// Advertising parameters
typedef struct {
    uint16_t interval_ms;       // Advertising interval (100-10000 ms)
    uint8_t *adv_data;          // Advertising data
    uint16_t adv_data_len;      // Advertising data length
    uint8_t *scan_rsp_data;     // Scan response data
    uint16_t scan_rsp_len;      // Scan response length
} ra_ble_adv_params_t;

// Initialize BLE stack
ra_ble_status_t ra_ble_init(void);

// Deinitialize BLE stack
ra_ble_status_t ra_ble_deinit(void);

// Get current BLE state
ra_ble_state_t ra_ble_get_state(void);

// Start advertising
ra_ble_status_t ra_ble_gap_start_advertising(const ra_ble_adv_params_t *params);

// Stop advertising
ra_ble_status_t ra_ble_gap_stop_advertising(void);

// Disconnect from peer
ra_ble_status_t ra_ble_gap_disconnect(uint16_t conn_handle);

// Send GATT notification
ra_ble_status_t ra_ble_gatts_notify(uint16_t conn_handle, uint16_t attr_handle,
                                    const uint8_t *data, uint16_t len);

// Send GATT indication
ra_ble_status_t ra_ble_gatts_indicate(uint16_t conn_handle, uint16_t attr_handle,
                                      const uint8_t *data, uint16_t len);

// Set GAP device name
ra_ble_status_t ra_ble_gap_set_device_name(const char *name, uint8_t len);

// Get stored device name (for advertising payload builder)
const char *ra_ble_gap_get_device_name(uint8_t *out_len);

// Get GAP device address
ra_ble_status_t ra_ble_gap_get_address(uint8_t addr[6]);

// Process BLE events (call from main loop)
void ra_ble_process_events(void);

#endif // MICROPY_INCLUDED_RENESAS_RA_BLE_H

