#ifndef MICROPY_INCLUDED_RENESAS_RA_BLE_H
#define MICROPY_INCLUDED_RENESAS_RA_BLE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RA_BLE_SUCCESS = 0,
    RA_BLE_ERR_INVALID_ARG,
    RA_BLE_ERR_INVALID_STATE,
    RA_BLE_ERR_NO_MEM,
    RA_BLE_ERR_TIMEOUT,
    RA_BLE_ERR_NOT_SUPPORTED,
} ra_ble_status_t;

typedef enum {
    RA_BLE_STATE_OFF = 0,
    RA_BLE_STATE_IDLE,
    RA_BLE_STATE_ADVERTISING,
    RA_BLE_STATE_CONNECTED,
} ra_ble_state_t;

typedef struct {
    uint16_t interval_ms;
    uint8_t *adv_data;
    uint16_t adv_data_len;
    uint8_t *scan_rsp_data;
    uint16_t scan_rsp_len;
} ra_ble_adv_params_t;

ra_ble_status_t ra_ble_init(void);
ra_ble_status_t ra_ble_deinit(void);
ra_ble_state_t  ra_ble_get_state(void);

ra_ble_status_t ra_ble_gap_start_advertising(const ra_ble_adv_params_t *params);
ra_ble_status_t ra_ble_gap_stop_advertising(void);
ra_ble_status_t ra_ble_gap_disconnect(uint16_t conn_handle);

ra_ble_status_t ra_ble_gatts_notify(uint16_t conn_handle, uint16_t attr_handle,
                                    const uint8_t *data, uint16_t len);
ra_ble_status_t ra_ble_gatts_indicate(uint16_t conn_handle, uint16_t attr_handle,
                                      const uint8_t *data, uint16_t len);

ra_ble_status_t ra_ble_gap_set_device_name(const char *name, uint8_t len);
const char *    ra_ble_gap_get_device_name(uint8_t *out_len);
ra_ble_status_t ra_ble_gap_get_address(uint8_t addr[6]);

/* Pump BLE and allow events to be consumed from ra_ble_events. */
void ra_ble_process_events(void);

#endif
